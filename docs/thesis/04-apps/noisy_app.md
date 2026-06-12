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

The current implementation is the **covert-opcode "piggyback"**
design, ported from the Draco baseline (commit `dd1d2cbb`). It
replaced the earlier 3-ping-beacon / broadcast-storm version. The
key idea: rather than carrying its own trigger MID, the app
**sniffs a legitimate command** off the Software Bus and reads a
covert opcode smuggled in that command's unused tail.

## Source files

- `nos3/fsw/apps/noisy_app/fsw/src/noisy_app.c` is the entire
  application. It hardcodes its own copy of the EPS housekeeping
  wire layout (`NOISY_EpsHkMimic_t`) rather than linking the
  victim struct, so it only needs `GENERIC_EPS_HK_TLM_MID` from
  `generic_eps_msgids.h`.
- `nos3/fsw/apps/noisy_app/fsw/platform_inc/noisy_app_msgids.h`
  declares the app's own MIDs: `NOISY_APP_CMD_MID = 0x18F2`,
  `NOISY_APP_SEND_HK_MID = 0x18F3`,
  `NOISY_APP_HK_TLM_MID = 0x08F2`.
- `nos3/fsw/apps/noisy_app/CMakeLists.txt` registers the app as an
  ordinary cFS app target and adds `fsw/platform_inc` (its own
  MIDs) and the generic_eps `platform_inc` (the EPS MID) to the
  include path.

The app is registered in `cpu1_cfe_es_startup.scr` under
`cfg/nos3_defs/` (and the matching build copy under
`cfg/build/nos3_defs/`). As shipped its row reads `CFE_APP`, so the
app loads on every boot. The row is a simple toggle: `CFE_APP`
means loaded on boot, `OFF_APP` means present in the image but not
loaded, so flipping the row to `OFF_APP` disables it. The
preceding `OFF_APP, ===_DTU_THESIS_MENU_===` line is only a visual
separator/marker, not a conditional. The supply-chain framing is
therefore "the malicious binary is in the image and loads like any
other app; the boot hook is a one-line toggle".
The app runs at a low priority (29) so the fixture never competes
with core apps - the piggyback design does not rely on a CPU
hijack the way the legacy storm did.

## Wire-level surface

At startup the app creates one command pipe and subscribes to
three MIDs:

- `NOISY_APP_CMD_MID` (`0x18F2`) - its own direct command channel.
- `CARRIER_CMD_MID` (`0x18E0`, CI_LAB's `CI_LAB_CMD_MID`) - the
  **sniffed legitimate carrier**. cFE SB delivers every message to
  all subscribers and performs no sender authentication, so a
  co-resident app can read traffic addressed to CI_LAB.
- `GENERIC_EPS_HK_TLM_MID` (`0x091A`) - genuine EPS housekeeping,
  so the persistent-override scenario can shadow each real packet.

A normal CI_LAB NOOP is header-only (8 bytes). When a NOOP arrives
on `0x18E0` that is **longer** than that, the app reads the trailing
byte as a covert opcode and dispatches:

| Opcode | Name          | Effect |
|--------|---------------|--------|
| `0x00` | DORMANT       | nothing (stealth); clears a running EPS override |
| `0x02` | EPS_SPOOF     | forge ONE `GENERIC_EPS_HK` (`0x091A`), `BatteryVoltage = 10000` mV |
| `0x04` | SB_BURST      | 64-packet capped Software Bus burst on `0x08F2` (yields periodically) |
| `0x06` | EPS_OVERRIDE  | persistent: shadow every genuine EPS HK so the spoof always wins LC's sample |
| `0x08` | SB_FLOOD      | DESTRUCTIVE: park ~15 max-size buffers unread to lock the 512 KB SB pool |
| `0x0A` | IMU_BIAS      | covert file dead-drop `/ram/.imu_cal` (Draco-only; no `generic_imu` backdoor here) |

The legitimate carrier endpoint (CI_LAB) calls
`CFE_MSG_VerifyCmdLength`, sees the over-length NOOP, **rejects it**
and bumps its `CommandErrorCounter`. That endpoint defense does not
stop the sniffer: the opcode byte is read off the bus independently
of the endpoint's length check. This asymmetry (endpoint rejects,
co-resident app acts) is the whole point of the scenario.

## Telemetry it emits

Everything observable is either an EVS event or a forged/shadowed
telemetry packet under another component's MID. The Logstash
pipeline picks the EVS events up via `cfs_evs.log` and tags them.
The relevant filter blocks live in `nos3/elk/logstash.conf`:

- `attack_app => NOISY_APP` is added to any `system_log` document
  from `NOISY_APP`.
- `attack_loaded` - the init event ("Initialized ... sniffing
  carrier").
- `attack_piggyback` - an over-length carrier was read ("piggyback
  opcode").
- `attack_eps_spoof` / `attack_eps_override` / `attack_sb_burst` /
  `attack_sb_flood` - the per-scenario events.
- `piggyback_carrier` - tags `software_bus` docs on `0x18E0`.
- `piggyback_length_error` - the CI_LAB "Invalid msg length" event
  on `0x18E0` (the bypassed endpoint defense).
- `noisy_app_spam_target` - the SB_BURST/SB_FLOOD sink MID `0x08F2`.
- `sb_pipe_overflow` - pool-exhaustion / `BUF_ALOC_ERR` side
  effects of SB_FLOOD.

The forged EPS packet appears in `nos3/attack_logs/cfs_god_view.json`
as a `software_bus` document with `msg_id = 0x091A`,
`msg_name = GENERIC_EPS_HK_TLM`, the low `BatteryVoltage` payload,
and a sequence counter one greater than the previous legitimate EPS
publish. The sequence gap between consecutive publishes on a MID
that only the EPS driver should publish is the structural forensic
signature.

## Autonomous response (parity with the Draco baseline)

The EPS spoof drives a real autonomous response via the Limit Checker.
To keep the two testbeds replicas, this fork's battery-low chain was
made identical to Draco's: watchpoint **WP #0** (`BatteryVoltage <
14800` mV, `lc_def_wdt.c`) -> actionpoint **AP #0**
(`SAFE_ON_LOW_BAT`, ACTIVE, `lc_def_adt.c`) -> **RTS 4**
(`sc_rts004.c`): `MGR SET_MODE SAFE`, DS-disable of the instrument
store, and `TO_LAB SET_SAFE_TLM` (downlink downgraded to the low-rate
HK beacon; the CI_LAB uplink stays alive). A one-shot `0x02` spoof is
unlikely to be the exact packet LC samples; the `override` (`0x06`)
path re-arms AP #0 (`LC_SET_AP_STATE_CC`, `APNumber=0`) and shadows
every genuine EPS HK so WP #0 stays FAIL and RTS 4 fires and holds.
With the attack, the injection path, and this response chain held
identical, the comparison isolates the cFS version (this fork cFE
6.7.99, the 6.7 "Bootes" line / legacy, vs Draco cFE 7.0.0 / new) - see
[../../security/comparison-secured-vs-draco.md](../../security/comparison-secured-vs-draco.md)
and [../../security/00-dtu-secured-fork-notes.md](../../security/00-dtu-secured-fork-notes.md).
(This fork also retains its own WP #27/#28 SciMode actionpoints, left
DISABLED by default as in Draco, so they do not fire on the spoof.)

## What was changed from upstream

The app does not exist upstream; the closest analogue is the
upstream `sample` app template. `noisy_app` is a research-fork
addition. The earlier version (commit `dd750790` and follow-ups)
was a 3-ping-beacon trigger on `0x18F0` plus a CPU-burn and a
32k-MID broadcast storm. The current version replaces that with the
piggyback covert-opcode sniffer described above, so the malicious
command rides a legitimate one rather than using a dedicated
trigger MID.

The supply-chain framing is that the build path itself is the
attack vector: a fork that pulls `noisy_app` into `fsw/apps/` and
registers it in `cpu1_cfe_es_startup.scr` produces a flight build
that loads the malicious app on boot. Upstream cFS has no
countermeasure because its trust model treats every loaded app as
co-trusted. This is the concrete instance of the abstract threat in
[../03-communication/02-fsw-software-bus.md](../03-communication/02-fsw-software-bus.md).

## Load-bearing invariants

These details have to stay aligned for the experiments to remain
reproducible and for the Kibana filters to keep matching:

- **`CARRIER_CMD_MID` must stay `0x18E0`** (CI_LAB's command MID).
  The sniffer subscribes to it and the `piggyback_carrier` /
  `piggyback_length_error` Logstash tags key off it. Re-pointing the
  carrier (e.g. to TO_LAB `0x18E8`) requires updating both.
- **A normal carrier NOOP must be header-only.** The opcode is read
  as `buf[Size-1]` only when the carrier is over-length; if the
  legitimate NOOP ever grows a payload, the dispatch logic must be
  revisited or every normal NOOP would be misread as an opcode.
- **The opcode map (`0x00/0x02/0x04/0x06/0x08/0x0A`)** is mirrored in
  three places that must agree: `noisy_app.c`, the COSMOS command
  `PIGGYBACK_POC.txt` (`OPCODE` dropdown), and `drive_poc.py`.
- **EPS spoof reuses `GENERIC_EPS_HK_TLM_MID = 0x091A`** and the
  `NOISY_EpsHkMimic_t` mirror layout (BatteryVoltage at wire offset
  16). If the EPS app changes its HK struct, the mirror must follow
  or LC samples the wrong field. The spoof value `10000` mV is below
  WP #27's `24240`; to mask a real low battery instead, use `> 24960`
  (WP #28).
- **SB_FLOOD parks on `NOISY_APP_HK_TLM_MID = 0x08F2`** with a deep
  never-drained pipe. This is the `noisy_app_spam_target` MID; keep
  the two aligned.

## Remaining apps

The next pass of per-app documentation should cover `mgr`,
`blackboard` (also holds `to_lab_sub.c`), `generic_tt_c`,
`generic_gnss`, and `generic_adcs`. The cFS housekeeping apps
(`mm`, `md`, `fm`, `cs`, `lc`, `sc`) are unmodified by this fork but
documented as attack surfaces in the Threat Model chapter; `syn`
(JPL SYNOPSIS) and `onair` are imported intact.
