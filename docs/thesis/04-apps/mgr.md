# 04. mgr - Mission Manager (DTU custom app)

This document describes the DTU-custom `mgr` cFS app: its
mission-state pipe, its persistence-restore behaviour, the
computed-MID API it uses to drive the rest of the FSW, and the
HK packet header initialisation order that turned out to be
load-bearing.

## 1. Real-world hardware

`mgr` has no flight-hardware analogue; it is a software
component. On a flying spacecraft, an equivalent role would be
the on-board mission-management software that owns spacecraft
state machines, sequences mode transitions, drives autonomy
hand-offs, and persists boot counters. The testbed implements
this role with a single cFS app rather than the layered C&DH
stack a real mission would use.

## 2. App overview (cFS side)

The app lives at `nos3/components/mgr/fsw/cfs/`. The key files:

- `src/mgr_app.c` (~860 lines as of the slice 1 baseline).
- `src/mgr_app.h` (~110 lines, shared types).
- `src/mgr_events.h` (~60 lines, event ID enumeration).
- `src/mgr_msg.h` (HK packet layout, command structs).

MIDs (resolved through the component-base macros at
`nos3/cfg/nos3_defs/nos3_component_msgid_mapping.h:19`):

- Topic ID `0xF8`.
- Command MID: `0x19F8`.
- Housekeeping TLM MID: `0x09F8`.
- HK request MID: a separate `MGR_REQ_HK_MID` that SCH publishes
  at the wakeup tick.

Pipes:

- One command pipe `MGR_CMD_PIPE` of depth `MGR_PIPE_DEPTH`,
  subscribed to:
  - `MGR_CMD_MID = 0x19F8` (ground commands).
  - `MGR_REQ_HK_MID` (HK wakeup from SCH).

The main loop `MGR_AppMain` at `mgr_app.c:53` is the standard
cFS pattern: `CFE_SB_ReceiveBuffer` with `PEND_FOREVER`, then
dispatch through `MGR_ProcessCommandPacket`. Performance log
entries bracket the dispatch; an SB read error transitions the
app to `CFE_ES_RunStatus_APP_ERROR` and exits.

## 3. State machine

`MGR_AppData.HkTelemetryPkt` carries the spacecraft state. Two
fields drive the testbed's autonomy:

- `SpacecraftMode`: one of `MGR_SAFE_REBOOT_MODE`,
  `MGR_SCIENCE_REBOOT_MODE` (and intermediate variants if
  defined). The mode controls which RTS sequences SC will
  execute and whether the science instruments are enabled.
- `BootCounter`, `AnomRebootCtr`, `TimeTics`: persisted across
  reboots through the persistence file (section 4).

The header `mgr_app.h` defines the data types and constants
those fields use. Mode-change commands arrive on `MGR_CMD_MID`
with a `U8` mode value; the validator at line 325 rejects
out-of-range modes and emits `MGR_INVALID_MODE_EID`.

## 4. The persistence-restore deep-dive

`MGR_RestoreHkFile` at `mgr_app.c:744` is the headline function
for this app. It runs once during init (called from
`MGR_AppInit` at line 184) and is responsible for re-establishing
the pre-reboot mission state.

### 4.1 The file path

`MGR_CFG_FILE_PATH` is the persistence file. It is the only
piece of mission state that survives a reboot.

### 4.2 The "no file" path (lines 752-762)

If `OS_OpenCreate` cannot find the file:

1. Log a fresh-create event.
2. `CFE_MSG_Init` the in-RAM HK packet so the next write has
   a valid header.
3. `OS_remove` any stale file (defensive against a partial
   create from an earlier failed boot).
4. Re-open in `OS_FILE_FLAG_CREATE | OS_FILE_FLAG_TRUNCATE` mode.
5. `OS_write` the freshly-init'd HK packet so the file has a
   valid initial state.

### 4.3 The "file present" path (lines 763-775)

If the file opens, `OS_TimedRead` pulls the entire
`MGR_Hk_tlm_t` (including the TlmHeader bytes) from disk. If
the read does not return exactly `sizeof(MGR_Hk_tlm_t)`:

1. Log the failure.
2. Zero the in-RAM HK packet.
3. `CFE_MSG_Init` to lay down a valid header.

If the read succeeds, the in-RAM packet now holds the persisted
state, including the TlmHeader bytes from the file.

### 4.4 The init-after-restore invariant (lines 187-194 of
`MGR_AppInit`)

This is the load-bearing detail. Because the restore path reads
the entire `MGR_Hk_tlm_t` (including TlmHeader bytes) from the
persisted file, any earlier `CFE_MSG_Init` call would be
clobbered. The fix is to call `CFE_MSG_Init` after
`MGR_RestoreHkFile` returns:

```
MGR_RestoreHkFile();
...
CFE_MSG_Init(CFE_MSG_PTR(MGR_AppData.HkTelemetryPkt.TlmHeader),
             CFE_SB_ValueToMsgId(MGR_HK_TLM_MID),
             MGR_HK_TLM_LNGTH);
```

Without this re-init, the first `CFE_SB_TransmitMsg` after a
file-present boot ships a packet whose MID matches whatever was
on disk; if the disk file was written before a MID-base shift
(or by a different app build), HK ends up routed to the wrong
subscribers. A single line of init order, but worth this much
prose.

### 4.5 Mode-validity guard (lines 784-797)

If the persisted `SpacecraftMode` is neither `MGR_SAFE_REBOOT_MODE`
nor `MGR_SCIENCE_REBOOT_MODE`, the app:

1. Forces mode to `MGR_SAFE_REBOOT_MODE`.
2. Increments `AnomRebootCtr`.
3. Zeros `TimeTics` (the persistence file is treated as
   untrusted on this path; folding a corrupt `TimeTics` into
   `OS_SetLocalTime` would otherwise stall `MGR_AppInit`).

Otherwise, increments `BootCounter` (the nominal reboot
accounting).

### 4.6 TimeTics sanity clamp (lines 805-816)

A prior boot that read a bogus NOS Engine startup tick can
persist an absurdly large `TimeTics` (one observed value was
1.8e15). Folding that into `OS_SetLocalTime` poisons OSAL time
and stalls `MGR_AppInit`. The clamp accepts any positive value
under `1e14` ticks (plausible across reasonable mission
lifetimes) and zeros anything else.

### 4.7 Time set (lines 820-826)

Only call `OS_SetLocalTime` if there is something to apply:

```
if (MGR_CFG_REBOOT_TIME_TIC_OFFSET != 0 ||
    MGR_AppData.HkTelemetryPkt.TimeTics != 0)
{
    OS_GetLocalTime(&temp_time);
    temp_time.ticks += MGR_CFG_REBOOT_TIME_TIC_OFFSET +
                       MGR_AppData.HkTelemetryPkt.TimeTics;
    OS_SetLocalTime(&temp_time);
}
```

Skipping the call when both deltas are zero avoids a stray
NOS Engine bogus startup tick read in `OS_GetLocalTime` from
poisoning OS time.

## 5. The computed-MID API

Six lines at the top of `mgr_app.c` (lines 31-37) declare
sensor command MIDs by hard value:

```
#define MGR_CSS_CMD_MID      0x1910
#define MGR_FSS_CMD_MID      0x1920
#define MGR_IMU_CMD_MID      0x1925
#define MGR_MAG_CMD_MID      0x192A
#define MGR_TORQUER_CMD_MID  0x193A
#define MGR_NOVATEL_CMD_MID  0x1970
#define MGR_SENSOR_ENABLE_CC 2
```

These are the MIDs MGR uses to enable each sensor at startup.
The DTU choice was to hardcode them in MGR rather than pull each
sensor's `*_msgids.h` into the MGR translation unit, on the
grounds that those headers transitively pull in `FILE *` and the
full payload type definitions. The alternative (a clean import)
would expand MGR's compile graph for no functional gain. The
hardcoded values are derived by OR-ing the topic ID against the
`0x1900` component base.

The `MGR_EnableAllSensors` helper iterates the list and sends a
NoArgs command (cmd code `2`) to each MID. It runs once during
init at line 217 of `MGR_AppInit`. If MGR were to skip this
step, the IMU/MAG/CSS/FSS apps would stay disabled, ADCS would
see zero-rate and zero-attitude every cycle, and the reaction
wheel commands would never lift above the int16 quantisation
threshold in `send_rw_commands`.

Similar mode-broadcasts to ADCS happen through
`MGR_SendAdcsSetMode` and `MGR_SendAdcsHmgmt` (forward declared
at lines 42-43). MGR drives the entire ADCS state machine
through these helpers; the rest of the FSW does not need to
know about MGR.

### 5a. Mode-driven sensor and ADCS cascade (2026-05-03)

Commit `b8da042d feat(mgr): drive ADCS modes from spacecraft mode and auto-enable sensors at boot` made the ADCS sub-mode an automatic function of `SpacecraftMode`:

| Spacecraft mode | ADCS sub-mode | Sensors enabled |
|---|---|---|
| BOOT | PASSIVE | all sensor apps auto-enabled by MGR |
| SAFE | BDOT | MAG (only) |
| NOMINAL | SUNSAFE | CSS / FSS / IMU |
| SCIENCE | INERTIAL | MAG / CSS / IMU / GNSS |
| RECOVERY | BDOT | MAG (only) |

The thesis baseline had ground or stored-command logic dispatching `MGR_SendAdcsSetMode` per mode change; the post-thesis change moves the cascade inside MGR itself so a single `MGR_SET_MODE_CC` covers the whole stack. Auto-enable-on-boot is the part that closed the "sensor not yet active when the first ADCS tick fires" race that earlier diagnosis sessions surfaced; if the sensor app is not yet started when MGR's broadcast fires, the broadcast silently no-ops, visible only in EVS.

`in_science_mode`, the boolean that powers the GNSS GS validation AND-gate panel, is derived directly from MGR HK (`spacecraft_mode == SCIENCE`).

## 6. ELK extraction

The MGR HK packet is captured by both:

- The Software-Bus capture (`cfs_god_view.json`), tagged
  `software_bus`.
- The EVS event stream, tagged `system_log`.

Numeric fields the Logstash filter extracts:

- `mgr_spacecraft_mode` (kept as a string keyword).
- `mgr_boot_counter`, `mgr_anom_reboot_ctr`.
- `mgr_time_tics`.
- `in_science_mode` (boolean, post-2026-05-03; derived from `mgr_spacecraft_mode == SCIENCE`). Powers the AND-gate panel and the spacecraft-mode chip context on the freshness tile row.

Kibana panels:

- "Mission Mode Timeline" on `nos3-std-telemetry-overview`.
- "Boot Counter Trend" on `nos3-std-mission-validation` (a
  rising trend across a single mission run is anomalous).

## 7. Attack-surface relevance

MGR is sensitive on two axes:

- **Persistence-file tampering**: the file at
  `MGR_CFG_FILE_PATH` is the only mission state that survives
  a reboot. An attacker with file-system write access can
  manipulate `SpacecraftMode`, `BootCounter`, or `TimeTics`.
  The TimeTics clamp and the mode-validity guard are the
  defence; `AnomRebootCtr` is the detection signal.
- **Sensor-enable broadcast**: an attacker who can disable
  `MGR_EnableAllSensors` (for example, by patching MGR's main
  loop) can degrade ADCS without obvious symptoms in the EPS
  or GPS panels. The cross-check is the IMU/MAG/CSS HK rate;
  if those drop to zero shortly after boot, MGR's broadcast
  has been intercepted.

## 8. DTU divergence vs upstream

MGR is a DTU-custom app added to the testbed; there is no
upstream NOS3 baseline to diverge from. The architecturally
notable choices:

- The persistence-file model is single-file, single-blob
  (whole `MGR_Hk_tlm_t` round-trip). A more conventional
  design would use cFS TBL services. The chosen model is
  simpler and explicit, which made the load-bearing bugs in
  section 4 tractable.
- The hardcoded sensor-MID list (section 5) is unconventional
  cFS practice. The trade-off is documented inline in the
  source comments.
- The HK packet header is initialised after the restore call
  (section 4.4). This order is the load-bearing fix.

Commits that materially shaped the app:
- The persistence-file path: see commits touching
  `mgr_app.c` and `mgr_events.h`.
- The TimeTics clamp: see commits referencing
  `docs/nos3_fsw_error_fixes.md` Section 4.

## 9. Verification

- **Persistence round-trip**: take down the testbed mid-mission,
  restart, and confirm the COSMOS UI shows the same
  `SpacecraftMode` and a `BootCounter` exactly one greater than
  before.
- **Anomalous-reboot detection**: corrupt the persistence file
  to set `SpacecraftMode` to a junk value (for example, 0xFF),
  restart, and confirm the COSMOS UI shows
  `SpacecraftMode = MGR_SAFE_REBOOT_MODE` and `AnomRebootCtr`
  incremented.
- **Sensor enablement**: confirm that the IMU, MAG, CSS, FSS,
  TORQUER, and NOVATEL HK packets all start emitting within a
  few seconds of MGR init. The Kibana panel "HK rate per
  component" on `nos3-std-mission-validation` is the visual
  check.
