# 04. generic_imu - Inertial Measurement Unit

This document describes the IMU cFS app and its simulator. The
IMU provides body-frame angular rates and accelerations; the
ADCS app uses both for attitude estimation.

## 1. Real-world hardware

An IMU on a small satellite typically combines a 3-axis MEMS
gyroscope (angular rate) and a 3-axis MEMS accelerometer. The
testbed models a single fused sensor.

## 2. App overview

The flight-software half lives at
`nos3/components/generic_imu/fsw/cfs/`. The simulator half lives
at `nos3/components/generic_imu/sim/`:

- `src/generic_imu_data_point.cpp`: the per-tick data-point
  parser, consumes 42's `SC[N].Accel[0].TrueAcc` and
  `SC[N].Gyro[0].rate` style keys.
- `src/generic_imu_hardware_model.cpp`: the bus simulator.
- `src/generic_imu_42_data_provider.cpp`: the 42 IPC consumer
  on port 4281 (per the Inp_IPC.txt table).
- `src/generic_imu_data_provider.cpp` /
  `generic_imu_shmem_data_provider.cpp`: alternate providers.

MIDs (component-base, `0x1900 / 0x0900`):

- Topic ID `0x80`.
- Command: `0x1980`.
- HK telemetry: `0x0980`.

Bus: I2C address `0x68`.

## 3. Headline function: the first-tick guard

`Generic_imuDataPoint::do_parsing` in
`generic_imu_data_point.cpp:41`. The DTU-added guard at line 64:

```
if (_dp.get_value_for_key(keya0).empty()) {
    sim_logger->debug(
        "Generic_imuDataPoint::do_parsing: empty SC[%d].Accel/Gyro frame, "
        "deferring to next tick", _sc);
    return;
}
_accelRates[0] = std::stof(_dp.get_value_for_key(keya0));
_accelRates[1] = std::stof(_dp.get_value_for_key(keya1));
_accelRates[2] = std::stof(_dp.get_value_for_key(keya2));
_gyroRates[0]  = std::stof(_dp.get_value_for_key(keyg0));
...
```

Six `std::stof` calls follow the guard. Without the early
return, the first 42 frame arrives with empty `Accel/Gyro`
values, the first `std::stof` throws, and the catch-handler at
line 83 logs `Generic_imuDataPoint::Generic_imuDataPoint:
Parsing exception` at error level.

The fresh-sim validation summary in commit `962b61e7` reports
`nos3-imu.log` dropping from 132 error hits to 18 per launch
after the guard, with the residual 18 traced to causes
unrelated to the first-tick race.

## 4. ELK extraction

The Logstash filter extracts the IMU HK fields (rates and
accelerations); the field names follow the `imu_*` prefix
pattern.

Kibana panels:

- "IMU Angular Rates" on `nos3-std-telemetry-overview`.
- "IMU First-Frame Errors" on `nos3-std-mission-validation`
  (used as a regression gate for the first-tick guard).

## 5. Attack-surface relevance

- **Component supply**: a tampered IMU simulator can bias rates
  and accelerations. ADCS estimates drift; the dashboard panel
  is the cross-check.

## 6. DTU divergence vs upstream

- Commit `962b61e7`: empty-string guard on first-tick
  `std::stof` for `Accel/Gyro` fields. Same family pattern as
  reaction-wheel and MAG. Reasoning lives in
  `debug/ADCS_DIAGNOSIS.md` and
  `debug/ADCS_VALIDATION.md`.

## 7. Verification

- `nos3-imu.log`: `rg -c "Parsing exception"` returns roughly 18
  per run (down from 132 pre-fix).
- Kibana panel "IMU Angular Rates" shows non-zero traces shortly
  after sim start.
