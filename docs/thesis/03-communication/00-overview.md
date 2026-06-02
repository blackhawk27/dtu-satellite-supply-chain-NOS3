# Communication layers: overview

A running NOS3 stack contains six distinct wire-level boundaries. A
single Software Bus message originating in a flight-software app may
cross three of them before it shows up as a document in Kibana, and
each boundary speaks a different protocol with different authentication
properties. The files in this directory document one boundary each.

The boundaries, in the order a typical message traverses them:

1. **Host to container.** Docker bridge networks and the few published
   host ports. Where the operator and the capture scripts attach.
   See [01-host-and-containers.md](01-host-and-containers.md).
2. **Inside the cFS Software Bus.** The in-process publish-subscribe
   bus on which every cFS app speaks. Pure local memory, no
   authentication, the most permissive boundary in the system, and
   the one the supply-chain threat model attacks.
   See [02-fsw-software-bus.md](02-fsw-software-bus.md).
3. **cFS to hardware simulator.** Per-component buses simulated by
   NOS Engine: I2C, SPI, CAN, UART. The cFS hardware abstraction
   library (HWLIB) issues the same calls it would against real
   hardware, and NOS Engine delivers them to the component
   simulator over a TCP carrier.
   See [03-fsw-to-sim.md](03-fsw-to-sim.md).
4. **Hardware simulator to dynamics engine.** Direct TCP IPC sockets
   between each simulator and the 42 dynamics container. 42 is the
   server, simulators subscribe by string prefix
   (`SC[0].Whl[0].H`, `SC[0].GPS[0].PosW`, and so on). The non-
   blocking write semantics on 42's side make this the most
   fragile boundary in the system.
   See [04-sim-to-42.md](04-sim-to-42.md).
5. **Flight software to ground software.** CCSDS command and
   telemetry over UDP, wrapped in Space Data Link frames by
   CryptoLib. The only boundary that is intentionally
   authenticated and encrypted.
   See [05-fsw-to-gsw.md](05-fsw-to-gsw.md).
6. **Telemetry to Elasticsearch.** Four host-side files
   (`cfs_god_view.json`, `cfs_evs.log`, `nos3-*.log`,
   `cpu_monitor.log`), tailed by Logstash, transformed, and
   indexed. The only boundary where the data leaves real-time
   semantics.
   See [06-telemetry-to-elk.md](06-telemetry-to-elk.md).

Figure F2 in [../08-figures/figures.md](../08-figures/figures.md)
diagrams these six boundaries on top of the container layout from
Figure F1. References below name them by number.

## Authentication at each boundary

Trust is uneven across the six boundaries, and the supply-chain
research question rests on that asymmetry. The summary table is in
the glossary entry "Authentication"; the precise model per layer is
in the individual files. Three points worth stating up front:

- Boundary 5 (FSW to GSW) is the only one where authentication is
  even available, and even there only one of its two parallel UDP
  paths uses it: the COSMOS `RADIO` interface is CryptoLib-wrapped
  while the COSMOS `DEBUG` interface goes plain UDP directly to
  `ci_lab` on 5012. Everything else inside the spacecraft is
  unauthenticated, by design, because the cFS apps collectively
  are the spacecraft software and there is no notion of "external"
  between them.
- The supply-chain attacker is therefore assumed to have full
  publish and subscribe rights on boundary 2 once their cFS app is
  loaded. Boundaries 3 and 4 are reachable through it: an app on
  the bus can issue commands that drive hardware simulators just
  like any legitimate app could.
- Boundary 6 (telemetry to ELK) is one-way and read-only from the
  spacecraft's point of view; nothing in the ELK pipeline can write
  back into the running sim. This is the property that makes the
  pipeline useful as a forensic record.

## What flows through each boundary at runtime

The traffic mix across the six boundaries is roughly:

- **Boundary 1** is quiet during a run. After `make launch`
  finishes, the only traffic is the operator's browser to Kibana
  on 5601 and the operator's commands to COSMOS on 2900.
- **Boundary 2** carries every cFS message: in a typical run on
  this profile, on the order of a few hundred publishes per second
  steady-state, spiking sharply when `noisy_app` enters its
  broadcast-storm payload (the spike is what
  `nos3/elk/logstash.conf` tags as `sb_pipe_overflow`).
- **Boundary 3** carries the per-component sensor polling and
  actuator commands at the rate each driver runs (typically
  10 Hz for the IMU and magnetometer, 1 Hz for the GNSS, on-
  demand for actuators).
- **Boundary 4** carries 42's truth state at 100 Hz. The 100 Hz
  rate is what makes the broad-prefix subscription crash hazard
  documented in [04-sim-to-42.md](04-sim-to-42.md) so easy to
  trigger: at 100 Hz, even a moderately wider prefix saturates 42's
  non-blocking writes within seconds.
- **Boundary 5** carries the curated CCSDS telemetry stream
  (`to_lab_sub.c` defines exactly which MIDs leave the spacecraft)
  and operator commands inbound.
- **Boundary 6** is decoupled by the four host files; the
  Logstash pipeline rescans them on file rotation and indexes
  what it sees. Throughput is gated by Logstash's pipeline
  concurrency, not by the simulation rate.

## Deviation from the upstream baseline

Five of the six boundaries are mechanically identical to the import
baseline (`55ad2148`). The exception is boundary 6, which is a DTU
addition: the upstream emits its logs to stdout and to ad-hoc files
and provides no aggregator, so the entire pipeline below
`omni_logs/` and `attack_logs/` is new. The other five boundaries
carry small but load-bearing edits, enumerated in their respective
files; the invariants are also encoded as inline comments at the
relevant code or config sites.
