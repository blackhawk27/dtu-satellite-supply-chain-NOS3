# noisy_app escalation design - persistent EPS spoof and full SB denial

> **DTU secured-fork note.** Ported from the Draco baseline (commit `dd1d2cbb`). In this repo the LC battery autonomy is WP #27 (`< 24240` mV) -> AP #27 -> RTS 27 (not Draco's WP0/AP0 SAFE chain), and the DEBUG-path injection means CryptoLib does not gate the attack. Scenarios `0x02/0x04/0x06/0x08` are in scope; IMU (`0x0A`) and CDS persistence are Draco-only. See [`00-dtu-secured-fork-notes.md`](00-dtu-secured-fork-notes.md).


Design analysis (with concrete code) for two escalations of the `noisy_app`
attack fixture, beyond the bounded demonstrations currently shipped:

1. **Routine EPS spoofing that defeats the real EPS** - so the spoof, not the
   genuine sensor, is what LC and the ground always see.
2. **Full Software Bus denial** - lock the single shared 512 KB SB buffer pool
   (not per-pipe flooding) so every app's `TransmitMsg` fails and comms stop.

Both are deliberately *not enabled by default*. They are destructive research
modes: #1 corrupts the autonomy inputs continuously; #2 makes the sim
unresponsive until restart. They stay behind the same reversible research toggle
(`targets.cmake` / `cpu1_cfe_es_startup.scr`) as the rest of noisy_app, and each
is paired below with its detection and mitigation (the thesis point: these are
loud in the right telemetry even when the endpoint defence misses them).

Context from the live run (2026-06-03, see `threat-analysis.md` Appendix A):
- The real EPS sim publishes `GENERIC_EPS_HK` (MID `0x091A`) at ~1 Hz (SCH
  `EPS HK Request`). LC actionpoints are **sampled periodically** (SCH
  `LC_SAMPLE_AP`), and `MaxFailsBeforeRTS` counts *consecutive AP-sample* fails.
- A single injected spoof loses the race: the next genuine ~1 Hz packet resets
  WP0 to PASS before the AP sample. The PoC only forced SAFE by bursting the
  spoof at ~8 Hz and re-arming the one-shot actionpoint.
- The SB is a single 512 KB pool (`CFE_PLATFORM_SB_BUF_MEMORY_BYTES = 524288`)
  shared by all apps/pipes; `CFE_PLATFORM_SB_DEFAULT_MSG_LIMIT = 4` per MID per
  pipe; max packet `CFE_MISSION_SB_MAX_SB_MSG_SIZE = 32768`. (All re-verified
  against cpu1_platform_cfg.h / cfe_mission_cfg.h - see section 2.)

---

## 1. Routine EPS spoof that defeats the real EPS

### Why the current version is not enough

`NOISY_SpoofEpsHk()` sends exactly one packet when triggered. To *defeat* the
real EPS you must guarantee the spoof is the value LC and COSMOS see at decision
time, continuously - i.e. win the temporal race against every genuine packet,
not just inject once.

### Technique: reactive override (subscribe to the victim's own MID)

The robust method is not rate-guessing but **reaction**: co-subscribe to the
victim telemetry MID (`0x091A`) and, the instant a genuine EPS HK arrives,
immediately transmit a spoofed one. Because SB delivery is last-writer-wins for
any later sampler (LC's AP sample, COSMOS display, DS recorder), the spoof that
follows each genuine packet is always the most recent value. This makes the
override independent of the real publish rate and of LC's sample phase.

Two intents (phase3 Strategy A/B):
- **Mask** (stealth, the dangerous one): emit a *healthy* value (e.g. 25000 mV)
  right after each genuine packet, so even when the real battery genuinely sags,
  WP0 never trips and the protective SAFE RTS is suppressed.
- **Force**: emit a *low* value (10000 mV) to drive SAFE persistently.

### Code (new opcode `OPCODE_EPS_OVERRIDE = 0x06`)

```c
#define OPCODE_EPS_OVERRIDE 0x06   /* persistent reactive EPS spoof */
#define OVERRIDE_BATTERY_MV 25000  /* mask: healthy value to suppress SAFE
                                      (use 10000 to force SAFE instead)      */

static bool   NOISY_OverrideOn = false;

/* Build a spoofed EPS HK with a chosen battery value. */
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
```

In `NOISY_APP_Main()`, also subscribe to the victim MID and react:

```c
CFE_SB_Subscribe(CFE_SB_ValueToMsgId(GENERIC_EPS_HK_TLM_MID), CmdPipe);
...
/* inside the receive loop, after MsgId/Size are read: */
if (NOISY_OverrideOn &&
    CFE_SB_MsgIdToValue(MsgId) == GENERIC_EPS_HK_TLM_MID)
{
    /* Avoid reacting to our own injected packet (feedback loop): only
       chase a packet whose battery is NOT already our override value. */
    const NOISY_EpsHkMimic_t *in = (const void *)BufPtr;
    if (in->BatteryVoltage != OVERRIDE_BATTERY_MV)
        NOISY_SendEpsHk(OVERRIDE_BATTERY_MV);   /* shadow it immediately */
    continue;
}
```

`OPCODE_EPS_OVERRIDE` just flips `NOISY_OverrideOn = true` (a piggyback
`0x06`); a `0x00`/disable opcode turns it off. No timers needed - the victim's
own cadence drives the attack, so the spoof tracks the real rate exactly.

Loop-avoidance is the only subtlety: because noisy_app is now subscribed to
`0x091A`, it receives its own spoof. The `BatteryVoltage != OVERRIDE_BATTERY_MV`
guard (or a reserved sentinel in an unused field) prevents amplification. If you
want both mask and the occasional genuine value hidden, track the last-seen real
sequence and only shadow new genuine packets.

### Detection (already mostly in the pipeline)

- **Duplicate MID per frame**: two `0x091A` packets per ~1 s window (phase3 Q13).
  KQL on the god view: `msg_id:"0x091A"` then alert count > 1 per 100 ms.
- **CCSDS sequence discontinuity**: the spoof's `CFE_MSG_Init` resets/duplicates
  the per-stream sequence counter; genuine EPS increments monotonically. A
  non-monotonic `sequence` for `0x091A` is unambiguous (phase3 Q12).
- **Value/physics sanity**: battery flat-lined at exactly 25000 (or 10000) with
  zero noise, while solar/Temperature fields stay 0, is non-physical.

### Mitigation

- Provenance on LC inputs (mitigation 5 in `threat-analysis.md`): require
  corroboration (multiple sources / rate-of-change sanity) instead of trusting a
  single bus HK field.
- SB subscriber review: an app subscribing to *and publishing* another app's
  telemetry MID is the build-time red flag (mitigation 4/6).

---

## 2. Full Software Bus denial (drown the pool and all pipes)

### Verified platform values (cpu1_platform_cfg.h / cfe_mission_cfg.h)

| Parameter | Value | Line |
|---|---|---|
| `CFE_PLATFORM_SB_BUF_MEMORY_BYTES` (the whole SB pool) | **524288** (512 KB) | cpu1_platform_cfg.h:156 |
| `CFE_PLATFORM_SB_DEFAULT_MSG_LIMIT` (per-MID, per-pipe) | **4** | :134 |
| `CFE_MISSION_SB_MAX_SB_MSG_SIZE` (max packet) | **32768** (32 KB) | cfe_mission_cfg.h:53 |
| `CFE_PLATFORM_SB_MAX_BLOCK_SIZE` | 32768 + 128 | cpu1_platform_cfg.h:306 |
| `CFE_PLATFORM_SB_MAX_MSG_IDS` | 256 | :86 |
| `CFE_PLATFORM_SB_MAX_PIPE_DEPTH` | cFE default (>= 128; TO uses 128) | - |

### Two corrections to the naive model

**(a) Pipe depth != per-MID limit.** A *single*-MID flood is capped not by the
pipe depth but by `MsgId2PipeLim` (the per-(MID,pipe) `MsgLimit`), default **4**
(`cfe_sb_api.c:986`). So sending a thousand copies of one MID to a victim pipe
only ever parks **4** of them; the rest are dropped with `CFE_SB_MSGID_LIM_ERR`
and the buffers are freed again. The large pipe depths in this build (TO=128,
TO_LAB-TLM=50, MD=50, DS=45, HK=40, CI_LAB=32, HS-event=32) are the *total*
across all MIDs on that pipe and do **not** help a single-MID flood. Flooding a
victim's command MID does deny *that* MID's legitimate traffic (its 4 slots stay
full of garbage), but it does not lock the system.

**(b) Buffers with no live destination are freed immediately.** `TransmitMsg`
allocates one buffer, hands a reference to each subscribed pipe that still has
room, then drops its own reference; if no pipe kept it, the use-count hits 0 and
it is freed at once (`CFE_SB_DecrBufUseCnt`, `cfe_sb_api.c:404/1425`). So
flooding a MID with **no subscribers** consumes **zero** lasting pool memory -
the legacy "transmit on all 8192 MIDs" storm mostly frees itself. A buffer
occupies the pool only while it is queued, unread, in some pipe.

### Most destructive scenario for this testbed: a self-hosted pool lock

The single 512 KB pool is the one unprotected, globally shared resource, and
(b) tells us how to pin it: get max-size buffers to sit **unread** in a pipe the
attacker controls. noisy_app can do this entirely to itself - no victim drain
rates to race:

1. Create its own pipe with a large depth (up to `MAX_PIPE_DEPTH`).
2. `CFE_SB_SubscribeEx` a private dummy MID on that pipe with a high `MsgLim`
   (it controls its own subscription - `MsgLim` is whatever it asks for).
3. Transmit **max-size (32 KB)** messages on that MID and **never** call
   `ReceiveBuffer` on the pipe. Each transmit parks one 32 KB buffer in the
   pipe (allowed because its own `MsgLim` is high).
4. After `524288 / (32768+128)` = **~15 messages**, the pool is empty and stays
   empty - the buffers are locked in noisy_app's own undrained pipe.

From that point **every other `CFE_SB_TransmitMsg` in the system fails** with
`CFE_SB_BUF_ALOC_ERR`:
- EPS/RW/GPS/IMU/... HK never reaches the bus -> all telemetry gone.
- `CI_LAB` cannot forward an uplinked command onto the SB -> **ground commanding
  dies** even though the UDP socket still accepts packets.
- `TO_LAB` gets nothing to downlink -> **ground goes blind**.
- LC watchpoints receive no fresh HK; after `ResultAgeWhenStale` they go stale
  (treated as PASS) -> autonomy/monitoring blind.

This is far more efficient and *stable* than the legacy 32k-packet storm: ~15
packets, no continuous CPU burn required, and it does not depend on out-running
any consumer. It is the worst case because it weaponises the shared pool
directly rather than fighting per-pipe `MsgLimit`s.

```c
#define OPCODE_SB_FLOOD 0x08   /* DESTRUCTIVE: locks the SB pool (research only) */
#define LOCK_MID        NOISY_APP_HK_TLM_MID   /* a MID we own and never drain */
#define LOCK_MSGLIM     64                     /* >> 15, so all our buffers park */

typedef struct {
    CFE_MSG_TelemetryHeader_t Hdr;
    uint8 Pad[CFE_MISSION_SB_MAX_SB_MSG_SIZE - sizeof(CFE_MSG_TelemetryHeader_t)];
} NOISY_MaxPkt_t;

static void NOISY_SbPoolLock(void)
{
    static NOISY_MaxPkt_t big;              /* 32 KB: keep it off the stack */
    CFE_SB_PipeId_t sink;

    /* A deep pipe we deliberately never read, with a high per-MID limit so our
       own buffers are allowed to pile up instead of being MSGID_LIM-dropped. */
    CFE_SB_CreatePipe(&sink, 128, "NOISY_SINK");
    CFE_SB_SubscribeEx(CFE_SB_ValueToMsgId(LOCK_MID), sink,
                       CFE_SB_DEFAULT_QOS, LOCK_MSGLIM);

    CFE_EVS_SendEvent(NOISY_EVT_SB_BURST, CFE_EVS_EventType_CRITICAL,
                      "NOISY_APP: SB POOL LOCK engaged (DoS, research mode).");

    memset(&big, 0, sizeof(big));
    CFE_MSG_Init(CFE_MSG_PTR(big.Hdr), CFE_SB_ValueToMsgId(LOCK_MID), sizeof(big));

    /* ~15 of these exhaust the 512 KB pool; they stay parked in NOISY_SINK
       because we never ReceiveBuffer() it. Keep pushing to re-lock anything
       that frees, then idle - no busy spin needed once the pool is dry. */
    for (int i = 0; i < LOCK_MSGLIM; i++)
    {
        CFE_SB_TimeStampMsg(CFE_MSG_PTR(big.Hdr));
        if (CFE_SB_TransmitMsg(CFE_MSG_PTR(big.Hdr), true) != CFE_SUCCESS)
            break;                          /* pool is now dry: lock achieved */
    }
    /* Return; the parked buffers keep the pool empty with no further CPU use. */
}
```

A nastier variant adds a slow top-up timer (re-transmit one max-size packet
whenever the pool frees a slot) to defeat any periodic relief, but the static
lock above already produces a total, persistent blackout. To also out-prioritise
recovery, the app's `cpu1_cfe_es_startup.scr` priority (currently 29) would be
raised; at any priority the lock holds because the buffers are simply never
returned to the pool.

### Cost / safety (why it is gated)

This makes the sim unrecoverable without `make stop` + relaunch, and will
trip the documented 42/sim sensitivities. It is the inverse of the shipped
`SB_BURST` (which is hard-capped at 64 with yields specifically to stay alive).
Keep `OPCODE_SB_FLOOD` behind the research toggle and never in a mission build.

### Detection

- **The drop events are throttled, then silent.** `CFE_SB_MSGID_LIM_ERR_EID`
  (cpu1_platform_cfg.h:261) and `CFE_SB_Q_FULL_ERR_EID` (:264) are both
  registered with mask `CFE_EVS_FIRST_16_STOP` - the first 16 occurrences emit,
  then EVS suppresses the rest. A sustained flood therefore goes quiet after 16
  events. The pool-exhaustion path (`CFE_SB_GetBufferFromPool` failure) is *not*
  in the filter list, but once the pool is dry EVS itself may be unable to send.
  Mitigation-side: lower these masks (or alert on the *first* occurrence) so the
  onset is visible.
- **Sudden simultaneous absence of all HK** (phase4 Q20) is the most reliable
  signal: EPS+RW+GPS HK all gone for more than a few seconds is the blackout
  signature even when EVS is starved. This is the detection to build, because it
  does not rely on the (throttled) SB events.
- **CPU**: the static pool-lock does *not* peg CPU (it stops after ~15 sends), so
  CPU saturation only catches the busy-spin variant - do not rely on it alone.
- **noisy_app armed event**: the `SB POOL LOCK engaged` CRITICAL event (Q21),
  if it escapes before the pool is dry, attributes the outage to the attack.

### Mitigation

- **Per-app pool quotas / rate limiting** so one publisher cannot consume the
  whole 512 KB arena.
- **Subscriber + publisher allowlist** reviewed at build time (mitigation 4/6) -
  an app publishing on other apps' command MIDs should never ship.
- **Un-filter the SB overflow events** so denial is observable.
- Authenticate/round-trip the uplink (mitigation 1) so a denied CI_LAB forward is
  at least detectable as lost command acks at the ground.

---

## Wiring (both)

Add the opcodes to the dispatcher in `NOISY_APP_RunScenario()`:

```c
case OPCODE_EPS_OVERRIDE: NOISY_OverrideOn = true;  break;   /* 0x06 */
case OPCODE_SB_FLOOD:     NOISY_SbPoolLock();        break;   /* 0x08 (pool lock) */
```

They are driven by the same piggyback path already proven live
(`CI_DEBUG CI_DEBUG_PIGGYBACK_NOOP_CC with OPCODE 6` / `8`, or
`drive_poc.py`). Neither is enabled by default; both are reversible by the
existing toggle and by removing these cases.
