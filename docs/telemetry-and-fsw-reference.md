# NOS3-DTU-Draco Telemetry and FSW Reference

A developer reference for what this simulation actually sends on the Software Bus, which scripts capture each byte of telemetry, how the ELK pipeline routes it to Kibana, and which flight-software functions drive each step.

If you want to know *what the mission does* (modes, autonomy, scenarios), read [`missions/NOS3-DTU-EO1-mission-implementation.md`](missions/NOS3-DTU-EO1-mission-implementation.md) first; this document is the *reference* that backs it.

For live triage during a run, jump straight to [Section 12: Live debugging cheat-sheet](#12-live-debugging-cheat-sheet).

---

## 1. How to read this doc

Every inter-app message in the simulation is a **CCSDS packet** pushed onto the **Software Bus (SB)**, cFE's publish/subscribe bus. Each packet is identified by a 16-bit **Message ID (MID)**:

| MID range | Direction | Source |
|---|---|---|
| `0x0800`..`0x08FF` | Telemetry (TLM) | cFE core + cFS heritage apps |
| `0x0900`..`0x09FF` | Telemetry (TLM) | NOS3 generic hardware-component apps |
| `0x1800`..`0x18FF` | Command (CMD)   | cFE core + cFS heritage apps |
| `0x1900`..`0x19FF` | Command (CMD)   | NOS3 generic hardware-component apps |

The `0x1900 / 0x0900` component base is defined in
`nos3/cfg/nos3_defs/nos3_component_msgid_mapping.h`:

```c
#define CFE_PLATFORM_COMPONENT_CMD_TOPICID_TO_MIDV(topic)  (0x1900 | (topic))
#define CFE_PLATFORM_COMPONENT_TLM_TOPICID_TO_MIDV(topic)  (0x0900 | (topic))
```

Each component declares a small `*_topicids.h` (e.g. `generic_eps_topicids.h`) with
the low byte; the full MID is formed by OR-ing in one of the two bases above.

### Single source of truth: `cfg/build/mid_registry.json`

`make config` runs `nos3/scripts/mid/gen_mid_registry.py`, which walks every
`*_topicids.h` / `*_msgids.h` / `*_msgid_values.h` in the FSW tree, computes
each MID the same way the C preprocessor does (base-mask OR topic-id), and
emits:

- `nos3/cfg/build/mid_registry.json` - one record per MID with `{name, layer, subsystem}`.
- `nos3/cfg/build/elk/mid_to_name.yml` - flat hex -> name dictionary (Logstash).
- `nos3/cfg/build/elk/mid_to_layer.yml` - flat hex -> attack layer.
- `nos3/cfg/build/elk/mid_to_subsystem.yml` - flat hex -> subsystem.

The three YAML files are mounted into the Logstash container at
`/etc/logstash/mid/` and consumed via `translate { dictionary_path => ... }`.
`god_view_capture.py` loads the JSON at startup to resolve its
`_DECODE_BY_NAME` table into integer MID keys. Add a new component:
drop in a `*_topicids.h`, run `make config`, both Logstash and the capture
script pick it up without touching any Python or Ruby.

Layer and subsystem assignment is driven by
`nos3/scripts/mid/mid_groupings.yaml` (name-prefix -> `{layer, subsystem}`);
this is the only part that remains hand-edited, and only at the *family*
level (e.g. `GENERIC_EPS_*` -> EPS / COMPONENT).

---

## 2. Pipeline at a glance

```
           +-----------------+    SB     +--------------+    UDP :5012   +----------+
Ground --->| CI_LAB (uplink) | --------> |   cFS apps   | <------------- |  Ground  | (send command)
           +-----------------+           |  publishing  |
                                         |   on SB      |
                                         +------+-------+
                                                | TO_LAB forwards packets listed in to_config.c
                                                v (UDP :5013)
                                         +--------------+
                                         | god_view_    |  appends every CCSDS packet to
                                         | capture.py   |  attack_logs/cfs_god_view.json
                                         +------+-------+
                                                |
                                                v
                                        Logstash -> Elasticsearch -> Kibana
                                        (also ingests omni_logs/*.log)
```

- **SB publisher**: any app `CFE_SB_TransmitMsg(...)`.
- **`TO_LAB`**: filters the SB and forwards only packets listed in
  `nos3/cfg/nos3_defs/tables/to_config.c` to UDP port 5013.
- **`god_view_capture.py`** (`nos3/scripts/god_view_capture.py`): sends
  `TO_LAB_OUTPUT_ENABLE_CC` to CI_LAB (UDP 5012), listens on 5013, writes a JSON
  line per packet to `attack_logs/cfs_god_view.json`. A subset of HKs is decoded
  in-process (see the `_DECODE_BY_NAME` dict at `god_view_capture.py:76`) and
  appended to `omni_logs/tlm_hk_decoded.log`.
- **`omni_logs/*.log`**: simulator stdout captured per subsystem (EPS, GPS,
  reaction wheels, etc.) plus the FSW EVS stream and a host-side CPU monitor.
  Logstash parses these separately from the SB stream.
- **Logstash** (`nos3/elk/logstash.conf`) ingests all three sources (see 9.5),
  enriches every event with `layer`, `subsystem`, `msg_name`, `msg_direction`,
  and writes to indices `nos3-telemetry-YYYY.MM.dd`.

### 2.1 Capture scripts at a glance

Four host-side capture processes run in parallel for the duration of a sim.
They are started by `make launch` (see `nos3/Makefile:244-261`) and killed by
`make stop` or `scripts/stop.sh`. There is no `log.sh` in this tree; the
canonical script names are exactly those in the table below.

| Script | Output file(s) | What it captures | How it reads |
|---|---|---|---|
| `scripts/cfs_evs_capture.sh` | `omni_logs/cfs_evs.log` | FSW stdout (cFE EVS console stream) | `docker logs -f --timestamps sc01-nos-fsw` |
| `scripts/sim_logs_capture.sh` | `omni_logs/nos3-<sim>.log` (17 streams: gps, eps, imu, mag, fss, css, startrk, rw0..2, torquer, thruster, radio, truth42, blackboard, sample, cam) | Per-simulator container stdout | one `docker logs -f` per `sc01-*` sim container |
| `scripts/cpu_monitor.sh` | `omni_logs/cpu_monitor.log` | Container CPU + per-thread CPU | `docker stats` + `docker exec ... ps -eLo comm,pcpu` (2 s cadence) |
| `scripts/god_view_capture.py` | `attack_logs/cfs_god_view.json`, `omni_logs/tlm_hk_decoded.log`, `omni_logs/god_view.log` | Every CCSDS packet `TO_LAB` forwards; decoded HK payload for MIDs in `_DECODE_BY_NAME`; own startup log | UDP :5013 listener (plus UDP :5012 enable command to `CI_LAB`) |

Every file written by these scripts lives under `nos3/omni_logs/` or
`nos3/attack_logs/`. Logstash mounts both directories read-only into the
container and matches them with the three inputs described in 9.5. A file
left at 0 bytes after a run is expected when the corresponding component is
disabled in the active spacecraft config (e.g. `nos3-thruster.log`,
`nos3-sample.log` in research runs).

---

## 3. Packet categories

The word "telemetry" covers several distinct kinds of packet. Knowing which
kind you're looking at tells you *when* and *why* it was sent.

### 3.1 Housekeeping (HK)

**What**: a compact snapshot of an app's internal counters and status.
Every app has exactly one: name pattern `<APP>_HK_TLM_MID`, MID `0x0XXX`.

**When**: 1 Hz. The Scheduler app (`SCH`) fires `<APP>_SEND_HK_MID` (a
command, not a telemetry packet) at a specific slot in its 1-second major
frame. The target app's main loop receives that command, calls its
`ReportHousekeeping()` function, which fills in the struct and calls
`CFE_SB_TransmitMsg`. Each app's HK arrives on the bus about once per second.

**Why**: ground (and the onboard LC watchpoints) need a periodic heartbeat
containing counters (commands accepted/rejected), app-specific state
(battery voltage, CPU %, mode), and the `CFE_ES` process state.

**Where the cadence is defined**:
- Which HK requests fire each tick -> `nos3/cfg/nos3_defs/tables/sch_def_schtbl.c`
- Which command MIDs they resolve to -> `nos3/cfg/nos3_defs/tables/sch_def_msgtbl.c`

### 3.2 Device TLM

**What**: hardware-specific data (positions, quaternions, magnetometer
readings, sun angles). Name pattern `<APP>_DEVICE_TLM_MID`, MID `0x0XXX`.

**When**: pushed asynchronously by the component's FSW app whenever it has
fresh data from its simulator. Typical rate is 1 to 10 Hz, driven by the
simulator, not SCH.

**Why**: this is the "real" payload data. HK tells you *whether* an app is
healthy; device TLM tells you *what the sensor measured*.

### 3.3 Event messages (EVS)

**What**: text log lines emitted by any app via `CFE_EVS_SendEvent(...)`.
Long events go on MID `CFE_EVS_LONG_EVENT_MSG_MID` (0x0808), short events on
`CFE_EVS_SHORT_EVENT_MSG_MID` (0x0809).

**When**: asynchronous, one per call. Frequency depends on what the app is
doing (startup spam, error conditions, LC actionpoint fires).

**Why**: operational logging, debugging, autonomy trails (e.g. an LC AP
firing produces an event like "Batt volt critical: SAFE").

### 3.4 Combined HK (`HK_COMBINED_PKTn`)

**What**: the `HK` app subscribes to many HK packets, copies selected fields
into a single wider packet, and publishes it on one of four MIDs:
`HK_COMBINED_PKT1_MID` (0x089B) through `HK_COMBINED_PKT4_MID`.
Mapping is driven by `nos3/cfg/nos3_defs/tables/hk_cpy_tbl.c`.

**When**: 1 Hz, fired by SCH just like individual HKs.

**Why**: downlink efficiency. Ground can subscribe to one compact packet
instead of fifteen per-app HKs.

### 3.5 Diagnostic TLM

**What**: on-command introspection dumps (CFE Table Registry, DS file-system
stats, SB subscription lists, MD dwell packets). Not periodic.

**When**: only when ground explicitly asks for it via a command.

### 3.6 Commands (CMD)

**What**: ground-to-spacecraft packets on MID `0x1XXX`. Each app has
`<APP>_CMD_MID` (user commands: NOOP, RESET, app-specific actions) and
`<APP>_REQ_HK_MID` (SCH-fired request to produce HK, considered a command
because it carries a command header).

**When**: whenever ground (or `SC`, running a stored RTS) decides.

**Path**: UDP :5012 -> `CI_LAB` -> SB -> target app's command pipe.

### 3.7 "HK_ENCODED" - clarification

**There is no `HK_ENCODED` MID in this repo.** Every packet on the wire is
already a **CCSDS-encoded** byte stream (primary header + optional secondary
header + payload); that encoding is what `cfs_god_view.json` records verbatim,
and it is the "encoded" representation you see in Kibana's raw document view.

The CryptoLib library *is* loaded as a cFE library (see
`cpu1_cfe_es_startup.scr:1`, `CFE_LIB, cryptolib, Crypto_SC_Init, CRYPTOLIB`),
but the current `to_config.c` does **not** wrap the TM path in CryptoLib's SDLS
encryption, and Logstash contains no `*_ENCODED_*` filters. If you later wire
CryptoLib into the TM output you will see a second set of MIDs (typically
prefixed `ENC_`) arrive at `TO_LAB`; treat those as the encrypted counterparts
of the plaintext MIDs listed in Section 4 below.

### 3.8 HK versus hk_decoded

These two terms mean different things and both appear in Kibana. They are
easy to confuse because a single FSW HK packet is indexed twice under
different `type` values.

- **HK** is a packet category (Section 3.1). The FSW app publishes it on the
  Software Bus once per second. `god_view_capture.py` always logs the CCSDS
  envelope (`msg_id`, `apid`, `pkt_type`, `sequence`, `length`, `timestamp`)
  to `attack_logs/cfs_god_view.json`. Logstash ingests that file with
  `type=software_bus` and enriches each document with `msg_name`, `layer`,
  `subsystem`, `msg_direction`, `msg_id_int`. Use this view to see *whether*
  a packet was produced, dropped (`sb_sequence_gap`), or targeted by an
  attack.

- **hk_decoded** is an index type, not a CCSDS concept. For the small set of
  MIDs listed in `_DECODE_BY_NAME` (see `god_view_capture.py:76`),
  `god_view_capture.py` additionally decodes the payload in Python using
  hardcoded byte offsets and appends a JSON record to
  `omni_logs/tlm_hk_decoded.log`. Logstash ingests that file with
  `type=hk_decoded` and coerces numeric types (logstash.conf:788-809). Use
  this view to *trend* the actual values (battery_mv, cpu_peak_pct,
  spacecraft_mode, etc.).

So the same HK packet on the bus produces two Kibana documents: one in
`software_bus` (envelope only) and, if its MID is in the decoder,
one in `hk_decoded` (envelope plus decoded fields). The `hk_decoded`
document carries the same `msg_id` and `sequence`, so they can be correlated
by `msg_id + timestamp` if needed.

One subtle ordering note: because the `omni_logs/*.log` input (Layer 2) uses
a wildcard while the `tlm_hk_decoded.log` input (Layer 3) is specific, each
JSON line in `tlm_hk_decoded.log` is actually indexed **twice**: once as
`type=system_log` (the raw JSON string in the `message` field, under
`subsystem=tlm_hk_decoded`) and once as `type=hk_decoded` (parsed fields).
This is by design; filter on `type` when you only want one.

---

## 4. Telemetry inventory

Tables below list every MID the simulation emits. **TO** means "subscribed
in `to_config.c` and therefore forwarded to `god_view_capture.py` / ground".
**Decoded** means the payload is unpacked in Python and appears in
`tlm_hk_decoded.log` in addition to the raw envelope in `cfs_god_view.json`
(see `_DECODE_BY_NAME` in `god_view_capture.py:76-114`).

### 4.1 cFE core (heritage base `0x0800`)

| MID | Name | Type | Cadence | TO | Decoded | Key fields |
|---|---|---|---|---|---|---|
| 0x0800 | `CFE_ES_HK_TLM_MID` | HK | 1 Hz | yes | yes | cmd_count, cmd_err_count, registered_apps, heap_free |
| 0x0801 | `CFE_EVS_HK_TLM_MID` | HK | 1 Hz | yes | no | cmd_count, cmd_err_count, log_enable |
| 0x0803 | `CFE_SB_HK_TLM_MID` | HK | 1 Hz | yes | yes | cmd_count, cmd_err_count, pipe_overflow_cnt |
| 0x0804 | `CFE_TBL_HK_TLM_MID` | HK | 1 Hz | yes | no | cmd_count, cmd_err_count, num_loaded_tables |
| 0x0805 | `CFE_TIME_HK_TLM_MID` | HK | 1 Hz | yes | yes | cmd_count, cmd_err_count, leap_seconds, utc_offset |
| 0x0808 | `CFE_EVS_LONG_EVENT_MSG_MID` | Event | async | yes | no | event_id, app_name, message[122] |
| 0x0809 | `CFE_EVS_SHORT_EVENT_MSG_MID` | Event | async | yes | no | event_id, app_name, message[28] |

### 4.2 cFS heritage apps (base `0x0800`)

| MID | Name | Type | Cadence | TO | Decoded | Fields of note |
|---|---|---|---|---|---|---|
| 0x0880 | `TO_HK_TLM_MID` | HK | 1 Hz | yes | no | cmd_count, heartbeat_counter |
| 0x0887 | `MM_HK_TLM_MID` | HK | 1 Hz | yes | no | mem_peek_count, mem_poke_count |
| 0x088A | `FM_HK_TLM_MID` | HK | 1 Hz | yes | no | open_files, child_cmd_count |
| 0x0890 | `MD_HK_TLM_MID` | HK | 1 Hz | yes | no | dwell_enabled_mask, dwell_count |
| 0x0891 | `MD_DWELL_PKT_MID_BASE` | Dwell | as configured | yes | no | values[] (user-sampled memory) |
| 0x0897 | `SCH_HK_TLM_MID` | HK | 1 Hz | yes | yes | scheduler_ticks, slot_count |
| 0x089B | `HK_HK_TLM_MID` | HK | 1 Hz | yes | yes | cmd_count, combined_pkt_count |
| 0x089C..0x089F | `HK_COMBINED_PKT1..4_MID` | Combined HK | 1 Hz | yes | no | packed subsets (see `hk_cpy_tbl.c`) |
| 0x08A4 | `CS_HK_TLM_MID` | HK | 1 Hz | yes | no | cmd_count, baseline_crc, checksum_result |
| 0x08A7 | `LC_HK_TLM_MID` | HK | 1 Hz | yes | yes | lc_state, ap_pass_count, ap_fail_count |
| 0x08AA | `SC_HK_TLM_MID` | HK | 1 Hz | yes | yes | num_rts_active, rts_active_ctr |
| 0x08AD | `HS_HK_TLM_MID` | HK | 1 Hz | yes | yes | cpu_avg_pct, cpu_peak_pct (stored as pct x 100) |
| 0x08B0 | `CF_HK_TLM_MID` | HK | 1 Hz | yes | no | num_active_xfers, completed_xfers |
| 0x08B8 | `DS_HK_TLM_MID` | HK | 1 Hz | yes | no | num_files_written, file_write_bytes |
| 0x08E0 | `CI_LAB_HK_TLM_MID` | HK | on-cmd | yes | no | cmd_count, ingest_count |
| 0x08E8 | `TO_LAB_HK_TLM_MID` | HK | 1 Hz | yes | yes | output_status, cmd_count |

The row you most often see in Kibana is `0x089B HK_HK_TLM_MID`; it carries
the HK app's own counters as well as the combined packet count. That is the
MID shown in the sample hit at the top of Section 12.

### 4.3 Hardware-component apps (base `0x0900`)

All MIDs are derived from each component's `*_topicids.h`:
`CMD = 0x1900 | topic`, `HK_TLM = 0x0900 | topic`, `DEVICE_TLM = 0x0900 | (topic+1)`.

| Component | CMD_MID | REQ_HK_MID | HK_TLM_MID | DEVICE_TLM_MID(s) | TO | Decoded |
|---|---|---|---|---|---|---|
| `generic_adcs`         | 0x1940 | 0x1941 | 0x0940 | 0x0941 DI, 0x0942 AD, 0x0943 GNC, 0x0944 AC, 0x0945 DO | HK only | no |
| `arducam` (CAM)        | 0x19C8 | 0x19C9 | 0x09C8 | 0x09C9 (EXP) | yes | no |
| `generic_css`          | 0x1910 | 0x1911 | 0x0910 | 0x0911 | yes | no |
| `generic_eps`          | 0x191A | 0x191B | 0x091A | -      | yes | yes |
| `generic_fss`          | 0x1920 | 0x1921 | 0x0920 | 0x0921 | yes | no |
| `generic_imu`          | 0x1925 | 0x1926 | 0x0925 | 0x0926 | yes | no |
| `generic_mag`          | 0x192A | 0x192B | 0x092A | 0x092B | yes | no |
| `generic_radio`        | 0x1930 | 0x1931 | 0x0930 | 0x0931 | HK only | no |
| `generic_reaction_wheel` | 0x1992 | 0x1993 | 0x0993 | -    | HK only | no |
| `generic_star_tracker` | 0x1935 | 0x1936 | 0x0935 | 0x0936 | yes | no |
| `generic_thruster`     | 0x19EA | 0x19EB | 0x09EA | -      | via MGR | no |
| `generic_torquer`      | 0x193A | 0x193B | 0x093A | -      | HK only | no |
| `mgr` (Mission Mgr)    | 0x19F8 | 0x19F9 | 0x09F8 | -      | yes | yes |
| `novatel_oem615` (GPS) | 0x1970 | 0x1971 | 0x0970 | 0x0971 | yes | yes (HK only) |
| `sample`               | 0x19FA | 0x19FB | 0x09FA | 0x09FB | yes | no |
| `syn`                  | 0x19FC | 0x19FD | 0x09FC | 0x09FD | yes | no |

> Note: any quick-reference numbers from upstream NOS3 docs (`GPS 0x1870/0x1871`,
> etc.) are the cFS heritage base. **This fork uses the `0x1900` component
> base; those upstream numbers are stale, and the values above are the
> source of truth.**

Key fields in the most-used packets:

- **`GENERIC_EPS_HK_TLM` (0x091A)**: `CommandCount`, `CommandErrorCount`,
  then `DeviceHK`: `BatteryVoltage` (uint16 mV, offset 16, watched by
  WP0/WP1), `BatteryTemperature`, `3V3Bus*`, `5VBus*`, `12VBus*`,
  `SolarArrayVoltage` (offset 28, watched by WP5), per-switch enable flags.
- **`NOVATEL_OEM615_DEVICE_TLM` (0x0971)**: ECEF position (3 x float64),
  ECEF velocity (3 x float64), `lat` (float32 at offset 74, watched by
  WP6/WP7), `lon` (float32 at offset 78, watched by WP8/WP9), `alt`
  (float32 at offset 82, watched by WP3), GPS time (weeks/sec/frac),
  leap-second count.
- **`GENERIC_RW_APP_HK_TLM` (0x0993)**: per-wheel `DeviceEnabled_RW[0..2]`
  (uint8 at offsets 20, 21, 22; offset 20 watched by WP4),
  `CurrentSpeed_RW[0..2]`, `CurrentMomentum_RW[0..2]`.
- **`HS_HK_TLM` (0x08AD)**: `UtilCpuAvg` (uint32 pct x 100 at offset 40,
  watched by WP2), `UtilCpuPeak` (offset 44), per-app execution-count
  delta array.

### 4.4 Not wired in this config

`sbn` (Software Bus Network) is listed in `cpu1_cfe_es_startup.scr:23` but
there is no second CPU to forward traffic to, so its HK rarely appears.
`noisy_app` and `cpu_killer` are `OFF_APP` entries, loaded only when a
thesis/attack scenario enables them.

---

## 5. Command inventory

Every component app accepts at minimum:

| Command code | Meaning |
|---|---|
| `NOOP_CC`   | ack only; used to verify the command path |
| `RESET_COUNTERS_CC` | zero the `CommandCount` / `CommandErrorCount` fields |

App-specific codes worth knowing:

| App | Code | Effect |
|---|---|---|
| `GENERIC_EPS`   | `SWITCH_CC`         | set one of 8 power switches on/off |
| `NOVATEL_OEM615`| `ENABLE_CC / DISABLE_CC / CONFIG_CC` | toggle GPS log rate and output format |
| `GENERIC_RW`    | `SET_TORQUE`, `ENABLE`, `DISABLE` | wheel torque/spin control |
| `GENERIC_TORQUER` | `ENABLE_CC / DISABLE_CC`     | used by RTS 003 for momentum dump |
| `MGR`           | `MGR_SET_MODE_CC`    | pass `MGR_SAFE_MODE` or `MGR_SCIENCE_MODE` as payload |
| `SC`            | `SC_START_RTS_CC`    | ground trigger for a stored RTS (001..004) |
| `LC`            | `LC_SET_AP_STATE_CC` | enable/disable an actionpoint |
| `MD`            | `MD_START_DWELL_CC`  | arm a memory-dwell sampler |
| `MM`            | `MM_POKE_MEM_CC`     | direct memory write (attack-surface; monitored via EVS) |
| `CS`            | `CS_ENABLE_CC / CS_DISABLE_CC` | toggle integrity monitoring |

`CI_LAB` listens for these on UDP :5012; `SC` executes them from compiled RTS
tables (see Section 7).

---

## 6. Scheduler cadence cheat-sheet

The SCH major frame is 1 s, 50 slots of 20 ms. One HK request fires per slot.
Key slot assignments (from `sch_def_schtbl.c` / `sch_def_msgtbl.c`):

| Slot | Fires | Index -> Target |
|---|---|---|
| 0   | CF wakeup (17) | `CF_AppMain` tick |
| 2   | EVS HK request (2) | `CFE_EVS` publishes `0x0801` |
| 5   | MD HK request (12) | `MD` publishes `0x0890` |
| 6   | MM HK request (13) | `MM` publishes `0x0887` |
| 7-8 | DS + FM HK (7, 8) | `0x08B8`, `0x088A` |
| 11  | LC HK (11) | `0x08A7` |
| 13  | SB HK (3) | `0x0803` |
| 14  | SC HK (14) | `0x08AA` |
| 19  | HK app HK (19) | `0x089B` and combined packets |
| 23  | TIME HK | `0x0805` |
| 24  | HS HK | `0x08AD` |
| 25-39 | component HKs | one slot per component (EPS, GPS, RW, RADIO, IMU, MAG, ADCS, CSS, FSS, ST, TORQUER, THRUSTER, SAMPLE, MGR, ...) |
| 40  | TO_LAB HK | `0x08E8` |
| 43  | ES HK (1) | `0x0800` |

The `SEND_HK` request is itself a command packet with the `0xC000` function
code in the secondary header; that is what you see as MID `0x18xx` on the bus
every second.

---

## 7. `cfs_god_view.json` schema

One JSON object per packet, one packet per line. Written by
`god_view_capture.py`, consumed by the `file { type => "software_bus" }`
input in `logstash.conf`.

| Field | Type | Meaning |
|---|---|---|
| `timestamp` | float (UNIX epoch) | when the capture script received the UDP packet |
| `msg_id`    | string `"0xNNNN"`  | CCSDS stream ID |
| `apid`      | int               | lower 11 bits of the stream ID |
| `pkt_type`  | `"TLM"` or `"CMD"` | derived from the high bit of the stream ID |
| `sequence`  | int               | 14-bit CCSDS sequence count (wraps at 16 384) |
| `length`    | int               | total packet length in bytes |

Logstash enrichments (`logstash.conf`):

| Enrichment | Values |
|---|---|
| `layer`          | `FSW_CORE`, `FSW_HERITAGE`, `COMPONENT`, `RESEARCH`, `UNKNOWN` |
| `subsystem`      | `EPS`, `GPS`, `RW`, `ADCS`, `LC`, `SC`, `MM`, `MD`, `CS`, `HS`, ... |
| `msg_name`       | human-readable MID (e.g. `GENERIC_EPS_HK_TLM_MID`) |
| `msg_direction`  | `CMD` or `TLM` |
| `msg_id_int`     | `msg_id` parsed to an integer (for numeric comparisons) |
| `sb_sequence_gap`| `true` when the per-MID sequence counter jumps (packet drop) |

A second file `omni_logs/tlm_hk_decoded.log` is written by the same script
for the HKs listed in its `_DECODE_BY_NAME` table (currently `HS`, `LC`,
`SC`, `HK`, `TO_LAB`, `EPS` basics, `MGR`, plus cFE `ES`, `SB`, `TIME`,
`SCH`, and the Novatel HK counter). The table is keyed by symbolic MID name
(e.g. `GENERIC_EPS_HK_TLM_MID`); integer MIDs are resolved at startup from
`cfg/build/mid_registry.json`, so a topic-ID change in the FSW only requires
a fresh `make config`. Fields match those in Section 4, and the decoded
documents land in Kibana with `type=hk_decoded` (see 3.8).

---

## 8. Log-file catalog (`omni_logs/` and `attack_logs/`)

Every file listed below is created by one of the four capture scripts from
Section 2.1. "Logstash type" is the `type` tag the Logstash input assigns,
which is how you filter in Kibana.

| File | Producer | Logstash type | Example line signature | Extracted numeric fields |
|---|---|---|---|---|
| `omni_logs/cfs_evs.log`      | `cfs_evs_capture.sh`  | `system_log`                | `YYYY-DDD-HH:MM:SS.sssss App ID/Type: text` (optional docker timestamp prefix) | `evs_app_name`, `evs_event_id`, `evs_event_type_num`, `evs_severity`, `evs_message`; plus attack tags (`cs_disabled`, `mm_memory_poke`, `md_jam_dwell`, `lc_actionpoint_fired`, `science_overfly`, `sb_pipe_overflow`, `cs_mm_combined_attack`) |
| `omni_logs/nos3-eps.log`     | `sim_logs_capture.sh` | `system_log`                | `[LEVEL] - EPS: ...`                 | `eps_solar_power_w`, `eps_power_used_w`, `eps_battery_wh`, `eps_battery_mv`, `eps_solar_array_enabled`, derived `eps_battery_soc_pct`, `eps_power_balance_w` |
| `omni_logs/nos3-gps.log`     | `sim_logs_capture.sh` | `system_log`                | `GPS Data Point: AbsTime ... ECEF ...` | `gps_lat`, `gps_lon`, `gps_alt`, `gps_ecef_x/y/z`, `gps_abs_time`, `gps_leap_seconds`, `gps_valid` |
| `omni_logs/nos3-rw0..2.log`  | `sim_logs_capture.sh` | `system_log`                | `REPLY CURRENT_MOMENTUM=...`        | `rw_momentum` |
| `omni_logs/nos3-imu.log`     | `sim_logs_capture.sh` | `system_log`                | `X linear acceleration: ... rotational rate: ...` | `imu_acc_x/y/z`, `imu_gyro_x/y/z` |
| `omni_logs/nos3-mag.log`     | `sim_logs_capture.sh` | `system_log`                | `Magnetometer values: ...`           | `mag_x`, `mag_y`, `mag_z` (scientific notation) |
| `omni_logs/nos3-fss.log`     | `sim_logs_capture.sh` | `system_log`                | `valid=TRUE, alpha=..., beta=...`    | `fss_valid`, `fss_alpha`, `fss_beta` |
| `omni_logs/nos3-css.log`     | `sim_logs_capture.sh` | `system_log`                | CSS sim stdout                      | subsystem tag only (no numeric extraction) |
| `omni_logs/nos3-startrk.log` | `sim_logs_capture.sh` | `system_log`                | `is_valid=1, data_point=q0, q1, q2, q3, ...` | `st_q0..q3`, `st_valid` |
| `omni_logs/nos3-torquer.log` | `sim_logs_capture.sh` | `system_log`                | `Dipole: x=..., y=..., z=...`        | `torquer_dipole_x/y/z` |
| `omni_logs/nos3-thruster.log`| `sim_logs_capture.sh` | `system_log`                | thruster sim stdout (often empty in the research config) | - |
| `omni_logs/nos3-radio.log`   | `sim_logs_capture.sh` | `system_log`                | radio sim stdout; `cryptolib` retries are dropped by noise filter | - |
| `omni_logs/nos3-truth42.log` | `sim_logs_capture.sh` | `system_log`                | `SC[0] PosN: ... VelN: ... qbn: ...` | `t42_pos_x/y/z`, `t42_vel_x/y/z`, `t42_q0..q3`, `t42_svb_x/y/z`, `t42_gyro_x/y/z` |
| `omni_logs/nos3-blackboard.log` | `sim_logs_capture.sh` | `system_log`              | IPC blackboard stdout                | - |
| `omni_logs/nos3-sample.log`  | `sim_logs_capture.sh` | `system_log`                | `sun body vector: x=..., y=..., z=...` | `sample_sun_body_x/y/z` |
| `omni_logs/nos3-cam.log`     | `sim_logs_capture.sh` | `system_log`                | Arducam sim stdout                  | - |
| `omni_logs/cpu_monitor.log`  | `cpu_monitor.sh`      | `system_log`                | `CPU_MONITOR TOTAL_CPU_PCT=... NUM_PIDS=...` / `CPU_THREAD THREAD_NAME=... CPU_PCT=...` | `TOTAL_CPU_PCT`, `CPU_PCT`, `NUM_PIDS`, `CMDCOUNTER`, `ERRCOUNTER`, `PINGCOUNTER`; tag `cpu_spike` at >= 80 % |
| `omni_logs/tlm_hk_decoded.log` | `god_view_capture.py` | `hk_decoded` (plus a duplicate `system_log` hit; see 3.8) | one JSON object per decoded HK      | `cpu_avg_pct`, `cpu_peak_pct`, `battery_mv`, `solar_array_mv`, `spacecraft_mode`, `sci_pass_count`, `num_rts_active`, `rts_active_ctr`, `cmd_count`, `cmd_err_count`, `lc_state`, `curr_ats_id` |
| `omni_logs/god_view.log`     | `god_view_capture.py` | `system_log` (startup only) | capture-script stdout/stderr        | - |
| `attack_logs/cfs_god_view.json` | `god_view_capture.py` | `software_bus`            | one JSON object per CCSDS packet (see Section 7) | envelope fields + Logstash enrichments |

A file at 0 bytes does not imply failure. In the minimal and research configs
several components (thruster, sample, camera) run without producing stdout.
The corresponding `nos3-*.log` files are created empty so Logstash still has
something to attach to and so missing files do not mask a startup crash.

---

## 9. Important FSW functions

One pattern repeats across every component: `AppMain` boots, `AppInit`
subscribes to pipes, the main loop pends on the SB, `ProcessCommandPacket`
dispatches by MID, per-command handlers run, `ReportHousekeeping` publishes
HK when asked.

### 9.1 Component apps

Paths below are rooted at `nos3/components/<component>/fsw/cfs/src/`.

#### EPS (`generic_eps/fsw/cfs/src/generic_eps_app.c`)

| Function | Line | Purpose |
|---|---|---|
| `EPS_AppMain` | 23 | Main task: register perf log, call `AppInit`, then pend on `CFE_SB_ReceiveBuffer` and dispatch. |
| `GENERIC_EPS_AppInit` | 95 | Create the command pipe, subscribe to `GENERIC_EPS_CMD_MID` and `GENERIC_EPS_REQ_HK_MID`, init the HK packet via `CFE_MSG_Init`. |
| `GENERIC_EPS_ProcessCommandPacket` | 210 | Top-level dispatcher: routes `CMD_MID` -> `ProcessGroundCommand`, `REQ_HK_MID` -> `ProcessTelemetryRequest`. |
| `GENERIC_EPS_ProcessGroundCommand` | 246 | Switch on the command code (`NOOP`, `RESET`, `SWITCH`); validates payload length before acting. |
| `GENERIC_EPS_ReportHousekeeping` | 375 | Calls the device layer (`RequestHK`) to populate voltages/currents/switch states, then `CFE_SB_TransmitMsg`. |
| `GENERIC_EPS_RequestHK` | `shared/generic_eps_device.c` | I2C transaction: read battery mV/C, solar array V, per-switch voltage/current/status; CRC-checked. |
| `GENERIC_EPS_CommandSwitch` | `shared/generic_eps_device.c` | I2C write to a power switch; verifies state change with a follow-up read. |

#### GPS / Novatel (`novatel_oem615/fsw/cfs/src/novatel_oem615_app.c`)

| Function | Line | Purpose |
|---|---|---|
| `NOVATEL_AppMain` | 22 | Main loop. |
| `NOVATEL_OEM615_AppInit` | 106 | Subscribes `CMD_MID`, `REQ_HK_MID`; opens UART port. |
| `NOVATEL_OEM615_ProcessCommandPacket` | 226 | Dispatcher. |
| `NOVATEL_OEM615_ReportHousekeeping` | 461 | Publishes HK (counters + last position status). |
| `NOVATEL_OEM615_ReportDeviceTelemetry` | 472 | Publishes `DEVICE_TLM_MID` with the ECEF / lat / lon / alt / velocity struct watched by WP3/6/7/8/9. |
| `NOVATEL_OEM615_SafeRequestData` | 626 | UART read of the binary GPS log; parsed into `NOVATEL_OEM615_Device_Data_tlm_t`. |
| `NOVATEL_OEM615_ChildProcessRequestData` | 645 | Runs the blocking UART read in a child task so `AppMain` does not stall. |

#### Reaction Wheel (`generic_reaction_wheel/fsw/cfs/src/generic_reaction_wheel_app.c`)

| Function | Line | Purpose |
|---|---|---|
| `RW_AppMain` | 38 | Main loop; also subscribes to a momentum-limit notification MID. |
| `GENERIC_RW_AppInit` | 104 | Opens the CAN bus; subscribes to CMD/REQ_HK. |
| `GENERIC_RW_ProcessCommandPacket` | 228 | Dispatcher (takes `CFE_MSG_Message_t *` explicitly). |
| `GENERIC_RW_ReportHousekeeping` | 338 | Publishes per-wheel speed + momentum + `DeviceEnabled` flags (WP4 watches byte 20). |
| `GENERIC_RW_Set_Torque` | 433 | CAN write of a torque command to a wheel. |
| `GENERIC_RW_Enable` / `Disable` | 532 / 620 | Spin-up / spin-down a wheel; flips the `DeviceEnabled` flag ground sees in HK. |

#### Radio (`generic_radio/fsw/cfs/src/generic_radio_app.c`)

| Function | Line | Purpose |
|---|---|---|
| `RADIO_AppMain` | 22 | Main loop. |
| `GENERIC_RADIO_AppInit` | 101 | UART init. |
| `GENERIC_RADIO_ProcessCommandPacket` | 238 | Dispatcher. |
| `GENERIC_RADIO_ReportHousekeeping` | 428 | Polls radio RSSI / TX power / frequency, publishes HK. |
| `GENERIC_RADIO_ProxyTask` | 503 | Background proxy that bridges radio traffic to ground. |

#### ADCS (`generic_adcs/fsw/cfs/src/generic_adcs_app.c`)

| Function | Line | Purpose |
|---|---|---|
| `ADCS_AppMain` | 52 | Main loop; subscribes to CMD/REQ_HK and the external ADaC update MID. |
| `Generic_ADCS_AppInit` | 124 | Standard init plus attitude-state bootstrapping. |
| `Generic_ADCS_ProcessCommandPacket` | 314 | Dispatcher. |
| `Generic_ADCS_ReportHousekeeping` | (declared line 40) | Publishes mode + quaternion + rate. |
| `Generic_ADCS_SendDI/AD/GNC/AC/DO Command` | lines 42-46 | Publishes the five device-telemetry variants (DI, AD, GNC, AC, DO; 0x0941..0x0945). |

#### Sample (`sample/fsw/cfs/src/sample_app.c`)

Follows the same shape (`AppMain:23`, `AppInit:100`, `ProcessCommandPacket:217`,
`ProcessGroundCommand:269`, `ReportHousekeeping:433`, `ReportDeviceTelemetry:464`).
Useful as a minimal reference when adding a new component.

### 9.2 cFS heritage apps

Paths below are rooted at `nos3/fsw/apps/<app>/fsw/src/`.

#### CI_LAB (`ci_lab/fsw/src/ci_lab_app.c`)

| Function | Line | Purpose |
|---|---|---|
| `CI_Lab_AppMain` | 78 | Listens on UDP :5012; decodes CCSDS command packets; injects them on the SB. This is the *only* uplink path in the simulation. |
| `CI_LAB_TaskInit` | 131 | Creates the uplink socket, SB command pipe. |
| `CI_LAB_ProcessCommandPacket` | 195 | Dispatcher for internal CI_LAB commands (NOOP, reset). |
| `CI_LAB_ReadUpLink` | 323 | Per-tick UDP `recvfrom`; forwards the bytes straight onto the SB as a new message. |

#### TO_LAB (`to_lab/fsw/src/to_lab_app.c`)

| Function | Line | Purpose |
|---|---|---|
| `TO_LAB_AppMain` | 89 | Main loop: subscribe to every MID listed in `to_config.c`, then forward as UDP to the configured host. |
| `TO_LAB_init` | 141 | Open downlink socket; subscribe the initial MID list. |
| `TO_LAB_EnableOutput` | 248 | **This is what `god_view_capture.py` commands.** Unlocks the UDP sender so packets start flowing to the ground address. |
| `TO_LAB_forward_telemetry` | 70 (decl) / 301 | Per-tick loop that drains the SB pipe and writes each packet to UDP. |
| `TO_LAB_AddPacket` / `RemovePacket` | 499 / 524 | Runtime add/remove a MID from the forwarded set. |

#### Others

- **LC, SC, HS, MM, MD, CS, DS, FM, CF, SCH** follow upstream cFS exactly;
  the interesting bits are in their *tables* (see Section 10), not the `.c`.
- **Custom autonomy lives entirely in tables**:
  `lc_def_wdt.c` (watchpoints), `lc_def_adt.c` (actionpoints),
  `sc_rts00[1-4].c` (stored sequences). No C-level LC/SC changes.

### 9.3 OSAL / time driver: `nos3/fsw/osal/src/os/nos/src/NOS-time.c`

This file replaces the host POSIX clock with NOS Engine's simulated tick
counter. **Every `OS_TaskDelay`, `CFE_TIME_Get`, `CFE_ES_PerfLog`, and
scheduler tick in the flight software resolves through here.** If the
simulation is running slower or faster than wall time, this is the file.

| Function | Line | Purpose |
|---|---|---|
| `NOS_clock_gettime`   | 39 | Reads `CFE_PSP_sim_time` (ticks since boot) and converts to `timespec`. |
| `NOS_clock_nanosleep` | 49 | Polls `CFE_PSP_sim_time` until the tick-equivalent of the requested sleep has elapsed; includes a "max residual" guard against startup-tick anomalies. |
| `NOS_clock_settime`   | 89 | Pushes a new time to the bus via `NE_bus_set_time`; used by ground commands and test harnesses. |
| `NOS_timer_create`    | 112 | Allocates an `NOS_timer_t` entry and registers it with the sim bus. |

### 9.4 Host-side capture scripts

For the full capture-script table see 2.1; details follow.

- **`nos3/scripts/cfs_evs_capture.sh`**: attaches `docker logs -f --timestamps`
  to `sc01-nos-fsw` and appends to `omni_logs/cfs_evs.log`. Reconnects on
  container restart; exits when NOS Engine stops.
- **`nos3/scripts/sim_logs_capture.sh`**: one `docker logs -f` per simulator
  container (`sc01-gps`, `sc01-generic-eps-sim`, ...). Waits for
  `sc01-nos-engine-server` to be ready before attaching. Tracks child PIDs
  and kills them on exit.
- **`nos3/scripts/cpu_monitor.sh`**: polls `docker stats` on `sc01-nos-fsw`
  every 2 s (configurable via argv[1]); emits `CPU_MONITOR ... CPU_THREAD ...`
  KV lines. `cpu_monitor.log` is what you grep during a CPU-attack scenario.
- **`nos3/scripts/god_view_capture.py`**: headless capture daemon. On
  startup, `_discover_ci_lab_ip()` (top of file) resolves the CI_LAB
  container IP via `docker inspect`; the main loop sends
  `TO_LAB_OUTPUT_ENABLE_CC` to `CI_LAB_HOST:5012`, listens on UDP :5013,
  parses the CCSDS primary header (`god_view_capture.py:227-249`), writes
  `{msg_id, apid, pkt_type, sequence, length, timestamp}` JSON lines to
  `attack_logs/cfs_god_view.json`. `_DECODE_BY_NAME`
  (`god_view_capture.py:76-114`) maps selected HK MIDs to
  `(app_name, [(field, offset, struct_fmt), ...])` tuples; matching packets
  are additionally written as JSON to `omni_logs/tlm_hk_decoded.log`. The
  enable command is resent every 30 s during warm-up, then every 300 s. A
  singleton lock (`/tmp/nos3_god_view.lock`) prevents duplicate listeners.
- **`nos3/scripts/stop.sh`**: kills the capture scripts, tears down Docker
  containers, clears Elasticsearch indices. The safe "reset to a clean run"
  button.
- **`nos3/Makefile`**: top-level targets: `config` (run `configure.py`),
  `prep`, `fsw`, `sim`, `gsw`, `launch`, `stop`, `test-fsw`,
  `code-coverage`, `save-run`, `load-run`, `debug`, `clean`. The launch
  target spawns the four capture scripts in parallel at lines 244-261.

### 9.5 Logstash pipeline: `nos3/elk/logstash.conf`

Three input layers, one output (Elasticsearch). Line ranges refer to
`logstash.conf` in the current tree.

#### Inputs (logstash.conf:24-52)

| Layer | Path | Codec | `type` | Meaning |
|---|---|---|---|---|
| 1 | `/attack_logs/cfs_god_view.json` | json | `software_bus` | CCSDS envelopes written by `god_view_capture.py` |
| 2 | `/omni_logs/*.log` | plain | `system_log` | Every capture script's text output (simulators, cFS EVS, cpu_monitor). Also matches `tlm_hk_decoded.log` as raw JSON lines. |
| 3 | `/omni_logs/tlm_hk_decoded.log` | json | `hk_decoded` | Decoded HK payload records (parsed JSON, ready for trending) |

All three use `start_position => beginning` and `sincedb_path => /dev/null`
so a Logstash restart re-ingests the files from the top.

#### Enrichment for `software_bus` (logstash.conf:61-145)

- `translate { msg_id -> msg_name }` via `/etc/logstash/mid/mid_to_name.yml`
  (logstash.conf:68-74). Dictionary is reloaded every 60 s; fallback
  `UNKNOWN_MSG`.
- `translate { msg_id -> layer }` via `mid_to_layer.yml` (79-85). Values
  `FSW_CORE`, `FSW_HERITAGE`, `COMPONENT`, `RESEARCH`, `UNKNOWN`.
- `translate { msg_id -> subsystem }` via `mid_to_subsystem.yml` (88-94).
- Ruby block (98-109) parses the hex string into `msg_id_int` and derives
  `msg_direction` from bit 12 (`0x1000`): `CMD` for `>=0x1800`, `TLM`
  otherwise.
- Attack tags: `noisy_app_spam_target` for the 7 known target MIDs
  (115-117); `beacon_cmd` for `0x18F0` (120-122).
- Per-MID sequence-counter state in a Ruby block (125-144) emits
  `sb_sequence_gap` and `sb_gap_size` when the 14-bit counter jumps.

#### Per-subsystem parsing for `system_log` (logstash.conf:151-782)

- `subsystem` is grokked from the filename (154-159); `sc01_-_` prefix is
  stripped.
- `cfs_evs` block (175-456): docker timestamp strip, cFE EVS grok
  (App + EventID/Type + text), `evs_severity` mapping, NOISY_APP attack
  lifecycle tags (`attack_armed`, `attack_trigger_ping`, `attack_loaded`),
  BEACON_APP `beacon_pong`, `sb_pipe_overflow`, MM/CS/MD command taggers
  (`mm_memory_poke`, `mm_memory_fill`, `mm_memory_load_wid`,
  `mm_eeprom_write_enable/_disable`, `cs_disabled/_enabled/_checksum_failure`,
  `md_dwell_start/_stop/_jam_dwell`), LC actionpoint transitions and
  `science_overfly`, HS `hs_app_failure` and `hs_cpu_hogging`, MGR mode
  transitions, and the combined `cs_mm_combined_attack` tag (449-455).
- Generic simulator block (464-487): ISO timestamp + `[LEVEL]` grok yields
  `sim_timestamp`, `log_level`, `sim_component`, `sim_message`.
- Subsystem-specific grok blocks extract the fields listed in Section 8
  (EPS 493-523, RW 529-535, GPS 541-562, IMU 574-591, MAG 602-615,
  FSS 625-636, ST 644-658, Truth42 666-702, Torquer 710-723,
  Sample 731-744).
- `cpu_monitor` block (754-776): `kv` parser converts
  `TOTAL_CPU_PCT`, `CPU_PCT`, `NUM_PIDS`, `CMDCOUNTER`, `ERRCOUNTER`,
  `PINGCOUNTER` to numeric; `cpu_spike` tag at `TOTAL_CPU_PCT >= 80`.

#### Noise-drop rules

Each drop has a one-line reason (see also `docs/noise-filters.md`):

- XML config lines and blank lines (162-164): mount noise.
- cFS startup chatter (222-224): "Startup Sync failed", "cFE startup complete",
  "attempting to start", "Startup.*timeout".
- OSAL symbol-table probes (226-231): LC / SC are statically linked; the
  "undefined symbol" messages are expected.
- MD table-staging 0xC0000003 (233-238): MD tables are not shipped in the
  research config.
- MGR fresh-container SAFE entry (240-245): "Restore Hk Packet error -
  Anomalous reboot" on a first boot is the designed behaviour.
- Radio cryptolib retries (478-480): SDLS service is not deployed; radio
  retries then runs unencrypted.
- 42 start-up race (482-487): component sims log "failed to connect host
  fortytwo" until dynamics is ready; auto-recovers.

#### Extraction for `hk_decoded` (logstash.conf:788-809)

Type coercion only (fields are already decoded by Python): `cpu_avg_pct`,
`cpu_peak_pct`, `battery_mv`, `solar_array_mv`, `spacecraft_mode`,
`sci_pass_count`, `num_rts_active`, `rts_active_ctr`, `cmd_count`,
`cmd_err_count`, `lc_state`, `curr_ats_id`. A `date` filter parses the
payload's UNIX `timestamp` into `@timestamp`, so hk_decoded events keep
the FSW-originated time (not Logstash ingest time).

#### Output (logstash.conf:816-822)

```
elasticsearch { hosts => ["elasticsearch:9200"] index => "nos3-telemetry-%{+YYYY.MM.dd}" }
stdout { codec => rubydebug }
```

Daily-rollover index; stdout echoes every event to the Logstash container
log for debugging.

#### Field provenance (which layer exposes which field)

| Field group | Source file | Layer |
|---|---|---|
| `msg_id`, `msg_name`, `msg_id_int`, `msg_direction`, `layer`, `subsystem`, `apid`, `pkt_type`, `length`, `sequence`, `sb_sequence_gap`, `sb_gap_size` | `cfs_god_view.json` | `software_bus` |
| `evs_*`, `cs_*`, `mm_*`, `md_*`, `lc_*`, `hs_*`, `mgr_*`, attack lifecycle tags | `cfs_evs.log` | `system_log` |
| `eps_*`, `gps_*`, `rw_momentum`, `imu_*`, `mag_*`, `fss_*`, `st_*`, `t42_*`, `torquer_*`, `sample_*` | `nos3-*.log` | `system_log` |
| `TOTAL_CPU_PCT`, `CPU_PCT`, `THREAD_NAME`, `NUM_PIDS`, `CMDCOUNTER`, `ERRCOUNTER`, `PINGCOUNTER`, `cpu_spike` | `cpu_monitor.log` | `system_log` |
| `cpu_avg_pct`, `cpu_peak_pct`, `battery_mv`, `solar_array_mv`, `spacecraft_mode`, `sci_pass_count`, `num_rts_active`, `rts_active_ctr`, `cmd_count`, `cmd_err_count`, `lc_state`, `curr_ats_id`, `app`, `subsystem` | `tlm_hk_decoded.log` | `hk_decoded` |

---

## 10. "Where do I change X?"

| Goal | File(s) | What to do |
|---|---|---|
| Add a new command to an existing app | `components/<name>/fsw/cfs/src/<name>_app.c` -> `*_ProcessGroundCommand` | Add a `case` in the switch, a length-verified handler, and (if user-visible) an EVS event. |
| Add a field to an existing HK packet | `components/<name>/fsw/cfs/inc/<name>_msg.h` -> HK struct; `<name>_ReportHousekeeping` | Add the field; populate it; rebuild (FSW struct is wire-format, so ground must match). |
| Change what ground sees | `cfg/nos3_defs/tables/to_config.c` | Add / remove an `_HK_TLM_MID` entry. |
| Change SCH cadence | `cfg/nos3_defs/tables/sch_def_schtbl.c` (slot layout) + `sch_def_msgtbl.c` (MIDs) | Each slot fires one message per second. |
| Add a new autonomy rule | `cfg/nos3_defs/tables/lc_def_wdt.c` (watchpoint) + `lc_def_adt.c` (actionpoint) | New WP entry at the next free index; new AP with an RPN equation referencing it. |
| Add a new stored sequence | `cfg/nos3_defs/tables/sc_rts00N.c` + add to `targets.cmake` | Follow the shape of `sc_rts001.c`; register via `CFE_TBL_FILEDEF`. |
| Parse a new simulator log | `elk/logstash.conf` | Add a grok/dissect block under the `omni_logs` input, tag the subsystem. |
| Monitor a new subsystem container | `scripts/sim_logs_capture.sh` + `elk/logstash.conf` | Add the container name and log path on the capture side, add a grok block on the Logstash side. |
| Add a new decoded HK | `scripts/god_view_capture.py` `_DECODE_BY_NAME` + `elk/logstash.conf` `hk_decoded` conversions (788-809) | Add a `{NAME: (app, [(field, offset, fmt), ...])}` entry; add matching `convert` entries so Kibana indexes the new field as a number. |
| New component app | Copy the `sample` component; register it in `cpu1_cfe_es_startup.scr`, `sch_def_schtbl.c`, `to_config.c`. | Walkthrough in `docs/cfs-resource-limits/ADDING_CFS_APPS.md`. |

---

## 11. Pointers to deeper reading

- Live debugging recipes -> [Section 12](#12-live-debugging-cheat-sheet) below.
- Mission scenario and operational modes -> [`missions/NOS3-DTU-EO1-mission-implementation.md`](missions/NOS3-DTU-EO1-mission-implementation.md)
- Upstream cFS concepts (LC, SC, EVS semantics) -> [NASA cFS docs](https://github.com/nasa/cFS)
- Adding a new cFS app -> [`cfs-resource-limits/ADDING_CFS_APPS.md`](cfs-resource-limits/ADDING_CFS_APPS.md)
- Logstash noise-filter rationale -> [`noise-filters.md`](noise-filters.md)
- Known build errors and fixes -> [`nos3_fsw_error_fixes.md`](nos3_fsw_error_fixes.md), [`fsw-build-errors-msgid-redefinition.md`](fsw-build-errors-msgid-redefinition.md)

---

## 12. Live debugging cheat-sheet

Commands here are meant to be pasted during a running sim. All paths assume
you are in `nos3/`. ELK must be up (`cd nos3/elk && docker compose up -d`)
for Kibana and the `curl` queries.

### 12.1 Tail the raw files on the host

```bash
# FSW event stream (most useful signal during an attack)
tail -f omni_logs/cfs_evs.log

# EPS sim stdout: watch battery drain live
tail -f omni_logs/nos3-eps.log

# Container CPU (TOTAL line only, noisy per-thread lines suppressed)
tail -f omni_logs/cpu_monitor.log | grep TOTAL_CPU_PCT

# Raw SB envelopes, filtered to HK_HK_TLM_MID (requires jq)
tail -f attack_logs/cfs_god_view.json | jq 'select(.msg_id=="0x089B")'

# Decoded HS housekeeping: CPU average and peak
tail -f omni_logs/tlm_hk_decoded.log | jq 'select(.app=="HS")'
```

### 12.2 Query Elasticsearch directly

```bash
# Every HK packet observed on the SB in the last 5 minutes
curl -s 'http://localhost:9200/nos3-telemetry-*/_search' \
  -H 'Content-Type: application/json' -d '{
  "query": {"bool": {"must": [
    {"term": {"type": "software_bus"}},
    {"wildcard": {"msg_name": "*_HK_TLM_MID"}},
    {"range": {"@timestamp": {"gte": "now-5m"}}}
  ]}},
  "size": 10
}'

# Decoded EPS battery voltage trend (latest 20 samples)
curl -s 'http://localhost:9200/nos3-telemetry-*/_search' \
  -H 'Content-Type: application/json' -d '{
  "query": {"bool": {"must": [
    {"term": {"type": "hk_decoded"}},
    {"term": {"app": "GENERIC_EPS"}}
  ]}},
  "_source": ["@timestamp", "battery_mv", "solar_array_mv"],
  "size": 20,
  "sort": [{"@timestamp": {"order": "desc"}}]
}'

# All CPU spikes
curl -s 'http://localhost:9200/nos3-telemetry-*/_search' \
  -H 'Content-Type: application/json' -d '{
  "query": {"term": {"tags": "cpu_spike"}},
  "size": 20,
  "sort": [{"@timestamp": {"order": "desc"}}]
}'

# Every dropped packet (any MID) in the active run
curl -s 'http://localhost:9200/nos3-telemetry-*/_search' \
  -H 'Content-Type: application/json' -d '{
  "query": {"term": {"sb_sequence_gap": true}},
  "size": 50,
  "sort": [{"@timestamp": {"order": "desc"}}]
}'
```

### 12.3 Kibana Discover filters

Paste these into the Discover search bar (`http://localhost:5601`):

- `type : "software_bus" and msg_direction : "CMD" and layer : "RESEARCH"` -
  attack-app command traffic (noisy_app, beacon_app, cpu_killer).
- `type : "system_log" and subsystem : "cfs_evs" and tags : "sb_pipe_overflow"` -
  the noisy_app footprint on the SB.
- `type : "hk_decoded" and cpu_peak_pct > 75` - HS reporting CPU pressure.
- `type : "software_bus" and sb_sequence_gap : true` - any dropped packet.
- `type : "hk_decoded" and app : "EPS" and battery_mv < 7000` - EPS
  watchpoint WP0/WP1 thresholds in Volts-milli.
- `tags : "cs_mm_combined_attack"` - integrity monitor disabled within the
  same run as a memory write (the combined attack signature).

### 12.4 Restart just the ingest path (without rebuilding FSW)

```bash
# Picks up logstash.conf and the mid_to_*.yml without restarting the sim.
docker restart nos3-logstash

# Clear indices, let Logstash re-ingest from the top of each file.
curl -X DELETE 'http://localhost:9200/nos3-telemetry-*'

# Verify the pipeline is actually reading files.
docker logs -f --tail=50 nos3-logstash
```

### 12.5 Known gotchas

- `tlm_hk_decoded.log` is matched by **both** `/omni_logs/*.log` (Layer 2
  `system_log`) and `/omni_logs/tlm_hk_decoded.log` (Layer 3 `hk_decoded`).
  Each line is therefore indexed twice under different `type` values. This
  is by design: Layer 2 keeps the raw JSON in the `message` field for text
  search, Layer 3 exposes parsed fields for trending. Always filter by
  `type`.
- `cfs_god_view.json` is **not** truncated by `make launch`; `make stop` /
  `scripts/stop.sh` deletes it. If you want a clean capture, stop the sim
  first.
- The `mid_to_*.yml` dictionaries are hot-reloaded every 60 s by Logstash.
  Running `make config` is enough for name lookups; no Logstash restart
  needed for new MIDs.
- If the three YAMLs are missing or empty (fresh checkout without
  `make config`), every SB document indexes with `msg_name=UNKNOWN_MSG`,
  `layer=UNKNOWN`, `subsystem=UNKNOWN`. Run `make config`.
- `god_view_capture.py` uses an advisory file lock at
  `/tmp/nos3_god_view.lock`. If a previous run's script was killed with
  `SIGKILL` the lock file may linger; delete it before `make launch`.
- `@timestamp` in Kibana comes from **ingest time** for `software_bus` and
  `system_log` documents, and from the packet's `timestamp` field for
  `hk_decoded`. If you need to correlate Layer 1 and Layer 3 by FSW time,
  match on `msg_id` + `sequence` rather than `@timestamp`.
