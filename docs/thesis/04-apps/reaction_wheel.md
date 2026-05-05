# 04. generic_reaction_wheel - Reaction Wheel Driver

This document describes the reaction-wheel cFS app and its
hardware simulator. Reaction wheels are the primary attitude
actuators in the EO1 mission; ADCS publishes torque commands,
the FSW driver translates them to per-wheel speed deltas, and
the simulator integrates the wheel-momentum response.

The DTU-relevant change for this app is the first-tick
empty-string guard added in commit `962b61e7`. See section 6.

## 1. Real-world hardware

A reaction wheel stores momentum: when the spacecraft body wants
a torque in some direction, the controller spins the wheel up or
down, and the body experiences the reaction torque. The EO1
spacecraft carries four wheels in a tetrahedral arrangement, so
no axis is unconstrained.

## 2. App overview

The flight-software half lives at
`nos3/components/generic_reaction_wheel/fsw/cfs/`. The simulator
half lives at
`nos3/components/generic_reaction_wheel/sim/`:

- `src/generic_rw_data_point.cpp`: the per-tick data-point
  parser (consumes 42's `SC[N].Whl[M].H` fields).
- `src/generic_rw_hardware_model.cpp`: the bus simulator.
- `src/generic_rw_sim_data_42socket_provider.cpp`: the 42 IPC
  consumer; one socket per wheel (ports 4277-4578 in the
  Inp_IPC.txt table, see
  [`../03-communication/04-sim-to-42-dynamics.md`](../03-communication/04-sim-to-42-dynamics.md)).
- `src/generic_rw_shmem_data_provider.cpp`: shared-memory
  alternate path; not used in the EO1 mission.

MIDs (component-base, `0x1900 / 0x0900`):

- Topic ID `0x92`.
- Command: `0x1992`.
- HK telemetry: `0x0992`.

Bus: SPI.

## 3. Headline function: the first-tick guard

`Generic_rwDataPoint::do_parsing` in
`generic_rw_data_point.cpp` parses the per-wheel momentum
strings the 42 IPC publishes. The DTU-added guard:

```
const std::string h_str = _dp.get_value_for_key(key_h);
if (h_str.empty()) {
    sim_logger->debug(
        "Generic_rwDataPoint::do_parsing: empty SC[%d].Whl[%d].H frame, "
        "deferring to next tick", _sc, _wheel);
    return;
}
_momentum = std::stof(h_str);
```

Without the early return, the first 42 IPC frame after sim
startup arrives with an empty `_h` value; `std::stof` throws
`std::invalid_argument`; the catch-handler logs at error level;
the `Parsing exception` line lands in Kibana as
`evs_severity:ERROR`. The fresh-sim validation in commit
`962b61e7` shows the count of those error lines drops from 17
hits to 2 hits per run after the guard.

The catch handler is deliberately kept at error level so a real
parse failure on a populated frame stays loud. Only the first
empty-frame race is reclassified as debug.

## 4. ELK extraction

Logstash extracts:

- `rw_momentum`: per-wheel scalar momentum.

Plus EVS-line decoding for ADCS-driven RW commands.

Kibana panels:

- "Reaction Wheel Momentum" on `nos3-std-telemetry-overview`:
  one line per wheel.
- "RW Momentum Saturation" on `nos3-std-mission-validation`.

## 5. Attack-surface relevance

- **Component supply**: a tampered RW simulator publishes
  fabricated momentum, hiding actuator failure or saturation.
- **Wheel-command tampering**: ADCS publishes RW command MIDs;
  an attacker with SB-publish capability can issue spurious
  torque commands that desaturate or saturate the wheels.

## 6. DTU divergence vs upstream

- Commit `962b61e7`: empty-string guard on first-tick
  `std::stof` for the wheel `_H` parser. The same commit
  applies the same guard to IMU and MAG; the design doc is
  `debug/EPS_DESIGN.md` (which covers the broader sweep) and
  `debug/ADCS_VALIDATION.md`.

## 7. Verification

- `nos3-rw0.log` post-launch: `rg -c "Parsing exception"` returns
  ~2 hits per run (down from 17 pre-fix).
- Kibana: `evs_severity:ERROR AND
  evs_message:*Parsing\\ exception*` returns zero documents
  on a fresh launch.
- "Reaction Wheel Momentum" panel shows four non-zero traces
  shortly after sim start (one per wheel).
