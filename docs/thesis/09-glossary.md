# 09. Glossary

This glossary defines every acronym and technical term used in
the testbed document set. Where a term has a corresponding
section in another thesis document, the cross-reference is
listed inline.

## Frameworks and platforms

| Term | Stands For | Description |
|---|---|---|
| **NOS3** | NASA Operational Simulator for Space Systems | NASA's open-source spacecraft simulation framework. Provides containerised flight software, hardware-bus simulators, and ground software around an extensible mission profile. |
| **cFS** | core Flight System | NASA's reusable flight-software framework. Runs as a single process inside the FSW container; loads cFS apps as shared objects. |
| **cFE** | core Flight Executive | The runtime layer of cFS: ES, EVS, SB, TBL, TIME services, and the OSAL OS-abstraction layer. |
| **OSAL** | Operating System Abstraction Layer | cFE's portability layer. Wraps POSIX in cFE-style APIs. The path-traversal CVE-2025-25371 lives here. |
| **42** | (proper noun) | NASA Goddard's open-source spacecraft dynamics simulator. Propagates orbit and attitude. The container hostname is `fortytwo`. |
| **NOS Engine** | NASA Operational Simulator Engine | The hardware-bus simulation layer (I2C, SPI, CAN, UART) inside NOS3. Brokers per-bus pub/sub between FSW HWLIB drivers and component simulators. |
| **HWLIB** | Hardware Library | The cFS-side abstraction that lets the same flight binary talk either to a real device or to a NOS Engine simulator. |
| **F'** | Fprime | A second flight-software framework, vendored under `nos3/fsw/fprime/`. Not used in the EO1 mission. |
| **OnAIR** | Onboard Artificial Intelligence Resilience | A cyber-physical resilience framework that runs alongside cFS. Enabled in the EO1 spacecraft profile (commit 5afaa4fa). |
| **SYNOPSIS / SYN** | Science Yield Improvement via Onboard Prioritisation of Science Information Streams | A JPL onboard prioritisation library. Has a known heap leak in `RESET_CC` (`syn_app.c:367`). |

## Ground software

| Term | Description |
|---|---|
| **COSMOS** | The default operator console used in the EO1 mission. Hosts the operator UI on port 2902; runs the uplink/downlink targets that talk to the FSW container. |
| **OpenC3** | A successor to COSMOS, available as an alternative GSW. Not used in the EO1 mission. |
| **Yamcs** | A Java-based mission control system, vendored under `nos3/gsw/yamcs/`. Available as an alternative GSW. |
| **AIT** | Ames-IT mission operations toolkit. Available as an alternative GSW. |
| **CryptoLib** | An SDLS-conforming encryption layer that wraps every CCSDS frame between COSMOS and the FSW. Lives in `nos3/components/cryptolib/`. |
| **SDLS** | Space Data Link Security | The CCSDS standard for over-the-air encryption that CryptoLib implements. |

## Telemetry and command

| Term | Description |
|---|---|
| **CCSDS** | Consultative Committee for Space Data Systems. The packet standard used for all SB messages and for ground-link traffic. |
| **MID** | Message ID. A 16-bit CCSDS stream identifier. The fork uses two bases: heritage cFS apps at `0x1800` (commands) / `0x0800` (telemetry); generic component apps at `0x1900` / `0x0900`. See `nos3/cfg/nos3_defs/cfe_msgid_api.h:18` (heritage) and `nos3/cfg/nos3_defs/nos3_component_msgid_mapping.h:19` (component). |
| **CC** | Command Code. An 8-bit secondary-header field that distinguishes commands within an app (for example `MGR_SET_MODE_CC`). |
| **HK** | Housekeeping. Periodic telemetry packet emitted by an app summarising its status and health counters. |
| **TLM** | Telemetry. Any packet flowing from the spacecraft to the ground (MID base `0x0xxx`). |
| **CMD** | Command. Any packet flowing from the ground to the spacecraft (MID base `0x1xxx`). |
| **WP** | Watchpoint. One LC threshold rule: which packet, which field, comparison operator, threshold value. |
| **AP** | Actionpoint. One LC response rule: which WPs trigger it, what RTS to start, how many failures before acting. |
| **RTS** | Real-Time Sequence. One numbered script stored in SC; a list of commands with relative timing. |
| **ATS** | Absolute Time Sequence. A longer SC script triggered by an absolute time. Not used in EO1. |
| **APID** | Application Process Identifier. The 11-bit field inside the CCSDS primary header that the SB uses for routing. |

## cFS service apps (heritage)

| App | Purpose |
|---|---|
| **ES** | Executive Services. Starts and stops apps; manages the CPU task list. |
| **EVS** | Event Services. Routes app log messages to ground and to Logstash. |
| **SB** | Software Bus. Pub/sub message router for inter-app communication. |
| **TBL** | Table Services. Manages configuration tables loaded from files. |
| **TIME** | Time Services. Maintains spacecraft clock; synced to NOS Engine. |
| **CF** | CFDP app. Transfers files to the ground using the CCSDS File Delivery Protocol. |
| **CI / CI_LAB** | Command Input. Receives commands from the GSW and injects them on the SB. |
| **CS** | Checksum app. Continuously checksums memory regions and code segments. |
| **DS** | Data Storage app. Records telemetry packets to files for later downlink. |
| **FM** | File Manager app. Filesystem operations (create, delete, rename, list). |
| **HK** | Housekeeping aggregator. Copies fields from many app HK packets into combined packets. |
| **HS** | Health & Safety app. Monitors apps (heartbeat, CPU load); restarts hung apps. |
| **LC** | Limit Checker app. Monitors telemetry against thresholds (WPs) and triggers responses (APs). |
| **MD** | Memory Dwell app. Periodically samples configurable memory addresses. |
| **MM** | Memory Manager app. Direct memory read/write by ground command. |
| **SBN / SBN_CLIENT** | Software Bus Network. Mirrors SB traffic across multi-FSW deployments. Single-FSW EO1 keeps it loaded but most peer-connect logging is filtered out. |
| **SC** | Stored Command app. Executes pre-loaded RTS scripts. |
| **SCH** | Scheduler app. Fires commands on a 1 Hz time-table (one major frame per second). |
| **TO / TO_LAB** | Telemetry Output. Collects packets from the SB and emits them on the GSW downlink. |

## DTU-altered hardware-component apps

Each row is a cFS app with a simulator counterpart. The "DTU
divergence" column points at the deep-dive in
[`../04-apps/`](../04-apps/).

| App | Subsystem | DTU divergence |
|---|---|---|
| **GENERIC_EPS** | Electrical Power System | Load-model extension, first-tick race fix. See [`04-apps/eps.md`](../04-apps/eps.md). |
| **GENERIC_GNSS** | GNSS receiver (new app) | Bracket-stripping data-point parser, IPC tight prefix. See [`04-apps/gnss.md`](../04-apps/gnss.md). |
| **GENERIC_TT_C** | Telemetry / telecommand (new app) | Same fix family as GENERIC_GNSS. See [`04-apps/tt_c.md`](../04-apps/tt_c.md). |
| **GENERIC_ADCS** | Attitude Determination and Control System | Resilience changes; SCH wakeup table entries. See [`04-apps/adcs.md`](../04-apps/adcs.md). |
| **GENERIC_REACTION_WHEEL** | Momentum-storage actuator | First-tick std::stof guard. See [`04-apps/reaction_wheel.md`](../04-apps/reaction_wheel.md). |
| **GENERIC_IMU** | Inertial Measurement Unit | Same first-tick stof guard. See [`04-apps/imu.md`](../04-apps/imu.md). |
| **GENERIC_MAG** | Magnetometer | Same first-tick stof guard. See [`04-apps/mag.md`](../04-apps/mag.md). |
| **GENERIC_CSS** | Coarse Sun Sensor | Stof fix variant. See [`04-apps/css.md`](../04-apps/css.md). |
| **GENERIC_TORQUER** | Magnetic torquer | Truth42 + Logstash parser. See [`04-apps/torquer.md`](../04-apps/torquer.md). |
| **GENERIC_THRUSTER** | Reaction control thruster | IPC OFF on port 4280 (load-bearing). See [`04-apps/thruster.md`](../04-apps/thruster.md). |
| **GENERIC_RADIO** | RF transceiver | (no per-app deep-dive; see customizations list) |
| **GENERIC_STAR_TRACKER** | Star tracker | Synthetic provider locked (port 4282 OFF, load-bearing). See `02-architecture.md` section 5 and [`07-customizations-vs-upstream.md`](../07-customizations-vs-upstream.md). |
| **GENERIC_FSS** | Fine Sun Sensor | (no per-app deep-dive) |
| **NOVATEL_OEM615** | Commercial GNSS receiver | (no per-app deep-dive) |
| **ARDUCAM** | Earth-observation camera | (no per-app deep-dive) |

## DTU custom apps

| App | Description |
|---|---|
| **MGR** | Mission Manager. DTU-authored cFS app that tracks spacecraft mode (SAFE / SCIENCE), enforces mode-based policies, persists state across reboot, and exposes a computed-MID API for cFS Draco compatibility. See [`../04-apps/mgr.md`](../04-apps/mgr.md). |
| **BEACON_APP** | Ping/pong telemetry app. DTU-authored. Used as a baseline for telemetry-injection attack experiments. See [`../04-apps/beacon_app.md`](../04-apps/beacon_app.md). |
| **NOISY_APP** | Attack-instrumentation app. DTU-authored. Spoofs EPS housekeeping packets to simulate a tampered hardware component. Documented as a one-paragraph entry in [`../07-customizations-vs-upstream.md`](../07-customizations-vs-upstream.md). |
| **BLACKBOARD** | Inter-app data exchange layer; modified by DTU to skip processing when a data point has no content. See [`../04-apps/blackboard.md`](../04-apps/blackboard.md). |
| **ONAIR** | Onboard AI Resilience framework, enabled by DTU in the default mission spacecraft config. See [`../04-apps/onair.md`](../04-apps/onair.md). |
| **SYN** | SYNOPSIS port. JPL onboard prioritisation. Documented as a deep-dive in [`../04-apps/syn.md`](../04-apps/syn.md). |

## Telemetry and observability

| Term | Description |
|---|---|
| **ELK** | Elasticsearch + Logstash + Kibana. The DTU-added telemetry analysis stack. |
| **Logstash** | Log ingestion and transformation service. Tails simulator and SB capture files; runs grok and mutate filters; writes structured documents to Elasticsearch. |
| **Elasticsearch** | Time-series and full-text database. Stores the parsed telemetry documents in daily-rolling indices `nos3-telemetry-YYYY.MM.DD`. |
| **Kibana** | Web dashboard. Renders saved Lens panels backed by Elasticsearch queries. URL `http://localhost:5601`. |
| **KQL** | Kibana Query Language. Filter syntax used in Kibana search bars and dashboard panels. |
| **grok** | Logstash plugin. Pattern-matching filter that extracts named fields from unstructured text. |
| **god view** | The full Software-Bus message log produced by `nos3/scripts/god_view_capture.py`. Lands at `attack_logs/cfs_god_view.json` and is tagged `software_bus` by Logstash. |

## Mission profile

| Term | Description |
|---|---|
| **EO1** | Earth Observation 1. The DTU mission name. A single-spacecraft Earth-observation CubeSat in low Earth orbit, with the orbit tuned for Denmark overfly. |
| **STF1** | Simulation-To-Flight 1. NASA IV&V's reference CubeSat scenario; the upstream NOS3 default. EO1 builds on top of STF1. |
| **SAFE / SCIENCE** | Spacecraft modes. SAFE is the default after boot or on fault; SCIENCE is the mission-active mode. The MGR app holds and enforces this. |
| **Denmark overfly** | The EO1 mission's autonomous trigger. When the GPS sub-satellite point enters a Denmark-bounding-box, MGR transitions SAFE -> SCIENCE for the duration of the pass. |
| **Svalbard / SvalSat** | Ground station added to 42 in commit `94f840af`. |
| **DTU Lyngby** | Ground station added to 42 in commit `94f940af`. The Lyngby station is the EO1 mission's primary downlink target. |

## Concepts that need a sentence

| Term | Description |
|---|---|
| **SoC** | State of Charge. Battery charge level expressed as a percentage of full capacity. The EPS app emits this on its HK packet. |
| **LEO** | Low Earth Orbit. Orbit below ~2000 km altitude. |
| **FOV** | Field of View. Angular range in which a sensor can make measurements. |
| **GSFC** | Goddard Space Flight Center. The NASA centre that develops 42 and many of the cFS service apps. |
| **IV&V** | Independent Verification & Validation. The NASA facility that maintains NOS3 upstream. |
| **CVE-2025-25371** | OSAL path traversal vulnerability in `OS_TranslatePath` (`osapi-filesys.c:675`). Confirmed reachable in this fork. Used in the Phase 2 traversal attack chain. |
| **F8 overflow** | A specific telemetry-validation weakness analysed in the Phase 3 audit. The "F8" prefix is the analysis label, not a function name. |
