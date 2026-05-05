# 04. generic_mag - Magnetometer

This document describes the magnetometer cFS app and its
simulator.

## 1. Real-world hardware

A magnetometer measures the local magnetic field vector. On a
small satellite, the dominant signal is the geomagnetic field
(IGRF model); the measurement provides one of the two reference
vectors used in attitude determination (the other typically
being a sun vector). MAG is also the only sensor B-dot
detumble needs.

## 2. App overview

The flight-software half lives at
`nos3/components/generic_mag/fsw/cfs/`. The simulator half lives
at `nos3/components/generic_mag/sim/`:

- `src/generic_mag_data_point.cpp`: per-tick data-point parser.
  Consumes 42's `SC[N].B[0].MagModel.Field` field.
- `src/generic_mag_hardware_model.cpp`: bus simulator.
- `src/generic_mag_42_data_provider.cpp`: 42 IPC consumer on
  port 4283.

MIDs (component-base, `0x1900 / 0x0900`):

- Topic ID `0x84`.
- Command: `0x1984`.
- HK telemetry: `0x0984`.

Bus: I2C address `0x0C`.

## 3. Headline function: the first-tick guard

`Generic_magDataPoint::do_parsing`. The DTU-added guard
(commit `962b61e7`) follows the IMU pattern:

```
if (_dp.get_value_for_key(key_field).empty()) {
    sim_logger->debug(
        "Generic_magDataPoint::do_parsing: empty SC[%d].B[0].MagModel.Field "
        "frame, deferring to next tick", _sc);
    return;
}
_field[0] = std::stof(_dp.get_value_for_key(key_field_x));
...
```

Without the guard, `std::stof` throws on the empty first-tick
field, the catch handler logs at error level, and Kibana picks
up the line as `evs_severity:ERROR`. The fresh-sim validation
in commit `962b61e7` reports `nos3-mag.log` dropping from 29
error hits to 14 hits per launch.

## 4. ELK extraction

Logstash extracts the MAG HK fields with `mag_*` prefix.

Kibana panels:

- "Magnetic Field Vector" on `nos3-std-telemetry-overview`.

## 5. Attack-surface relevance

- **Component supply**: a tampered MAG simulator can publish a
  rotated field vector, which causes B-dot detumble to push
  the spacecraft instead of damping it.

## 6. DTU divergence vs upstream

- Commit `962b61e7`: empty-string guard on first-tick
  `std::stof` for the field-vector parser. Same family
  pattern.

## 7. Verification

- `nos3-mag.log` post-launch: `rg -c "Parsing exception"` returns
  roughly 14 hits per run (down from 29 pre-fix).
- Kibana panel "Magnetic Field Vector" shows non-zero traces
  shortly after sim start.
