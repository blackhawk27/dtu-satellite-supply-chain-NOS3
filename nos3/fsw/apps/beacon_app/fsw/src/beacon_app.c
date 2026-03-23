#include "cfe.h"
#include "cfe_sb.h"
#include "cfe_evs.h"
#include "cfe_msg.h"
#include "beacon_app_msgids.h"

/* Command function codes */
#define BEACON_APP_NOOP_CC            0
#define BEACON_APP_RESET_COUNTERS_CC  1
#define BEACON_APP_PING_CC            2

/* Event IDs */
#define BEACON_APP_INIT_EID           1
#define BEACON_APP_NOOP_EID           2
#define BEACON_APP_RESET_EID          3
#define BEACON_APP_PING_EID           4
#define BEACON_APP_CMD_ERR_EID        5
#define BEACON_APP_HK_EID             6

/* Telemetry packet */
typedef struct
{
    CFE_MSG_TelemetryHeader_t TlmHeader;
    uint16 CmdCounter;
    uint16 ErrCounter;
    uint32 PingCounter;
} BEACON_APP_HkTlm_t;

/* App global state */
static struct
{
    uint16             CmdCounter;
    uint16             ErrCounter;
    uint32             PingCounter;
    CFE_SB_PipeId_t    CmdPipe;
    BEACON_APP_HkTlm_t HkTlm;
} BEACON_APP_Data;

static void BEACON_APP_SendHk(void)
{
    BEACON_APP_Data.HkTlm.CmdCounter  = BEACON_APP_Data.CmdCounter;
    BEACON_APP_Data.HkTlm.ErrCounter  = BEACON_APP_Data.ErrCounter;
    BEACON_APP_Data.HkTlm.PingCounter = BEACON_APP_Data.PingCounter;
    CFE_SB_TimeStampMsg(CFE_MSG_PTR(BEACON_APP_Data.HkTlm.TlmHeader));
    CFE_SB_TransmitMsg(CFE_MSG_PTR(BEACON_APP_Data.HkTlm.TlmHeader), true);

    /* Emit counters as key=value pairs so Logstash kv filter indexes them as separate Kibana fields */
    CFE_EVS_SendEvent(BEACON_APP_HK_EID, CFE_EVS_EventType_INFORMATION,
                      "BEACON_APP PINGCOUNTER=%u CMDCOUNTER=%u ERRCOUNTER=%u",
                      (unsigned int)BEACON_APP_Data.PingCounter,
                      (unsigned int)BEACON_APP_Data.CmdCounter,
                      (unsigned int)BEACON_APP_Data.ErrCounter);
}

static void BEACON_APP_ProcessCmd(CFE_SB_Buffer_t *BufPtr)
{
    CFE_MSG_FcnCode_t FcnCode = 0;
    CFE_MSG_GetFcnCode(&BufPtr->Msg, &FcnCode);

    switch (FcnCode)
    {
        case BEACON_APP_NOOP_CC:
            BEACON_APP_Data.CmdCounter++;
            CFE_EVS_SendEvent(BEACON_APP_NOOP_EID, CFE_EVS_EventType_INFORMATION,
                              "BEACON_APP: NOOP command received (Cmds=%u)", BEACON_APP_Data.CmdCounter);
            break;

        case BEACON_APP_RESET_COUNTERS_CC:
            BEACON_APP_Data.CmdCounter = 0;
            BEACON_APP_Data.ErrCounter = 0;
            BEACON_APP_Data.PingCounter = 0;
            CFE_EVS_SendEvent(BEACON_APP_RESET_EID, CFE_EVS_EventType_INFORMATION,
                              "BEACON_APP: Counters reset");
            break;

        case BEACON_APP_PING_CC:
            BEACON_APP_Data.CmdCounter++;
            BEACON_APP_Data.PingCounter++;
            CFE_EVS_SendEvent(BEACON_APP_PING_EID, CFE_EVS_EventType_INFORMATION,
                              "BEACON_APP: PONG! Ping #%u acknowledged from ground", (unsigned int)BEACON_APP_Data.PingCounter);
            BEACON_APP_SendHk(); /* Immediately downlink a telemetry packet as the "pong" */
            break;

        default:
            BEACON_APP_Data.ErrCounter++;
            CFE_EVS_SendEvent(BEACON_APP_CMD_ERR_EID, CFE_EVS_EventType_ERROR,
                              "BEACON_APP: Unknown command code %d", (int)FcnCode);
            break;
    }
}

void BEACON_APP_Main(void)
{
    uint32           RunStatus = CFE_ES_RunStatus_APP_RUN;
    CFE_SB_Buffer_t *BufPtr;
    int32            Status;

    CFE_EVS_Register(NULL, 0, CFE_EVS_EventFilter_BINARY);
    CFE_ES_WaitForStartupSync(10000);

    memset(&BEACON_APP_Data, 0, sizeof(BEACON_APP_Data));

    /* Create command pipe and subscribe */
    CFE_SB_CreatePipe(&BEACON_APP_Data.CmdPipe, 10, "BEACON_CMD_PIPE");
    CFE_SB_Subscribe(CFE_SB_ValueToMsgId(BEACON_APP_CMD_MID),     BEACON_APP_Data.CmdPipe);
    CFE_SB_Subscribe(CFE_SB_ValueToMsgId(BEACON_APP_SEND_HK_MID), BEACON_APP_Data.CmdPipe);

    /* Initialize telemetry packet header */
    CFE_MSG_Init(CFE_MSG_PTR(BEACON_APP_Data.HkTlm.TlmHeader),
                 CFE_SB_ValueToMsgId(BEACON_APP_HK_TLM_MID),
                 sizeof(BEACON_APP_Data.HkTlm));

    CFE_EVS_SendEvent(BEACON_APP_INIT_EID, CFE_EVS_EventType_INFORMATION,
                      "BEACON_APP: Initialized. CMD=0x%04X TLM=0x%04X. Send PING to get a PONG!",
                      BEACON_APP_CMD_MID, BEACON_APP_HK_TLM_MID);

    while (CFE_ES_RunLoop(&RunStatus) == true)
    {
        Status = CFE_SB_ReceiveBuffer(&BufPtr, BEACON_APP_Data.CmdPipe, 1000);
        if (Status == CFE_SUCCESS)
        {
            CFE_SB_MsgId_t MsgId;
            CFE_MSG_GetMsgId(&BufPtr->Msg, &MsgId);

            if (CFE_SB_MsgIdToValue(MsgId) == BEACON_APP_SEND_HK_MID)
                BEACON_APP_SendHk();
            else
                BEACON_APP_ProcessCmd(BufPtr);
        }
    }

    CFE_ES_ExitApp(RunStatus);
}
