# NOS3-DTU-EO1 Mission Implementation Guide

This document is the authoritative deep technical description of how the NOS3-DTU-EO1 simulated Earth-observation CubeSat mission is currently running. It covers the full chain from orbit propagation in the 42 dynamics simulator, through the cFS Software Bus, out to Logstash and the Kibana dashboard. It assumes no prior knowledge of NOS3, cFS, or 42; every abbreviation used in the doc is defined in the Glossary, and every concept used has either a definition or a citation to the file that implements it.

For a packet-level reference of every MID byte layout and the FSW function bodies behind the pipeline, see the companion document [`../telemetry-and-fsw-reference.md`](../telemetry-and-fsw-reference.md). All other mission-related markdown files in this repository point back here as the canonical source.

---

## Reading Guide

The doc is layered in three abstraction levels so the reader can stop at whatever depth is useful:

- **Level 1: Mission in One Page.** What this mission does, the two operational modes, the autonomous Denmark-overfly trigger, and how telemetry reaches the dashboard. One screen of text. Useful for stakeholders, new team members on day one, and anyone who needs a single sentence answer.
- **Level 2: Subsystem Architecture.** The Docker container topology, process and network boundaries, the time architecture, the orbit and position chain, the Software Bus message flow, the IPC port map, and the ground-software path. Useful for engineers who need to understand how the parts fit but do not need to read source code yet.
- **Level 3: Function-Call Detail.** Verbatim file paths, line numbers, function signatures, and argument-by-argument explanations. Includes the position-determination chain (the user's specific question: "how does it know where it is, and why does it start before Denmark?") and per-component walkthroughs. Useful for developers who are about to modify the code.

Sections are tagged `[L1]`, `[L2]`, `[L3]` so you can skim by depth.

---

## Glossary of Abbreviations

This project uses many acronyms from the NASA cFS ecosystem. Read this once; the rest of the document assumes you know these.

| Abbreviation | Stands For | What It Does |
|---|---|---|
| **NOS3** | NASA Operational Simulator for Space Systems | The open-source spacecraft simulation framework this project extends |
| **cFS** | core Flight System | NASA's reusable flight software framework; runs inside the simulated spacecraft |
| **cFE** | core Flight Executive | The operating system layer inside cFS: ES, EVS, SB, TBL, TIME |
| **ES** | Executive Services | cFE service that starts/stops apps, manages the CPU task list |
| **EVS** | Event Services | cFE service that routes app log messages (events) to ground and to Logstash |
| **SB** | Software Bus | cFE publish-subscribe message bus; all inter-app communication goes through this |
| **TBL** | Table Services | cFE service that manages configuration tables loaded from files |
| **TIME** | Time Services | cFE service that maintains spacecraft clock, synced to NOS Engine |
| **CCSDS** | Consultative Committee for Space Data Systems | The packet standard used for all SB messages; every packet has a primary header with a MID, sequence counter, and length |
| **MID** | Message ID | A 16-bit CCSDS stream identifier. `0x1xxx` = command, `0x0xxx` = telemetry |
| **CC** | Command Code | A 8-bit secondary-header field that distinguishes which command within an app (e.g. `MGR_SET_MODE_CC`) |
| **HK** | Housekeeping | Periodic telemetry packet an app sends containing its status and health counters |
| **TLM** | Telemetry | Any packet flowing from the spacecraft to the ground (MID prefix `0x0xxx`) |
| **CMD** | Command | Any packet flowing from the ground to the spacecraft (MID prefix `0x1xxx`) |
| **SCH** | Scheduler app | cFS app that fires commands on a time-table at 1 Hz (1 major frame per second) |
| **HK app** | Housekeeping Aggregator app | cFS app that copies fields from many HK packets into a single combined packet |
| **LC** | Limit Checker app | cFS app that monitors telemetry values against thresholds and triggers responses |
| **WP** | Watchpoint | One LC threshold rule: which packet, which field, comparison operator, threshold value |
| **AP** | Actionpoint | One LC response rule: which WP(s) trigger it, what RTS to start, how many failures before acting |
| **RPN** | Reverse Polish Notation | The expression format used by LC to combine multiple watchpoints (e.g. `WP_0 AND WP_1`) |
| **SC** | Stored Command app | cFS app that executes pre-loaded time-sequenced command scripts |
| **RTS** | Real-Time Sequence | One script stored in SC; a numbered list of commands with relative timing |
| **ATS** | Absolute Time Sequence | A longer SC script triggered by an absolute time (not used in EO1) |
| **DS** | Data Storage app | cFS app that records telemetry packets to files for later downlink |
| **CF** | CFDP app | cFS app that transfers files to the ground using the CCSDS File Delivery Protocol |
| **FM** | File Manager app | cFS app for filesystem operations (create/delete/rename files and directories) |
| **MM** | Memory Manager app | cFS app that allows direct memory read/write by ground command |
| **MD** | Memory Dwell app | cFS app that periodically samples configurable memory addresses |
| **CS** | Checksum app | cFS app that continuously checksums memory regions and code segments |
| **HS** | Health & Safety app | cFS app that monitors other apps (heartbeat, CPU load) and restarts them if they hang |
| **MGR** | Mission Manager app | DTU custom app that tracks spacecraft mode (SAFE / SCIENCE) and enforces mode-based policies |
| **EPS** | Electrical Power System | Hardware component; manages solar panels, battery, and power buses |
| **GPS** | Global Positioning System | Hardware component; provides position, velocity, and time from GNSS |
| **RW** | Reaction Wheel | Hardware component; stores angular momentum to control spacecraft attitude |
| **ADCS** | Attitude Determination and Control System | Hardware component; fuses sensor data and drives actuators to point the spacecraft |
| **IMU** | Inertial Measurement Unit | Sensor; measures linear acceleration and angular rate |
| **MAG** | Magnetometer | Sensor; measures the local magnetic field vector |
| **CSS** | Coarse Sun Sensor | Sensor; detects sun direction coarsely (analog current ratio) |
| **FSS** | Fine Sun Sensor | Sensor; detects sun direction precisely (alpha/beta angles) |
| **ELK** | Elasticsearch + Logstash + Kibana | The telemetry analysis pipeline added by DTU |
| **Logstash** | (product name) | Log ingestion and transformation service; parses raw logs into structured documents |
| **Elasticsearch** | (product name) | Full-text search and time-series database; stores parsed telemetry documents |
| **Kibana** | (product name) | Web dashboard for visualizing Elasticsearch data; runs at `http://localhost:5601` |
| **KQL** | Kibana Query Language | Filter syntax used in Kibana search bars and dashboard panels |
| **grok** | (Logstash plugin) | Pattern-matching filter that extracts named fields from unstructured text |
| **SoC** | State of Charge | Battery charge level expressed as a percentage of full capacity |
| **LEO** | Low Earth Orbit | Orbit below ~2000 km altitude; the EO1 mission target orbit |
| **FOV** | Field of View | Angular range in which a sensor can make measurements |
| **HWLIB** | Hardware Library | NOS3 abstraction layer that lets the same FSW app talk to either a real device or a NOS Engine simulator |
| **NOS Engine** | NASA Operational Simulator Engine | The hardware bus simulation layer (I2C, SPI, CAN, UART buses) inside NOS3 |
| **42** | (proper noun, NASA Goddard simulator) | NASA Goddard's open-source spacecraft dynamics simulator. Propagates orbit and attitude. Container/hostname is `fortytwo`. The number is a Hitchhiker's Guide reference, not an acronym. |
| **STF1** | Simulation-To-Flight 1 | The default NOS3 mission scenario; a CubeSat reference design provided by NASA IV&V. EO1 builds on top of STF1. |
| **EO1** | Earth Observation 1 | DTU's mission name; a single-spacecraft Earth observation CubeSat in LEO targeting Denmark. |
| **GSFC** | Goddard Space Flight Center | The NASA centre that develops 42 and many of the cFS apps used here. |
| **GMSEC** | Goddard Mission Services Evolution Centre | A messaging middleware used by 42's IPC; in EO1 we use plain TCP sockets, not GMSEC. |
| **OSAL** | OS Abstraction Layer | The portable OS layer cFS sits on top of (file I/O, threads, mutexes). |
| **EDS** | Electronic Data Sheets | A schema language cFS Draco uses to declare command/telemetry structures and topic IDs. |
| **CCSDS** | (already defined above) | Header layout: 6-byte primary header (Stream ID + sequence + length) + optional secondary header. |
| **TLE** | Two-Line Element | A standard text encoding of an orbit (NORAD format). Not used in EO1 (we use Keplerian elements directly). |
| **TRV** | Time-Range-Vector | A 42-specific ephemeris file format. Referenced in `Orb_LEO.txt` but unused because we run in `KEP` mode. |
| **KEP** | Keplerian | One of 42's orbit-input modes. Specifies the orbit by 6 classical orbital elements rather than state vector. |
| **PA** / **AE** | Periapsis-Apoapsis / Altitude-Eccentricity | Two ways to declare a Keplerian orbit's size and shape. EO1 uses **PA** (Periapsis Altitude 400 km, Apoapsis Altitude 400 km, hence circular). |
| **RAAN** | Right Ascension of the Ascending Node | The longitude in the inertial frame where the orbit crosses the equator going north. One of the 6 Keplerian elements. Determines where the ground track lies east-to-west. |
| **GMST** | Greenwich Mean Sidereal Time | Earth's rotation angle relative to the inertial frame. Combined with RAAN, sets the sub-satellite longitude at any given UTC time. |
| **J2** | (Earth oblateness term) | Second zonal harmonic of Earth's gravity field. Causes secular drift of RAAN over time. EO1 has J2 perturbation **disabled** in `Orb_LEO.txt`. |
| **J2000** | Julian Date 2000.0 | Reference epoch: 2000-01-01 12:00:00 Terrestrial Time. cFS measures time in seconds since this epoch. |
| **ECI** | Earth-Centered Inertial | A non-rotating reference frame fixed to the stars. 42's orbit propagator works in ECI. |
| **ECEF** | Earth-Centered Earth-Fixed | A frame that rotates with the Earth. Latitude/longitude are computed from ECEF coordinates. |
| **ECEF2LLA** | (function name) | Vallado Algorithm 12: convert ECEF (x, y, z) into geodetic Latitude, Longitude, ellipsoid Altitude. Implemented in `nos3/sims/sim_common/src/sim_coordinate_transformations.cpp`. |
| **WGS-84** | World Geodetic System 1984 | The reference ellipsoid used for "latitude" and "longitude". Equatorial radius 6378.137 km, eccentricity-squared 0.00669437999. |
| **NMEA** | National Marine Electronics Association | The text serial protocol GPS receivers commonly emit. |
| **GPGGA** | (NMEA sentence type) | "Global Positioning System Fix Data" sentence: time, lat, lon, fix quality, altitude. |
| **BESTXYZA** | (NovAtel proprietary sentence) | NovAtel OEM615 best-position sentence, ECEF format. |
| **IGRF** | International Geomagnetic Reference Field | Earth magnetic field model used by 42; configured in `Inp_Sim.txt` (degree/order 8/8). |
| **F10.7** | (solar flux index) | 10.7 cm radio flux from the Sun, used as an atmospheric density proxy. Set USER=230.0 in `Inp_Sim.txt`. |
| **Ap** | (geomagnetic activity index) | Geomagnetic disturbance proxy. Set USER=100.0 in `Inp_Sim.txt`. |
| **IPC** | Inter-Process Communication | The TCP-socket layer 42 uses to broadcast spacecraft state to the NOS3 simulators. |
| **CFDP** | CCSDS File Delivery Protocol | The file-transfer protocol the CF cFS app implements. |
| **SBN** | Software Bus Network | A cFS app that lets multiple cFS instances exchange SB messages. **Disabled** in EO1 (`<sbn><enable>false</enable></sbn>`). |
| **ATS** | Absolute Time Sequence | An SC sequence triggered by absolute time (vs. RTS = Real-Time Sequence triggered by relative time). Not used in EO1. |
| **CDS** | Critical Data Store | cFS persistence area surviving warm reboots. Configured at 128 KB. |
| **FOV** | Field of View | Angular coverage of a sensor (e.g. star tracker, sun sensor). |
| **SoC** | State of Charge | Battery charge expressed as a percentage of full capacity. |
| **OIPP** | Orbit / In-view / Power Predictor | A separate ground tool under `nos3/gsw/OrbitInviewPowerPrediction/`. Not on the runtime mission path. |
| **ST** | Star Tracker | Precision attitude sensor. In EO1 the ST FSW app is loaded and the IPC port 4282 is set to `OFF` so the simulator emits synthetic data. See the IPC Port Map below. |
| **OnAir** | OnAir component | A NOS3 add-on that simulates over-the-air radio links. |
| **MGR** | Mission Manager | DTU custom app that owns the SAFE/SCIENCE mode state and validates mode-change commands. |
| **ELK** | Elasticsearch + Logstash + Kibana | (already defined; reiterated for completeness) |
| **fortytwo** | (Docker hostname) | Container name for the 42 simulator process. NOS3 simulators connect to ports on this host. |
| **DTU** | Danmarks Tekniske Universitet (Technical University of Denmark) | The institution maintaining this fork. |
| **GNSS GS Responder** | (DTU custom) | Cosmos-side Ruby task at `nos3/gsw/cosmos/COMPONENTS/GENERIC_GNSS/lib/gnss_gs_responder_task.rb`. Polls GNSS HK, dispatches `BEACON_PING_CC` and waits up to `ECHO_TIMEOUT_S` for the round-trip ack. Closes the GS Hello-Flow loop. |
| **Hello-Flow** | (informal name) | The closed-loop liveness check: GS responder -> ci_lab -> beacon_app -> to_lab -> mirror socket -> COSMOS DEBUG listener -> responder. Each pass advances `ping_count` and `last_ping_seq` in the dashboard. |
| **Mirror Socket** | (DTU 2026-05-03) | Host-side UDP duplication path inside `nos3/scripts/god_view_capture.py`, controlled by `NOS3_TLM_MIRROR_DEST`. Solves the COSMOS netns isolation problem: COSMOS's DEBUG listener on UDP 5013 lives in its own netns; without the mirror, downlink packets only reach the host's god_view, never COSMOS, so the responder ACK never fires. |
| **CmdTlmServerHeadless** | (DTU 2026-05-03) | Pure-Ruby Cosmos CmdTlmServer wrapper at `nos3/gsw/cosmos/tools/CmdTlmServerHeadless`. Replaces `xvfb-run ruby CmdTlmServer` because the apt-snapshot used by the build container could not install xvfb. Launched by `ci_launch.sh` with `-p 5113:5013/udp` port map. |
| **ECHO_TIMEOUT_S** | (constant) | The GS responder's ping-wait deadline. Tuned to 5.0 s on 2026-05-03 (commit `8a45426f`); pinned above the 90th-percentile observed echo latency (~2.93 s) to absorb LEO jitter and re-poll cycles. |
| **Freshness Tile** | (Kibana panel) | Per-component "last seen" heatmap row added on 2026-05-03 (commit `7afa0107`). A tile flips red if its component HK has not arrived within the panel's 30 s window; spacecraft-mode chip context shows the active mode at the same moment. |
| **AND-gate Panel** | (Kibana panel) | GNSS GS validation row that requires `last_ping_seq` advancing AND `ping_count > 0` AND `in_denmark_box` to be true simultaneously before reporting healthy. Added 2026-05-03 (commit `bc6369c8`). |
| **in_denmark_box** | (computed boolean) | Logstash-emitted flag: true when `gnss_lat in [55, 58]` AND `gnss_lon in [8, 16]`. Powers the AND-gate panel and the geofence dashboards. |
| **in_science_mode** | (computed boolean) | Logstash-emitted flag derived from MGR HK: true when `spacecraft_mode == SCIENCE`. |

---

## Level 1: Mission in One Page  `[L1]`

**What is being simulated.** A single small spacecraft (CubeSat-class) in a circular Low Earth Orbit at 400 km altitude. The orbit is inclined 60 degrees, which gives a ground track reaching the latitude of Denmark (55-58 deg North).

**What the mission does.** The spacecraft flies with two operational modes:

1. `SAFE` -- the default. Sun-pointing attitude, charge battery, listen for ground commands.
2. `SCIENCE` -- entered automatically when the spacecraft's GPS position falls inside the Denmark bounding box (latitude 55-58 deg N AND longitude 8-16 deg E). Nadir-pointing attitude, sensors active, telemetry recorded for later downlink.

**How the mode change happens.** The cFS LC (Limit Checker) app continuously watches the GPS-app telemetry packet. Four watchpoints (WP6, WP7, WP8, WP9) form the Denmark bounding box. When all four conditions are met simultaneously, LC actionpoint AP3 fires, which tells the SC (Stored Command) app to run the pre-loaded sequence RTS 002, which sends `MGR_SET_MODE_CC` with payload `MGR_SCIENCE_MODE` to the MGR (Mission Manager) app, which flips the spacecraft state.

**Where the position comes from.** Position is **not** computed by any DTU-written module. The 42 dynamics simulator (a NASA Goddard tool) propagates the orbit from a set of 6 Keplerian elements declared in `nos3/cfg/InOut/Orb_LEO.txt`, then broadcasts the result over a TCP socket on port 4245. The stock NOS3 GPS simulator (`libgps_sim.so`, NovAtel OEM615 model) consumes that socket, packages the position into a `NOVATEL_OEM615_DEVICE_TLM` Software Bus packet, and the cFS GPS application republishes it. The only DTU customisation in this whole position chain is three numbers (Inclination, RAAN, True Anomaly) tuned in `Orb_LEO.txt` so the ground track passes over Denmark in the first ~80-140 seconds of the run, then again every ~90 minutes (one orbit).

**Where telemetry ends up.** Two parallel logging streams feed the Kibana dashboard:

1. The Software Bus stream: `god_view_capture.py` enables TO_LAB output and writes every CCSDS packet to `attack_logs/cfs_god_view.json`. Logstash parses it, decorates each packet with a human-readable name and subsystem tag, and indexes it into Elasticsearch.
2. The simulator log stream: every NOS3 simulator container writes a plain-text log into `omni_logs/<name>.log`. Logstash extracts numeric fields with `grok` patterns (e.g. battery voltage, GPS lat/lon, reaction-wheel momentum), converts radians to degrees for the GPS coordinates, and indexes those too.

The Kibana dashboard at `http://localhost:5601` then uses the KQL filter `gps_lat >= 55 and gps_lat <= 58 and gps_lon >= 8 and gps_lon <= 16` to highlight the Denmark passes.

**Closed-loop liveness check.** Beyond raw telemetry, the canonical "is everything working?" probe is the GS Hello-Flow loop added on 2026-05-03. The Cosmos-side `gnss_gs_responder_task.rb` polls GNSS HK, dispatches `BEACON_PING_CC` to the spacecraft, and waits up to `ECHO_TIMEOUT_S = 5.0` s for the round-trip. Each closed pass advances `ping_count` and `last_ping_seq` in the dashboard's GNSS-GS-validation AND-gate panel, which fires green only when (a) the seq is advancing, (b) the count is non-zero, and (c) the spacecraft is in the Denmark bounding box. The mirror-socket plumbing in `god_view_capture.py` is the load-bearing piece that lets COSMOS see the downlink at all (the COSMOS DEBUG listener sits inside its own netns; without `NOS3_TLM_MIRROR_DEST`, the responder never sees an ack).

**One-line summary.** Stock NOS3 + 42 + cFS, with three orbital elements tuned by DTU so the ground track crosses Denmark, plus a DTU-built ELK pipeline that visualises the result and a closed-loop GS Hello-Flow that validates round-trip command-and-telemetry health.

---

## Mission Overview

**NOS3-DTU-EO1** is a low-Earth-orbit Earth observation CubeSat simulation. The mission adds a coherent operational concept on top of the stock NOS3 cFS stack:

- Two spacecraft modes (SAFE and SCIENCE)
- Autonomous health monitoring via the LC and HS apps
- Stored-command sequences (RTS scripts) for autonomous responses
- Full telemetry routing through the ELK stack into Kibana

### Operational Modes

| Mode | Purpose | Entry Trigger |
|------|---------|---------------|
| **SAFE** | Maintain sun-pointing attitude, charge battery, await contact window | Power-on default; LC BATTERY_LOW actionpoint; manual ground command |
| **SCIENCE** | Nadir-point via ADCS, activate sensors, store data via DS, downlink via CF | LC AP3 SCIENCE_OVERFLY (autonomous, fires when GPS lat/lon enters the Denmark box); or ground command -- both execute RTS 002 |

The MGR app is the gatekeeper for mode changes. It receives `MGR_SET_MODE_CC` commands (either from ground or from SC-executed RTS scripts) and enforces which hardware components are active in each mode.

### Autonomous Mission Triggers (as implemented)

| Watchpoint (WP) | Condition Monitored | Actionpoint (AP) | Response | RTS |
|---|---|---|---|---|
| WP0 BATTERY_LOW | EPS battery voltage < 14800 mV (~20% SoC) | AP0 SAFE_ON_LOW_BAT | Enter SAFE mode | RTS 001 |
| WP1 BATTERY_OVERCHARGE | EPS battery voltage > 22200 mV (overcharge) | (alert only, no RTS) | EVS CRITICAL event | -- |
| WP2 CPU_HIGH | HS CPU utilization average > 75% | AP1 CPU_SPIKE_ALERT | EVS CRITICAL event | -- |
| WP3 ORBIT_LOW | GPS altitude < 400,000 m (400 km, deorbit risk) | (alert only, no RTS) | EVS CRITICAL event | -- |
| WP4 RW_OFFLINE | RW DeviceEnabled == 0 (wheel dropped offline) | AP2 RW_OFFLINE | Desaturation sequence | RTS 003 |
| WP5 SOLAR_LOW | EPS solar array voltage < 100 mV (array off/shadowed) | (alert only, no RTS) | EVS CRITICAL event | -- |
| WP6 LAT_SOUTH_BOUND | GPS latitude > 55.0° (north of Denmark's southern edge) | AP3 SCIENCE_OVERFLY (combined with WP7, WP8, WP9) | Enter SCIENCE mode | RTS 002 |
| WP7 LAT_NORTH_BOUND | GPS latitude < 58.0° (south of Denmark's northern edge) | AP3 SCIENCE_OVERFLY | Enter SCIENCE mode | RTS 002 |
| WP8 LON_WEST_BOUND | GPS longitude > 8.0° (east of Denmark's western edge) | AP3 SCIENCE_OVERFLY | Enter SCIENCE mode | RTS 002 |
| WP9 LON_EAST_BOUND | GPS longitude < 16.0° (west of Denmark's eastern edge) | AP3 SCIENCE_OVERFLY | Enter SCIENCE mode | RTS 002 |

WP6–WP9 are info-only individually -- no AP fires on any single one. AP3 requires **all four** to fail simultaneously (i.e. the spacecraft is inside the lat 55–58°N, lon 8–16°E box). Watched fields: `NOVATEL_OEM615_DEVICE_TLM_MSG` offset 74 (`lat`, float LE) and offset 78 (`lon`, float LE).

---

## Level 2: Subsystem Architecture  `[L2]`

This section describes the runtime topology: which Docker containers exist, which networks they share, which sockets cross which boundary, and how time is synchronised. Source-of-truth files are cited inline.

### 2.1 Active mission configuration

The single top-level mission selector is `nos3/cfg/nos3-mission.xml`:

```xml
<nos3-mission-cfg>
    <start-time>830217600.0</start-time>            <!-- J2000 seconds; 23 Apr 2026 -->
    <gsw>cosmos</gsw>                                <!-- Ground Software stack -->
    <fsw>cfs</fsw>                                   <!-- Flight Software stack -->
    <scenario>STF1</scenario>                        <!-- 42 scenario template -->
    <number-spacecraft>1</number-spacecraft>
    <sc-1-cfg>spacecraft/sc-mission-config.xml</sc-1-cfg>
</nos3-mission-cfg>
```

The selected spacecraft profile `nos3/cfg/spacecraft/sc-mission-config.xml` enables the following:

- **cFS apps:** `cf` (CCSDS File Delivery Protocol), `ds` (Data Storage), `fm` (File Manager), `lc` (Limit Checker), `sc` (Stored Command). cFS heritage apps `ci_lab`, `to_lab`, `sch`, `hk`, `evs`, `tbl`, `time`, `es`, `sb` are always loaded.
- **Components (each contributes a sim and a FSW app):** `adcs`, `css`, `eps`, `fss`, `gps`, `imu`, `mag`, `mgr`, `onair`, `radio`, `rw`, `sample`, `st` (Star Tracker), `torquer`.
- **Disabled:** `cam` (arducam), `sbn` (Software Bus Network), `syn` (synthetic data generator), `thruster`.

### 2.2 Container topology

NOS3 uses Docker. Running `make launch` brings up one container per simulator/sub-component plus shared service containers. Two Docker networks exist:

- `nos3-core` -- shared services that any spacecraft instance can reach: NOS Engine time driver, Cosmos GSW container, ELK trio (Elasticsearch, Logstash, Kibana).
- `nos3_sc_1` -- private network for spacecraft #1: the 42 container (`fortytwo`), all 13 component simulators (`gps`, `eps`, `imu`, ...), the cFS process container (`cfs1`), the truth provider, and the various NOS Engine bus servers.

Key containers:

| Container | Image / process | Network | Role |
|---|---|---|---|
| `fortytwo` | NASA 42 simulator (compiled from upstream + DTU `Orb_LEO.txt`) | `nos3_sc_1` | Truth source: orbit and attitude propagation |
| `cfs1` | cFS-Draco built locally | `nos3_sc_1` | Flight software runtime; all cFS apps live here |
| Per-component sim containers | `lib<name>_sim.so` loaded by the NOS3 simulator framework | `nos3_sc_1` | Hardware models that talk between 42 and cFS over NOS Engine buses |
| `nos_time_driver` | NOS Engine time tick service | `nos3-core` | Drives cFS TIME service; defines simulation tick |
| `cosmos` | Ball Aerospace COSMOS | `nos3-core` | Ground software UI, command uplink, telemetry display |
| `nos3-elasticsearch`, `nos3-logstash`, `nos3-kibana` | Official ELK images | `nos3-core` | Telemetry indexing and dashboard |

### 2.3 Time architecture

There are **two distinct time references** that must not be confused:

- **cFS / mission time:** controlled by `<start-time>830217600.0</start-time>` in `nos3-mission.xml`. This is seconds since J2000, which corresponds to 23 April 2026 00:00:00 UTC. cFS TIME service starts here and is incremented by the NOS Engine tick (1 Hz major frame; 100 Hz minor frame).
- **42 internal epoch:** declared in `nos3/cfg/InOut/Inp_Sim.txt` line 16-18:
  - Date: `10 20 2025` (20 October 2025)
  - Time: `17 43 20.00` UTC
  - Leap Seconds: `37.0`

These two epochs are **not the same**. The 42 internal epoch is what 42 uses to seed its environmental models (Earth's rotation, Sun position, magnetic field, atmospheric density). The cFS start-time is what flight software believes about the calendar. The discrepancy is intentional and harmless because:

1. The 42 orbit propagation is purely Newtonian (orbit elements + epoch -> ECI state at any t); the actual UTC date does not affect the relative ground track shape.
2. cFS apps that care about absolute time (DS file naming, CFDP timestamps) will use the 830217600.0 start; 42's environmental data uses 2025-10-20.
3. Earth rotation (GMST) couples the 42 epoch into the sub-satellite longitude. The DTU RAAN value of 346 deg was tuned with the 2025-10-20 epoch active, so changing the 42 date will move the ground track east or west by GMST-difference degrees. Keep the 42 epoch unless you are willing to re-tune RAAN.

### 2.4 Cross-boundary data movement

Five distinct movement mechanisms connect the running processes:

1. **42 -> simulator (TCP):** 42 broadcasts spacecraft state (`SC[0].*`, `Orb[0].*`) over per-component TCP sockets. Configured by `nos3/cfg/InOut/Inp_IPC.txt`. See section "IPC Port Map" below.
2. **Simulator -> FSW (NOS Engine bus):** Each component simulator presents itself as an I2C, SPI, CAN, or UART device on a NOS Engine bus. The cFS HWLIB layer issues bus reads/writes that look identical to a real device.
3. **FSW <-> FSW (Software Bus):** All cFS-internal communication is publish/subscribe over the SB. Each packet carries a 16-bit MID; apps subscribe to the MIDs they want.
4. **FSW -> Cosmos / god_view (UDP):** TO_LAB sends every SB packet to UDP port 5013 once enabled.
5. **All processes -> Logstash (file tail):** Every NOS3 simulator writes a plain-text log into `omni_logs/<name>.log`. cFS EVS writes events into the same stream. Logstash uses the `file` input plugin to tail these.

#### 2.4.1 Telemetry Mirror Path (added 2026-05-03)

The fifth mechanism above (FSW -> COSMOS via UDP) hides a netns subtlety that took most of one debug session to track down. COSMOS's `CmdTlmServer` listens on UDP 5013 *inside its own container's netns*. The default `god_view_capture.py` only sent each downlink packet to the host's god_view receiver, so COSMOS saw nothing on its DEBUG listener and the GS Hello-Flow never closed.

The fix lives in two coordinated changes:

1. **`nos3/scripts/god_view_capture.py`** added a second send call: after writing the packet to god_view, it duplicates the bytes to the address in the `NOS3_TLM_MIRROR_DEST` env var. `ci_launch.sh` sets that to the COSMOS container's mapped UDP port.
2. **`nos3/gsw/cosmos/tools/CmdTlmServerHeadless`** replaced the `xvfb-run ruby CmdTlmServer` invocation. The apt-snapshot used by the cosmos image could not install xvfb, so CmdTlmServer never started; the headless wrapper bypasses Qt/Xvfb entirely and runs the same Ruby logic. `ci_launch.sh` launches cosmos with `-p 5113:5013/udp` so the host can reach the listener and the mirror packets land on the right netns endpoint.

Without both halves, the GS responder loop times out every cycle (`ECHO_TIMEOUT_S = 5.0 s`) and the AND-gate panel never goes green. See section 6.10 below for the file-by-file walk-through and `debug/journal.md` (2026-05-03 sessions) for the diagnosis.

---

## Orbit and Position Initialisation  `[L2]` and `[L3]`

This section answers the user's central question: how does the spacecraft "know" where it is, and why does it start west of Denmark before crossing it?

### 3.1 What the 42 simulator is

42 (always written as the digit-pair, never spelled out) is a NASA Goddard open-source spacecraft dynamics simulator. In NOS3 it is the **truth source**: it propagates a 6-degree-of-freedom rigid-body model of the spacecraft (orbit + attitude + actuators) and broadcasts the resulting state over TCP sockets so that every other simulator can compute its own measurement based on truth.

42 is configured by three plain-text files in `nos3/cfg/InOut/`:

- `Inp_Sim.txt` -- simulation control (time mode, duration, step size, Earth model, ground stations, environmental parameters).
- `Orb_LEO.txt` -- the reference orbit (Keplerian elements or state vector).
- `SC_NOS3.txt` -- the spacecraft itself (mass, inertia, sensors, actuators, geometry).

42 itself is **stock upstream code**. The DTU customisation lives entirely in these three text files.

### 3.2 The Keplerian elements that place the spacecraft near Denmark

This is the core answer. From `nos3/cfg/InOut/Orb_LEO.txt` lines 11-20 (verbatim, with DTU comments preserved as written by the DTU author of this fork):

```
EARTH                         !  Orbit Center
FALSE                         !  Secular Orbit Drift Due to J2
KEP                           !  Use Keplerian elements (KEP) or (RV) or FILE
PA                            !  Use Peri/Apoapsis (PA) or min alt/ecc (AE)
400.0      400.0              !  Periapsis & Apoapsis Altitude, km
400.0  2.0                    !  Min Altitude (km), Eccentricity   (ignored in PA mode)
60.0                          !  Inclination (deg) - DTU: bumped from 52 so ground track reaches Denmark (55-58 N)
346.0                         !  Right Ascension of Ascending Node (deg) - DTU: empirically tuned so the sub-sat point enters the Denmark bbox (55-58 N, 8-16 E) at t~1.3 min and exits at t~2.3 min (first-run RAAN of 277.9 with my book GMST was ~68 deg too far west)
0.0                           !  Argument of Periapsis (deg)
61.6                          !  True Anomaly (deg) - DTU: argument of latitude at epoch; combined with RAAN above, places sub-sat at lat~50 N lon~3 W pre-pass
```

Each parameter explained from first principles:

| Parameter | Meaning | Effect |
|---|---|---|
| **Orbit Center: EARTH** | The body the orbit revolves around. | Selects the gravity model and the rotating frame for ground-track computation. |
| **Secular Orbit Drift Due to J2: FALSE** | Whether 42 will let J2 (Earth's oblateness) slowly precess RAAN. | Disabled; RAAN stays fixed at 346 deg for the run. If enabled, the ground track would shift west over hours/days. |
| **KEP** | Use the 6 classical Keplerian orbital elements (vs. RV state vector or FILE-loaded ephemeris). | Selects the input format. |
| **PA** | Specify orbit shape by Periapsis Altitude and Apoapsis Altitude (vs. minimum altitude + eccentricity). | Selects which two numbers on the next line are read. |
| **Periapsis Altitude 400.0 km, Apoapsis Altitude 400.0 km** | Closest and farthest distance from Earth's surface. Equal -> circular orbit. | Semi-major axis = `Earth_radius (6378.137 km) + 400 km = 6778.137 km`. Eccentricity = 0. |
| **Inclination 60.0 deg** | Angle between the orbit plane and the equator. | The ground track reaches latitudes between -60 deg and +60 deg. Stock NOS3 used 52 deg; DTU bumped to 60 deg specifically to reach Denmark's 55-58 deg N. |
| **RAAN 346.0 deg** | The longitude (in the inertial ECI frame, measured eastward from the vernal equinox) where the orbit crosses the equator going north. | Combined with Earth's rotation (GMST at the 42 epoch), this picks where on Earth the ascending equator crossing happens. DTU tuned this empirically: the comment in the file notes that the first attempt at RAAN 277.9 deg placed the sub-satellite point about 68 deg too far west; 346 deg shifted it to enter Denmark airspace ~1.3 minutes after run start and exit ~2.3 minutes after. |
| **Argument of Periapsis 0.0 deg** | The angle within the orbit plane from the ascending node to the closest approach. | For a circular orbit (eccentricity ~ 0) this value is geometrically degenerate; 42 just uses it to define the angle from which True Anomaly is measured. |
| **True Anomaly 61.6 deg** | The angular position of the spacecraft along the orbit at the start epoch. | Combined with the inclination and RAAN, this places the spacecraft at approximately latitude +50 deg N, longitude -3 deg W (over the North Atlantic, west of Ireland) at t = 0. This is the "before Denmark" starting position the user asked about. |

Together: at t = 0 the spacecraft is at lat ~50 N, lon ~3 W; it travels north-east at orbital velocity (~7.7 km/s ground speed); it crosses the 55 N latitude line about 80 seconds later (entering Denmark airspace); it exits the 58 N line at about 140 seconds. One full orbit takes ~5550 s (about 92 minutes for a 400 km circular orbit), so the Denmark window repeats roughly every 92 minutes -- though shifted west each time by the amount Earth has rotated under the orbit (about 23 deg per orbit), so a given longitude on Earth is over-flown only every ~14-15 orbits (i.e. about once a day).

### 3.3 Stock NOS3 vs. DTU customisation

The position chain is **almost entirely stock NOS3**. The only DTU edits in the entire chain are three lines in `Orb_LEO.txt`:

| Element | Stock NOS3 | EO1 (DTU) | Reason for change |
|---|---|---|---|
| Inclination | 52 deg | 60 deg | Stock 52 deg never reached Denmark (ground track topped out at 52 deg N). |
| RAAN | (template default) | 346 deg | Empirically tuned so the sub-satellite longitude enters 8-16 deg E during the first orbit. |
| True Anomaly | (template default) | 61.6 deg | Pre-positions the spacecraft just west of Denmark at t=0 so the first overfly happens within ~2 minutes. |

Everything else (ECEF<->LLA conversion, the GPS hardware model, the 42 socket protocol, the Logstash radians-to-degrees conversion) is upstream NOS3 code unmodified.

### 3.4 End-to-end position-data path

```
              42 simulator (container "fortytwo")
                       |
                       | propagates orbit using Orb_LEO.txt elements
                       | every 0.01 s timestep (Inp_Sim.txt line 4)
                       |
                       v
          SC[0].GPS[0].Lat (rad), Lng (rad), Alt (m), PosW (km ECEF), VelW (km/s ECEF)
                       |
                       | broadcast on TCP fortytwo:4245   (Inp_IPC.txt entry 9, "GPS IPC")
                       v
       NOS3 GPS simulator -- libgps_sim.so, type OEM615 -- container reads socket
                       |
                       | 1.  GPSSimData42SocketProvider::get_data_point()
                       | 2.  GPSSimDataPoint::do_parsing()  parses the 42 frame
                       | 3.  gps_sim_hardware_model_OEM615.cpp builds GPGGA+BESTXYZA
                       |     (calls ECEF2LLA when it needs to recompute lat/lon
                       |      from the ECEF position; otherwise uses 42's lat/lon directly)
                       |
                       | over NOS Engine USART bus "usart_1" port 1 (nos3-simulator.xml)
                       v
       cFS Novatel app -- novatel_oem615_app.c, child task novatel_oem615_child.c
                       |
                       | parses NMEA into NOVATEL_OEM615_Device_tlm_t struct
                       | publishes on Software Bus
                       v
       MID NOVATEL_OEM615_DEVICE_TLM_MID  (CCSDS packet)
                       |
                       +--> LC subscribes; checks WP3 (alt < 400 km) and WP6-WP9 (Denmark box)
                       |
                       +--> HK app subscribes; copies fields into HK_COMBINED_PKT1_MID
                       |
                       +--> TO_LAB forwards to UDP:5013
                                |
                                v
                       god_view_capture.py  -> attack_logs/cfs_god_view.json
                                                  (one JSON line per packet)
                                                          |
                                                          v
                                            Logstash file input + grok + ruby radians->degrees
                                                          |
                                                          v
                                                  Elasticsearch index nos3-telemetry-*
                                                          |
                                                          v
                                              Kibana KQL: gps_lat 55..58 AND gps_lon 8..16
```

### 3.5 Function-call detail for the position chain  `[L3]`

Every step has an absolute path so a developer can jump to it.

**Step 1 -- 42 broadcasts state on port 4245.**  This is internal to the upstream 42 binary; we do not modify it. The configuration that selects the GPS-output port is `nos3/cfg/InOut/Inp_IPC.txt` block 9 (titled `GPS IPC`):

```
TX                                      ! IPC Mode
"State03.42"                            ! File name
SERVER                                  ! Socket Role (42 is the server)
fortytwo       4245                     ! Server Host Name, Port
FALSE                                   ! Allow Blocking
FALSE                                   ! Echo to stdout
1                                       ! Number of TX prefixes
"SC[0].GPS[0]"                          ! Prefix 0
```

42 publishes any line matching the prefix `SC[0].GPS[0]` (e.g. `SC[0].GPS[0].Lat 0.872`).

**Step 2 -- the NOS3 GPS simulator connects to that socket.**  Configured in `nos3/cfg/sims/nos3-simulator.xml` (the master simulator XML for the running scenario):

```xml
<simulator>
    <name>gps</name>
    <library>libgps_sim.so</library>
    <hardware-model>
        <type>OEM615</type>
        <connections>
            <connection><type>usart</type>
                <bus-name>usart_1</bus-name>
                <node-port>1</node-port>
            </connection>
        </connections>
        <data-provider>
            <type>GPS42SOCKET</type>
            <hostname>fortytwo</hostname>
            <port>4245</port>
            <max-connection-attempts>180</max-connection-attempts>     <!-- DTU: bumped from 30 -->
            <retry-wait-seconds>1</retry-wait-seconds>
            <spacecraft>0</spacecraft>
            <GPS>0</GPS>
            <leap-seconds>37</leap-seconds>
        </data-provider>
    </hardware-model>
</simulator>
```

A DTU comment in this file explains the 180-attempt retry budget:

> "DTU: boosted GPS 42-socket retry budget so the sim survives containers-launch race with 42. Default was 30x1s; 42 opens port 4245 lazily once the FSW side connects, and on a cold start that can take more than 30s, causing the GPS sim to give up and emit all-zero positions forever (breaks the Denmark geofence panel). 180 attempts = 3 min budget."

**Step 3 -- GPS sim provider reads from the socket.**  Source: `nos3/components/novatel_oem615/sim/src/gps_sim_data_42socket_provider.cpp`, lines 33-58.

```cpp
GPSSimData42SocketProvider::GPSSimData42SocketProvider(const boost::property_tree::ptree& config)
    : SimData42SocketProvider(config)
{
    connect_reader_thread_as_42_socket_client(
        config.get("simulator.hardware-model.data-provider.hostname", "localhost"),
        config.get("simulator.hardware-model.data-provider.port", 4242));    // overridden to 4245 by XML
    _sc           = config.get("simulator.hardware-model.data-provider.spacecraft", 0);
    _gps          = config.get("simulator.hardware-model.data-provider.GPS", 0);
    _leap_seconds = config.get("simulator.hardware-model.data-provider.leap-seconds", 37);
}
```

Arguments explained:

- `config` -- the `<data-provider>` XML subtree, parsed by Boost into a property tree. Default values shown are used only if the XML element is absent.
- `hostname` -- DNS name of the 42 container (`fortytwo` inside the Docker network).
- `port` -- TCP port. Default in code is 4242 (the generic 42 telemetry port), overridden to 4245 by the deployed XML.
- `spacecraft` -- index into 42's `SC[]` array. EO1 has only `SC[0]`.
- `GPS` -- index into 42's `SC[0].GPS[]` array. EO1 has only `GPS[0]`.
- `leap-seconds` -- TAI minus UTC offset (37 seconds as of 2025).

**Step 4 -- one tick of GPS data flows through the parser.**  `GPSSimData42SocketProvider::get_data_point()` at lines 47-58 of the same file pulls the next 42 frame and wraps it in a `GPSSimDataPoint`:

```cpp
boost::shared_ptr<SimIDataPoint> GPSSimData42SocketProvider::get_data_point(void) const
{
    const boost::shared_ptr<Sim42DataPoint> dp42 =
        boost::dynamic_pointer_cast<Sim42DataPoint>(SimData42SocketProvider::get_data_point());
    SimIDataPoint *dp = new GPSSimDataPoint(_sc, _gps, _leap_seconds, dp42);
    return boost::shared_ptr<SimIDataPoint>(dp);
}
```

Arguments to `GPSSimDataPoint::GPSSimDataPoint`:

- `_sc` -- spacecraft index (0).
- `_gps` -- GPS index within that spacecraft (0).
- `_leap_seconds` -- 37.
- `dp42` -- the raw 42 data frame as a key/value bag.

**Step 5 -- the data point parses the 42 keys.**  `nos3/components/novatel_oem615/sim/src/gps_sim_data_point.cpp`. The `do_parsing()` method extracts these keys:

- `SC[0].GPS[0].Valid` -- GPS fix validity (string "1"/"0").
- `SC[0].GPS[0].Lat` -- geodetic latitude **in radians**.
- `SC[0].GPS[0].Lng` -- geodetic longitude **in radians**.
- `SC[0].GPS[0].Alt` -- ellipsoidal altitude above WGS-84 in metres.
- `SC[0].GPS[0].PosW` -- ECEF position vector in km.
- `SC[0].GPS[0].VelW` -- ECEF velocity vector in km/s.

Note: 42 itself has already done the ECEF -> latitude/longitude conversion. The NOS3 GPS sim does not need to call ECEF2LLA on its own except to format the GPGGA NMEA sentence; both paths produce the same numbers because both use the same WGS-84 constants.

**Step 6 -- the OEM615 hardware model formats GPGGA / BESTXYZA messages.**  `nos3/components/novatel_oem615/sim/src/gps_sim_hardware_model_OEM615.cpp` calls `SimCoordinateTransformations::ECEF2LLA(x, y, z, &phi_gd, &lambda, &h_ellp)` from `nos3/sims/sim_common/src/sim_coordinate_transformations.cpp`.

This function implements **Vallado Algorithm 12** (David Vallado, *Fundamentals of Astrodynamics and Applications*, 4th ed., p. 174):

- Inputs: ECEF Cartesian (`x`, `y`, `z`) in km.
- Outputs by reference:
  - `phi_gd` -- geodetic latitude in degrees.
  - `lambda` -- geodetic longitude in degrees.
  - `h_ellp` -- ellipsoidal altitude in metres.
- Constants used: `R_plus = 6378.137 km` (WGS-84 equatorial radius), `e_plus_sq = 0.00669437999014` (eccentricity squared).
- Algorithm: longitude is direct via `atan2(y, x)`; latitude requires an iterative solve because the geodetic latitude depends on altitude, which depends on geodetic latitude. The convergence tolerance is `1e-9 rad`.

The hardware model then formats two NMEA-style messages:

- `GPGGA` -- standard NMEA Global-Positioning-System-Fix sentence, ASCII text, terminated by CRLF.
- `BESTXYZA` -- NovAtel proprietary ECEF-XYZ sentence.

These are written byte-by-byte onto the `usart_1` NOS Engine UART bus.

**Step 7 -- the cFS Novatel app reads the bus and publishes on the SB.**  `nos3/components/novatel_oem615/fsw/cfs/src/novatel_oem615_app.c` and `..._child.c`.

- `NOVATEL_OEM615_AppMain()` initialises the app, opens the UART device, registers SB pipes, then loops on SB messages (commands, HK requests).
- A child task `NOVATEL_OEM615_ChildTask()` polls the UART, parses NMEA frames into the `NOVATEL_OEM615_Device_tlm_t` struct (defined in `novatel_oem615_msg.h`), and publishes each new sample on `NOVATEL_OEM615_DEVICE_TLM_MID` via `CFE_SB_TransmitMsg(...)`.
- The struct layout:

```c
typedef struct {
    uint8_t  TlmHdr[12];     /* CCSDS primary + cFE secondary header */
    uint16_t Week;            /* offset 12 */
    uint32_t Sec;             /* offset 14 */
    double   FracSec;         /* offset 18 */
    double   ECEF_X, ECEF_Y, ECEF_Z;   /* offsets 26, 34, 42 */
    double   Vel_X,  Vel_Y,  Vel_Z;    /* offsets 50, 58, 66 */
    float    Lat;             /* offset 74 -- **degrees** */
    float    Lon;             /* offset 78 -- **degrees** */
    float    Alt;             /* offset 82 -- metres above WGS-84 */
} NOVATEL_OEM615_Device_tlm_t;
```

The byte offsets of `Lat`, `Lon`, `Alt` (74, 78, 82) are exactly what LC watchpoints WP3, WP6, WP7 reference. **In this struct lat and lon are in degrees**, having been converted from the radians 42 originally produced. The conversion happens implicitly inside `ECEF2LLA` (its return values are in degrees) and in the NMEA formatter.

**Step 8 -- the SB packet is captured by `god_view_capture.py`.**  TO_LAB forwards to UDP, the script deserialises the CCSDS primary header, and writes one JSON record per packet to `attack_logs/cfs_god_view.json`.

**Step 9 -- Logstash reads the GPS log line and converts radians to degrees.**  `nos3/elk/logstash.conf` GPS block. Important: this radians-to-degrees conversion applies to the **simulator log line** (`omni_logs/nos3-gps.log`, which carries 42's raw radians), NOT to the SB packet (which already has degrees). Both end up in Elasticsearch as `gps_lat`, `gps_lon` in decimal degrees.

```ruby
ruby { code => "event.set('gps_lat', event.get('gps_lat') * 180.0 / Math::PI)" }
ruby { code => "event.set('gps_lon', event.get('gps_lon') * 180.0 / Math::PI)" }
```

**Step 10 -- Kibana KQL geofence.**  The Mission Validation dashboard runs:

```
gps_lat >= 55 and gps_lat <= 58 and gps_lon >= 8 and gps_lon <= 16
```

A document matching this filter in the first ~80-140 seconds of run time confirms that the entire chain (42 -> GPS sim -> cFS -> SB -> god_view -> Logstash -> Elasticsearch -> Kibana) is healthy.

---

## IPC Port Map  `[L2]`

`nos3/cfg/InOut/Inp_IPC.txt` declares 20 sockets that 42 will open (entries 19-20 added when the DTU TT&C and GNSS components were wired in). Each entry has a fixed structure (8 lines per entry). The full table as currently configured:

| # | Title | Mode | Host:Port | Direction | Prefix(es) | Notes |
|---|---|---|---|---|---|---|
| 1 | RW 0 to 42 | RX | fortytwo:4278 | sim -> 42 | `SC` | Reaction wheel #0 commands going INTO 42. |
| 2 | RW 0 from 42 | TX | fortytwo:4277 | 42 -> sim | `SC[0].Whl[0].H` | Wheel-0 momentum back to the RW simulator. |
| 3 | RW 1 to 42 | RX | fortytwo:4378 | sim -> 42 | `SC` | Wheel-1 commands. |
| 4 | RW 1 from 42 | TX | fortytwo:4377 | 42 -> sim | `SC[0].Whl[1].H` | Wheel-1 momentum. |
| 5 | RW 2 to 42 | RX | fortytwo:4478 | sim -> 42 | `SC` | Wheel-2 commands. |
| 6 | RW 2 from 42 | TX | fortytwo:4477 | 42 -> sim | `SC[0].Whl[2].H` | Wheel-2 momentum. |
| 7 | Torquer IPC | RX | fortytwo:4279 | sim -> 42 | `SC` | Magnetic-torquer dipole commands into 42. |
| 8 | Thruster IPC | **OFF** | fortytwo:4280 | (disabled) | `SC` | **Load-bearing!** `<thruster><enable>false</enable>` in `sc-mission-config.xml` means the thruster sim exits early ("Simulator 'generic-thruster-sim' is not active") and nothing connects to fortytwo:4280. With `RX`, 42 hangs in `inet_csk_accept()` and the sim freezes. Source aligned to the working build copy on 2026-04-28. See `debug/journal.md` and the project root "DO NOT REVERT" notes. |
| 9 | **GPS IPC** | TX | **fortytwo:4245** | **42 -> sim** | `SC[0].GPS[0]` | **The position-data feed** described in the section above. |
| 10 | CSS IPC | TX | fortytwo:4227 | 42 -> sim | `SC[0].CSS` | Coarse Sun Sensor sun-direction stream to the CSS sim. |
| 11 | MAG IPC | TX | fortytwo:4234 | 42 -> sim | `SC[0].MAG` | Magnetic-field stream to the MAG sim. |
| 12 | Truth-data fan-out | TX | fortytwo:9999 | 42 -> sim | `SC`, `Orb` | Generic spacecraft + orbit truth, consumed by `truth_42_sim` (forwards onward to Cosmos). |
| 13 | WRITEFILE | WRITEFILE | fortytwo:6008 | 42 -> file | `SC`, `Orb` | 42 writes State.42 file for off-line analysis. |
| 14 | FSS IPC | TX | fortytwo:4284 | 42 -> sim | `SC[0].FSS[0]` | Fine Sun Sensor stream. |
| 15 | IMU IPC | TX | fortytwo:4281 | 42 -> sim | `SC[0].Accel`, `SC[0].Gyro` | IMU acceleration and angular-rate stream. |
| 16 | **Star Tracker IPC** | **OFF** | fortytwo:4282 | (disabled) | `SC[0].ST` | **Load-bearing!** Originally `TX`. DTU set `OFF` because the active sim XML (`nos3-simulator.xml`) registers `GENERIC_STAR_TRACKER_PROVIDER` (synthetic data), not `_42_PROVIDER`, so nothing connects to fortytwo:4282. With `TX`, 42 hangs in `inet_csk_accept()` waiting for a client and the whole sim freezes at startup. See `debug/journal.md` and the project root "DO NOT REVERT" notes. |
| 17 | EPS IPC | TX | fortytwo:4283 | 42 -> sim | `SC[0].svb`, `SC[0].PosR`, `SC[0].qn`, `Orb[0].PosN` | Sun-vector body-frame + position + quaternion + Sun direction for the EPS power model. |
| 18 | Blackboard IPC | TX | fortytwo:4285 | 42 -> sim | `SC[0]` | Generic blackboard channel (currently unused at runtime). |
| 19 | **TT&C IPC** | TX | **fortytwo:4286** | 42 -> sim | `SC[0].GPS[0].PosW`, `SC[0].GPS[0].VelW` | **Load-bearing tight prefix!** Exactly the two keys above (count = 2). Do **not** widen back to `SC` or `Orb`. Reason: 42's `WriteToSocket` (`/home/vscode/.nos3/42/Source/AutoCode/TxRxIPC.c:464-468`) calls `write()` on a non-blocking socket and `exit(1)`s if it returns -1. With broad prefixes 42 emits ~5 KB per tick at 100 Hz; the TT&C sim's byte-by-byte `rgetc` reader cannot drain the kernel TCP send buffer fast enough; 42 hits EAGAIN within seconds and crashes. Tight prefixes drop the per-tick payload to ~150 bytes and keep 42 alive. See `debug/journal.md` 2026-04-29 session. |
| 20 | **GNSS IPC** | TX | **fortytwo:4287** | 42 -> sim | `SC[0].GPS[0].PosW` | **Load-bearing tight prefix!** Exactly that one key (count = 1). Same EAGAIN-crash story as entry 19. Bracket-stripping in the GNSS sim's parser (see entry 6.10 below) is the matching FSW-side load-bearing edit. |

**Direction terminology** (42's perspective, opposite of what feels natural):

- 42's `TX` mode: 42 writes data **out** of itself (TX = transmit). The connecting simulator is a TCP client that reads.
- 42's `RX` mode: 42 reads data **in** from a client (RX = receive). The connecting simulator writes commands.
- 42's `WRITEFILE` mode: 42 writes to a local file instead of the network.
- 42's `OFF` mode: the entry is parsed but the socket is never opened. Required when no client will ever connect (otherwise 42 blocks forever).

---

## Architecture: How the Layers Connect

```
┌──────────────────────────────────────────────────────────────────┐
│  SCH (Scheduler app)                                             │
│  Fires HK request commands at 1 Hz on the Software Bus          │
│  sch_def_msgtbl.c  ←→  sch_def_schtbl.c                        │
└────────────────────────────┬─────────────────────────────────────┘
                             │ HK request commands (MIDs 0x1xxx)
                             ▼
┌──────────────────────────────────────────────────────────────────┐
│  cFS Apps (EPS, GPS, ADCS, IMU, MAG, RW, Radio, MGR, ...)       │
│  Each app responds with its own HK telemetry packet (0x0xxx)    │
└────────────────────────────┬─────────────────────────────────────┘
                             │ HK telemetry on Software Bus
                             ▼
┌──────────────────────────────────────────────────────────────────┐
│  HK (Housekeeping Aggregator app)                                │
│  Copies selected bytes from component HK into COMBINED_PKT1     │
│  hk_cpy_tbl.c                                                    │
└────────────────────────────┬─────────────────────────────────────┘
                             │ COMBINED_PKT1 on Software Bus
                             ▼
┌──────────────────────────────────────────────────────────────────┐
│  LC (Limit Checker app)                                          │
│  Monitors individual HK packets AND COMBINED_PKT1 for threshold  │
│  crossings. WP = one threshold rule; AP = one response rule.    │
│  lc_def_wdt.c  ←→  lc_def_adt.c                                │
└────────────────────────────┬─────────────────────────────────────┘
                             │ On violation: fires EVS event + starts SC RTS
                             ▼
┌──────────────────────────────────────────────────────────────────┐
│  SC (Stored Command app)                                         │
│  Executes pre-loaded command sequences (RTS scripts)            │
│  sc_rts001.c  sc_rts002.c  sc_rts003.c  sc_rts004.c            │
└────────────────────────────┬─────────────────────────────────────┘
                             │ Commands (e.g. MGR SET_MODE SAFE)
                             ▼
┌──────────────────────────────────────────────────────────────────┐
│  TO_LAB / CI_LAB                                                 │
│  TO_LAB: Telemetry Output -- forwards SB packets to UDP          │
│  CI_LAB: Command Ingest -- receives UDP commands and puts on SB  │
│  god_view_capture.py enables TO_LAB and logs every packet       │
└────────────────────────────┬─────────────────────────────────────┘
                             │ CCSDS UDP packets → attack_logs/cfs_god_view.json
                             │ omni_logs/*.log  (one file per NOS3 simulator)
                             ▼
┌──────────────────────────────────────────────────────────────────┐
│  ELK Stack (Logstash → Elasticsearch → Kibana)                   │
│  Logstash: parses raw text and JSON into structured documents   │
│  Elasticsearch: stores documents as time-series                  │
│  Kibana: web UI for dashboards and KQL queries                  │
│  logstash.conf                                                   │
└──────────────────────────────────────────────────────────────────┘
```

### Software Bus topology

The **Software Bus (SB)** is cFE's in-process publish-subscribe message router. It does not understand semantics; it just delivers any packet whose 16-bit MID matches a subscriber's interest. The SB has three primary functions:

- `CFE_SB_TransmitMsg(MsgPtr, IncrementSequenceCount)` -- publish. The publisher fills a buffer (CCSDS primary header + payload), calls this, and the SB delivers a copy to every subscribed pipe.
- `CFE_SB_Subscribe(MsgId, PipeId)` -- subscribe. Apps create a pipe (FIFO) at startup, then register interest in each MID they want.
- `CFE_SB_ReceiveBuffer(BufPtrPtr, PipeId, TimeOut)` -- receive. Blocks (or returns immediately) until a packet arrives on the pipe.

**MID convention** (NOS3-wide):

- Commands: `0x18nn` or `0x19nn` (bit 12 set indicates "command packet").
- Telemetry: `0x08nn` or `0x09nn` (bit 12 clear).

The full registry of allocated MIDs is generated at `make config` time and lives at `nos3/cfg/build/mid_registry.json`. Every entry has a `name`, `layer` (FSW_CORE / FSW_HERITAGE / COMPONENT / RESEARCH), and `subsystem` tag. Logstash reads a YAML version of this file (`nos3/cfg/build/elk/mid_to_*.yml`) to enrich each packet with a human-readable name. Always cite the registry rather than hand-listing MIDs in this document.

**Major SB flows in EO1:**

1. **HK request fan-out** (1 Hz). SCH publishes a `*_REQ_HK_MID` for every active component (13 commands per second). Each component's FSW app subscribes to its own request MID, reads its current state, and publishes a `*_HK_TLM_MID` packet back on the SB.
2. **HK aggregation**. The HK app subscribes to every `*_HK_TLM_MID`, copies selected bytes into the `HK_COMBINED_PKT1_MID` packet, then re-publishes the combined packet at 1 Hz.
3. **Limit checking**. LC subscribes to the packets it watches (declared per-watchpoint in `lc_def_wdt.c`). On a threshold crossing, it publishes an EVS event and (if the AP says so) a `SC_START_RTS_CC` to the SC app.
4. **Stored commanding**. SC publishes the commands embedded in the RTS struct on the SB. Targets like MGR subscribe to their own command MID and act.
5. **Telemetry output**. TO_LAB subscribes to a configured set of MIDs (declared in its `to_lab_sub.c` table) and forwards them by UDP to whatever address the last `TO_LAB_OUTPUT_ENABLE_CC` provided.

### HWLIB call pattern (FSW <-> NOS Engine bus <-> simulator)

Every cFS component app talks to its simulated hardware via the same pattern, abstracted by the **Hardware Library (HWLIB)**. Conceptually:

```
Component cFS app
   |
   | calls e.g.   GENERIC_EPS_DEVICE_ReadData(buffer, length)
   v
HWLIB (libhwlib.so)
   |
   | issues the right NOS Engine bus operation:
   |   I2C:  hwlib_i2c_read(bus, address, buf, len)
   |   SPI:  hwlib_spi_transaction(bus, tx_buf, rx_buf, len)
   |   UART: hwlib_uart_read(handle, buf, len)
   |   CAN:  hwlib_can_recv(handle, frame)
   v
NOS Engine bus server (separate container or in-process server)
   |
   | dispatches to the registered slave / server simulator
   v
Component simulator (libgeneric_eps_sim.so etc.)
   |
   | implements ProcessCommand() / GetTelemetry() with truth from 42
```

The benefit of the HWLIB layer is that the cFS app source code is **identical** between simulator builds and real flight builds. Replacing `libhwlib.so` with a hardware-specific implementation is the only thing required to fly the same FSW on a real CubeSat.

### Ground-software path (FSW telemetry -> god_view + Cosmos)

```
TO_LAB cFS app
   |
   | encapsulates each subscribed SB packet inside a UDP datagram
   | sends to <CI_LAB_HOST>:5013 (default cFS TLM port)
   v
   +--> Cosmos GSW container (port 5013)         -- displays in COSMOS UI
   +--> god_view_capture.py (port 5013)          -- writes attack_logs/cfs_god_view.json
                                                    Logstash file input picks up new lines
```

Note: Cosmos and god_view both listen on 5013. They share the UDP traffic via `SO_REUSEPORT` (each gets every packet). This is intentional: god_view is a passive logger; turning it off does not affect Cosmos, and vice versa.

---

## Step-by-Step: What Was Changed and Why

### 1. Scheduler Message Table (`sch_def_msgtbl.c`)

**What:** Added 13 new message entries (indices 25–37) that define the HK request commands for every active hardware component.

**Why:** The SCH app (Scheduler) operates from two tables: a message table (what packets exist) and a schedule table (when to send them). The message table must contain an entry for every command SCH will ever fire. Without an entry here, SCH has nothing to send.

**How (CCSDS packet format):**
```c
{{CFE_MAKE_BIG16(GENERIC_EPS_REQ_HK_MID), CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}},
```
- Field 1: CCSDS Stream ID -- the MID in big-endian byte order
- Field 2: `0xC000` -- command packet (bit 12 set), no secondary header, sequence flags
- Field 3: `0x0001` -- CCSDS data length field (1 byte beyond header = no payload)
- Field 4: checksum (0 = disabled)

**Entries added:**

| Index | MID Macro | Component |
|-------|-----------|-----------|
| 25 | `GENERIC_EPS_REQ_HK_MID` | EPS (Electrical Power System) |
| 26 | `NOVATEL_OEM615_REQ_HK_MID` | GPS (Novatel OEM615 receiver) |
| 27 | `GENERIC_ADCS_REQ_HK_MID` | ADCS (Attitude Determination & Control) |
| 28 | `GENERIC_IMU_REQ_HK_MID` | IMU (Inertial Measurement Unit) |
| 29 | `GENERIC_CSS_REQ_HK_MID` | CSS (Coarse Sun Sensor) |
| 30 | `GENERIC_FSS_REQ_HK_MID` | FSS (Fine Sun Sensor) |
| 31 | `GENERIC_MAG_REQ_HK_MID` | MAG (Magnetometer) |
| 32 | `GENERIC_RW_APP_SEND_HK_MID` | RW (Reaction Wheel) |
| 33 | `GENERIC_RADIO_REQ_HK_MID` | Radio (RF transceiver) |
| 34 | `GENERIC_STAR_TRACKER_REQ_HK_MID` | Star Tracker (precision attitude sensor) |
| 35 | `GENERIC_TORQUER_REQ_HK_MID` | Torquer (magnetic torquer rod for momentum dumping) |
| 36 | `MGR_REQ_HK_MID` | MGR (Mission Manager -- mode control) |
| 37 | `SAMPLE_REQ_HK_MID` | Sample App (NOS3 reference component) |

**How to find the correct MID macro:** Each component's `*_msgids.h` header (under `nos3/components/<name>/fsw/cfs/inc/`) defines `<COMPONENT>_REQ_HK_MID` or `<COMPONENT>_SEND_HK_MID`. Use `grep -r REQ_HK_MID nos3/components/` to list all available macros.

---

### 2. Scheduler Schedule Table (`sch_def_schtbl.c`)

**What:** Added schedule entries in slots 25–39, one per component, each enabled at 1 Hz.

**Why:** The schedule table is the clock. It determines when each message table entry gets sent. Each slot in the table corresponds to a 1 Hz major frame. Without a schedule entry, the message table entry is never fired and the component never sends HK.

**How (entry format):**
```c
/* slot #25 */
{SCH_ENABLED, SCH_ACTIVITY_SEND_MSG, 1, 0, 25, SCH_GROUP_CDH},  /* EPS HK Request */
{SCH_UNUSED,  0,                     0, 0, 0,  SCH_GROUP_NONE},
{SCH_UNUSED,  0,                     0, 0, 0,  SCH_GROUP_NONE},
{SCH_UNUSED,  0,                     0, 0, 0,  SCH_GROUP_NONE},
{SCH_UNUSED,  0,                     0, 0, 0,  SCH_GROUP_NONE},
```

Fields: `{enable_state, activity_type, frequency, remainder, message_index, group}`
- `SCH_ENABLED` / `SCH_UNUSED`: whether this entry is active
- `SCH_ACTIVITY_SEND_MSG`: tells SCH to send the packet defined at `message_index` in the message table
- `frequency=1`: fire once per major frame (every 1 second)
- `remainder=0`: fire on frame 0 (must be strictly less than `frequency`; for frequency=1, the only valid value is 0)
- `message_index=25`: points to entry 25 in `sch_def_msgtbl.c`
- Each slot holds 5 sub-entries; only the first is active; the other four are `SCH_UNUSED` padding

**Critical constraint:** `remainder` must be strictly less than `frequency`. For `frequency=1`, only `remainder=0` is valid. The SCH app rejects the table at load time if this rule is violated, which stops all scheduling and prevents any HK data from flowing.

**Group assignment:** `SCH_GROUP_CDH` for power/comms/data-handling components; `SCH_GROUP_GNC_HK` for guidance/navigation/control sensor components (ADCS, IMU, CSS, FSS, MAG, RW, Star Tracker, Torquer).

---

### 3. Housekeeping Copy Table (`hk_cpy_tbl.c`)

**What:** Added entries 5–19 that copy selected bytes from each component's individual HK packet into the single aggregated `HK_COMBINED_PKT1_MID` packet.

**Why:** The HK app (Housekeeping Aggregator) acts as a field-level mux. LC watches this combined packet for threshold crossings. Without copy entries, LC has no input data and all watchpoints remain in the "stale" state indefinitely. The combined packet is also what TO_LAB forwards to the ground and what god_view_capture.py logs.

**How (one copy entry):**
```c
/* 5 - EPS: CommandErrorCount + CommandCount (2 bytes from offset 12) */
{
    CFE_SB_MSGID_WRAP_VALUE(GENERIC_EPS_HK_TLM_MID),  /* source packet MID */
    12,                                                 /* source byte offset */
    CFE_SB_MSGID_WRAP_VALUE(HK_COMBINED_PKT1_MID),    /* destination packet MID */
    32,                                                 /* destination byte offset */
    2,                                                  /* bytes to copy */
},
```

**Source offset convention:** All cFE apps place their payload at a fixed offset from the start of their HK struct. The first 12 bytes are always the CCSDS primary header (6 bytes) + cFE telemetry secondary header (6 bytes). Payload fields start at offset 12. The standard layout is: `[0..5]` CCSDS primary header, `[6..11]` cFE timestamp secondary header, `[12]` CommandErrorCount, `[13]` CommandCount, `[14+]` app-specific payload.

**Destination offset:** Must be tracked manually. Entries must not overlap. EO1 starts at destination offset 32 (leaving 32 bytes for the combined packet's own CCSDS header region) and increments by the number of bytes copied in each entry.

**Key copy entries in EO1:**

| Entry | Source Packet | Source Offset | Dest Offset | Bytes | Field Copied |
|-------|--------------|---------------|-------------|-------|--------------|
| 5 | EPS HK | 12 | 32 | 2 | CmdErrCnt + CmdCnt |
| 6 | EPS HK | 16 | 34 | 2 | BatteryVoltage (mV) |
| 7 | GPS HK | 12 | 36 | 2 | CmdErrCnt + CmdCnt |
| 8 | ADCS HK | 12 | 38 | 2 | CmdErrCnt + CmdCnt |
| ... | ... | ... | ... | ... | ... |
| 17 | MGR HK | 12 | 56 | 3 | CmdErrCnt + CmdCnt + SpacecraftMode |
| 18 | MGR HK | 28 | 59 | 2 | SciPassCount |

**Finding payload offsets:** Open the component's `*_msg.h` (e.g. `components/generic_eps/fsw/cfs/inc/generic_eps_msg.h`). Count bytes from the start of the struct; subtract 12 for the CCSDS header already accounted for by the source-offset convention.

---

### 4. Limit Checker Watchpoint Table (`lc_def_wdt.c`)

**What:** Defined 10 watchpoints (WP0–WP9) that monitor numeric fields in telemetry packets for threshold crossings. WP0–WP5 are health/resource limits (battery, CPU, altitude, wheel, solar). WP6–WP9 form the Denmark lat/lon box (GPS `lat` > 55.0° AND `lat` < 58.0° AND `lon` > 8.0° AND `lon` < 16.0°) that AP3 combines to autonomously trigger SCIENCE mode.

**Why:** LC (Limit Checker) provides the autonomous fault-detection layer. Each watchpoint subscribes to a specific packet, extracts one field at a given byte offset, and continuously compares it against a threshold. When the comparison triggers, LC records a "fail" result for that watchpoint. Actionpoints (APs) then decide whether to fire an event, start a recovery sequence, or both.

**Watchpoint entry structure:**
```c
{
    .DataType             = LC_DATA_UWORD_LE,       /* data type of the field */
    .OperatorID           = LC_OPER_LT,             /* comparison: less-than */
    .MessageID            = GENERIC_EPS_TLM_MSG,    /* which SB packet to watch */
    .WatchpointOffset     = 16,                     /* byte offset inside that packet */
    .BitMask              = LC_NO_BITMASK,          /* no bit masking */
    .CustomFuncArgument   = 0,                      /* not used */
    .ResultAgeWhenStale   = 5,                      /* declare stale after 5 missed frames */
    .ComparisonValue.Unsigned16in32.Unsigned16 = 14800,  /* threshold value */
},
```

**Data types supported by LC:**

| LC constant | C type | Width | Notes |
|---|---|---|---|
| `LC_DATA_UBYTE` | uint8 | 1 byte | unsigned 8-bit integer |
| `LC_DATA_UWORD_LE` | uint16 | 2 bytes | unsigned 16-bit, little-endian |
| `LC_DATA_UDWORD_LE` | uint32 | 4 bytes | unsigned 32-bit, little-endian |
| `LC_DATA_SWORD_LE` | int16 | 2 bytes | signed 16-bit, little-endian |
| `LC_DATA_FLOAT_LE` | float | 4 bytes | IEEE 754 single-precision float, little-endian |

**Implemented watchpoints in EO1:**

| WP | Name | Packet | Offset | Type | Op | Threshold | Purpose |
|----|------|--------|--------|------|----|-----------|---------|
| 0 | BATTERY_LOW | `GENERIC_EPS_TLM_MSG` | 16 | UWORD_LE | LT | 14800 mV | ~20% State of Charge -- triggers SAFE mode |
| 1 | BATTERY_OVERCHARGE | `GENERIC_EPS_TLM_MSG` | 16 | UWORD_LE | GT | 22200 mV | Overcharge protection -- alert only |
| 2 | CPU_HIGH | `HS_HK_TLM_MSG` | 40 | UDWORD_LE | GT | 75 | CPU utilization spike -- alert only |
| 3 | ORBIT_LOW | `NOVATEL_OEM615_DEVICE_TLM_MSG` | 82 | FLOAT_LE | LT | 400000.0 m | Altitude < 400 km = deorbit risk -- alert only |
| 4 | RW_OFFLINE | `GENERIC_RW_APP_HK_TLM_MSG` | 20 | UBYTE | EQ | 0 | Reaction wheel dropped offline -- triggers desaturation |
| 5 | SOLAR_LOW | `GENERIC_EPS_TLM_MSG` | 28 | UWORD_LE | LT | 100 mV | Solar array voltage near zero = array shadowed or failed -- alert only |

**How WP3 offset 82 was calculated (GPS altitude):**
The `NOVATEL_OEM615_Device_tlm_t` struct layout from offset 0:
- `[0..11]` TlmHdr (CCSDS primary + secondary header, 12 bytes)
- `[12..13]` GPS week number (uint16, 2 bytes)
- `[14..17]` GPS seconds (uint32, 4 bytes)
- `[18..25]` GPS fractional seconds (double, 8 bytes)
- `[26..49]` ECEF X, Y, Z position (3 × double = 24 bytes)
- `[50..73]` ECEF velocity X, Y, Z (3 × double = 24 bytes)
- `[74..77]` latitude (float, 4 bytes)
- `[78..81]` longitude (float, 4 bytes)
- **`[82..85]` altitude (float, 4 bytes) ← WP3 offset**

**How WP4 offset 20 was calculated (RW DeviceEnabled):**
`GENERIC_RW_HkTlm_t.Payload`:
- `[0..11]` TlmHdr (12 bytes)
- `[12]` CommandErrorCounter (1 byte)
- `[13]` CommandCounter (1 byte)
- `[14]` DeviceErrorCount (1 byte, uint8)
- `[15]` DeviceCount (1 byte, uint8)
- `[16..19]` padding / alignment (4 bytes)
- **`[20]` DeviceEnabled_RW0 (uint8) ← WP4 offset**

**How WP5 offset 28 was calculated (EPS SolarArrayVoltage):**
`GENERIC_EPS_Hk_tlm_t` payload:
- `[0..11]` TlmHdr (12 bytes)
- `[12..15]` CommandErrorCount + CommandCount + alignment (4 bytes)
- `[16..17]` BatteryVoltage (uint16, 2 bytes)
- `[18..19]` BatteryTemperature (uint16, 2 bytes)
- `[20..21]` Bus3V3Current (uint16, 2 bytes)
- `[22..23]` Bus5VCurrent (uint16, 2 bytes)
- `[24..25]` Bus12VCurrent (uint16, 2 bytes)
- `[26..27]` EpsTemperature (uint16, 2 bytes)
- **`[28..29]` SolarArrayVoltage (uint16, 2 bytes) ← WP5 offset**

---

### 5. Limit Checker Actionpoint Table (`lc_def_adt.c`)

**What:** Defined 4 actionpoints (AP0–AP3) that map watchpoint failures to responses. AP0–AP2 handle health/resource faults (battery, CPU, reaction wheel); AP3 is the autonomous SCIENCE-mode trigger that combines WP6–WP9.

**Why:** A watchpoint failure alone does nothing. The actionpoint layer decides: how many consecutive failures before acting, what EVS event to emit, and optionally which SC RTS to execute.

**How (actionpoint entry):**
```c
/* AP #0 - SAFE_ON_LOW_BAT */
{
    .DefaultState         = LC_APSTATE_ACTIVE,           /* active from boot */
    .MaxPassiveEvents     = 2,                            /* max "pass→fail" EVS events */
    .MaxPassFailEvents    = 2,                            /* max "fail→pass" EVS events */
    .MaxFailPassEvents    = 2,                            /* max "pass→fail" EVS events */
    .RTSId                = 1,                           /* start RTS 001 when triggered */
    .MaxFailsBeforeRTS    = 3,                           /* require 3 consecutive fails */
    .EventType            = CFE_EVS_EventType_CRITICAL,  /* EVS severity level */
    .EventText            = {"Battery voltage critical - entering SAFE mode"},
    .RPNEquation          = { LC_RPN_EQUAL, 0, LC_RPN_OPER_NOT_USED, ... },
},
```

**RPN equation format:** LC uses Reverse Polish Notation to combine watchpoint results. The equation is a flat array of tokens:
- A watchpoint number (0–254) pushes that WP's current result (pass/fail) onto a stack
- `LC_RPN_EQUAL` pops the top value and checks if it equals FAIL
- `LC_RPN_AND` / `LC_RPN_OR` pops two values and pushes their logical combination
- `LC_RPN_OPER_NOT_USED` pads unused slots

Example: `{ LC_RPN_EQUAL, 0, LC_RPN_OPER_NOT_USED, ... }` means "evaluate if WP0 is in FAIL state".
Example: `{ 0, LC_RPN_EQUAL, 2, LC_RPN_EQUAL, LC_RPN_AND, LC_RPN_OPER_NOT_USED, ... }` means "WP0 FAIL AND WP2 FAIL".

**`MaxFailsBeforeRTS`:** LC accumulates consecutive fail samples. Only when the count reaches this value does it fire the RTS. This debounces transient noise -- a single bad telemetry sample will not trigger a mode change.

**Implemented actionpoints in EO1:**

| AP | Name | Trigger WP | RTS Fired | Event Severity | Description |
|----|------|------------|-----------|----------------|-------------|
| 0 | SAFE_ON_LOW_BAT | WP0 (BATTERY_LOW) | RTS 001 | CRITICAL | Enter SAFE mode when battery drops below 20% SoC |
| 1 | CPU_SPIKE_ALERT | WP2 (CPU_HIGH) | none | CRITICAL | Alert ground when CPU load exceeds 75% -- no automated action |
| 2 | RW_OFFLINE | WP4 (RW_OFFLINE) | RTS 003 | CRITICAL | Start desaturation sequence when reaction wheel drops offline |
| 3 | SCIENCE_OVERFLY | WP6 AND WP7 AND WP8 AND WP9 | RTS 002 | INFORMATION | Enter SCIENCE mode autonomously when GPS position enters the Denmark lat/lon box (55–58°N, 8–16°E). RPN: `{6, LC_RPN_EQUAL, 7, LC_RPN_EQUAL, LC_RPN_AND, 8, LC_RPN_EQUAL, LC_RPN_AND, 9, LC_RPN_EQUAL, LC_RPN_AND}`. `MaxFailsBeforeRTS = 2`. |

---

### 6. SC Sequence Tables (`sc_rts*.c`)

**What:** Pre-loaded command sequences that SC executes atomically when triggered by LC or by a direct ground command.

**Why:** SC (Stored Command) allows the spacecraft to execute complex command sequences autonomously, without waiting for a round-trip to the ground (which would be 500+ ms even in LEO). Each RTS is a C struct compiled into a binary `.tbl` file that SC loads at boot.

**How (general structure for a single-command RTS):**
```c
#include "sc_tbldefs.h"         /* SC_RtsEntryHeader_t, SC_RTS_BUFF_SIZE */
#include "sc_platform_cfg.h"   /* SC configuration constants */
#include "sc_msgdefs.h"        /* SC command codes */
#include "sc_msgids.h"         /* SC MIDs */
#include "sc_msg.h"            /* SC message structs */
#include "mgr_msgids.h"        /* MGR_CMD_MID */
#include "mgr_msg.h"           /* MGR_U8_cmd_t */
#include "mgr_app.h"           /* MGR_SET_MODE_CC, MGR_SAFE_MODE */

typedef struct {
    SC_RtsEntryHeader_t hdr1;   /* timing header: WakeupCount relative delay */
    MGR_U8_cmd_t        cmd1;   /* CCSDS command packet for MGR */
} SC_RtsStruct001_t;

typedef union {
    SC_RtsStruct001_t rts;
    uint16            buf[SC_RTS_BUFF_SIZE];  /* required by SC table framework */
} SC_RtsTable001_t;

#define SC_MEMBER_SIZE(member) (sizeof(((SC_RtsStruct001_t *)0)->member))

SC_RtsTable001_t SC_Rts001 = {
    .rts.hdr1.WakeupCount = 0,                /* fire at wakeup tick 0 (immediately) */
    .rts.cmd1 = {CFE_MSG_CMD_HDR_INIT(        /* initialize CCSDS command header */
        MGR_CMD_MID,                           /* destination app's MID */
        SC_MEMBER_SIZE(cmd1),                  /* packet length in bytes */
        MGR_SET_MODE_CC,                       /* command code within MGR */
        0x00)},                                /* checksum byte (0 = no checksum) */
    .rts.cmd1.Payload.U8 = MGR_SAFE_MODE,     /* command payload: mode value */
};

CFE_TBL_FILEDEF(SC_Rts001, SC.RTS_TBL001, SC RTS001 Enter SAFE Mode, sc_rts001.tbl)
/* CFE_TBL_FILEDEF args: variable_name, table_name, description, output_filename */
```

**Key rules for writing RTS tables:**
- Every command in the struct requires its own `SC_RtsEntryHeader_t` immediately before it
- `WakeupCount` in the header is the number of SC 1 Hz wakeup ticks to wait **after the previous command** (0 = execute immediately / same tick as previous)
- `CFE_MSG_CMD_HDR_INIT(mid, size, cc, cksum)` initializes the CCSDS primary + secondary command header
- The payload struct type (e.g. `MGR_U8_cmd_t`) must exactly match what the target app's `*_msg.h` defines for that command code
- The union with `uint16 buf[SC_RTS_BUFF_SIZE]` is required by the SC table loader -- it ensures the table occupies the full allocated buffer
- `CFE_TBL_FILEDEF` registers the table with cFE's Table Services so it appears in the table registry and can be uplinked/validated
- The build system automatically compiles all `sc_rts*.c` files it finds in `nos3/cfg/nos3_defs/tables/`

**Implemented RTS sequences in EO1:**

| RTS | File | Trigger | Commands | Purpose |
|-----|------|---------|----------|---------|
| 001 | `sc_rts001.c` | LC AP0 (BATTERY_LOW) or SC auto-start | MGR SET_MODE SAFE | Transition spacecraft to SAFE mode -- sun-point attitude, stop science |
| 002 | `sc_rts002.c` | LC AP3 (SCIENCE_OVERFLY) when GPS is over Denmark; or ground uplink (start RTS 002 command) | MGR SET_MODE SCIENCE | Transition spacecraft to SCIENCE mode for data collection pass |
| 003 | `sc_rts003.c` | LC AP2 (RW_OFFLINE) | GENERIC_TORQUER ENABLE at t=0, GENERIC_TORQUER DISABLE at t=10 s | Reaction wheel desaturation: enable torquer rods for 10 seconds to dump momentum into Earth's magnetic field, then disable |
| 004 | `sc_rts004.c` | Ground uplink or future HS-linked AP | MGR SET_MODE SAFE | Critical health response: force SAFE mode when an app failure is detected |

**RTS 003 desaturation explanation:**
When a reaction wheel goes offline or saturates (stores more angular momentum than it can handle), the spacecraft loses one axis of attitude control. The standard recovery is magnetic torquing: magnetorquer rods generate a magnetic dipole that interacts with Earth's field, transferring momentum out of the wheels and into the spacecraft body, which then dissipates into the environment. RTS 003 enables the torquer for 10 wakeup ticks (10 seconds at 1 Hz) then disables it to avoid over-correction.

---

### 7. god_view Capture Script (`scripts/god_view_capture.py`)

**What:** Headless Python script that logs every CCSDS packet seen on the cFS Software Bus to `attack_logs/cfs_god_view.json` (one JSON line per packet).

**Why:** The Logstash `software_bus` input layer reads this file. Without it, Kibana has no Software Bus data and all SB-layer dashboards and attack detection are blind.

**How it works:**
1. On startup, it calls `docker inspect` to find the IP address of the CI_LAB container (`nos3-sc01-ci-lab`) on the `nos3_sc_1` network. Falls back to `172.19.0.4` if the container is not found.
2. Sends a `TO_LAB_OUTPUT_ENABLE_CC` command to CI_LAB via UDP. This tells the TO_LAB (Telemetry Output) app to start forwarding all Software Bus packets to a UDP port.
3. Listens on UDP port 5013 and parses the CCSDS primary header (stream ID, sequence counter, packet length) from each received packet.
4. Appends a JSON object to `attack_logs/cfs_god_view.json` for each packet: `{"msg_id": "0x...", "sequence": N, "length": N, "timestamp": "..."}`.
5. Re-sends the enable command every 30 seconds so telemetry output survives CI_LAB restarts.

**Changes made:**
- **Dynamic IP discovery**: Previously `CI_LAB_HOST` was hardcoded to `172.19.0.4`, which fails if Docker assigns a different IP. Now uses `docker inspect` with three container-name patterns (`nos3-sc01-ci-lab`, `nos3-ci-lab`, `sc01-ci-lab`) and only falls back to the static IP if none are found.
- **Auto-start in `make launch`**: Added to `nos3/Makefile` so the capture starts automatically 10 seconds after FSW boots (the delay allows cFS and TO_LAB to finish initializing before the enable command is sent). Logs to `omni_logs/god_view.log`.

```makefile
@echo "Starting cFS Software Bus god-view capture (attack_logs/cfs_god_view.json)..."
@sleep 10 && python3 $(CURDIR)/scripts/god_view_capture.py >> omni_logs/god_view.log 2>&1 &
```

---

### 8. Logstash Telemetry Parsers (`nos3/elk/logstash.conf`)

**What:** Filter blocks that parse two raw log sources into structured Elasticsearch documents with typed numeric fields, human-readable labels, and attack-detection tags.

**Two input streams:**

| Stream | Source file | Document `type` field | Contents |
|--------|-------------|----------------------|----------|
| Software Bus | `attack_logs/cfs_god_view.json` | `software_bus` | Every CCSDS packet seen on the SB: MID, sequence counter, length, timestamp |
| Simulator/FSW logs | `omni_logs/*.log` | `system_log` | One line per log event from each NOS3 simulator container and from cFS EVS |

**Software Bus layer enrichments:**
- `msg_name`: human-readable name for each MID (via a 200-entry `translate` dictionary)
- `layer`: architectural layer (`FSW_CORE`, `FSW_HERITAGE`, `COMPONENT`, `RESEARCH`, `UNKNOWN`)
- `subsystem`: short subsystem name (`EPS`, `GPS`, `LC`, `SC`, etc.)
- `msg_direction`: `CMD` (0x1xxx) or `TLM` (0x0xxx)
- `msg_id_int`: integer version of the hex MID string (for range queries in KQL)
- `noisy_app_spam_target` tag: marks packets targeting the 7 SB saturation targets
- `sb_sequence_gap` tag + `sb_gap_size` field: detects dropped packets by tracking the 14-bit CCSDS sequence counter per MID

**Simulator log layer parsers:**
- **EPS**: solar panel power (W), total power used (W), battery Wh, battery voltage (mV), solar array connected flag; plus two derived fields: `eps_battery_soc_pct` (State of Charge %) and `eps_power_balance_w` (solar − used)
- **Reaction Wheel**: `rw_momentum` (angular momentum in mNms)
- **GPS**: `gps_abs_time`, `gps_ecef_x/y/z` (Earth-Centered Earth-Fixed coordinates), `gps_lat`, `gps_lon`, `gps_alt`, `gps_valid`
- **IMU**: linear acceleration `imu_acc_x/y/z` and angular rate `imu_gyro_x/y/z`
- **Magnetometer**: field vector `mag_x/y/z` (supports scientific notation values like `8.44e-07`)
- **FSS (Fine Sun Sensor)**: validity flag, `fss_alpha`, `fss_beta` sun angles
- **Star Tracker**: validity flag, attitude quaternion `st_q0/q1/q2/q3`
- **Truth42 Dynamics**: position `t42_pos_x/y/z`, velocity `t42_vel_x/y/z`, attitude quaternion `t42_q0-3`, sun body vector `t42_svb_x/y/z`, gyro `t42_gyro_x/y/z`
- **Torquer**: magnetic dipole command `torquer_dipole_x/y/z`
- **Sample**: sun body vector `sample_sun_body_x/y/z`
- **CPU monitor**: `TOTAL_CPU_PCT`, `CPU_PCT` (per-thread), `NUM_PIDS`, with `cpu_spike` tag when total CPU ≥ 80%
- **cfs_evs**: parses EVS events from all apps including MGR mode changes, LC actionpoint firings, HS app failures, NOISY_APP attack lifecycle, CS checksum failures, MM memory writes, MD dwell operations

**Noise filters (added to suppress unactionable boot messages):**

| Filter | What it suppresses | Why |
|--------|--------------------|-----|
| Drop `Startup Sync failed` and `cFE startup timeout` | These EVS messages appear at every boot during the 30-second cFS initialization window. They are not errors -- they are normal synchronization retries while apps wait for dependencies. Including them in Elasticsearch floods the index with thousands of irrelevant documents per boot. | Boot noise |
| Tag `md_table_error` on MD table load failures | MD (Memory Dwell) table files may not exist if the SC1 profile does not include all optional dwell configurations. These are kept visible (not dropped) as they indicate a real but non-critical configuration gap. | Config awareness |
| Tag `sim_connectivity_warning` on radio→cryptolib failures | The radio simulator tries to resolve `cryptolib` by hostname, which fails if NOS3 was started without the cryptography library container. Expected in minimal-config runs. Tagged instead of dropped so the count is visible but does not pollute error dashboards. | Expected in minimal runs |

**MGR mode change parser:**
```
EVS format: "MGR: Set mode command received [1]"
Extracted:  mgr_mode_num (integer), mgr_mode ("SAFE" or "SCIENCE")
```
This allows Kibana to display mode transition events as discrete markers on a timeline.

**LC actionpoint state parser:**
```
EVS format: "LC: AP state change from PASS to FAIL: AP = 0"
Extracted:  lc_ap_from, lc_ap_to, lc_ap_num (integer)
Tag added:  "lc_actionpoint_fired" when lc_ap_to == "FAIL"
```

**Complete field catalog:**

| Subsystem | Fields |
|-----------|--------|
| SB routing | `msg_id`, `msg_name`, `layer`, `subsystem`, `msg_direction`, `sequence`, `length` |
| EVS | `evs_app_name`, `evs_event_id`, `evs_event_type_num`, `evs_severity`, `evs_message` |
| MGR | `mgr_mode_num`, `mgr_mode` |
| LC | `lc_ap_num`, `lc_ap_from`, `lc_ap_to` |
| HS | `hs_failed_app`, `hs_cpu_pct`, `hs_cpu_util_scaled`, `hs_cpu_util_peak` |
| EPS | `eps_solar_power_w`, `eps_power_used_w`, `eps_battery_wh`, `eps_battery_mv`, `eps_solar_array_enabled`, `eps_battery_soc_pct`, `eps_power_balance_w` |
| GPS | `gps_abs_time`, `gps_ecef_x/y/z`, `gps_lat`, `gps_lon`, `gps_alt`, `gps_valid` |
| IMU | `imu_acc_x/y/z`, `imu_gyro_x/y/z` |
| MAG | `mag_x`, `mag_y`, `mag_z` |
| FSS | `fss_valid`, `fss_alpha`, `fss_beta` |
| Star Tracker | `st_valid`, `st_q0`, `st_q1`, `st_q2`, `st_q3` |
| Reaction Wheel | `rw_momentum` |
| Torquer | `torquer_dipole_x`, `torquer_dipole_y`, `torquer_dipole_z` |
| Truth42 | `t42_pos_x/y/z`, `t42_vel_x/y/z`, `t42_q0-3`, `t42_svb_x/y/z`, `t42_gyro_x/y/z` |
| CPU monitor | `TOTAL_CPU_PCT`, `CPU_PCT`, `NUM_PIDS`, `CMDCOUNTER`, `ERRCOUNTER` |

---

### 9. Kibana Index-Pattern Fix

**What:** Recreated the `nos3-telemetry*` index pattern that all dashboards reference.

**Why:** Every Kibana dashboard panel references an index pattern by its internal UUID (e.g. `03771670-2663-11f1-9950-757ed527c264`). This UUID is stored in the saved-object database. If the index pattern object is deleted (which happens when Kibana's saved-object store is wiped or the ELK stack is rebuilt from scratch), every panel that references that UUID shows a "Could not locate that index-pattern" error and renders nothing.

**Root cause:** The index pattern UUID `03771670-2663-11f1-9950-757ed527c264` had been deleted. Recreating an index pattern via the Kibana UI generates a new random UUID, which does not match what the dashboard panels reference. The fix is to recreate the index pattern with the **exact same UUID** that the dashboards were built against.

**Fix applied:** The index pattern was recreated via the Kibana Saved Objects API (`POST /api/saved_objects/index-pattern/<UUID>`) pointing at the `nos3-telemetry*` wildcard index pattern, restoring the reference for all dashboards simultaneously without needing to re-import or re-create each panel.

**Prevention:** Before wiping the ELK stack, export saved objects from Kibana (`Stack Management → Saved Objects → Export all`). The export includes the index pattern UUID, and re-importing it after restart restores all references.

### 10. Bracket-stripping UART parsers (GNSS, TT&C; 2026-04-29)

**Files:**
- `nos3/components/generic_gnss/sim/src/generic_gnss_data_point.cpp`
- `nos3/components/generic_tt_c/sim/src/generic_tt_c_data_point.cpp`

**Why:** 42 emits vector values as `[x y z]`, square brackets included. The `do_parsing()` routine in both data-point classes feeds the value string into a `stringstream >> double` chain, which fails on the leading `[` and leaves the field marked `INVALID` *forever*, even when the line arrives. Truth42 sidesteps this with `Sim42DataPoint::parse_double_vector()`; these two DTU components do not.

**Fix:** the comma sanitiser at the start of `do_parsing` was extended to also strip `[` and `]`:

```cpp
if (sP[i] == ',' || sP[i] == '[' || sP[i] == ']') sP[i] = ' ';
```

The TT&C parser additionally got an empty-string guard in commit `c6c79140` (2026-05-03) so that an empty value does not spin the loop on garbage. Without these, the FSW HK still ticks but lat/lon and link-state stay at zero.

### 11. GS Hello-Flow Responder + ECHO_TIMEOUT_S=5.0 (2026-05-03)

**Files:**
- `nos3/gsw/cosmos/COMPONENTS/GENERIC_GNSS/lib/gnss_gs_responder_task.rb`
- Commit `8a45426f`: bumped `ECHO_TIMEOUT_S` from 2.0 -> 3.0 -> 5.0; commit `7eaca741`: journalled the rationale.

**Why:** the GS responder polls GNSS HK, dispatches `BEACON_PING_CC`, and waits for the round-trip ack. Initial timeout of 2.0 s was below the observed echo latency: most ports closed in 3 - 5 s due to LEO jitter plus the re-poll cycle when a mirror packet arrived late. The empirical p90 from in-run measurements was 2.93 s; pinning the timeout at 5.0 s puts it above p90 and below the 5.0 s `POLL_INTERVAL_S` so a slow echo does not stack with the next tick.

**Code shape:**

```ruby
ECHO_TIMEOUT_S = 5.0
# ...
send_time = Time.now                    # captured immediately after cmd dispatch
deadline = send_time + ECHO_TIMEOUT_S
# ...
elapsed_ms = ((Time.now - send_time) * 1000).round
log "ping seq=#{seq} elapsed_ms=#{elapsed_ms} status=ok"
```

The elapsed_ms is emitted in both success and timeout log lines so future tuning can re-measure without code changes.

### 12. CmdTlmServerHeadless + Mirror Socket plumbing (2026-05-03)

**Files:**
- `nos3/gsw/cosmos/tools/CmdTlmServerHeadless` (new, +28 lines)
- `nos3/scripts/ci_launch.sh` (cosmos branch)
- `nos3/scripts/god_view_capture.py` (mirror socket + NaN scrub)

**Why:** the COSMOS image's apt snapshot could not install `xvfb`, so `xvfb-run ruby CmdTlmServer` never started; the Qt-dependent CmdTlmServer never came up; the GS responder had no place to send commands. The headless wrapper bypasses Qt/Xvfb and runs the same Ruby logic.

The mirror-socket half: COSMOS's DEBUG listener on UDP 5013 sits inside the cosmos container's netns. `god_view_capture.py` was sending downlink only to the host, so COSMOS got nothing. Mirror duplication via `NOS3_TLM_MIRROR_DEST` env var sends a second copy to the COSMOS container's mapped UDP port (`-p 5113:5013/udp`).

NaN-scrub: a separate small fix in `god_view_capture.py` checks `math.isnan(x)` before serialising fields. Without it, NaN values from 42 in early ticks broke Logstash's JSON parser and the whole packet was dropped.

**Verification numbers from the post-fix run (logged 2026-05-03 ~02:45 UTC):**
- cmd_count advanced from 1 -> 39
- 11 GS_PING events appeared
- ping_count and last_ping_seq populated in dashboard
- 39 round-trip-closed log lines
- 68 docs in Denmark box, 31 in SCIENCE mode

---

## Component Walkthroughs  `[L3]`

One subsection per active component. Each walkthrough names the abbreviation in full, the simulator and FSW source paths, the MIDs the component participates in, and a representative function-call chain. MIDs cited here are stable hand-checked values; for the full live registry consult `nos3/cfg/build/mid_registry.json`.

### EPS -- Electrical Power System

- **Sim:** `nos3/components/generic_eps/sim/src/generic_eps_sim.cpp` (library `libgeneric_eps_sim.so`).
- **FSW app:** `nos3/components/generic_eps/fsw/cfs/src/generic_eps_app.c`.
- **MIDs:** `GENERIC_EPS_CMD_MID` (cmd), `GENERIC_EPS_REQ_HK_MID` (request HK), `GENERIC_EPS_HK_TLM_MID` (response HK), `GENERIC_EPS_DEVICE_TLM_MID` (raw device data).
- **Truth feed:** From 42 on `fortytwo:4283` -- consumes `SC[0].svb` (Sun-vector body), `SC[0].PosR` (position relative), `SC[0].qn` (attitude quaternion), `Orb[0].PosN` (Sun direction in ECI). Used to compute solar-array illumination, hence solar power generation.
- **Bus:** I2C (typical). Address declared in EPS sim XML.
- **Sample call chain on a HK request:**
  1. SCH publishes `GENERIC_EPS_REQ_HK_MID` on the SB.
  2. `GENERIC_EPS_AppMain()` returns from `CFE_SB_ReceiveBuffer(...)` with the request packet.
  3. `GENERIC_EPS_ProcessGroundCommand()` dispatches on the request and calls `GENERIC_EPS_DEVICE_ReadData(...)`.
  4. HWLIB issues an I2C read; the EPS sim returns simulated battery voltage, solar array voltage, bus currents.
  5. App fills `GENERIC_EPS_Hk_tlm_t` and calls `CFE_SB_TransmitMsg(...)` on `GENERIC_EPS_HK_TLM_MID`.
- **Watched by LC:** WP0 (battery voltage < 14800 mV), WP1 (battery voltage > 22200 mV), WP5 (solar-array voltage < 100 mV).

### GPS -- Global Positioning System (NovAtel OEM615)

Already covered in detail in the "Orbit and Position Initialisation" section. Summary:

- **Sim:** `nos3/components/novatel_oem615/sim/src/gps_sim_*.cpp`. Provider type `GPS42SOCKET` connects to `fortytwo:4245`.
- **FSW app:** `nos3/components/novatel_oem615/fsw/cfs/src/novatel_oem615_app.c` + child task in `novatel_oem615_child.c`.
- **MIDs:** `NOVATEL_OEM615_CMD_MID`, `NOVATEL_OEM615_REQ_HK_MID`, `NOVATEL_OEM615_HK_TLM_MID`, `NOVATEL_OEM615_DEVICE_TLM_MID`.
- **Bus:** UART (`usart_1`, port 1).
- **Watched by LC:** WP3 (alt < 400 km), WP6-WP9 (Denmark lat/lon box).

### GNSS -- Generic GNSS Receiver (DTU custom)

- **Sim:** `nos3/components/generic_gnss/sim/src/generic_gnss_data_point.cpp` + the simulator wrapper. Connects to **fortytwo:4287** with the load-bearing tight prefix `SC[0].GPS[0].PosW` (count = 1). Bracket-stripping in `do_parsing()` is mandatory (see section 6.10 above).
- **FSW app:** `nos3/components/generic_gnss/fsw/cfs/src/generic_gnss_app.c`.
- **MIDs:** topic IDs `0x52` / `0x53` / `0x54` resolved via `0x1900 | topic` to CMD `0x1952`, HK TLM `0x0952`, Device TLM `0x0953`, Sat-Hello TLM `0x0954`.
- **GS Hello-Flow integration:** the cosmos-side `gnss_gs_responder_task.rb` polls this app's HK; on each tick, dispatches `BEACON_PING_CC` and waits up to `ECHO_TIMEOUT_S = 5.0` s for the response. The closed loop is the canonical liveness check.
- **ELK fields:** `gps_lat`, `gps_lon`, `gps_alt`, `gps_ecef_x/y/z`, `gps_abs_time`, plus the post-2026-05-03 additions: `gnss_lat`, `gnss_lon`, `in_denmark_box`, `last_ping_seq`, `last_ping_time`, `ping_count`. The dashboard's GNSS-GS-validation AND-gate panel and the freshness tile row consume these fields.

### TT&C -- Telemetry Tracking & Command Radio (DTU custom)

- **Sim:** `nos3/components/generic_tt_c/sim/src/generic_tt_c_data_point.cpp` + simulator wrapper. Connects to **fortytwo:4286** with the load-bearing tight prefix `SC[0].GPS[0].PosW` + `SC[0].GPS[0].VelW` (count = 2). Bracket-stripping plus an empty-string guard (commit `c6c79140`) are mandatory.
- **FSW app:** `nos3/components/generic_tt_c/fsw/cfs/src/generic_tt_c_app.c`.
- **MIDs:** topic IDs `0x50` / `0x51` resolved to CMD `0x1950`, HK TLM `0x0950`, Device TLM `0x0951`.
- **HK fields:** `ttc_status_code`, `ttc_link_state`, `ttc_downlink_rate_bps`. Dashboard panel "TT&C Downlink Status" added in commit `6c6b2029`.
- **Operational role:** carries the link-state used by the dashboards to colour the AOS / LOS chips. Does not itself enforce SDLS / CryptoLib (that path is in `ci` / `to`).

### Beacon -- GS Hello-Flow ack source (DTU custom)

- **FSW app:** `nos3/fsw/apps/beacon_app/`. Listed as `OFF_APP` in the startup script (`cfe_es_startup.scr` line 11); enabled only for the GS Hello-Flow loop tests.
- **MIDs:** CMD `0x18F0`, send-HK `0x18F1`, HK TLM `0x08F0`.
- **Behaviour:** handler for `BEACON_PING_CC` increments an in-RAM `pingcounter` and emits an EVS `INFORMATION` line in `key=value` shape so the Logstash kv filter can extract `pingcounter`, `cmdcounter`, and `errcounter` straight into the index.
- **Role in Hello-Flow:** beacon is the round-trip ack source. Without it loaded the GS responder times out every cycle and the dashboard's `last_ping_seq` never advances. A flat counter is the canonical link-down signal.

### IMU -- Inertial Measurement Unit

- **Sim:** `nos3/components/generic_imu/sim/src/generic_imu_sim.cpp`. Truth feed: `fortytwo:4281` (`SC[0].Accel`, `SC[0].Gyro`).
- **FSW app:** `nos3/components/generic_imu/fsw/cfs/src/generic_imu_app.c`.
- **MIDs:** `GENERIC_IMU_CMD_MID`, `GENERIC_IMU_REQ_HK_MID`, `GENERIC_IMU_HK_TLM_MID`, `GENERIC_IMU_DEVICE_TLM_MID`.
- **Bus:** I2C.
- **Output:** Linear acceleration (m/s^2) and angular rate (rad/s) per axis. Logstash extracts as `imu_acc_x/y/z`, `imu_gyro_x/y/z`.

### MAG -- Magnetometer

- **Sim:** `nos3/components/generic_mag/sim/src/generic_mag_sim.cpp`. Truth feed: `fortytwo:4234` (`SC[0].MAG`). 42 derives the field from IGRF (8x8 truncation, see `Inp_Sim.txt`).
- **FSW app:** `nos3/components/generic_mag/fsw/cfs/src/generic_mag_app.c`.
- **MIDs:** `GENERIC_MAG_REQ_HK_MID`, `GENERIC_MAG_HK_TLM_MID`, `GENERIC_MAG_DEVICE_TLM_MID`.
- **Bus:** I2C.
- **Output:** Magnetic field vector in body frame, in tesla (often scientific notation like 8.44e-07).

### CSS -- Coarse Sun Sensor

- **Sim:** `nos3/components/generic_css/sim/src/generic_css_sim.cpp`. Truth feed: `fortytwo:4227` (`SC[0].CSS`).
- **FSW app:** `nos3/components/generic_css/fsw/cfs/src/generic_css_app.c`.
- **MIDs:** `GENERIC_CSS_REQ_HK_MID`, `GENERIC_CSS_HK_TLM_MID`, `GENERIC_CSS_DEVICE_TLM_MID`.
- **Bus:** I2C.
- **Output:** 6 photodiode currents (analog ADC counts); ADCS infers Sun direction by ratiometric comparison.

### FSS -- Fine Sun Sensor

- **Sim:** `nos3/components/generic_fss/sim/src/generic_fss_sim.cpp`. Truth feed: `fortytwo:4284` (`SC[0].FSS[0]`).
- **FSW app:** `nos3/components/generic_fss/fsw/cfs/src/generic_fss_app.c`.
- **MIDs:** `GENERIC_FSS_REQ_HK_MID`, `GENERIC_FSS_HK_TLM_MID`, `GENERIC_FSS_DEVICE_TLM_MID`.
- **Output:** Validity flag plus alpha/beta angles to the Sun in radians. Logstash extracts as `fss_valid`, `fss_alpha`, `fss_beta`.

### ADCS -- Attitude Determination and Control System

- **Sim:** `nos3/components/generic_adcs/sim/src/generic_adcs_sim.cpp`. Reads truth attitude from 42 via the truth fan-out (`fortytwo:9999`).
- **FSW app:** `nos3/components/generic_adcs/fsw/cfs/src/generic_adcs_app.c`.
- **MIDs:** `GENERIC_ADCS_CMD_MID`, `GENERIC_ADCS_REQ_HK_MID`, `GENERIC_ADCS_HK_TLM_MID`.
- **Role:** Subscribes to IMU, MAG, CSS, FSS, ST telemetry; runs an attitude estimator and a controller; commands the reaction wheels and the torquer. Mode-aware: in SAFE it pursues sun-pointing; in SCIENCE it pursues nadir-pointing.

### RW -- Reaction Wheel

- **Sim:** `nos3/components/generic_reactionwheel/sim/src/generic_reactionwheel_sim.cpp`. Three independent wheel sims, each on its own pair of 42 ports (4277/4278 for wheel 0, 4377/4378 for wheel 1, 4477/4478 for wheel 2).
- **FSW app:** `nos3/components/generic_reactionwheel/fsw/cfs/src/generic_reactionwheel_app.c`.
- **MIDs:** `GENERIC_RW_CMD_MID`, `GENERIC_RW_APP_SEND_HK_MID`, `GENERIC_RW_APP_HK_TLM_MID`, `GENERIC_RW_DEVICE_TLM_MID`.
- **Watched by LC:** WP4 (DeviceEnabled == 0).

### Star Tracker (ST)

- **Sim:** `nos3/components/generic_startracker/sim/src/generic_startracker_sim.cpp`. **Synthetic** mode in EO1: the simulator generates an attitude quaternion internally rather than reading from 42, because IPC port 4282 is `OFF` (see Inp_IPC.txt entry 16). This is the load-bearing decision documented in the project root notes and `debug/journal.md`.
- **FSW app:** `nos3/components/generic_startracker/fsw/cfs/src/generic_star_tracker_app.c`.
- **MIDs:** `GENERIC_STAR_TRACKER_REQ_HK_MID`, `GENERIC_STAR_TRACKER_HK_TLM_MID`, `GENERIC_STAR_TRACKER_DEVICE_TLM_MID`.

### Torquer -- Magnetic Torquer

- **Sim:** `nos3/components/generic_torquer/sim/src/generic_torquer_sim.cpp`. Connects to 42 on `fortytwo:4279` (RX direction; the simulator pushes commanded dipole vectors into 42 so 42 can compute the resulting torque).
- **FSW app:** `nos3/components/generic_torquer/fsw/cfs/src/generic_torquer_app.c`.
- **MIDs:** `GENERIC_TORQUER_CMD_MID`, `GENERIC_TORQUER_REQ_HK_MID`, `GENERIC_TORQUER_HK_TLM_MID`, `GENERIC_TORQUER_DEVICE_TLM_MID`.
- **Used by:** RTS 003 to dump reaction-wheel momentum (10-second pulse).

### Radio

- **Sim:** `nos3/components/generic_radio/sim/src/generic_radio_sim.cpp`. Communicates with the OnAir simulator and (optionally) the CryptoLib container.
- **FSW app:** `nos3/components/generic_radio/fsw/cfs/src/generic_radio_app.c`.
- **MIDs:** `GENERIC_RADIO_CMD_MID`, `GENERIC_RADIO_REQ_HK_MID`, `GENERIC_RADIO_HK_TLM_MID`, `GENERIC_RADIO_DEVICE_TLM_MID`.
- **Note:** A Logstash filter tags `sim_connectivity_warning` whenever the radio sim cannot resolve `cryptolib` by hostname, which is expected in minimal-config runs.

### MGR -- Mission Manager (DTU custom)

- **Sim:** none -- MGR is FSW-only.
- **FSW app:** `nos3/components/mgr/fsw/cfs/src/mgr_app.c`.
- **MIDs:** `MGR_CMD_MID`, `MGR_REQ_HK_MID`, `MGR_HK_TLM_MID`.
- **Command codes:** `MGR_NOOP_CC`, `MGR_RESET_CC`, `MGR_SET_MODE_CC`. Payload of `MGR_SET_MODE_CC` is one byte: `MGR_SAFE_MODE` (0) or `MGR_SCIENCE_MODE` (1).
- **Behaviour:** On receipt of `MGR_SET_MODE_CC`, validates the payload, updates internal `SpacecraftMode` field (offset 14 in HK), emits an EVS INFORMATION event, and may publish enable/disable commands to other components based on mode policy.

### CF -- CFDP (CCSDS File Delivery Protocol)

- **FSW app:** `nos3/fsw/apps/cf/...` (cFS-Heritage).
- **MIDs:** `CF_CMD_MID`, `CF_HK_TLM_MID`, plus PDU MIDs.
- **Role:** Reliable file transfer to ground. Used to downlink stored DS files.

### DS -- Data Storage

- **FSW app:** `nos3/fsw/apps/ds/...` (cFS-Heritage).
- **MIDs:** `DS_CMD_MID`, `DS_HK_TLM_MID`, plus filter-table-driven subscription to mission telemetry MIDs.
- **Role:** Records selected SB packets to disk for later downlink. Filter table determines which MIDs to log and which file to log to.

### FM -- File Manager

- **FSW app:** `nos3/fsw/apps/fm/...` (cFS-Heritage). Filesystem operations (mkdir, rm, ls, mv).

### LC -- Limit Checker

Already covered in detail (sections 4 and 5 below). Watches packets, evaluates Reverse Polish Notation expressions over watchpoints, fires actionpoints.

### SC -- Stored Command

Already covered in detail (section 6 below). Loads RTS / ATS tables at boot, fires stored sequences on demand.

### Sample -- NOS3 reference component

- **Sim:** `nos3/components/sample/sim/src/sample_sim.cpp`. Truth: minimal Sun-body-vector synthesis.
- **FSW app:** `nos3/components/sample/fsw/cfs/src/sample_app.c`.
- **MIDs:** `SAMPLE_CMD_MID`, `SAMPLE_REQ_HK_MID`, `SAMPLE_HK_TLM_MID`, `SAMPLE_DEVICE_TLM_MID`.
- **Role:** Reference implementation; useful as a copy-paste template when adding a new component.

### OnAir

- **Sim:** `nos3/components/onair/sim/src/...`. Simulates the over-the-air radio link (path loss, link budget, packet drop). Counterparty for the Radio component.

### Truth42 -- 42 truth fan-out

- **Sim:** `nos3/sims/truth_42_sim/src/truth_42_data_provider.cpp`. Connects to `fortytwo:9999`, consumes `SC[0]`, `Orb[0]` prefixes, and forwards onward to Cosmos at `cosmos:5111`.
- **Role:** Lets COSMOS show 6-DOF truth (position, velocity, attitude quaternion, sun-body vector) alongside FSW's reported state. Logstash extracts as `t42_pos_x/y/z`, `t42_vel_x/y/z`, `t42_q0..q3`, `t42_svb_x/y/z`, `t42_gyro_x/y/z`.

---

## How to Rebuild After Table Changes

All FSW table `.c` files must be compiled into binary `.tbl` files before they take effect. The cFS table loader reads the binary `.tbl` files, not the C source.

```bash
cd nos3
make config          # copies cfg/nos3_defs/ → cfg/build/nos3_defs/ and re-generates scripts
make build-fsw       # compiles all table .c files + the full FSW image inside Docker
make stop
make launch          # starts the full NOS3 stack with new tables loaded
```

The `make config` step is required if new table files were added (e.g. `sc_rts002.c`, `sc_rts004.c`) -- it regenerates the `targets.cmake` file that tells CMake which table sources to compile.

To start only the ELK stack (after editing `logstash.conf`):
```bash
cd nos3/elk && docker compose up -d
# Or to reload Logstash without restarting Elasticsearch:
docker restart nos3-logstash
```

---

## How to Derive a New Mission

### Step 1: Define your mission concept

Answer these questions before touching any file:
- What modes will the spacecraft have?
- What conditions trigger mode transitions?
- Which hardware components are active in each mode?
- What health conditions should trigger autonomous response?
- What telemetry fields matter for mission success evaluation?

### Step 2: Add new component HK requests to SCH tables

For each hardware component that needs to send HK:
1. Find its `REQ_HK_MID` or `SEND_HK_MID` in `nos3/components/<name>/fsw/cfs/inc/<name>_msgids.h`
2. Add a message entry in `sch_def_msgtbl.c` (next unused index)
3. Add a schedule entry in `sch_def_schtbl.c` (next unused slot) pointing to that index

### Step 3: Aggregate mission-critical fields into the HK combined packet

For each field LC needs to watch:
1. Open the component's `*_msg.h` and find the field in the HK struct
2. Calculate its byte offset from the start of the struct (add 12 for the CCSDS header)
3. Add a copy entry in `hk_cpy_tbl.c` with that source offset and the next available destination offset in `HK_COMBINED_PKT1_MID`
4. Track destination offsets in a comment or spreadsheet -- they must not overlap

### Step 4: Define LC watchpoints for each mission condition

For each condition that should trigger autonomous response:
1. Identify which packet contains the field and its byte offset (from Step 3 or directly from the component HK packet)
2. Choose the data type (`LC_DATA_UWORD_LE`, `LC_DATA_FLOAT_LE`, etc.)
3. Choose the comparison operator (`LC_OPER_LT`, `LC_OPER_GT`, `LC_OPER_EQ`)
4. Add a watchpoint entry in `lc_def_wdt.c` (next unused index)

### Step 5: Wire actionpoints to SC sequences

For each watchpoint that should trigger an automated response:
1. Decide which RTS number to use (pick one not already taken by another AP)
2. Add an actionpoint entry in `lc_def_adt.c` with the watchpoint's RPN equation and RTSId
3. Create the corresponding `sc_rts<NNN>.c` file following the template in Step 6

### Step 6: Write SC sequences

For each RTS:
1. Create `nos3/cfg/nos3_defs/tables/sc_rts<NNN>.c` using the template pattern above
2. Include the header files for each target app (`*_msgids.h`, `*_msg.h`, `*_app.h`)
3. Define the struct with one `SC_RtsEntryHeader_t` + command struct pair per command
4. Set `WakeupCount` relative to the previous command (0 for the first command)
5. Register with `CFE_TBL_FILEDEF`

### Step 7: Add Logstash parsers for new telemetry fields

For each new numeric field you want visible in Kibana:
1. Determine which log source contains it (`omni_logs/*.log` or `cfs_god_view.json`)
2. Add a `grok` pattern to extract the raw string value
3. Add a `mutate { convert => { "field_name" => "float" } }` block to type it as numeric
4. Restart Logstash: `docker restart nos3-logstash`

### Step 8: Rebuild FSW and verify

```bash
cd nos3
make config && make build-fsw
make stop && make launch
# Check that god_view capture started:
tail -f omni_logs/god_view.log
# Check that Logstash is parsing without errors:
docker logs nos3-logstash --tail 50
# Verify data flows into Elasticsearch:
curl http://localhost:9200/nos3-telemetry-*/_count
```

---

## Verification Procedures

How a future reader can confirm the mission is still working as this document describes. None of these steps modifies anything; they are read-only health checks.

### V1. Verify the active configuration

```bash
grep -E '<gsw>|<fsw>|<scenario>|<sc-1-cfg>|<start-time>' nos3/cfg/nos3-mission.xml
grep -E '<enable>' nos3/cfg/spacecraft/sc-mission-config.xml
```

Expect `gsw=cosmos`, `fsw=cfs`, `scenario=STF1`, `sc-1-cfg=spacecraft/sc-mission-config.xml`, `start-time=830217600.0`. Component enables should match the list in section 2.1 above. If they differ, the active mission has drifted from this document's assumptions.

### V2. Verify the orbit

```bash
sed -n '11,20p' nos3/cfg/InOut/Orb_LEO.txt
```

Expect Inclination `60.0`, RAAN `346.0`, True Anomaly `61.6`. Any drift here will move the ground track and may cause the Denmark window to shift in time or miss entirely.

### V3. Verify the IPC entry 16 is OFF

```bash
grep -A1 "Star Tracker IPC" nos3/cfg/InOut/Inp_IPC.txt
```

Expect mode `OFF`. If it is `TX`, 42 will hang on startup and the whole mission will fail to launch. This is the load-bearing fix documented in the project root notes and `debug/journal.md`.

### V4. Run the mission and watch for the GPS feed

```bash
cd nos3
make config && make launch
```

Then in another shell:

```bash
tail -f nos3/omni_logs/nos3-gps.log | grep "Geodetic"
```

Within roughly 10-30 seconds of 42 connecting (the DTU 180-attempt retry budget allows up to 3 minutes for this), the values should switch from all-zero to non-zero. The first non-zero values will be approximately `0.872, -0.052, 400000` (radians, radians, metres -- which is ~50 deg N, -3 deg W, 400 km).

### V5. Verify the Denmark overfly via Kibana

Open `http://localhost:5601` -> Discover -> select the `nos3-telemetry-*` data view. Run the KQL filter:

```
gps_lat >= 55 and gps_lat <= 58 and gps_lon >= 8 and gps_lon <= 16
```

Expect at least one matching document within the first ~140 seconds of run start. If the Time picker is set to "Last 15 minutes" and there are no hits after 3 minutes, suspect:

1. 42 connection failed (check `nos3/omni_logs/nos3-gps.log` for `Successfully connected TELEMETRY`).
2. Logstash is not parsing GPS lines (`docker logs nos3-logstash --tail 50` for grok failures).
3. The Kibana index pattern UUID is missing again (see section 9 above).

### V6. Verify the mode change fires

```bash
grep -E "MGR_SET_MODE|SET_MODE_CC|0x18A8" nos3/attack_logs/cfs_god_view.json | head -5
```

Within a few minutes of run start, expect a packet on `MGR_CMD_MID` carrying the mode-change command. The EVS event in Kibana ("MGR: Set mode command received [1]") confirms MGR accepted it.

### V7. Verify the GS Hello-Flow round-trip

The closed-loop liveness check added on 2026-05-03. Run the sim for at least 10 minutes, then query Kibana for the Hello-Flow validation fields:

```bash
# In Kibana Discover (or via the saved-search REST API):
#   index = nos3-telemetry-*
#   filter = ping_count > 0 AND last_ping_seq exists
# Expect: a non-empty hit list with last_ping_seq advancing across the window.
```

Or, if checking from the host shell:

```bash
curl -s 'http://localhost:9200/nos3-telemetry-*/_search?q=ping_count:>0&size=1&sort=@timestamp:desc' \
  | jq '.hits.hits[0]._source | {last_ping_seq, ping_count, in_denmark_box, in_science_mode, "@timestamp"}'
```

The GNSS GS validation AND-gate panel turns green only when `last_ping_seq` is advancing AND `ping_count > 0` AND `in_denmark_box == true` (only during Denmark passes; the AND gate is dark outside passes by design). The freshness tile row should show all loaded components within the 30 s freshness window.

If V7 fails, the typical chain is: COSMOS CmdTlmServer not started (check `CmdTlmServerHeadless` log) -> mirror socket not configured (check `NOS3_TLM_MIRROR_DEST` env var in cosmos container) -> beacon_app not loaded (check `cfe_es_startup.scr` for `CFE_APP, beacon_app`).

### V8. Cross-check this document against current code

Each citation in this document includes a file path; running `grep -n "<symbol>" <path>` should still produce a hit. Specifically:

- `connect_reader_thread_as_42_socket_client` in `gps_sim_data_42socket_provider.cpp`.
- `SimCoordinateTransformations::ECEF2LLA` in `nos3/sims/sim_common/src/sim_coordinate_transformations.cpp`.
- `MGR_SET_MODE_CC` in `nos3/components/mgr/fsw/cfs/inc/mgr_app.h`.
- `WP6` and `AP3` in `nos3/cfg/nos3_defs/tables/lc_def_wdt.c` and `lc_def_adt.c`.
- `ECHO_TIMEOUT_S` in `nos3/gsw/cosmos/COMPONENTS/GENERIC_GNSS/lib/gnss_gs_responder_task.rb`.
- `NOS3_TLM_MIRROR_DEST` in `nos3/scripts/god_view_capture.py`.
- `CmdTlmServerHeadless` at `nos3/gsw/cosmos/tools/CmdTlmServerHeadless`.

---

## Known Gaps (EO1 Baseline -- Updated)

The following items were completed in the session that produced this version of the guide:

| Item | Status |
|------|--------|
| WP3 (GPS altitude) | **Complete** -- `lc_def_wdt.c` WP3, offset 82, float LE < 400000 m |
| WP4 (RW offline) | **Complete** -- `lc_def_wdt.c` WP4, offset 20, uint8 == 0 |
| WP5 (solar array off) | **Complete** -- `lc_def_wdt.c` WP5, offset 28, uint16 < 100 mV |
| AP2 (RW_OFFLINE) | **Complete** -- `lc_def_adt.c` AP2, WP4 → RTS 003 |
| WP6–WP9 (Denmark lat/lon box) | **Complete** -- `lc_def_wdt.c` WP6–WP9, GPS offsets 74 (lat) and 78 (lon) |
| AP3 (SCIENCE_OVERFLY) | **Complete** -- `lc_def_adt.c` AP3, WP6 AND WP7 AND WP8 AND WP9 → RTS 002 |
| RTS 002 (SCIENCE mode) | **Complete** -- `sc_rts002.c` sends MGR SET_MODE SCIENCE; fired by AP3 or ground |
| RTS 003 (RW desaturation) | **Complete** -- `sc_rts003.c` torquer enable at t=0, disable at t=10s |
| RTS 004 (health response) | **Complete** -- `sc_rts004.c` sends MGR SET_MODE SAFE |
| god_view hardcoded IP | **Complete** -- `docker inspect` dynamic discovery with static fallback |
| god_view auto-start | **Complete** -- `make launch` starts it 10 s after boot |
| Kibana index-pattern broken | **Complete** -- recreated with original UUID via Saved Objects API |
| Logstash boot noise | **Complete** -- startup-sync and cFE-timeout messages dropped |

**Closed by post-thesis commits (2026-05-01 to 2026-05-03):**

| Item | Closing commit | Notes |
|------|----------------|-------|
| GS round-trip not yet verified | `bc6369c8`, `8a45426f`, `7afa0107` | Hello-Flow loop closes; AND-gate panel + freshness tiles validate it; ECHO_TIMEOUT_S=5.0 tuned to empirical p90 |
| COSMOS CmdTlmServer fails on apt-snapshot | `c6c79140` (CmdTlmServerHeadless) + `f45ad877` | Pure-Ruby headless wrapper; mirror socket via `NOS3_TLM_MIRROR_DEST` |
| ADCS sensor wakeup / FOV-exit recovery / AP4 retargeting | `8af2fdea` | LC ADT/WDT updated; SC RTS extended; the "HS -> SC integration" gap collapsed into AP4 |
| MGR mode-driven sensors | `b8da042d` | MGR auto-enables ADCS sensors at boot and drives ADCS sub-modes from spacecraft mode |
| EPS eclipse hysteresis oscillation | `1b441712` | 90 - 100 % SOC hysteresis band + tuned battery capacity; visible eclipse drop on dashboard |
| Kibana dashboards (Mission Validation + Telemetry Overview) | `0dcccd58`, `7afa0107` | Two curated dashboards rebuilt from python source; freshness tiles + spacecraft-mode chip context |

**Remaining gaps:**

| Gap | What's Missing | Files to Create/Modify |
|-----|---------------|----------------------|
| SCIENCE mode full activation | RTS 002 sends mode command and the ADCS sub-mode now follows MGR (commit `b8da042d`); DS science filter and CF downlink schedule still pending | Extend `sc_rts002.c` with DS and CF commands |
| AP for WP3 / WP5 | Orbit-low and solar-array-off are alert-only; no automated recovery RTS is wired | Add APs in `lc_def_adt.c`; create recovery RTS files |
| `lc_def_adt.c` / `lc_def_wdt.c` working-tree dirty | Pending changes from the ADCS sweep are uncommitted as of 2026-05-03 | Commit the in-progress edits or revert; either way reconcile against the current SC RTS table |

---

## Quick Reference: Key File Locations

| Purpose | File |
|---------|------|
| SCH message table (what commands exist) | `nos3/cfg/nos3_defs/tables/sch_def_msgtbl.c` |
| SCH schedule table (when commands fire) | `nos3/cfg/nos3_defs/tables/sch_def_schtbl.c` |
| HK copy table (field aggregation) | `nos3/cfg/nos3_defs/tables/hk_cpy_tbl.c` |
| LC watchpoint table (threshold definitions) | `nos3/cfg/nos3_defs/tables/lc_def_wdt.c` |
| LC actionpoint table (response rules) | `nos3/cfg/nos3_defs/tables/lc_def_adt.c` |
| SC sequences | `nos3/cfg/nos3_defs/tables/sc_rts001.c` … `sc_rts004.c` |
| Logstash telemetry parsing pipeline | `nos3/elk/logstash.conf` |
| god-view SB capture script | `nos3/scripts/god_view_capture.py` |
| Component MID header files | `nos3/components/<name>/fsw/cfs/inc/<name>_msgids.h` |
| Component message struct definitions | `nos3/components/<name>/fsw/cfs/inc/<name>_msg.h` |
| Mission manager app source | `nos3/components/mgr/fsw/cfs/` |
| ELK stack compose file | `nos3/elk/docker-compose.yml` |
| cFS app startup script (which apps load) | `nos3/cfg/nos3_defs/cpu1_cfe_es_startup.scr` |
| CMake target definitions | `nos3/cfg/nos3_defs/targets.cmake` |
| GS Hello-Flow responder (Cosmos side) | `nos3/gsw/cosmos/COMPONENTS/GENERIC_GNSS/lib/gnss_gs_responder_task.rb` |
| Cosmos CmdTlmServer headless wrapper | `nos3/gsw/cosmos/tools/CmdTlmServerHeadless` |
| god_view capture + telemetry mirror | `nos3/scripts/god_view_capture.py` (`NOS3_TLM_MIRROR_DEST`) |
| Container launch (cosmos branch) | `nos3/scripts/ci_launch.sh` |
| ELK index template (field mappings) | `nos3/elk/index_template.json` |
| Kibana index-pattern field-cache refresh | `nos3/elk/refresh_index_pattern_fields.py` |
| Kibana dashboards source (regenerable) | `nos3/elk/dashboards/nos3-eo1-dashboards.ndjson` |
| TT&C / GNSS UART parsers | `nos3/components/generic_tt_c/sim/src/generic_tt_c_data_point.cpp`, `nos3/components/generic_gnss/sim/src/generic_gnss_data_point.cpp` |
| cFS resource limits (per-layer + per-app) | `docs/cfs-resource-limits/` (split into `layers/` and `apps/`) |
