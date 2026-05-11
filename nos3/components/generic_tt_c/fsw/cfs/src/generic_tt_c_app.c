/*******************************************************************************
** File: generic_tt_c_app.c
**
** Purpose:
**   Generic TT&C cFS application. Mirrors the cFS skeleton (NOOP / RESET /
**   ENABLE / DISABLE command set, HK and device telemetry on request) but
**   does not bring up a hardware bus: the simulator publishes link-budget
**   telemetry directly via the [TTC] log stream that Logstash ingests, so
**   the FSW HK packet starts at zero and is updated by future NOS Engine
**   bridging if and when a hardware-side link is added.
*******************************************************************************/

#include "generic_tt_c_app.h"
#include <string.h>

GENERIC_TT_C_AppData_t GENERIC_TT_C_AppData;

void TT_C_AppMain(void)
{
    int32 status = OS_SUCCESS;

    CFE_ES_PerfLogEntry(GENERIC_TT_C_PERF_ID);

    status = GENERIC_TT_C_AppInit();
    if (status != CFE_SUCCESS)
    {
        GENERIC_TT_C_AppData.RunStatus = CFE_ES_RunStatus_APP_ERROR;
    }

    while (CFE_ES_RunLoop(&GENERIC_TT_C_AppData.RunStatus) == true)
    {
        CFE_ES_PerfLogExit(GENERIC_TT_C_PERF_ID);
        status = CFE_SB_ReceiveBuffer((CFE_SB_Buffer_t **)&GENERIC_TT_C_AppData.MsgPtr,
                                      GENERIC_TT_C_AppData.CmdPipe, CFE_SB_PEND_FOREVER);
        CFE_ES_PerfLogEntry(GENERIC_TT_C_PERF_ID);
        if (status == CFE_SUCCESS)
        {
            GENERIC_TT_C_ProcessCommandPacket();
        }
        else
        {
            CFE_EVS_SendEvent(GENERIC_TT_C_PIPE_ERR_EID, CFE_EVS_EventType_ERROR,
                              "GENERIC_TT_C: SB Pipe Read Error = %d", (int)status);
            GENERIC_TT_C_AppData.RunStatus = CFE_ES_RunStatus_APP_ERROR;
        }
    }

    GENERIC_TT_C_Disable();
    CFE_ES_PerfLogExit(GENERIC_TT_C_PERF_ID);
    CFE_ES_ExitApp(GENERIC_TT_C_AppData.RunStatus);
}

int32 GENERIC_TT_C_AppInit(void)
{
    int32 status = OS_SUCCESS;

    GENERIC_TT_C_AppData.RunStatus = CFE_ES_RunStatus_APP_RUN;

    status = CFE_EVS_Register(NULL, 0, CFE_EVS_EventFilter_BINARY);
    if (status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("GENERIC_TT_C: Error registering for event services: 0x%08X\n",
                             (unsigned int)status);
        return status;
    }

    status = CFE_SB_CreatePipe(&GENERIC_TT_C_AppData.CmdPipe, GENERIC_TT_C_PIPE_DEPTH, "TT_C_CMD_PIPE");
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(GENERIC_TT_C_PIPE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Error Creating SB Pipe,RC=0x%08X", (unsigned int)status);
        return status;
    }

    status = CFE_SB_Subscribe(CFE_SB_ValueToMsgId(GENERIC_TT_C_CMD_MID), GENERIC_TT_C_AppData.CmdPipe);
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(GENERIC_TT_C_SUB_CMD_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Error Subscribing to HK Gnd Cmds, MID=0x%04X, RC=0x%08X",
                          GENERIC_TT_C_CMD_MID, (unsigned int)status);
        return status;
    }

    status = CFE_SB_Subscribe(CFE_SB_ValueToMsgId(GENERIC_TT_C_REQ_HK_MID), GENERIC_TT_C_AppData.CmdPipe);
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(GENERIC_TT_C_SUB_REQ_HK_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Error Subscribing to HK Request, MID=0x%04X, RC=0x%08X",
                          GENERIC_TT_C_REQ_HK_MID, (unsigned int)status);
        return status;
    }

    CFE_MSG_Init(CFE_MSG_PTR(GENERIC_TT_C_AppData.HkTelemetryPkt.TlmHeader),
                 CFE_SB_ValueToMsgId(GENERIC_TT_C_HK_TLM_MID), GENERIC_TT_C_HK_TLM_LNGTH);
    CFE_MSG_Init(CFE_MSG_PTR(GENERIC_TT_C_AppData.DevicePkt.TlmHeader),
                 CFE_SB_ValueToMsgId(GENERIC_TT_C_DEVICE_TLM_MID), GENERIC_TT_C_DEVICE_TLM_LNGTH);

    GENERIC_TT_C_ResetCounters();

    GENERIC_TT_C_AppData.HkTelemetryPkt.DeviceEnabled              = GENERIC_TT_C_DEVICE_DISABLED;
    GENERIC_TT_C_AppData.HkTelemetryPkt.DeviceHK.LinkState         = GENERIC_TT_C_LINK_STATE_IDLE;
    GENERIC_TT_C_AppData.HkTelemetryPkt.DeviceHK.PassNumber        = 0;
    GENERIC_TT_C_AppData.HkTelemetryPkt.DeviceHK.PassCount         = 0;
    GENERIC_TT_C_AppData.HkTelemetryPkt.DeviceHK.PacketsDownlinked = 0;
    GENERIC_TT_C_AppData.HkTelemetryPkt.DeviceHK.BytesDownlinked   = 0;
    GENERIC_TT_C_AppData.HkTelemetryPkt.DeviceHK.PacketsDropped    = 0;
    memset(GENERIC_TT_C_AppData.HkTelemetryPkt.DeviceHK.GroundStation, 0, GENERIC_TT_C_GS_NAME_LEN);

    status = CFE_EVS_SendEvent(GENERIC_TT_C_STARTUP_INF_EID, CFE_EVS_EventType_INFORMATION,
                               "GENERIC_TT_C App Initialized. Version %d.%d.%d.%d",
                               GENERIC_TT_C_MAJOR_VERSION, GENERIC_TT_C_MINOR_VERSION,
                               GENERIC_TT_C_REVISION, GENERIC_TT_C_MISSION_REV);
    if (status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("GENERIC_TT_C: Error sending initialization event: 0x%08X\n",
                             (unsigned int)status);
    }
    return status;
}

void GENERIC_TT_C_ProcessCommandPacket(void)
{
    CFE_SB_MsgId_t MsgId = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_GetMsgId(GENERIC_TT_C_AppData.MsgPtr, &MsgId);
    switch (CFE_SB_MsgIdToValue(MsgId))
    {
        case GENERIC_TT_C_CMD_MID:
            GENERIC_TT_C_ProcessGroundCommand();
            break;
        case GENERIC_TT_C_REQ_HK_MID:
            GENERIC_TT_C_ProcessTelemetryRequest();
            break;
        default:
            GENERIC_TT_C_AppData.HkTelemetryPkt.CommandErrorCount++;
            CFE_EVS_SendEvent(GENERIC_TT_C_PROCESS_CMD_ERR_EID, CFE_EVS_EventType_ERROR,
                              "GENERIC_TT_C: Invalid command packet, MID = 0x%x",
                              CFE_SB_MsgIdToValue(MsgId));
            break;
    }
}

void GENERIC_TT_C_ProcessGroundCommand(void)
{
    CFE_SB_MsgId_t    MsgId       = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_FcnCode_t CommandCode = 0;
    CFE_MSG_GetMsgId(GENERIC_TT_C_AppData.MsgPtr, &MsgId);
    CFE_MSG_GetFcnCode(GENERIC_TT_C_AppData.MsgPtr, &CommandCode);
    switch (CommandCode)
    {
        case GENERIC_TT_C_NOOP_CC:
            if (GENERIC_TT_C_VerifyCmdLength(GENERIC_TT_C_AppData.MsgPtr,
                                             sizeof(GENERIC_TT_C_NoArgs_cmd_t)) == OS_SUCCESS)
            {
                GENERIC_TT_C_AppData.HkTelemetryPkt.CommandCount++;
                CFE_EVS_SendEvent(GENERIC_TT_C_CMD_NOOP_INF_EID, CFE_EVS_EventType_INFORMATION,
                                  "GENERIC_TT_C: NOOP command received");
            }
            break;
        case GENERIC_TT_C_RESET_COUNTERS_CC:
            if (GENERIC_TT_C_VerifyCmdLength(GENERIC_TT_C_AppData.MsgPtr,
                                             sizeof(GENERIC_TT_C_NoArgs_cmd_t)) == OS_SUCCESS)
            {
                CFE_EVS_SendEvent(GENERIC_TT_C_CMD_RESET_INF_EID, CFE_EVS_EventType_INFORMATION,
                                  "GENERIC_TT_C: RESET counters command received");
                GENERIC_TT_C_ResetCounters();
            }
            break;
        case GENERIC_TT_C_ENABLE_CC:
            if (GENERIC_TT_C_VerifyCmdLength(GENERIC_TT_C_AppData.MsgPtr,
                                             sizeof(GENERIC_TT_C_NoArgs_cmd_t)) == OS_SUCCESS)
            {
                CFE_EVS_SendEvent(GENERIC_TT_C_CMD_ENABLE_INF_EID, CFE_EVS_EventType_INFORMATION,
                                  "GENERIC_TT_C: Enable command received");
                GENERIC_TT_C_Enable();
            }
            break;
        case GENERIC_TT_C_DISABLE_CC:
            if (GENERIC_TT_C_VerifyCmdLength(GENERIC_TT_C_AppData.MsgPtr,
                                             sizeof(GENERIC_TT_C_NoArgs_cmd_t)) == OS_SUCCESS)
            {
                CFE_EVS_SendEvent(GENERIC_TT_C_CMD_DISABLE_INF_EID, CFE_EVS_EventType_INFORMATION,
                                  "GENERIC_TT_C: Disable command received");
                GENERIC_TT_C_Disable();
            }
            break;
        default:
            GENERIC_TT_C_AppData.HkTelemetryPkt.CommandErrorCount++;
            CFE_EVS_SendEvent(GENERIC_TT_C_CMD_ERR_EID, CFE_EVS_EventType_ERROR,
                              "GENERIC_TT_C: Invalid command code, MID=0x%x cmd=0x%x",
                              CFE_SB_MsgIdToValue(MsgId), CommandCode);
            break;
    }
}

void GENERIC_TT_C_ProcessTelemetryRequest(void)
{
    CFE_SB_MsgId_t    MsgId       = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_FcnCode_t CommandCode = 0;
    CFE_MSG_GetMsgId(GENERIC_TT_C_AppData.MsgPtr, &MsgId);
    CFE_MSG_GetFcnCode(GENERIC_TT_C_AppData.MsgPtr, &CommandCode);
    switch (CommandCode)
    {
        case GENERIC_TT_C_REQ_HK_TLM:
            GENERIC_TT_C_ReportHousekeeping();
            break;
        case GENERIC_TT_C_REQ_DATA_TLM:
            GENERIC_TT_C_ReportDeviceTelemetry();
            break;
        default:
            GENERIC_TT_C_AppData.HkTelemetryPkt.CommandErrorCount++;
            CFE_EVS_SendEvent(GENERIC_TT_C_DEVICE_TLM_ERR_EID, CFE_EVS_EventType_ERROR,
                              "GENERIC_TT_C: Invalid tlm cmd code, MID=0x%x cmd=0x%x",
                              CFE_SB_MsgIdToValue(MsgId), CommandCode);
            break;
    }
}

void GENERIC_TT_C_ReportHousekeeping(void)
{
    if (GENERIC_TT_C_AppData.HkTelemetryPkt.DeviceEnabled == GENERIC_TT_C_DEVICE_ENABLED)
    {
        GENERIC_TT_C_AppData.HkTelemetryPkt.DeviceCount++;
    }
    CFE_SB_TimeStampMsg((CFE_MSG_Message_t *)&GENERIC_TT_C_AppData.HkTelemetryPkt);
    CFE_SB_TransmitMsg((CFE_MSG_Message_t *)&GENERIC_TT_C_AppData.HkTelemetryPkt, true);
}

void GENERIC_TT_C_ReportDeviceTelemetry(void)
{
    if (GENERIC_TT_C_AppData.HkTelemetryPkt.DeviceEnabled == GENERIC_TT_C_DEVICE_ENABLED)
    {
        memcpy(&GENERIC_TT_C_AppData.DevicePkt.DeviceHK,
               &GENERIC_TT_C_AppData.HkTelemetryPkt.DeviceHK,
               sizeof(GENERIC_TT_C_Device_HK_tlm_t));
        CFE_SB_TimeStampMsg((CFE_MSG_Message_t *)&GENERIC_TT_C_AppData.DevicePkt);
        CFE_SB_TransmitMsg((CFE_MSG_Message_t *)&GENERIC_TT_C_AppData.DevicePkt, true);
        GENERIC_TT_C_AppData.HkTelemetryPkt.DeviceCount++;
    }
}

void GENERIC_TT_C_ResetCounters(void)
{
    GENERIC_TT_C_AppData.HkTelemetryPkt.CommandErrorCount = 0;
    GENERIC_TT_C_AppData.HkTelemetryPkt.CommandCount      = 0;
    GENERIC_TT_C_AppData.HkTelemetryPkt.DeviceErrorCount  = 0;
    GENERIC_TT_C_AppData.HkTelemetryPkt.DeviceCount       = 0;
}

void GENERIC_TT_C_Enable(void)
{
    if (GENERIC_TT_C_AppData.HkTelemetryPkt.DeviceEnabled == GENERIC_TT_C_DEVICE_DISABLED)
    {
        GENERIC_TT_C_AppData.HkTelemetryPkt.CommandCount++;
        GENERIC_TT_C_AppData.HkTelemetryPkt.DeviceEnabled = GENERIC_TT_C_DEVICE_ENABLED;
        CFE_EVS_SendEvent(GENERIC_TT_C_ENABLE_INF_EID, CFE_EVS_EventType_INFORMATION,
                          "GENERIC_TT_C: Device enabled");
    }
    else
    {
        GENERIC_TT_C_AppData.HkTelemetryPkt.CommandErrorCount++;
        CFE_EVS_SendEvent(GENERIC_TT_C_ENABLE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "GENERIC_TT_C: Device enable failed, already enabled");
    }
}

void GENERIC_TT_C_Disable(void)
{
    if (GENERIC_TT_C_AppData.HkTelemetryPkt.DeviceEnabled == GENERIC_TT_C_DEVICE_ENABLED)
    {
        GENERIC_TT_C_AppData.HkTelemetryPkt.CommandCount++;
        GENERIC_TT_C_AppData.HkTelemetryPkt.DeviceEnabled = GENERIC_TT_C_DEVICE_DISABLED;
        CFE_EVS_SendEvent(GENERIC_TT_C_DISABLE_INF_EID, CFE_EVS_EventType_INFORMATION,
                          "GENERIC_TT_C: Device disabled");
    }
    else
    {
        GENERIC_TT_C_AppData.HkTelemetryPkt.CommandErrorCount++;
        CFE_EVS_SendEvent(GENERIC_TT_C_DISABLE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "GENERIC_TT_C: Device disable failed, already disabled");
    }
}

int32 GENERIC_TT_C_VerifyCmdLength(CFE_MSG_Message_t *msg, uint16 expected_length)
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
        CFE_EVS_SendEvent(GENERIC_TT_C_LEN_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Invalid msg length: ID=0x%X CC=%d Len=%ld Expected=%d",
                          CFE_SB_MsgIdToValue(msg_id), cmd_code, actual_length, expected_length);
        status = OS_ERROR;
        GENERIC_TT_C_AppData.HkTelemetryPkt.CommandErrorCount++;
    }
    return status;
}
