# 04. generic_adcs - Attitude Determination and Control

This document describes the `generic_adcs` cFS app: the
attitude-control state machine that consumes IMU, MAG, CSS, FSS,
and GNSS telemetry, runs the ADAC (Attitude Determination And
Control) algorithm, and emits reaction-wheel torque commands.
ADCS itself has no hardware simulator; it is software-only and
sits at the centre of the sensor-actuator loop.

The ADCS-cluster family theme runs across this document and the
five sibling deep-dives ([`reaction_wheel.md`](reaction_wheel.md),
[`imu.md`](imu.md), [`mag.md`](mag.md), [`css.md`](css.md),
[`torquer.md`](torquer.md)): the first 42 frame after sim startup
arrives with empty value strings for several keys, and the
upstream `std::stof / std::stod` calls in the data-point parsers
throw on the empty string. The DTU fix added an
`if (... .empty()) return;` early-exit in the affected parsers
(commit `962b61e7` for IMU/MAG/RW, an earlier commit for CSS).

## 1. Real-world hardware

A spacecraft ADCS subsystem combines:

- A computational core (this app).
- Inertial sensors (IMU).
- Magnetic-field reference (magnetometer, MAG).
- Sun-vector references (Coarse Sun Sensors, CSS, and Fine Sun
  Sensors, FSS).
- Position/time reference (GNSS).
- Actuators (reaction wheels, RW; magnetic torquers).

The job is to estimate the attitude (often as a quaternion plus
angular rates) and drive the actuators to match a commanded
attitude.

## 2. App overview

The flight-software half lives at
`nos3/components/generic_adcs/fsw/`:

- `cfs/src/generic_adcs_app.c`: cFS app entry point.
- `cfs/platform_inc/generic_adcs_msgids.h`,
  `generic_adcs_msgid_values.h`,
  `generic_adcs_topicids.h`: MID definitions.
- `shared/generic_adcs_adac.h` / `.c`: control modes, ADAC
  algorithm core.
- `shared/generic_adcs_utilities.h` / `.c`: math helpers.

MIDs:

- Topic ID `0x40`.
- Command MID: `0x1940`.
- HK telemetry MID: `0x0940`.

Command codes include set-mode (control mode selection),
momentum-management on/off, and parameter-table updates. The
mode set, mirrored in `mgr_app.c` for the manager app's lookup, is:

- `MGR_ADCS_PASSIVE = 0`: no torque commanded.
- `MGR_ADCS_BDOT = 1`: B-dot detumble. Uses MAG only.
- `MGR_ADCS_SUNSAFE = 2`: sun-pointing. Uses CSS / FSS plus IMU.
- `MGR_ADCS_INERTIAL = 3`: inertial pointing. Uses MAG, CSS, IMU, and the GNSS reference for ephemeris.

Post-thesis (2026-05-03) commit `b8da042d` made ADCS sub-modes follow MGR's spacecraft-mode automatically: BOOT enables sensors, SAFE drives BDOT, NOMINAL drives SUNSAFE, SCIENCE drives INERTIAL. The thesis baseline had MGR computing the ADCS mode and dispatching the cmd; the post-thesis change wires the cascade in MGR itself so the ADCS sub-mode is a derived field of `spacecraft_mode` rather than a separately-commanded value. See [`mgr.md#5a-mode-driven-sensor-and-adcs-cascade-2026-05-03`](mgr.md#5a-mode-driven-sensor-and-adcs-cascade-2026-05-03).

Commit `8af2fdea` added the ADCS sensor wakeup slots to the SCH SDT (CSS / FSS / IMU / MAG / NAV) and a FOV-exit recovery RTS that fires when the science target leaves the field. AP4 in `lc_def_adt.c` was retargeted to fire the recovery RTS instead of the older HS->SC integration that the thesis baseline left as a gap.

## 3. The control loop

ADCS subscribes to the wakeup tick. Each tick:

1. Collect the latest IMU, MAG, CSS, FSS, and GNSS HK packets
   from the SB. Extract per-sensor body-frame and
   reference-frame vectors.
2. Run the ADAC core (`generic_adcs_adac.c`) for the active
   control mode. The output is a torque command vector and an
   internal estimated attitude state.
3. Quantise the torque vector and publish reaction-wheel
   commands and torquer commands.
4. Publish the ADCS HK packet on `0x0940`.

## 4. Headline function (this family)

The headline routine is `MGR_EnableAllSensors` in
`mgr_app.c:217`. It runs once at MGR initialisation and broadcasts
`SENSOR_ENABLE_CC` (cmd code `2`) to every sensor MID:

- `0x1910` (CSS), `0x1920` (FSS), `0x1925` (IMU), `0x192A`
  (MAG), `0x193A` (TORQUER), `0x1970` (NOVATEL/GNSS).

Without this broadcast, the sensor apps stay disabled, ADCS DI
ingest sees zero-rate and zero-attitude every cycle, and the
reaction-wheel commands never lift above the int16 quantisation
threshold. ADCS is therefore the load-bearing consumer of MGR's
broadcast.

## 5. ELK extraction

The Logstash filter at `nos3/elk/logstash.conf` extracts ADCS
fields from the ADCS HK packet on the SB capture:

- `adcs_mode`, `adcs_quaternion_q[0..3]`,
  `adcs_body_rate_x/y/z`, `adcs_torque_cmd_x/y/z`.

Plus EVS lines from the ADAC algorithm noting mode entries and
exits.

Kibana panels:

- "ADCS Mode Timeline" on `nos3-std-telemetry-overview`.
- "Body Rates and Torque Commands" on
  `nos3-std-mission-validation`.

## 6. Attack-surface relevance

- **Sensor supply (component bus)**: a tampered IMU or MAG
  simulator can publish biased rates; ADCS estimates drift
  silently.
- **MGR broadcast**: an attacker who suppresses
  `MGR_EnableAllSensors` flat-lines the loop without obvious
  symptoms in EPS or GPS.
- **Parameter-table tampering**: an attacker with TBL service
  access can swap the ADAC gains.

## 7. DTU divergence vs upstream

- Commit `2976dc74` ("ADCS resilience"): adds the LC-throttling
  pattern that suppresses the LC startup-transient float-NaN
  watchpoint events from the ADCS HK fields. This is what
  cleared a recurring ERROR sweep in Kibana.
- The `MGR_EnableAllSensors` integration is a DTU addition;
  upstream NOS3's ADCS expected operator-driven enable.

## 8. Verification

- ADCS HK published at 1 Hz on the SB capture.
- After `MGR_AppInit`, the IMU, MAG, CSS, and FSS HK packets
  appear within a few seconds; the "HK rate per component" panel
  on `nos3-std-mission-validation` shows non-zero rates.
- Mode commanded from COSMOS lands in the ADCS HK
  `adcs_mode` field one tick later.
