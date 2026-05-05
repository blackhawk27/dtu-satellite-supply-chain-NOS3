# 02. Architecture

This document describes the testbed at the level of containers,
processes, networks, and trust boundaries. It assumes the reader
has run through [`01-reproducibility.md`](01-reproducibility.md)
and has the stack live, and pairs with figure F1 (system
overview) and figure F2 (container and network topology) in
[`08-figures/figures.md`](08-figures/figures.md).

## 1. Layering

The testbed has five horizontal layers; each one of them lives
in its own set of containers and runs its own processes.

| Layer | Purpose | Container(s) | Source location |
|---|---|---|---|
| 1. Dynamics | Propagates orbit and attitude; emits ground-truth state to the simulators | `sc01-fortytwo` | `~/.nos3/42/` (vendored, host-side) |
| 2. Hardware-bus simulators | One container per simulated hardware component; emulates I2C/SPI/CAN/UART traffic against the FSW | `nos3-generic-eps-sim`, `nos3-generic-tt_c-sim`, `nos3-generic-gnss-sim`, `nos3-generic-adcs-sim`, etc. | `nos3/components/<name>/sim/` and `nos3/sims/` |
| 3. Flight software | Runs the cFS binary plus all loaded apps; this is the same binary that would fly | `nos3-fsw` | `nos3/fsw/`, `nos3/components/<name>/fsw/cfs/` |
| 4. Ground software | Operator console; sends commands and receives telemetry over a CCSDS-framed link | `nos3-cosmos` | `nos3/gsw/cosmos/` (default), `nos3/gsw/yamcs/`, `nos3/gsw/openc3/`, `nos3/gsw/ait/` |
| 5. Telemetry analysis | Tails simulator and Software-Bus logs; indexes structured documents; renders dashboards | `nos3-elasticsearch`, `nos3-logstash`, `nos3-kibana` | `nos3/elk/` |

A schematic of the five layers, with the data-flow arrows
between them, is figure F1 in
[`08-figures/figures.md`](08-figures/figures.md).

## 2. Docker networks

Two Docker networks segregate traffic:

- `nos3-core`: the shared services network. Hosts the NOS
  Engine time driver, the ground software, the ELK stack,
  and any per-mission shared resources. Created by
  `make start-elk` (or by `make launch`, which calls it).
- `nos3_sc_N`: the per-spacecraft network. One per simulated
  spacecraft; the active EO1 mission uses `nos3_sc01`. The
  flight-software container, the per-component simulator
  containers, and the 42 dynamics container all join this
  network. Created by `scripts/ci_launch.sh` at launch time.

Containers on `nos3_sc_N` resolve each other by DNS using the
short hostname (for example, the FSW container resolves
`fortytwo` to the IP of the dynamics container on the same
network). Containers on `nos3-core` likewise resolve each
other (for example, `nos3-logstash` resolves `nos3-elasticsearch`).
A small set of services join both networks because they need
to see traffic from both planes; the COSMOS ground software
and the ELK ingest paths fall in that set.

Figure F2 captures the network membership map and the
exposed host ports (Kibana 5601, Elasticsearch 9200, COSMOS
ports 5010 / 7779 depending on configuration).

## 3. Processes inside the FSW container

The flight-software container runs a single OS-level process,
`core-cpu1`, which is the cFE binary. cFE-internal threading
gives the appearance of many concurrent processes:

- One thread per loaded cFS app. The Executive Services (ES)
  registry tracks them; the active set for the EO1 mission
  is roughly 30 apps spanning heritage cFS service apps
  (CFE, ES, EVS, SB, TBL, TIME, CF, CI, CS, DS, FM, HK, HS,
  LC, MD, MM, SBN, SBN_CLIENT, SC, SCH, TO) and the
  per-component apps (GENERIC_EPS, GENERIC_GNSS, GENERIC_TT_C,
  GENERIC_ADCS, GENERIC_REACTION_WHEEL, GENERIC_IMU,
  GENERIC_MAG, GENERIC_CSS, GENERIC_FSS, GENERIC_TORQUER,
  GENERIC_THRUSTER, GENERIC_RADIO, GENERIC_STAR_TRACKER,
  ARDUCAM, NOVATEL_OEM615, MGR, BEACON_APP, NOISY_APP, SAMPLE,
  SYN, ONAIR).
- One thread per HWLIB driver actively bound to a NOS Engine
  bus. These appear as TaskMain handlers polling the
  underlying bus subscription.
- The cFE-internal SB delivery thread and the EVS log-emission
  thread.

All inter-app communication inside cFE goes through the
Software Bus (SB), a publish/subscribe message router. Each
message has a 16-bit Message ID (MID); a packet with MID
`0x1900 | topic` is a command for component-app topic; a
packet with MID `0x0900 | topic` is its telemetry. The
heritage cFS service apps use the `0x1800 / 0x0800` base; the
DTU-added components use the `0x1900 / 0x0900` base, with
the boundary documented in
`nos3/cfg/nos3_defs/cfe_msgid_api.h:18` (heritage) and
`nos3/cfg/nos3_defs/nos3_component_msgid_mapping.h:19`
(component).

Figure F3 shows the SB topology, with apps as nodes and
MIDs as edges; the DTU-altered apps are highlighted.

## 4. The simulator-side processes

Each hardware-component simulator is its own container with
its own binary. The contract between the FSW and a simulator
is the NOS Engine bus: the FSW's HWLIB driver subscribes to
the relevant bus URI; the simulator publishes bus traffic
that looks like the real device's wire format. This is what
allows the same flight binary to fly against simulated and
real hardware.

In the EO1 mission, every simulator inherits from the
`Sim_Hardware_Model` base class in
`nos3/sims/sim_common/inc/sim_hardware_model.hpp`. Most
component simulators also instantiate a 42 socket data
provider (`Sim42SocketProvider` in
`nos3/sims/sim_common/`) that opens a TCP socket to the 42
container and reads ground-truth state from there; the
TT&C and GNSS apps use this. A smaller set of simulators
generate state internally without touching 42 (the
synthetic star-tracker provider is the canonical example;
its selection is load-bearing because the 42 path on port
4282 has a known TCP-buffer pathology).

The NOS Engine time driver is a separate process on
`nos3-core` that publishes a 100 Hz tick to all simulators
and to cFE TIME. This decouples the simulation from
wall-clock time so headless runs can fast-forward without
the operator console diverging.

## 5. The 42 dynamics container

The `sc01-fortytwo` container runs NASA Goddard's 42
simulator, configured via `nos3/cfg/InOut/Inp_*.txt` for
the EO1 orbit (60 deg inclination, 346 deg RAAN, 61.6 deg
true anomaly, tuned for Denmark overfly). 42 emits
ground-truth orbit and attitude state on TCP sockets per
the `Inp_IPC.txt` table; component simulators connect to
those sockets and read the lines that match their
subscribed prefixes.

The IPC table is small but load-bearing. Two rules apply:

- Ports 4280 (thruster) and 4282 (star tracker) are `OFF`.
  Those component simulators run synthetic, so 42 must not
  open the corresponding listening sockets; if it does, no
  client connects, and 42 blocks indefinitely in
  `inet_csk_accept`.
- Ports 4286 (TT&C) and 4287 (GNSS) carry tight string
  prefixes (`SC[0].GPS[0].PosW`, `SC[0].GPS[0].VelW` for
  TT&C; `SC[0].GPS[0].PosW` for GNSS). 42's `WriteToSocket`
  in `~/.nos3/42/Source/AutoCode/TxRxIPC.c:464-468` uses
  non-blocking writes and exits on EAGAIN; widening the
  prefixes back to the upstream defaults overflows the TCP
  send buffer at 100 Hz and crashes 42.

Figure F5 dedicates a focused diagram to the 42 IPC topology
and the backpressure path that motivates the tight-prefix
invariant.

## 6. Ground-software path

The COSMOS container hosts the operator console at port
2902 (HTML) plus per-target uplink/downlink ports that are
generated by the COSMOS configuration at config time. The
GSW container also hosts CryptoLib, the SDLS-conforming
encryption layer that wraps every CCSDS frame between
COSMOS and the FSW.

Operator commands go: COSMOS UI -> COSMOS uplink port ->
CryptoLib (SDLS encrypt) -> over `nos3-core` to the FSW
container's CI/CI_LAB app -> SB inject -> handled by the
target app (for example, `MGR_SET_MODE_CC` lands in
`mgr_app.c`'s command handler). Telemetry runs the same
path in reverse: app emits a TLM packet on SB -> TO_LAB
collects it -> CryptoLib (SDLS decrypt) -> COSMOS
downlink port -> rendered in the operator UI.

Figure F6 shows the GSW path with byte-level annotations
on the CCSDS frame, the SDLS wrapper, and the CI/CI_LAB
versus TO_LAB split.

## 7. The ELK ingest path

ELK lives on `nos3-core` and tails two file sources from
the host (the host paths are bind-mounted into the
Logstash container):

- `nos3/attack_logs/cfs_god_view.json`: written by
  `nos3/scripts/god_view_capture.py`, which subscribes
  to the Software Bus (via TO_LAB) and emits one JSON
  document per packet. Logstash tags these
  `software_bus`.
- `nos3/omni_logs/*.log`: per-component capture logs
  written by `nos3/scripts/sim_logs_capture.sh`,
  `cfs_evs_capture.sh`, and `cpu_monitor.sh`. Logstash
  tags these `system_log` and runs grok patterns to
  extract per-app fields.

The logstash filter at `nos3/elk/logstash.conf` is the
single source of truth for which fields end up in
Elasticsearch and which Kibana panels can query. The
deep-dive walks every filter at
[`05-elk/01-logstash-filters.md`](05-elk/01-logstash-filters.md).

Indices are time-partitioned at the day boundary
(`nos3-telemetry-YYYY.MM.DD`). The index template at
`nos3/elk/index_template.json` declares the field types
and lifecycle. Kibana resolves the human-friendly index
pattern `nos3-telemetry-*` to the auto-generated saved
object ID `5b3163a0-3ea7-11f1-adf4-55f5fc5a104a`.

Figure F7 traces a single packet from FSW emission
through the JSON capture, the Logstash filter pipeline,
the index template, and the panel that surfaces it.

## 8. Trust boundaries

The figure that matters most for the thesis is F8 in
[`08-figures/figures.md`](08-figures/figures.md): the
attack-surface overlay. F8 takes the F1 system overview
and marks each layer with the supply-chain attack zones
that apply to it.

In short:

- **Build-time attacks** sit at Phase 2 (config) and Phase
  4 (build). The configuration generators at
  `nos3/scripts/cfg/` and the CMake apps lists at
  `nos3/cfg/nos3_defs/targets.cmake` are write-points an
  attacker could compromise to silently include or
  exclude FSW apps.
- **Vendored-source attacks** sit inside `nos3/fsw/fprime/`
  and `nos3/gsw/yamcs/`: an attacker who tampers with the
  vendored googletest fixtures or the Yamcs Java sources
  affects everything downstream that depends on those
  modules.
- **Component-supply attacks** sit on the FSW <-> sim bus
  (figure F4): a compromised hardware-bus simulator can
  feed the FSW falsified sensor data without changing any
  FSW source. `noisy_app` is the in-band realisation of
  this scenario.
- **Ground-station attacks** sit at the GSW boundary
  (figure F6) before CryptoLib: an attacker with COSMOS
  command access can issue any command the operator can.
  Stored-command tables and parameter tables are
  particular concentrations of attack surface here.
- **Operations-layer attacks** sit at the ELK boundary
  (figure F7): an attacker who tampers with the Logstash
  filter set can cause attack telemetry to be silently
  filtered out before it reaches the dashboard. The
  noise-filter document at `docs/noise-filters.md`
  summarises the legitimate filters and their rationale,
  so divergence from that set is detectable.

The four research-phase audits under `debug/` use
F8 as their indexing key: each audit attaches its
findings to the corresponding zone.
