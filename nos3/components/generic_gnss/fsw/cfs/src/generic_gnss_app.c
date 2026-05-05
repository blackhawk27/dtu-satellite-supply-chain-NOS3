/*******************************************************************************
** File: generic_gnss_app.c
**
** Purpose:
**   Generic GNSS cFS application. Reads ASCII position lines off a NOS Engine
**   UART bus the simulator publishes on each tick (see generic_gnss_device.h
**   for the line format) and mirrors lat/lon into HK telemetry. Subscribes to
**   MGR HK to gate DATA_TLM publication on SCIENCE mode and exposes a
**   GS_PING_CC handler so a ground responder can verify the round-trip
**   uplink/echo path. The simulator also writes a "[GNSS_TRUTH]" log line
**   with the same lat/lon so Kibana can compare the bus-borne value (which
**   an attacker can spoof) against the simulator's internal truth.
*******************************************************************************/

#include "generic_gnss_app.h"
#include "generic_gnss_device.h"
#include "mgr_msg.h"
#include "mgr_msgids.h"
#include "hwlib.h"
#include <string.h>

/* Mirrors MGR_SCIENCE_MODE from mgr_app.h. We don't include mgr_app.h here
 * because it pulls in device_cfg.h, which mgr does not ship. The MGR enum
 * is stable and small enough to mirror directly. */
#ifndef MGR_SCIENCE_MODE
#define MGR_SCIENCE_MODE 3
#endif

GENERIC_GNSS_AppData_t GENERIC_GNSS_AppData;

void GNSS_AppMain(void)
{
    int32 status = OS_SUCCESS;

    CFE_ES_PerfLogEntry(GENERIC_GNSS_PERF_ID);

    status = GENERIC_GNSS_AppInit();
    if (status != CFE_SUCCESS)
    {
        GENERIC_GNSS_AppData.RunStatus = CFE_ES_RunStatus_APP_ERROR;
    }

    while (CFE_ES_RunLoop(&GENERIC_GNSS_AppData.RunStatus) == true)
    {
        CFE_ES_PerfLogExit(GENERIC_GNSS_PERF_ID);
        status = CFE_SB_ReceiveBuffer((CFE_SB_Buffer_t **)&GENERIC_GNSS_AppData.MsgPtr,
                                      GENERIC_GNSS_AppData.CmdPipe, CFE_SB_PEND_FOREVER);
        CFE_ES_PerfLogEntry(GENERIC_GNSS_PERF_ID);
        if (status == CFE_SUCCESS)
        {
            GENERIC_GNSS_ProcessCommandPacket();
        }
        else
        {
            CFE_EVS_SendEvent(GENERIC_GNSS_PIPE_ERR_EID, CFE_EVS_EventType_ERROR,
                              "GENERIC_GNSS: SB Pipe Read Error = %d", (int)status);
            GENERIC_GNSS_AppData.RunStatus = CFE_ES_RunStatus_APP_ERROR;
        }
    }

    GENERIC_GNSS_Disable();
    CFE_ES_PerfLogExit(GENERIC_GNSS_PERF_ID);
    CFE_ES_ExitApp(GENERIC_GNSS_AppData.RunStatus);
}

int32 GENERIC_GNSS_AppInit(void)
{
    int32 status = OS_SUCCESS;

    GENERIC_GNSS_AppData.RunStatus = CFE_ES_RunStatus_APP_RUN;

    status = CFE_EVS_Register(NULL, 0, CFE_EVS_EventFilter_BINARY);
    if (status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("GENERIC_GNSS: Error registering for event services: 0x%08X\n",
                             (unsigned int)status);
        return status;
    }

    status = CFE_SB_CreatePipe(&GENERIC_GNSS_AppData.CmdPipe, GENERIC_GNSS_PIPE_DEPTH, "GNSS_CMD_PIPE");
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(GENERIC_GNSS_PIPE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Error Creating SB Pipe,RC=0x%08X", (unsigned int)status);
        return status;
    }

    status = CFE_SB_Subscribe(CFE_SB_ValueToMsgId(GENERIC_GNSS_CMD_MID), GENERIC_GNSS_AppData.CmdPipe);
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(GENERIC_GNSS_SUB_CMD_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Error Subscribing to HK Gnd Cmds, MID=0x%04X, RC=0x%08X",
                          GENERIC_GNSS_CMD_MID, (unsigned int)status);
        return status;
    }

    status = CFE_SB_Subscribe(CFE_SB_ValueToMsgId(GENERIC_GNSS_REQ_HK_MID), GENERIC_GNSS_AppData.CmdPipe);
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(GENERIC_GNSS_SUB_REQ_HK_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Error Subscribing to HK Request, MID=0x%04X, RC=0x%08X",
                          GENERIC_GNSS_REQ_HK_MID, (unsigned int)status);
        return status;
    }

    /* Subscribe to MGR HK so we can mirror SpacecraftMode into in_science_mode
     * and gate DEVICE_TLM publication on SCIENCE. Failure is non-fatal: the
     * app still runs and reports HK, just with InScienceMode pinned to 0. */
    status = CFE_SB_Subscribe(CFE_SB_ValueToMsgId(MGR_HK_TLM_MID), GENERIC_GNSS_AppData.CmdPipe);
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(GENERIC_GNSS_SUB_MGR_HK_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Error Subscribing to MGR HK, MID=0x%04X, RC=0x%08X",
                          MGR_HK_TLM_MID, (unsigned int)status);
    }

    CFE_MSG_Init(CFE_MSG_PTR(GENERIC_GNSS_AppData.HkTelemetryPkt.TlmHeader),
                 CFE_SB_ValueToMsgId(GENERIC_GNSS_HK_TLM_MID), GENERIC_GNSS_HK_TLM_LNGTH);
    CFE_MSG_Init(CFE_MSG_PTR(GENERIC_GNSS_AppData.DevicePkt.TlmHeader),
                 CFE_SB_ValueToMsgId(GENERIC_GNSS_DEVICE_TLM_MID), GENERIC_GNSS_DEVICE_TLM_LNGTH);
    CFE_MSG_Init(CFE_MSG_PTR(GENERIC_GNSS_AppData.SatHelloPkt.TlmHeader),
                 CFE_SB_ValueToMsgId(GENERIC_GNSS_SAT_HELLO_TLM_MID),
                 GENERIC_GNSS_SAT_HELLO_TLM_LNGTH);
    GENERIC_GNSS_AppData.SatHelloSeq = 0;

    GENERIC_GNSS_ResetCounters();
    GENERIC_GNSS_AppData.CachedMgrMode = 0;

    GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceEnabled            = GENERIC_GNSS_DEVICE_DISABLED;
    GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceHK.SolutionType    = GENERIC_GNSS_SOLN_NONE;
    GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceHK.NumSatsTotal    = 0;
    GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceHK.NumSatsGps      = 0;
    GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceHK.NumSatsGlonass  = 0;
    GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceHK.NumSatsGalileo  = 0;
    GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceHK.NumSatsBeidou   = 0;
    GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceHK.Pdop            = 0.0f;
    GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceHK.Hdop            = 0.0f;
    GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceHK.Vdop            = 0.0f;
    GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceHK.GnssLat          = 0.0;
    GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceHK.GnssLon          = 0.0;
    GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceHK.InDenmarkBox     = 0;
    GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceHK.InScienceMode    = 0;
    GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceHK.LastPingSeq      = 0;
    GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceHK.LastPingTime     = 0;
    GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceHK.PingCount        = 0;

    /* Configure UART struct (libuart pattern). The port is opened by Enable;
     * the child task tolerates the port being closed and just sleeps. */
    GENERIC_GNSS_AppData.GnssUart.deviceString  = GENERIC_GNSS_CFG_STRING;
    GENERIC_GNSS_AppData.GnssUart.handle        = GENERIC_GNSS_CFG_HANDLE;
    GENERIC_GNSS_AppData.GnssUart.isOpen        = PORT_CLOSED;
    GENERIC_GNSS_AppData.GnssUart.baud          = GENERIC_GNSS_CFG_BAUDRATE_HZ;
    GENERIC_GNSS_AppData.GnssUart.access_option = uart_access_flag_RDWR;

    GENERIC_GNSS_AppData.BusRxCount      = 0;
    GENERIC_GNSS_AppData.BusRxErrorCount = 0;
    GENERIC_GNSS_AppData.LastBusLat      = 0.0;
    GENERIC_GNSS_AppData.LastBusLon      = 0.0;
    GENERIC_GNSS_AppData.LastBusAlt      = 0.0;

    status = OS_MutSemCreate(&GENERIC_GNSS_AppData.HkDataMutex,
                             GENERIC_GNSS_HK_MUTEX_NAME, 0);
    if (status != OS_SUCCESS)
    {
        CFE_ES_WriteToSysLog("GENERIC_GNSS: HK mutex create failed: 0x%08X\n",
                             (unsigned int)status);
        return status;
    }

    /* Auto-enable so the UART child task starts polling without waiting for
     * a ground operator to send ENABLE_CC. Mirrors novatel_oem615 behaviour
     * the dashboard relies on. */
    GENERIC_GNSS_Enable();

    /* Spawn the child task that reads lines off the UART. Done last so the
     * device is open by the time the task starts looping. */
    {
        uint32 child_id = 0;
        int32 child_status = CFE_ES_CreateChildTask(&child_id, GENERIC_GNSS_CHILD_TASK_NAME,
                                                    GENERIC_GNSS_BusRxChildTask, 0,
                                                    GENERIC_GNSS_CHILD_TASK_STACK_SIZE,
                                                    GENERIC_GNSS_CHILD_TASK_PRIORITY, 0);
        if (child_status != CFE_SUCCESS)
        {
            CFE_ES_WriteToSysLog("GENERIC_GNSS: bus child task create failed: 0x%08X\n",
                                 (unsigned int)child_status);
        }
    }

    status = CFE_EVS_SendEvent(GENERIC_GNSS_STARTUP_INF_EID, CFE_EVS_EventType_INFORMATION,
                               "GENERIC_GNSS App Initialized. Version %d.%d.%d.%d",
                               GENERIC_GNSS_MAJOR_VERSION, GENERIC_GNSS_MINOR_VERSION,
                               GENERIC_GNSS_REVISION, GENERIC_GNSS_MISSION_REV);
    if (status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("GENERIC_GNSS: Error sending initialization event: 0x%08X\n",
                             (unsigned int)status);
    }
    return status;
}

void GENERIC_GNSS_ProcessCommandPacket(void)
{
    CFE_SB_MsgId_t MsgId = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_GetMsgId(GENERIC_GNSS_AppData.MsgPtr, &MsgId);
    switch (CFE_SB_MsgIdToValue(MsgId))
    {
        case GENERIC_GNSS_CMD_MID:
            GENERIC_GNSS_ProcessGroundCommand();
            break;
        case GENERIC_GNSS_REQ_HK_MID:
            GENERIC_GNSS_ProcessTelemetryRequest();
            break;
        case MGR_HK_TLM_MID:
            GENERIC_GNSS_ProcessMgrHk();
            break;
        default:
            GENERIC_GNSS_AppData.HkTelemetryPkt.CommandErrorCount++;
            CFE_EVS_SendEvent(GENERIC_GNSS_PROCESS_CMD_ERR_EID, CFE_EVS_EventType_ERROR,
                              "GENERIC_GNSS: Invalid command packet, MID = 0x%x",
                              CFE_SB_MsgIdToValue(MsgId));
            break;
    }
}

void GENERIC_GNSS_ProcessMgrHk(void)
{
    /* MGR HK arrives once per HK cycle. Cache SpacecraftMode so the next HK
     * report can mirror it into InScienceMode and gate DEVICE_TLM. */
    MGR_Hk_tlm_t *mgr_hk = (MGR_Hk_tlm_t *)GENERIC_GNSS_AppData.MsgPtr;
    GENERIC_GNSS_AppData.CachedMgrMode = mgr_hk->SpacecraftMode;
}

void GENERIC_GNSS_ProcessGroundCommand(void)
{
    CFE_SB_MsgId_t    MsgId       = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_FcnCode_t CommandCode = 0;
    CFE_MSG_GetMsgId(GENERIC_GNSS_AppData.MsgPtr, &MsgId);
    CFE_MSG_GetFcnCode(GENERIC_GNSS_AppData.MsgPtr, &CommandCode);
    switch (CommandCode)
    {
        case GENERIC_GNSS_NOOP_CC:
            if (GENERIC_GNSS_VerifyCmdLength(GENERIC_GNSS_AppData.MsgPtr,
                                             sizeof(GENERIC_GNSS_NoArgs_cmd_t)) == OS_SUCCESS)
            {
                GENERIC_GNSS_AppData.HkTelemetryPkt.CommandCount++;
                CFE_EVS_SendEvent(GENERIC_GNSS_CMD_NOOP_INF_EID, CFE_EVS_EventType_INFORMATION,
                                  "GENERIC_GNSS: NOOP command received");
            }
            break;
        case GENERIC_GNSS_RESET_COUNTERS_CC:
            if (GENERIC_GNSS_VerifyCmdLength(GENERIC_GNSS_AppData.MsgPtr,
                                             sizeof(GENERIC_GNSS_NoArgs_cmd_t)) == OS_SUCCESS)
            {
                CFE_EVS_SendEvent(GENERIC_GNSS_CMD_RESET_INF_EID, CFE_EVS_EventType_INFORMATION,
                                  "GENERIC_GNSS: RESET counters command received");
                GENERIC_GNSS_ResetCounters();
            }
            break;
        case GENERIC_GNSS_ENABLE_CC:
            if (GENERIC_GNSS_VerifyCmdLength(GENERIC_GNSS_AppData.MsgPtr,
                                             sizeof(GENERIC_GNSS_NoArgs_cmd_t)) == OS_SUCCESS)
            {
                CFE_EVS_SendEvent(GENERIC_GNSS_CMD_ENABLE_INF_EID, CFE_EVS_EventType_INFORMATION,
                                  "GENERIC_GNSS: Enable command received");
                GENERIC_GNSS_Enable();
            }
            break;
        case GENERIC_GNSS_DISABLE_CC:
            if (GENERIC_GNSS_VerifyCmdLength(GENERIC_GNSS_AppData.MsgPtr,
                                             sizeof(GENERIC_GNSS_NoArgs_cmd_t)) == OS_SUCCESS)
            {
                CFE_EVS_SendEvent(GENERIC_GNSS_CMD_DISABLE_INF_EID, CFE_EVS_EventType_INFORMATION,
                                  "GENERIC_GNSS: Disable command received");
                GENERIC_GNSS_Disable();
            }
            break;
        case GENERIC_GNSS_GS_PING_CC:
            if (GENERIC_GNSS_VerifyCmdLength(GENERIC_GNSS_AppData.MsgPtr,
                                             sizeof(GENERIC_GNSS_GsPing_cmd_t)) == OS_SUCCESS)
            {
                GENERIC_GNSS_ProcessGsPing();
            }
            break;
        default:
            GENERIC_GNSS_AppData.HkTelemetryPkt.CommandErrorCount++;
            CFE_EVS_SendEvent(GENERIC_GNSS_CMD_ERR_EID, CFE_EVS_EventType_ERROR,
                              "GENERIC_GNSS: Invalid command code, MID=0x%x cmd=0x%x",
                              CFE_SB_MsgIdToValue(MsgId), CommandCode);
            break;
    }
}

void GENERIC_GNSS_ProcessTelemetryRequest(void)
{
    CFE_SB_MsgId_t    MsgId       = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_FcnCode_t CommandCode = 0;
    CFE_MSG_GetMsgId(GENERIC_GNSS_AppData.MsgPtr, &MsgId);
    CFE_MSG_GetFcnCode(GENERIC_GNSS_AppData.MsgPtr, &CommandCode);
    switch (CommandCode)
    {
        case GENERIC_GNSS_REQ_HK_TLM:
            GENERIC_GNSS_ReportHousekeeping();
            break;
        case GENERIC_GNSS_REQ_DATA_TLM:
            GENERIC_GNSS_ReportDeviceTelemetry();
            break;
        default:
            GENERIC_GNSS_AppData.HkTelemetryPkt.CommandErrorCount++;
            CFE_EVS_SendEvent(GENERIC_GNSS_DEVICE_TLM_ERR_EID, CFE_EVS_EventType_ERROR,
                              "GENERIC_GNSS: Invalid tlm cmd code, MID=0x%x cmd=0x%x",
                              CFE_SB_MsgIdToValue(MsgId), CommandCode);
            break;
    }
}

void GENERIC_GNSS_ProcessGsPing(void)
{
    GENERIC_GNSS_GsPing_cmd_t *cmd = (GENERIC_GNSS_GsPing_cmd_t *)GENERIC_GNSS_AppData.MsgPtr;
    CFE_TIME_SysTime_t         now = CFE_TIME_GetTime();
    GENERIC_GNSS_Device_HK_tlm_t *dhk = &GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceHK;

    GENERIC_GNSS_AppData.HkTelemetryPkt.CommandCount++;
    dhk->LastPingSeq  = cmd->Seq;
    dhk->PingCount   += 1;
    dhk->LastPingTime = ((uint64)now.Seconds * 1000000ULL)
                      + ((uint64)now.Subseconds / 4295ULL);
    CFE_EVS_SendEvent(GENERIC_GNSS_GS_PING_INF_EID, CFE_EVS_EventType_INFORMATION,
                      "GENERIC_GNSS: GS_PING seq=%u count=%u",
                      (unsigned int)dhk->LastPingSeq, (unsigned int)dhk->PingCount);
}

void GENERIC_GNSS_UpdatePositionAndFlags(void)
{
    /* Pull the freshest lat/lon/alt sample read from the GNSS sim UART bus
     * by the child task. Mirroring is mutex-guarded so we never publish a
     * partially-written sample. If the device is disabled or the child has
     * not yet seen a sample, the cached values default to 0. The child task
     * stamps `BusRxCount` on every successful parse so the dashboard can
     * show "FSW saw N bus reads" alongside the ground-truth log. */
    double lat = 0.0;
    double lon = 0.0;

    if (OS_MutSemTake(GENERIC_GNSS_AppData.HkDataMutex) == OS_SUCCESS)
    {
        lat = GENERIC_GNSS_AppData.LastBusLat;
        lon = GENERIC_GNSS_AppData.LastBusLon;
        OS_MutSemGive(GENERIC_GNSS_AppData.HkDataMutex);
    }

    GENERIC_GNSS_Device_HK_tlm_t *dhk = &GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceHK;
    dhk->GnssLat = lat;
    dhk->GnssLon = lon;
    dhk->InDenmarkBox = (lat >= 55.0 && lat <= 58.0 &&
                         lon >= 8.0  && lon <= 16.0) ? 1 : 0;
    dhk->InScienceMode = (GENERIC_GNSS_AppData.CachedMgrMode == MGR_SCIENCE_MODE) ? 1 : 0;
}

void GENERIC_GNSS_ReportHousekeeping(void)
{
    GENERIC_GNSS_UpdatePositionAndFlags();
    if (GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceEnabled == GENERIC_GNSS_DEVICE_ENABLED)
    {
        GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceCount++;
    }
    CFE_SB_TimeStampMsg((CFE_MSG_Message_t *)&GENERIC_GNSS_AppData.HkTelemetryPkt);
    CFE_SB_TransmitMsg((CFE_MSG_Message_t *)&GENERIC_GNSS_AppData.HkTelemetryPkt, true);

    /* Piggyback the SAT_HELLO beacon on the HK send so we get a 1 Hz pulse
     * to ground without spending a separate SCH slot. The helper only
     * transmits when InDenmarkBox just got set to 1 by UpdatePositionAndFlags
     * above, so on-orbit the beacon stops the moment the geofence flips off. */
    GENERIC_GNSS_EmitSatHelloIfInBox();
}

void GENERIC_GNSS_EmitSatHelloIfInBox(void)
{
    GENERIC_GNSS_Device_HK_tlm_t *dhk = &GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceHK;
    if (dhk->InDenmarkBox != 1)
    {
        return;
    }

    CFE_TIME_SysTime_t now = CFE_TIME_GetTime();
    GENERIC_GNSS_AppData.SatHelloSeq++;

    GENERIC_GNSS_SatHello_tlm_t *hello = &GENERIC_GNSS_AppData.SatHelloPkt;
    hello->HelloSeq     = GENERIC_GNSS_AppData.SatHelloSeq;
    hello->GnssTimeUs   = ((uint64)now.Seconds * 1000000ULL)
                        + ((uint64)now.Subseconds / 4295ULL);
    hello->GnssLat      = dhk->GnssLat;
    hello->GnssLon      = dhk->GnssLon;
    hello->InDenmarkBox = 1;

    CFE_SB_TimeStampMsg((CFE_MSG_Message_t *)hello);
    CFE_SB_TransmitMsg((CFE_MSG_Message_t *)hello, true);
}

void GENERIC_GNSS_ReportDeviceTelemetry(void)
{
    /* DATA_TLM is SCIENCE-gated per the GNSS-to-GS validation design: only
     * publish when MGR is in SCIENCE mode, so the ground responder can use
     * presence of DEVICE_TLM as a "spacecraft is observing" signal. */
    if (GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceEnabled == GENERIC_GNSS_DEVICE_ENABLED &&
        GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceHK.InScienceMode == 1)
    {
        memcpy(&GENERIC_GNSS_AppData.DevicePkt.DeviceHK,
               &GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceHK,
               sizeof(GENERIC_GNSS_Device_HK_tlm_t));
        CFE_SB_TimeStampMsg((CFE_MSG_Message_t *)&GENERIC_GNSS_AppData.DevicePkt);
        CFE_SB_TransmitMsg((CFE_MSG_Message_t *)&GENERIC_GNSS_AppData.DevicePkt, true);
        GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceCount++;
    }
}

void GENERIC_GNSS_ResetCounters(void)
{
    GENERIC_GNSS_AppData.HkTelemetryPkt.CommandErrorCount = 0;
    GENERIC_GNSS_AppData.HkTelemetryPkt.CommandCount      = 0;
    GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceErrorCount  = 0;
    GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceCount       = 0;
}

void GENERIC_GNSS_Enable(void)
{
    int32 status = OS_SUCCESS;
    if (GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceEnabled == GENERIC_GNSS_DEVICE_DISABLED)
    {
        GENERIC_GNSS_AppData.HkTelemetryPkt.CommandCount++;
        status = uart_init_port(&GENERIC_GNSS_AppData.GnssUart);
        if (status == OS_SUCCESS)
        {
            GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceEnabled = GENERIC_GNSS_DEVICE_ENABLED;
            CFE_EVS_SendEvent(GENERIC_GNSS_ENABLE_INF_EID, CFE_EVS_EventType_INFORMATION,
                              "GENERIC_GNSS: Device enabled (uart=%s handle=%d)",
                              GENERIC_GNSS_AppData.GnssUart.deviceString,
                              GENERIC_GNSS_AppData.GnssUart.handle);
        }
        else
        {
            GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceErrorCount++;
            CFE_EVS_SendEvent(GENERIC_GNSS_ENABLE_ERR_EID, CFE_EVS_EventType_ERROR,
                              "GENERIC_GNSS: uart_init_port failed: %d", (int)status);
        }
    }
    else
    {
        GENERIC_GNSS_AppData.HkTelemetryPkt.CommandErrorCount++;
        CFE_EVS_SendEvent(GENERIC_GNSS_ENABLE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "GENERIC_GNSS: Device enable failed, already enabled");
    }
}

void GENERIC_GNSS_Disable(void)
{
    if (GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceEnabled == GENERIC_GNSS_DEVICE_ENABLED)
    {
        GENERIC_GNSS_AppData.HkTelemetryPkt.CommandCount++;
        GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceEnabled = GENERIC_GNSS_DEVICE_DISABLED;
        uart_close_port(&GENERIC_GNSS_AppData.GnssUart);
        CFE_EVS_SendEvent(GENERIC_GNSS_DISABLE_INF_EID, CFE_EVS_EventType_INFORMATION,
                          "GENERIC_GNSS: Device disabled");
    }
    else
    {
        GENERIC_GNSS_AppData.HkTelemetryPkt.CommandErrorCount++;
        CFE_EVS_SendEvent(GENERIC_GNSS_DISABLE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "GENERIC_GNSS: Device disable failed, already disabled");
    }
}

void GENERIC_GNSS_BusRxChildTask(void)
{
    /* Drain the GNSS sim UART line by line. The protocol is one ASCII line
     * per tick: "GNSS,<lat>,<lon>,<alt>\n". A small accumulator buffer
     * collects bytes until a newline arrives, then we hand the line off to
     * GENERIC_GNSS_ParseBusLine and stash the result under the HK mutex.
     * If the device is disabled or the UART has nothing pending, we sleep
     * GENERIC_GNSS_CFG_POLL_PERIOD_MS so we don't spin. */
    char    accum[GENERIC_GNSS_BUS_LINE_MAX];
    size_t  accum_len = 0;
    uint8_t buf[64];

    while (1)
    {
        if (GENERIC_GNSS_AppData.HkTelemetryPkt.DeviceEnabled == GENERIC_GNSS_DEVICE_DISABLED ||
            GENERIC_GNSS_AppData.GnssUart.isOpen != PORT_OPEN)
        {
            OS_TaskDelay(GENERIC_GNSS_CFG_POLL_PERIOD_MS);
            accum_len = 0;
            continue;
        }

        int32_t avail = uart_bytes_available(&GENERIC_GNSS_AppData.GnssUart);
        if (avail <= 0)
        {
            OS_TaskDelay(GENERIC_GNSS_CFG_POLL_PERIOD_MS);
            continue;
        }

        uint32_t want = (avail > (int32_t)sizeof(buf)) ? sizeof(buf) : (uint32_t)avail;
        int32_t got = uart_read_port(&GENERIC_GNSS_AppData.GnssUart, buf, want);
        if (got <= 0)
        {
            OS_TaskDelay(GENERIC_GNSS_CFG_POLL_PERIOD_MS);
            continue;
        }

        for (int32_t i = 0; i < got; i++)
        {
            char c = (char)buf[i];
            if (c == '\n' || c == '\r')
            {
                if (accum_len > 0)
                {
                    accum[accum_len] = '\0';
                    double lat = 0.0, lon = 0.0, alt = 0.0;
                    if (GENERIC_GNSS_ParseBusLine(accum, &lat, &lon, &alt) == OS_SUCCESS)
                    {
                        if (OS_MutSemTake(GENERIC_GNSS_AppData.HkDataMutex) == OS_SUCCESS)
                        {
                            GENERIC_GNSS_AppData.LastBusLat = lat;
                            GENERIC_GNSS_AppData.LastBusLon = lon;
                            GENERIC_GNSS_AppData.LastBusAlt = alt;
                            GENERIC_GNSS_AppData.BusRxCount++;
                            OS_MutSemGive(GENERIC_GNSS_AppData.HkDataMutex);
                        }
                    }
                    else
                    {
                        GENERIC_GNSS_AppData.BusRxErrorCount++;
                    }
                }
                accum_len = 0;
            }
            else if (accum_len < sizeof(accum) - 1)
            {
                accum[accum_len++] = c;
            }
            else
            {
                /* Overflow on a malformed line - reset and drop. */
                accum_len = 0;
                GENERIC_GNSS_AppData.BusRxErrorCount++;
            }
        }
    }
}

int32 GENERIC_GNSS_VerifyCmdLength(CFE_MSG_Message_t *msg, uint16 expected_length)
{
    int32             status        = OS_SUCCESS;
    CFE_SB_MsgId_t    msg_id        = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_FcnCode_t cmd_code      = 0;
    size_t            actual_length = 0;

    CFE_MSG_GetSize(msg, &actual_length);
    if (expected_length != actual_length)
    {
        CFE_MSG_GetMsgId(msg, &msg_id);
        CFE_MSG_GetFcnCode(msg, &cmd_code);
        CFE_EVS_SendEvent(GENERIC_GNSS_LEN_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Invalid msg length: ID=0x%X CC=%d Len=%ld Expected=%d",
                          CFE_SB_MsgIdToValue(msg_id), cmd_code, actual_length, expected_length);
        status = OS_ERROR;
        GENERIC_GNSS_AppData.HkTelemetryPkt.CommandErrorCount++;
    }
    return status;
}
