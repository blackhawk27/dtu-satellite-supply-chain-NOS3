# 04. beacon_app - Ping/Pong Beacon (DTU custom app)

This document describes the DTU-custom `beacon_app`. It is a
deliberately small cFS app that exposes a single ground command
(`PING`) and replies with a telemetry packet (`PONG`) plus a
running counter. The app exists to provide a clean, low-noise
ground-link liveness signal and a reproducible attack target for
ground-station scenarios.

## 1. Real-world hardware

`beacon_app` has no hardware analogue. It is the simplest possible
operator-facing telemetry source: an app whose only job is to
reply.

## 2. App overview

The app lives at `nos3/fsw/apps/beacon_app/fsw/src/beacon_app.c`,
133 lines of code as of the slice 1 baseline. Compact by design:
no helper headers, no message tables, no schedule entries beyond
the SCH wakeup that drives HK emission.

MIDs (defined in `beacon_app_msgids.h`, included from the source):

- Command: `BEACON_APP_CMD_MID = 0x18FA`.
- HK request: `BEACON_APP_SEND_HK_MID`.
- HK telemetry: `BEACON_APP_HK_TLM_MID`.

`beacon_app` uses the heritage 0x1800 / 0x0800 base rather than
the 0x1900 / 0x0900 component base. The choice is historical:
the app predates the component-base convention and was kept on
the heritage base to avoid touching its consumers.

Command function codes (lines 7-9):

- `BEACON_APP_NOOP_CC = 0`.
- `BEACON_APP_RESET_COUNTERS_CC = 1`.
- `BEACON_APP_PING_CC = 2`.

Event IDs (lines 12-17):

- `BEACON_APP_INIT_EID = 1` through `BEACON_APP_HK_EID = 6`.

The HK packet `BEACON_APP_HkTlm_t` (line 22) is small:

```
typedef struct {
    CFE_MSG_TelemetryHeader_t TlmHeader;
    uint16 CmdCounter;
    uint16 ErrCounter;
    uint32 PingCounter;
} BEACON_APP_HkTlm_t;
```

## 3. Initialisation and main loop

`BEACON_APP_Main` at line 92:

1. `CFE_EVS_Register` for event services.
2. `CFE_ES_WaitForStartupSync(10000)` to wait for cFE startup.
3. `memset` the global app data to zero.
4. `CFE_SB_CreatePipe(&CmdPipe, 10, "BEACON_CMD_PIPE")` (depth
   `10`).
5. `CFE_SB_Subscribe` to `BEACON_APP_CMD_MID` and
   `BEACON_APP_SEND_HK_MID`.
6. `CFE_MSG_Init` on the HK telemetry header.
7. Send `BEACON_APP_INIT_EID` with the bound MIDs in the message.
8. Enter the standard `CFE_ES_RunLoop` with
   `CFE_SB_ReceiveBuffer` timeout `1000` ms.

The dispatch in the receive loop checks the MID:

- If the message MID is `BEACON_APP_SEND_HK_MID`, call
  `BEACON_APP_SendHk` (the wakeup-driven HK emission).
- Otherwise call `BEACON_APP_ProcessCmd`, which switches on the
  command function code.

## 4. Headline function: the PING handler

The `BEACON_APP_PING_CC` arm of the `ProcessCmd` switch (lines
77-86) is the headline function for this app:

```
case BEACON_APP_PING_CC:
    BEACON_APP_Data.CmdCounter++;
    BEACON_APP_Data.PingCounter++;
    CFE_EVS_SendEvent(BEACON_APP_PING_EID,
                      CFE_EVS_EventType_INFORMATION,
                      "BEACON_APP: PONG! Ping #%u acknowledged from ground",
                      (unsigned int)BEACON_APP_Data.PingCounter);
    BEACON_APP_SendHk();
    break;
```

Three actions:

1. Increment `CmdCounter` and `PingCounter`.
2. Emit `BEACON_APP_PING_EID` with a human-readable "PONG"
   string. This goes onto the EVS stream and is what the
   operator sees first.
3. Immediately call `BEACON_APP_SendHk` so a fresh HK packet is
   downlinked as the actual "pong" telemetry.

The combination is the testbed's tight ground-link loop: a
single ping command produces both an EVS event and an updated HK
packet, both of which reach the operator within one downlink
hop. The combined signature is what makes the app a useful
liveness probe.

`BEACON_APP_SendHk` itself (lines 39-49) is also small:

```
static void BEACON_APP_SendHk(void)
{
    BEACON_APP_Data.HkTlm.CmdCounter  = BEACON_APP_Data.CmdCounter;
    BEACON_APP_Data.HkTlm.ErrCounter  = BEACON_APP_Data.ErrCounter;
    BEACON_APP_Data.HkTlm.PingCounter = BEACON_APP_Data.PingCounter;
    CFE_SB_TimeStampMsg(...);
    CFE_SB_TransmitMsg(...);
    CFE_EVS_SendEvent(BEACON_APP_HK_EID, ...,
        "BEACON_APP PINGCOUNTER=%u CMDCOUNTER=%u ERRCOUNTER=%u",
        ...);
}
```

The trailing EVS line emits the three counters as `key=value`
pairs. The pattern is deliberate: Logstash's `kv` filter parses
`key=value` pairs out of free-text log lines; emitting the
counters in that format means each of the three counters lands
in Kibana as a separate field with no per-app grok pattern
required.

## 5. ELK extraction

The Logstash filter at `nos3/elk/logstash.conf` extracts:

- `pingcounter`, `cmdcounter`, `errcounter` (from the `key=value` pattern emitted by `BEACON_APP_SendHk`).

Plus the standard EVS-line decoding: `evs_app_name`, `evs_event_id`, `evs_event_type`, `evs_message`.

Kibana panels:

- "Beacon Ping/Pong" on `nos3-std-telemetry-overview`: a monotonic line of `pingcounter` over time. A flat `pingcounter` while operator-side ping commands are sent is the link-down signature.
- The ground-link liveness indicator on `nos3-std-mission-validation`.

## 5a. Role in the GS Hello-Flow loop (2026-05-03)

`beacon_app` is the round-trip ack source for the GS Hello-Flow loop added on 2026-05-03. The Cosmos-side responder at `nos3/gsw/cosmos/COMPONENTS/GENERIC_GNSS/lib/gnss_gs_responder_task.rb` dispatches `BEACON_APP_PING_CC` and waits up to `ECHO_TIMEOUT_S = 5.0` s for the response. Each closed pass increments `pingcounter` and advances the index-template fields `last_ping_seq`, `last_ping_time`, and `ping_count` populated by the Logstash kv-extraction.

Without `beacon_app` loaded, the loop never closes; the GNSS GS validation AND-gate panel on `nos3-std-mission-validation` (advancing-seq AND ping_count > 0 AND in_denmark_box) stays dark. See [gnss.md#4a-gs-hello-flow--responder-loop-2026-05-03](gnss.md#4a-gs-hello-flow--responder-loop-2026-05-03) for the responder side.

## 6. Attack-surface relevance

`beacon_app` exists partly as a deliberate, controllable target
for two attack chains:

- **Ground-station compromise**: an attacker with COSMOS access
  can inject `BEACON_APP_RESET_COUNTERS_CC` to clear the
  counters and hide their own command-injection trail. The
  detection signal is the `cmdcounter` and `pingcounter`
  trace in Kibana: an unexpected reset is itself the alert.
- **TO_LAB filter tampering**: removing the
  `BEACON_APP_HK_TLM_MID` from the TO_LAB downlink set silences
  the operator's beacon panel without affecting any other
  telemetry. This is the cleanest "drop a single MID"
  experiment in the testbed.

The thesis chapter on ground-station-and-operations attacks
uses `beacon_app` as the experiment surface for both.

## 7. DTU divergence vs upstream

`beacon_app` is a DTU-custom app added by commit `90e024c1`. There
is no upstream baseline.

Notable design choices:

- Heritage 0x1800 / 0x0800 base (rather than the component
  0x1900 / 0x0900 base). Historical, see section 2.
- 133 lines total, deliberately compact. The goal is a minimal
  ground-link probe that does one thing visibly.
- No persistence. A reboot zeroes the counters; the operator's
  expected baseline is therefore the time since last boot.
- `key=value` EVS emission rather than a custom grok pattern.
  This trades a small amount of log noise for a much simpler
  Logstash filter.

## 8. Verification

- **Init event present**: at startup, the EVS log contains
  `BEACON_APP_INIT_EID` with `CMD=0x18FA TLM=0x18FB` (the MID
  values from `beacon_app_msgids.h`).
- **Ping/pong round-trip**: from COSMOS, send
  `BEACON_APP_PING_CC`. Within one downlink tick, COSMOS shows
  an updated HK packet with `PingCounter` incremented and an
  EVS message containing `"PONG! Ping #N acknowledged"`.
- **Counter monotonicity in Kibana**: panel "Beacon Ping/Pong"
  on `nos3-std-telemetry-overview` shows a strictly monotonic
  `pingcounter` series across the run, with steps every time
  a ping was sent.
- **Counter reset**: send `BEACON_APP_RESET_COUNTERS_CC`. The
  next HK packet shows zeroed counters; the EVS stream
  contains `BEACON_APP_RESET_EID`.
