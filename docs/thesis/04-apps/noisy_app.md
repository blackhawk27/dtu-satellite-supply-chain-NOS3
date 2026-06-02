# `noisy_app`

## Purpose

`noisy_app` is the supply-chain testbed's centrepiece: a cFS
application written and shipped by the DTU fork to model what a
compromised payload looks like from inside the spacecraft. It
ships through the same build path as every legitimate cFS app,
loads through the same `cfe_es_startup.scr`, and registers itself
through the same `cfe_sb` API. From the running cFS process's
point of view it is indistinguishable from any other app. Its
purpose is to give every other component of the testbed (the
Software Bus visibility, the ELK pipeline, the `cfs_god_view.json`
record, the Kibana dashboards) a real attacker to observe.

## Source files

- `nos3/fsw/apps/noisy_app/fsw/src/noisy_app.c` is the entire
  application; it is roughly 120 lines and self-contained. There
  is no header file and no platform-include layer because the app
  reuses the EPS app's message headers
  (`generic_eps_msg.h`, `generic_eps_msgids.h`) to spoof EPS
  telemetry without re-declaring the struct.
- `nos3/fsw/apps/noisy_app/CMakeLists.txt` registers the app as an
  ordinary cFS app target. The 2026-03 cFS 7.0 hardening commit
  (`32257387`) corrected the include paths so the EPS-header
  cross-import resolves at build time.

The app is registered in `cpu1_cfe_es_startup.scr` under
`cfg/build/nos3_defs/` (the `make config`-generated startup
script) but it is ENTERED AS `OFF_APP`, NOT `CFE_APP`. cFE skips
`OFF_APP` lines at startup, so `noisy_app` does not load on boot
under the default profile. The entry sits inside a
`===_DTU_THESIS_MENU_===` block in the source script at
`cfg/nos3_defs/cpu1_cfe_es_startup.scr`; the operator flips a
single keyword (`OFF_APP` -> `CFE_APP`) and rebuilds the FSW
image to arm the testbed. The compromised binary is still
shipped on every clean build: it sits in
`fsw/build/exe/cpu1/` after `make fsw` regardless of whether the
operator flipped the keyword. The supply-chain framing is
therefore "the malicious binary is in the image; only its boot
hook is gated", not "the malicious app loads automatically".

## Wire-level surface

`noisy_app` is interesting at exactly two points on the Software
Bus, both via the trigger MID `0x18F0`
(`MALWARE_TRIGGER_MID` in `noisy_app.c:9`). The app subscribes to
this MID at startup and dispatches on the CCSDS function code
field of the received packet:

- **Function code 2 (`BEACON_PING_FC`).** Arms the attack. The
  app counts ping packets internally; the third one
  (`TRIGGER_THRESHOLD = 3`) flips it into the broadcast-storm
  phase. The arming phase emits two EVS events:
  `NOISY_APP: PING #N intercepted. N of 3.` (event id 3,
  INFORMATION) and on the third ping
  `NOISY_APP: THRESHOLD REACHED. OMNIDIRECTIONAL STORM INITIATED.`
  (event id 2, CRITICAL).
- **Function code 3 (`SPOOF_EPS_FC`).** Issues a single forged
  EPS housekeeping telemetry packet under
  `GENERIC_EPS_HK_TLM_MID` (`0x091A`) with
  `BatteryVoltage = 0xDEAD`. The app calls `CFE_MSG_Init`,
  `CFE_SB_TimeStampMsg`, and `CFE_SB_TransmitMsg` directly, so
  from the Software Bus's perspective the forged packet is
  indistinguishable from a legitimate EPS publish. The forgery
  emits an EVS event `NOISY_APP: EPS HK SPOOF transmitted
  (BatteryVoltage=0xDEAD).` (event id 4, CRITICAL).

In the armed state the app stops being well-behaved and enters
two simultaneous behaviours that are documented in the source
comments as PHASE 2:

- A tight integer-arithmetic loop
  (`5_000_000` iterations of `burn += i * i`) executed on every
  cycle. The comment in the source labels this "CPU BURN" and
  notes its purpose as raising core power and temperature.
- A "carpet bombing" loop that iterates every possible MID in
  the cFS namespace (`0x0000` through `0x1FFF`) and publishes a
  4 KB packet to each one four times in a row. The buffer pool
  on cFS is 512 KB; the comment in the source notes that the
  pool fills in a fraction of a millisecond, which is what
  drives the `SB_PIPE_OVERFLOW` events that the Logstash
  pipeline tags as `sb_pipe_overflow`.

The app intentionally omits any `OS_TaskDelay` in the armed
state. The source comment makes the rationale explicit: at task
priority 20, with no yield, the loop permanently hijacks the cFS
scheduler. This is the symptom that the testbed's CPU monitor
(`scripts/cpu_monitor.sh`) and the cFS `hs` (health and safety)
app are meant to detect.

Figure F6 in [../08-figures/figures.md](../08-figures/figures.md)
draws the full attack timeline: load, three pings, threshold,
storm, observable tags. Subsequent sections in this document
treat that figure as the reference for the order of operations.

## Telemetry it emits

The app publishes nothing under its own MID range. Everything it
produces is either an EVS event or a spoofed telemetry packet
under another component's MID. The Logstash pipeline picks the
EVS events up via `cfs_evs.log` and tags them. The relevant
filter blocks live in `nos3/elk/logstash.conf` around lines 328
through 347:

- `attack_app => NOISY_APP` is added as a field on any
  `system_log` document originating from `NOISY_APP`.
- Event id 2 produces the tag `attack_armed`.
- Event id 3 produces the tag `attack_trigger_ping`.
- The startup event (id 1) produces the tag `attack_loaded`.
- The `SB_PIPE_OVERFLOW` event from `cfe_sb` (a side effect of
  the carpet-bombing loop) produces the tag `sb_pipe_overflow`.

The forged EPS packet itself appears in
`nos3/attack_logs/cfs_god_view.json` as a `software_bus` document
with `msg_id = 0x091A`, `msg_name = GENERIC_EPS_HK_TLM`, the
`BatteryVoltage = 0xDEAD` payload, and a sequence counter one
greater than the previous legitimate EPS publish on the same
MID. The sequence gap between the two consecutive publishes is
the forensic signature: in a clean run, only the EPS driver
publishes that MID, and the counter advances monotonically.

The `noisy_app_spam_target` tag (logstash.conf line 150-155) is
applied only to seven specific MIDs:

```
0x1806 (ES), 0x1801 (EVS), 0x1803 (SB), 0x1805 (TIME),
0x1804 (TBL), 0x1884 (CI),  0x1880 (TO)
```

These are the cFE-core-service and uplink/downlink MIDs whose
saturation has the highest operational impact, not the full
carpet-bomb range. The current `noisy_app.c` actually iterates
every MID in `0x0000..0x1FFF`, so most of the storm packets do
NOT receive this tag in Kibana. The storm is still detectable
through the `attack_armed` and `sb_pipe_overflow` tags (both fire
on EVS events that always accompany the storm) and through the
raw burst rate on `software_bus` documents. Widening the
`noisy_app_spam_target` tag set to cover the whole carpet-bomb
range would be a one-line Logstash edit; the current narrow tag
is preserved deliberately so the seven cFE-service MIDs are
visible as a distinct Kibana series.

## What was changed from upstream

The app does not exist upstream. The closest analogue in the
import baseline is the `sample` app, which is the upstream-shipped
template for "minimal new cFS app". `noisy_app` is a research
fork addition, first introduced in commit `dd750790`
("feat: implemented and staged noisy_app broadcast storm
attack"). Subsequent commits added the OFF_APP payload menu
(`c1642e3c`), the cFS 7.0 build hardening (`32257387`), and the
EPS HK spoof (`e9465af8`).

The supply-chain framing is that the upstream NOS3 build path
itself is the attack vector. A repository fork that pulls
`noisy_app` into `fsw/apps/` and registers it in
`cpu1_cfe_es_startup.scr` produces a flight build that loads the
malicious app on boot. The upstream cFS has no countermeasure for
this because the cFS trust model treats every loaded app as
co-trusted. `noisy_app` is the concrete instance of the abstract
threat described in
[../03-communication/02-fsw-software-bus.md](../03-communication/02-fsw-software-bus.md).

## Load-bearing invariants

A small number of details have to stay aligned for the attack
experiments to remain reproducible. None of these have yet
broken in production. They are recorded here so that future
edits do not silently change attack semantics.

- **`MALWARE_TRIGGER_MID` must stay `0x18F0`.** The Kibana
  filter for `beacon_cmd` (logstash.conf line 160) keys off this
  exact MID. The function-code dispatch (`BEACON_PING_FC = 2`
  and `SPOOF_EPS_FC = 3`) is also encoded in the same filter
  block. Changing either value silently breaks attack-window
  detection.
- **`TRIGGER_THRESHOLD = 3`.** Two pings are not enough; four
  pings overshoots and produces extra Kibana documents that the
  saved searches do not expect. Changing this changes the
  attack-arming latency and therefore the shape of the attack
  window in `cfs_god_view.json`.
- **The 4 KB `MaliciousPayload` size.** The source comment is
  explicit: 512 KB buffer pool divided by 4 KB equals ~128
  packets. Shrinking the payload pushes the pool-exhaustion
  point out of the first storm cycle, which changes the
  `SB_PIPE_OVERFLOW` signature. Growing it past the cFS maximum
  message size (`CFE_MISSION_SB_MAX_SB_MSG_SIZE`, defaulted to
  32 KB but build-configurable) makes `CFE_SB_TransmitMsg`
  reject the packet outright.
- **The EPS spoof reuses `GENERIC_EPS_HK_TLM_MID = 0x091A` and
  `GENERIC_EPS_HK_TLM_LNGTH` from `generic_eps_msgids.h` and
  `generic_eps_msg.h`.** If the EPS app changes its struct, the
  spoof packet becomes the wrong size and the cFS message length
  check (in `CFE_MSG_Init`) rejects it. The spoof's value
  (`BatteryVoltage = 0xDEAD`) is intentional: `0xDEAD` is not a
  number a healthy EPS would ever emit and is therefore a
  reliable Kibana filter.
- **The app must remain at default cFS task priority 20.** The
  source comment depends on this for the "permanent CPU
  hijack" claim. A higher priority preempts cFE services and
  destabilises the run before the bus floods, which is a
  different (and less interesting) attack.
- **The 100 ms `OS_TaskDelay` in the dormant state.** The
  source comment calls it stealth; the operational effect is
  that the app consumes ~0% CPU until armed, which is what
  makes the unarmed run statistically indistinguishable from a
  clean run.

## Remaining apps

The next pass should cover:

- `mgr` (DTU-added mission manager component)
- `blackboard` (DTU-added internal data exchange; also holds
  `to_lab_sub.c`)
- `generic_tt_c` (load-bearing parser edit)
- `generic_gnss` (load-bearing parser edit plus the recent
  source-restore commit)
- `generic_adcs` (research-relevant -O2 LICM concern)

The cFS housekeeping apps (`mm`, `md`, `fm`, `cs`, `lc`, `sc`)
are unmodified by this fork but are documented as attack
surfaces in the Threat Model chapter. They are out of scope
for the per-app deep dives unless you want them folded in.

`syn` (JPL SYNOPSIS) and `onair` (ONAIR agent) are imported
intact; the only DTU concern is the SYN heap leak, which is
covered in the Threat Model chapter. They are also out of
scope unless you want short stubs.

Tell me which of the five to continue with next (or to do all
five in order), whether you want the cFS housekeeping apps
added, and whether `syn` / `onair` get stubs or get skipped.
