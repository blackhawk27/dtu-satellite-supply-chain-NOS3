# Architecture

This document describes the runtime layout: which processes exist,
where they run, what networks they share, and which trust boundary
each one sits behind. It does not describe the build (covered in
[01-reproducibility.md](01-reproducibility.md)) and it does not
describe the wire protocols between processes (covered in
[03-communication/](03-communication/)). It describes the box
diagram that those documents reference.

Figure F1 in [08-figures/figures.md](08-figures/figures.md) draws
this layout end-to-end. References to it below are by number only.

## Container topology

Every process in a running NOS3 stack lives in a Docker container.
There are no host-resident services. The containers are organised
onto two named Docker networks:

- **`nos3-core`.** Shared services that have to be visible to
  multiple spacecraft and to the operator. The NOS Engine time
  driver, the ground software (COSMOS by default), the CryptoLib
  standalone, and the three ELK containers (`nos3-elasticsearch`,
  `nos3-logstash`, `nos3-kibana`) all sit here.
- **`nos3-sc01`.** The per-spacecraft network. The cFS process
  (one container named after the spacecraft profile), every
  hardware simulator, and the 42 dynamics engine sit here. With
  `<number-spacecraft>` greater than one, additional networks
  `nos3-sc02`, `nos3-sc03`, and so on would be created; in the
  configuration these documents target, only `nos3-sc01` exists.

The networks are bridged Docker networks, not overlay networks.
Containers on `nos3-sc01` cannot reach containers on `nos3-core`
unless the launch scripts explicitly attach them to both. The
COSMOS operator container (`cosmos-openc3-operator-1`) and the
four NOS Engine helper containers (`nos-time-driver`,
`nos-terminal`, `nos-udp-terminal`, `nos-sim-bridge`) are
dual-homed: each is started on one network and then attached to
the other through a `docker network connect` in
`scripts/ci_launch.sh` (around lines 184-269). The cFS container
itself sits only on `nos3-sc01`; its uplink and downlink to the
ground software cross the `nos3-sc01` bridge because COSMOS is
attached to that bridge through the same `docker network connect`
mechanism.

Two additional categories of container exist but only ephemerally.
First, the build containers (named `nos_build_fsw`, `nos_build_sim`,
`nos_build_gsw`) are spun up by the corresponding `make` targets,
bind-mount the working tree, and exit when the build finishes; they
are not part of the running stack. Second, the COSMOS GUI launcher
(`cosmos-openc3-operator-1`) is brought up by `make cosmos-gui`
rather than by `make launch`, and replaces the headless COSMOS
container in place; either flavour exists, never both.

## Trust boundaries

There are four distinguishable trust boundaries in the running
system. The supply-chain threat model these documents support
treats only the outermost as adversary-free.

1. **Host to Docker daemon.** The host kernel is trusted; the
   Docker daemon is trusted. The build scripts assume the user can
   talk to Docker without sudo. Outside this boundary, only the
   git remote is read.
2. **Docker to network boundary.** Containers on `nos3-core` can
   reach Elasticsearch directly on `nos3-elasticsearch:9200` and
   Kibana on `nos3-kibana:5601`. The host can reach them on
   `localhost:9200` and `localhost:5601` because the compose file
   publishes those ports. No authentication is enabled
   (`xpack.security.enabled=false`); this is acceptable because
   the network is local-bridge-only.
3. **Ground software to flight software.** COSMOS issues CCSDS
   commands over UDP to the cFS `CI_LAB` app and receives telemetry
   from `TO_LAB` the same way. The COSMOS cmd_tlm_server config
   (`gsw/cosmos/config/tools/cmd_tlm_server/cmd_tlm_server.txt`)
   declares two parallel UDP interfaces:
   - `DEBUG`, direct to `nos-fsw:5012/5013`, no CryptoLib in
     the path. Targets riding this interface (`CFS`, `CMD_UTIL`,
     `GENERIC_*`, `MGR`, `NOVATEL_OEM615`, `SAMPLE`, `TO_DEBUG`,
     `GENERIC_ADCS`) traverse plain UDP.
   - `RADIO`, to `cryptolib:6010/6011`. CryptoLib applies a CCSDS
     Space Data Link frame wrap (authentication and encryption per
     the configured Security Association) and forwards to FSW on
     `8010/8011`. The `*_RADIO` mirror targets ride this interface.
   Both interfaces are active. The DEBUG path is the one a default
   operator workflow typically uses; the RADIO path exists for the
   subset of operations where the link-layer wrap matters. This
   boundary is described in
   [03-communication/05-fsw-to-gsw.md](03-communication/05-fsw-to-gsw.md).
4. **Inside the cFS Software Bus.** The Software Bus is the
   internal pub-sub message bus on which every cFS app speaks. It
   has no authentication, no per-message provenance, and no
   per-subscriber capability check. Any in-process task that can
   call `CFE_SB_TransmitMsg` is, from the bus's point of view, a
   legitimate publisher of whatever MID it names. This is the
   trust boundary the supply-chain threat model attacks; see
   [03-communication/02-fsw-software-bus.md](03-communication/02-fsw-software-bus.md).

## Inventory of runtime processes

The list below is exhaustive for the default
`sc-mission-config.xml` profile under the cFS / COSMOS choice. One
paragraph per process. Apps and simulators that were added or
modified relative to the import baseline are documented in detail
under [04-apps/](04-apps/); the paragraphs here are the
one-sentence orientation.

### Flight software (cFS) apps

The cFS image is one process; the apps below are tasks running
inside it. They are loaded in the order specified by
`cfg/build/nos3_defs/cpu1_cfe_es_startup.scr`.

- **`cfe_es`, `cfe_evs`, `cfe_sb`, `cfe_tbl`, `cfe_time`.** The
  five Core Flight Executive services. `cfe_es` is the executive
  (task start, application load, syslog); `cfe_evs` is the event
  service (the source of every line tagged as `system_log` after
  routing through `cfs_evs_capture.sh`); `cfe_sb` is the Software
  Bus; `cfe_tbl` is the table service; `cfe_time` is the mission
  clock, locked to the NOS Engine ticker rather than to the host.
- **`ci_lab`, `to_lab`.** The minimal command-ingest and
  telemetry-output apps. `ci_lab` listens for CCSDS command packets
  on UDP 5012 and republishes them onto the Software Bus; `to_lab`
  subscribes to a curated set of MIDs and ships them back to the
  ground on UDP 5013. Both are upstream cFS sample apps and are
  treated as the GSW boundary. CryptoLib wraps only the RADIO
  path (COSMOS `INTERFACE RADIO`); the DEBUG path
  (COSMOS `INTERFACE DEBUG`) goes plain UDP straight to these
  apps. See
  [03-communication/05-fsw-to-gsw.md](03-communication/05-fsw-to-gsw.md).
- **`sch`, `sc`, `lc`, `ds`, `fm`, `hk`, `hs`, `cs`, `mm`, `md`.**
  The cFS housekeeping suite. `sch` runs the scheduler tables;
  `sc` runs stored command sequences (the RTS / ATS tables under
  `cfg/build/nos3_defs/tables/sc_rts*.c`); `lc` runs the limit
  checker, whose watchpoint definition table is at
  `cfg/build/nos3_defs/tables/lc_def_wdt.c`); `ds` is the
  data storage app; `fm` is the file manager; `hk` is the
  housekeeping aggregator; `hs` is health and safety; `cs` is the
  checksum app; `mm` is the memory manager; `md` is the memory
  dwell app. Several of these (`mm`, `md`, `fm`, `cs`, `lc`) are
  confirmed or candidate supply-chain attack surfaces; the Threat
  Model chapter develops those.
- **`io_lib`, `hwlib`, `sbn`, `sbn_client`, `cf`, `beacon_app`.**
  Libraries and ancillary apps that are loaded for completeness
  but are not load-bearing for the default mission. `sbn` is the
  Software Bus Network bridge (one cFS instance to another),
  unused with a single spacecraft. `cf` is the CCSDS file
  delivery protocol implementation. `beacon_app` is the periodic
  beacon transmitter.
- **`mgr`.** The mission manager (`components/mgr/`). It runs the
  EO1 mission timeline against the active orbit and owns the
  cross-boot persistence of the spacecraft mode. Present at the
  import baseline; the DTU fork has changed its reboot policy
  (always resume in SAFE) and suppressed redundant SET_MODE EVS
  spam. Documented in [04-apps/mgr.md](04-apps/mgr.md).
- **`syn`.** The JPL SYNOPSIS onboard science prioritisation
  library, exposed as a cFS app. Imported intact but disabled
  in the default profile (`cfg/spacecraft/sc-mission-config.xml`
  sets `<syn><enable>false</enable></syn>`), so `make config`
  does not emit a `CFE_APP` line for it and cFE does not load it
  at boot. The component is therefore present in the tree as
  reference material and as a documented heap-leak attack
  surface (developed in the Threat Model chapter); it is not
  active in the default run.
- **`onair`.** ONAIR (Onboard Artificial Intelligence Research)
  agent, also imported intact.
- **`sample`.** A generic example cFS app, kept enabled because
  several test paths depend on its MID range.
- **`noisy_app`.** The DTU-added compromised payload. It is
  built exactly like the genuine apps and the resulting binary
  ships in every clean FSW image, but the startup script registers
  it as `OFF_APP` (not `CFE_APP`), so cFE skips it at boot under
  the default profile. The entry sits inside a
  `===_DTU_THESIS_MENU_===` block in
  `cfg/nos3_defs/cpu1_cfe_es_startup.scr`; flipping the keyword
  to `CFE_APP` and rebuilding the image is the operator-side
  arming step. The supply-chain framing is the build-time
  registration, not a runtime injection. See
  [04-apps/noisy_app.md](04-apps/noisy_app.md).
- **`cpu_killer`.** A second `OFF_APP` entry in the same thesis
  menu, also registered at priority 160. No source file with
  that name exists in `fsw/apps/` or `components/`; the entry
  is a placeholder for a future attack variant rather than a
  currently-buildable app.

### Hardware-component simulators

Each peripheral has its own simulator container, talking the
component's real wire protocol back to the cFS image and pulling
its physical state from 42 over a TCP IPC connection. Every
component in the list below has a `<component>` block in
`cfg/spacecraft/sc-mission-config.xml`; the `<enable>` element
controls whether the simulator runs. The default profile enables
all of these except generic-thruster and generic-star-tracker.

- **`generic_eps`.** Electrical power subsystem simulator.
  Publishes solar-array voltage and current, battery state of
  charge, and bus-rail enables. The telemetry fields
  `eps_solar_power_w`, `eps_power_used_w`, `eps_battery_wh`, and
  `eps_battery_mv` extracted by the Logstash pipeline come from
  this simulator.
- **`generic_gnss` and `novatel_oem615`.** Two GNSS receiver
  models. Only one is wired at a time; the default profile uses
  `generic_gnss`, which is the source of `gps_lat`, `gps_lon`,
  `gps_alt`, `gps_ecef_*`, and `gps_abs_time`. The Novatel model
  exists for fidelity comparison and is documented under
  [04-apps/](04-apps/) for completeness.
- **`generic_imu`, `generic_mag`, `generic_css`, `generic_fss`.**
  The attitude-sensing chain: inertial measurement unit,
  magnetometer, coarse sun sensor, fine sun sensor. All four
  publish on cFS MIDs in the 0x194x range and are consumed by
  `generic_adcs`.
- **`generic_adcs`.** The attitude determination and control
  subsystem. Consumes the four attitude sensors and the reaction
  wheel telemetry, runs the control law, and emits torque
  commands. The research notes identify it as a candidate for
  LICM-induced hardware-blinding under `-O2`; see
  [04-apps/generic_adcs.md](04-apps/generic_adcs.md).
- **`generic_reaction_wheel`, `generic_torquer`.** The two
  actuator chains. The reaction wheel publishes `rw_momentum`,
  which the Logstash pipeline extracts directly.
- **`generic_star_tracker`.** Disabled by default. Its IPC port
  (4282) must therefore stay configured as `OFF` in
  `cfg/InOut/Inp_IPC.txt`; the load-bearing reason is recorded
  inline at that file's port-4282 entry and in
  [03-communication/04-sim-to-42.md](03-communication/04-sim-to-42.md).
- **`generic_thruster`.** Disabled by default. Same story as the
  star tracker: IPC port 4280 must stay `OFF`.
- **`generic_radio`.** RF transceiver model. Publishes the
  radio-state telemetry that the ground software's link budget
  view consumes.
- **`generic_tt_c`.** Telemetry, tracking, and command subsystem
  simulator. Its data-point parser is one of the load-bearing
  edits relative to upstream (the `[` and `]` bracket stripping
  and `stof_safe` helper). See
  [04-apps/generic_tt_c.md](04-apps/generic_tt_c.md).
- **`blackboard`.** An internal data exchange used by the mission
  manager and the ADCS. The component existed at the import
  baseline; the DTU fork has modified its sim-side parser
  (`blackboard_data_point.cpp`) with the same bracket-stripping
  and empty-frame deferral pattern as `generic_tt_c` and
  `generic_gnss`. Documented in
  [04-apps/blackboard.md](04-apps/blackboard.md).
- **`arducam`.** Imaging payload simulator (disabled in the
  default profile; enabled in `sc-research-config.xml`).

### Dynamics and time

- **42.** The orbit and attitude dynamics engine. Lives in
  `~/.nos3/42/` (populated by `make prep`) and is launched by
  `scripts/ci_launch.sh` in its own container. Reads
  `cfg/InOut/Orb_LEO.txt` (Denmark overfly: 60 degree
  inclination, 346 degree RAAN, 61.6 degree true anomaly) and the
  generated `cfg/build/InOut/Inp_*` files. It publishes truth
  state over per-port TCP IPC sockets to whichever simulators
  have subscribed; 42's own `WriteToSocket` is non-blocking and
  exits on `EAGAIN`, which constrains how broad those
  subscriptions can be (see
  [03-communication/04-sim-to-42.md](03-communication/04-sim-to-42.md)
  for the full reasoning).
- **NOS Engine time driver.** A separate container on
  `nos3-core` that emits monotonic time ticks consumed by every
  other component. Without it, every component free-runs against
  the host clock and the sim is no longer deterministic.

### Ground software (COSMOS)

- **`cosmos-openc3-operator-1`, `cosmos-openc3-cmd-tlm-api-1`,
  `cosmos-openc3-script-runner-api-1`, `cosmos-openc3-redis-*`,
  `cosmos-openc3-traefik-1`, and friends.** The headless
  OpenC3 / COSMOS deployment. Issues CCSDS commands to cFS,
  decodes the telemetry stream from `TO_LAB`, and serves the
  operator UI on `http://localhost:2900`. CryptoLib runs as a
  separate `sc01-cryptolib` container on the spacecraft network
  and is reachable from COSMOS as `cryptolib:6010/6011`; it
  wraps only the commands and telemetry routed through COSMOS's
  RADIO interface. The COSMOS DEBUG interface bypasses CryptoLib
  and talks directly to `nos-fsw:5012/5013`.

### Capture and observability

- **`god_view_capture.py`.** A host-side Python process started
  by `make launch` that subscribes to every Software Bus
  message via the TO_LAB UDP port and writes one JSON line per
  message to `nos3/attack_logs/cfs_god_view.json`. This is the
  most privileged observer in the system; it sees what `to_lab`
  is configured to ship, which on this fork is everything that
  `to_lab_sub.c` enumerates. The file is the source of the
  `software_bus` documents in Elasticsearch.
- **`cfs_evs_capture.sh`.** Tails the cFS Event Service into
  `omni_logs/cfs_evs.log`. The file is the source of the
  `system_log` documents tagged with `subsystem=cfs`.
- **`sim_logs_capture.sh`.** Wraps every simulator's stdout into
  one file per simulator under `omni_logs/nos3-*.log`. Source of
  `system_log` documents with `subsystem` set to the simulator
  name.
- **`cpu_monitor.sh`.** A `top`-style sampler against the cFS
  container; produces `omni_logs/cpu_monitor.log` and the
  `TOTAL_CPU_PCT`, `CPU_PCT`, and `NUM_PIDS` numeric fields.

### ELK stack

- **`nos3-elasticsearch`.** Elasticsearch 7.17.10, single-node,
  no security. Stores `nos3-telemetry-YYYY.MM.DD` daily-rolled
  indices on a named volume (`es_data_vol`). The volume is
  preserved across `docker compose down`; `make stop` deletes
  the indices but not the volume.
- **`nos3-logstash`.** Logstash 7.17.10. Runs the pipeline in
  `nos3/elk/logstash.conf` against the two file inputs bind-
  mounted in from the host (`attack_logs/`, `omni_logs/`). Uses
  the generated MID-registry YAMLs under `cfg/build/elk/` as
  `translate` filter dictionaries to turn raw MID numbers into
  human-readable names, subsystems, and protocol layers.
- **`nos3-kibana`.** Kibana 7.17.10. Hosts the fifteen
  DTU-built dashboards (built by
  `elk/build_kibana_dashboards.py` and re-imported on every
  `make launch`).

## How the pieces start

The startup sequence below is the one `make launch` actually
executes; it is reproduced here so that the order of trust
boundaries crossing is explicit.

1. The capture scripts from the previous run, if any, are killed
   (`pkill -f god_view_capture.py` and the three shell siblings).
   The `attack_logs/` and `omni_logs/` files are truncated so the
   new run starts on a clean slate.
2. `make start-elk` ensures the `nos3-core` Docker network
   exists, seeds the MID-registry YAMLs if they are missing,
   brings up the three ELK containers, and waits until
   Elasticsearch's cluster health is at least yellow.
3. `nos3-telemetry-*` indices from the previous run are deleted
   so Kibana opens against a fresh dataset. (If the run is meant
   to be preserved, the previous run was archived through
   `make save-run`, which sets `KEEP_TLM=1` on `make stop`.)
4. `scripts/ci_launch.sh` brings up 42, then every simulator on
   `nos3-sc01`, then the cFS container, then the ground software
   on `nos3-core`. Each step waits for the previous step's
   health check.
5. The four capture scripts attach. Logstash is restarted so it
   picks up the freshly-created log files.
6. `elk/build_kibana_dashboards.py` runs against the Kibana
   Saved Objects API, importing or updating the fifteen
   dashboards. `elk/refresh_index_pattern_fields.py` rewrites
   the index-pattern field cache so Lens panels do not render
   "No data" on the first opening.

After step 6 the system is steady-state: simulators publishing,
cFS scheduling, the four capture surfaces feeding Logstash,
Elasticsearch indexing, and Kibana serving dashboards on
`http://localhost:5601`. The wire-level detail of each arrow
between the boxes drawn by Figure F1 is the subject of
[03-communication/](03-communication/).
