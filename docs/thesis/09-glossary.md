# Glossary

Acronyms, wire-format identifiers, and key terms used in this
documentation set. Each entry is short on purpose; deeper
treatment is in the document linked from the entry.

## Acronyms

- **ADCS.** Attitude Determination and Control Subsystem. The
  cFS app and matching hardware-simulator pair that runs the
  attitude control loop. MID range `0x094x` and `0x194x`.
  See [04-apps/generic_adcs.md](04-apps/generic_adcs.md).
- **AIT.** Ammos Instrument Toolkit, one of the four ground-
  software options NOS3 supports
  (`<gsw>` value `ait`). Not used in this fork.
- **AP.** Action Point. A row in a Limit Checker (`LC`)
  definition table. Each AP names a watchpoint expression
  and a stored-command sequence to fire on transition.
- **APID.** Application Process Identifier. The 11-bit field
  inside a CCSDS primary header. Equivalent to MID in the
  cFE-internal layout used here.
- **CCSDS.** Consultative Committee for Space Data Systems.
  The set of standards that define the Space Packet Protocol
  (SPP, the on-the-wire packet format inside the spacecraft)
  and the Space Data Link Protocol (the link-layer wrap
  applied by CryptoLib).
- **cFE.** Core Flight Executive. NASA's flight-software
  executive that this fork uses. Provides the Software Bus,
  Event Service, Table Service, Time service, and Executive
  Service.
- **cFS.** Core Flight System. cFE plus a curated set of
  flight-software apps. The fork's FSW build is a cFS image.
- **COSMOS.** Ball Aerospace's open-source ground software
  framework, redistributed by OpenC3 as OpenC3 COSMOS. The
  default ground-software choice in `nos3-mission.xml`.
- **CSS.** Coarse Sun Sensor. One of the attitude-sensing
  components.
- **DTU.** Technical University of Denmark. The institution
  this fork originates from.
- **ELK.** Elasticsearch, Logstash, Kibana. The telemetry
  aggregation pipeline this fork adds. See
  [05-elk/](05-elk/).
- **EPS.** Electrical Power Subsystem. The cFS app and
  hardware-simulator pair that models the spacecraft power
  bus. MID `0x191A` / `0x191B` for commands, `0x091A` /
  `0x091B` for telemetry. The target of `noisy_app`'s EPS
  HK spoof attack.
- **EVS.** Event Service. The cFE service that handles
  per-app text events. Logged into `cfs_evs.log` and
  consumed by Logstash as the `system_log` input.
- **FSS.** Fine Sun Sensor.
- **FSW.** Flight Software. The software that runs on the
  spacecraft, as opposed to GSW (ground software). In this
  fork, FSW means the cFS image.
- **GNC.** Guidance, Navigation, and Control. One of the
  ADCS telemetry MIDs (`0x0943`).
- **GNSS.** Global Navigation Satellite System. Generic term
  for GPS-class receivers. `generic_gnss` is the DTU-added
  GNSS simulator; `novatel_oem615` is the upstream
  Novatel-receiver model.
- **GSW.** Ground Software. The COSMOS / OpenC3 / YAMCS /
  AIT stack on the operator side. This fork uses COSMOS.
- **HK.** Housekeeping. cFS app that aggregates per-app
  housekeeping packets into a unified telemetry stream.
- **HS.** Health and Safety. cFS app that monitors per-app
  execution and emits failure events when an app misses its
  expected cadence. The detector for `noisy_app`'s CPU
  starvation.
- **HWLIB.** Hardware Library. The cFS abstraction layer
  that decouples device drivers from the underlying bus
  implementation. Talks to NOS Engine in the simulator
  build, to OS device nodes on a flight build. See
  [03-communication/03-fsw-to-sim.md](03-communication/03-fsw-to-sim.md).
- **IPC.** Inter-Process Communication. In the NOS3 context,
  specifically the TCP-based communication between hardware
  simulators and the 42 dynamics engine. Configured by
  `nos3/cfg/InOut/Inp_IPC.txt`.
- **IMU.** Inertial Measurement Unit.
- **IV&V.** Independent Verification and Validation. The
  NASA facility that maintains the upstream NOS3.
- **LC.** Limit Checker. cFS app that monitors numeric
  fields against watchpoint definitions and fires stored-
  command sequences on threshold transitions.
- **LICM.** Loop-Invariant Code Motion. The GCC
  optimisation that hoists invariant computations out of
  loop bodies. The mechanism behind the ADCS `-O2` hardware-
  blinding hazard documented in
  [04-apps/generic_adcs.md](04-apps/generic_adcs.md).
- **MAC.** Message Authentication Code. The cryptographic
  authenticator on CryptoLib's Space Data Link frames.
- **MD.** Memory Dwell. cFS app that periodically reads
  named memory addresses and publishes their values.
  Identified in research notes as an attack-surface for
  payload exfiltration.
- **MID.** Message Identifier. The 16-bit field that
  identifies a Software Bus message. Equivalent to APID in
  the underlying CCSDS layer. Commands occupy `0x1XXX`,
  telemetry occupies `0x0XXX`.
- **MM.** Memory Manager. cFS app that supports memory
  read, write, fill, and load operations. Identified as
  an attack surface in the research notes.
- **NOS3.** NASA Operational Simulator for Space Systems.
  The upstream project this repository forks.
- **NOSA.** NASA Open Source Agreement. The licence under
  which NOS3 is distributed.
- **OS.** Operating System. In cFS context, refers to OSAL
  (Operating System Abstraction Layer), the cFE component
  that abstracts host-OS differences.
- **PSP.** Platform Support Package. The cFE component that
  abstracts hardware-platform differences. The `amd64-nos3`
  PSP is what this fork builds against.
- **RW.** Reaction Wheel. Both the actuator and its cFS
  app. MID range `0x1992` / `0x1993` (commands) and
  `0x0992` / `0x0993` (telemetry).
- **SB.** Software Bus. The cFE in-process publish-subscribe
  message bus. The central trust boundary of the supply-
  chain threat model. See
  [03-communication/02-fsw-software-bus.md](03-communication/02-fsw-software-bus.md).
- **SBN.** Software Bus Network. The cFS bridge that
  forwards Software Bus messages between cFE instances on
  different cores or different spacecraft. Loaded but
  unused in the single-spacecraft profile.
- **SC.** Stored Commands. cFS app that executes pre-loaded
  command sequences (RTSs and ATSs) on schedule.
- **SCH.** Scheduler. cFS app that issues periodic
  Software Bus messages on configured cadences, including
  housekeeping requests to every other app.
- **SDL / SDLP.** Space Data Link / Space Data Link Protocol.
  The CCSDS link-layer protocol applied by CryptoLib at
  the FSW-to-GSW boundary.
- **SPP.** Space Packet Protocol. The CCSDS packet format
  used inside the spacecraft (Software Bus and TO_LAB
  output).
- **SYN.** SYNOPSIS. JPL's onboard science prioritisation
  library, packaged as a cFS app in this fork. Imported
  intact; carries a known heap leak (developed in the
  Threat Model chapter).
- **TLM.** Telemetry. The downlink direction of CCSDS
  packets, as opposed to CMD (command, uplink).
- **TT&C.** Telemetry, Tracking, and Command. The
  spacecraft subsystem responsible for the radio link.
  `generic_tt_c` is the DTU-added TT&C simulator. See
  [04-apps/generic_tt_c.md](04-apps/generic_tt_c.md).
- **UTC.** Coordinated Universal Time. The reference clock
  for the mission start time and the daily-index rollover.
- **YAMCS.** An alternative open-source ground-software
  framework (`<gsw>` value `yamcs`). Not used in this fork.
- **ZTA.** Zero Trust Architecture. The framing used by a
  separate NOS3 variant that does add per-message
  authentication on the Software Bus. Not the variant in
  this repository.

## Wire-format identifiers

- **`0x0XXX` MIDs.** Telemetry packets. The exact subsystem
  allocation is in the MID-registry YAMLs under
  `cfg/build/elk/`. Notable values: `0x091A` (EPS HK),
  `0x0940`-`0x0945` (ADCS telemetry suite), `0x0950`
  (TT&C HK), `0x0952`-`0x0954` (GNSS HK, device, hello),
  `0x08F8` (mgr HK).
- **`0x1XXX` MIDs.** Command packets. Notable values:
  `0x18F0` (`noisy_app`'s `MALWARE_TRIGGER_MID`), `0x18F8`
  (mgr command), `0x18F9` (mgr HK request), `0x1940`
  (ADCS command), `0x1950` (TT&C command), `0x1952` (GNSS
  command).
- **Function code.** The 7-bit value in the CCSDS secondary
  header on a command packet. Selects the command code
  within a MID. `noisy_app` uses `BEACON_PING_FC = 2` and
  `SPOOF_EPS_FC = 3` against its trigger MID.
- **Layer taxonomy.** Logstash classifies every MID into
  one of `FSW_CORE`, `FSW_HERITAGE`, `COMPONENT`,
  `RESEARCH`, or `UNKNOWN`. Definitions are at the top of
  `nos3/elk/logstash.conf` and reproduced in
  [05-elk/01-logstash-filters.md](05-elk/01-logstash-filters.md).
- **42 IPC ports.** 42xx range on the `fortytwo` container.
  Notable values: 4277 / 4377 / 4477 (reaction-wheel
  outbound), 4278 / 4378 / 4478 (reaction-wheel actuator),
  4279 (torquer), 4280 (thruster, `OFF`), 4282 (star
  tracker, `OFF`), 4286 (TT&C), 4287 (GNSS).

## Key terms

- **Import baseline.** Commit `55ad2148`. The state at which
  the upstream NOS3 was flattened into this repository.
  Every divergence in
  [07-customizations-vs-upstream.md](07-customizations-vs-upstream.md)
  is measured against this commit.
- **Capture surface.** A host-side file that one of the four
  capture processes writes into. The pipeline has six such
  files (the three Layer-2 logs collapse onto one input
  glob); see
  [05-elk/00-pipeline.md](05-elk/00-pipeline.md).
- **MID-registry seed.** The fallback YAMLs under
  `nos3/elk/seed_mid/` that Logstash uses when
  `scripts/mid/gen_mid_registry.py` fails. The generator
  fails silently against the current cFE because it
  targets Draco-era topic macros. Load-bearing for
  fresh-clone launches; the seed-copy fallback is at
  `nos3/Makefile:375-384`.
- **Carpet bombing.** The term used in `noisy_app.c`'s
  source comments for the attack's broadcast-storm phase:
  iterate every MID in `0x0000`-`0x1FFF` and publish four
  4 KB packets to each. See
  [04-apps/noisy_app.md](04-apps/noisy_app.md).
- **God view.** The host-side observer
  (`god_view_capture.py`) that records every CCSDS packet
  on the TO_LAB downlink stream. Called "god view" because
  it sees the cFS image's unencrypted bus traffic before
  CryptoLib applies the link wrap; from inside the
  spacecraft this is the most privileged possible
  observer.
- **Denmark overfly.** The orbit configured in
  `cfg/InOut/Orb_LEO.txt` (60 degree inclination, 346
  degree RAAN, 61.6 degree true anomaly). Drives the
  `gps_lat` / `gps_lon` window that the LC table uses to
  fire the science autonomy.
- **Run.** A single end-to-end execution of `make launch`
  through `make stop`, producing one daily index in
  Elasticsearch and one set of capture files on disk. A
  run can be archived by `make save-run` and replayed by
  `make load-run`.
- **Attack window.** The interval, within a run, during
  which `noisy_app` is in the armed state. Bounded by the
  `attack_armed` tag on one side and the
  `hs_app_failure` tag on the other in Kibana.
- **Supply-chain threat model.** The framing used
  throughout this documentation set: the adversary is
  assumed to have a compromised cFS app loaded into the
  spacecraft FSW build through the build supply chain.
  Their capabilities are full Software Bus publish /
  subscribe; their goal is the operational disruption or
  data exfiltration that `noisy_app` and the research
  scripts demonstrate. See
  [00-overview.md](00-overview.md) for the framing in
  context.
