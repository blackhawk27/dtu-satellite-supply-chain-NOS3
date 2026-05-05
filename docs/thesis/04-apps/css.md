# 04. generic_css - Coarse Sun Sensor

This document describes the Coarse Sun Sensor cFS app and its
simulator.

## 1. Real-world hardware

A coarse sun sensor estimates the sun direction in the
spacecraft body frame. CSS arrays typically use small
photodiodes mounted on each face; the differential between face
readings recovers the sun vector with a few-degree accuracy.
The EO1 mission's CSS feeds the sun-safe and inertial control
modes in ADCS.

## 2. App overview

The flight-software half lives at
`nos3/components/generic_css/fsw/cfs/`. The simulator half lives
at `nos3/components/generic_css/sim/`. The simulator consumes 42's
`SC[N].svn` (sun vector in body frame) on port 4284.

MIDs (component-base, `0x1900 / 0x0900`):

- Topic ID `0x10`.
- Command: `0x1910`.
- HK telemetry: `0x0910`.

Bus: I2C.

## 3. Headline function: the first-tick guard

The DTU-added empty-string guard appeared first in CSS (commit
`26279b3b`), then was generalised to the IMU/MAG/RW family in
the follow-up commit `962b61e7`. The pattern in
`Generic_cssDataPoint::do_parsing` is the template the others
copy:

```
if (_dp.get_value_for_key(key_svn).empty()) {
    sim_logger->debug(
        "Generic_cssDataPoint::do_parsing: empty SC[%d].svn frame, "
        "deferring to next tick", _sc);
    return;
}
```

The chronology matters for the thesis: the CSS commit was the
diagnostic stake-out (the author saw that the first-tick race
was real); the IMU/MAG/RW commit was the family generalisation.
Both are documented under `debug/ADCS_DIAGNOSIS.md`.

## 4. ELK extraction

Logstash extracts the CSS HK fields under `css_*`.

Kibana panel:

- "CSS Sun Vector" on `nos3-std-telemetry-overview`.

## 5. Attack-surface relevance

- **Component supply**: a tampered CSS simulator can publish a
  rotated sun vector; sun-safe mode fails to point at the sun.
  Fragile; eclipsed-state cross-check is the dashboard
  defence.

## 6. DTU divergence vs upstream

- Commit `26279b3b`: introduced the empty-string guard on
  first-tick `std::stof` in `generic_css_data_point.cpp`. This
  was the first instance of the family pattern.
- Plus the broader sweep of the same commit (LC AP-fail
  severity exception in Logstash, thruster CMD port note,
  HS_SysMon adjustment) addressed adjacent issues in the same
  pass.

## 7. Verification

- Kibana: `evs_severity:ERROR AND evs_message:*Parsing\\
  exception*` returns zero documents on a fresh launch
  attributable to CSS.
- "CSS Sun Vector" panel populates within a few seconds of sim
  start.
