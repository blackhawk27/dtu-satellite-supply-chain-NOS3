# 04. generic_torquer - Magnetic Torquer

This document describes the magnetic-torquer cFS app and its
simulator. Magnetic torquers are the testbed's secondary
attitude actuator (after reaction wheels) and the only actuator
that does not store momentum. They are also the actuator
B-dot detumble uses.

The DTU-relevant change for this app, commit `6c67213e`, is the
Truth42 + Logstash parser pair that surfaces the torquer
commands and the 42 ground-truth attitude in the same
dashboard.

## 1. Real-world hardware

A magnetic torquer is a coil. Driven with current, it generates
a magnetic dipole; the dipole crossed with the local geomagnetic
field gives a body torque. The torque magnitude is small but
it is the simplest and lowest-mass actuator available and is
ubiquitous on small satellites.

## 2. App overview

The flight-software half lives at
`nos3/components/generic_torquer/fsw/cfs/`. The simulator half
lives at `nos3/components/generic_torquer/sim/`:

- `src/generic_torquer_42_data_provider.cpp`: 42 IPC consumer.
  The torquer simulator reads the spacecraft body-frame field
  `SC[N].B[0].MagModel.Field` (same as MAG, port 4283 in
  Inp_IPC.txt).
- `src/generic_torquer_hardware_model.cpp`: bus simulator; runs
  the torquer current model and emits the resulting dipole
  magnitude.

MIDs (component-base, `0x1900 / 0x0900`):

- Topic ID `0x3A`.
- Command: `0x193A`.
- HK telemetry: `0x093A`.

## 3. Headline function: Truth42 ELK pair

The headline change for this app is on the ELK side, not the
sim side. Commit `6c67213e` adds three new filter sections to
`nos3/elk/logstash.conf`:

- A `nos3-truth42` filter that extracts `SC[0].PosN`,
  `SC[0].VelN`, and the attitude quaternion (`q0` through `q3`)
  from a 42-side dump log. This is the ground-truth surface
  for cross-checking the FSW's estimated state.
- A torquer-sim filter that extracts the per-axis torquer
  current and the resulting dipole.
- A sample-sim filter that extracts the canary fields the
  upstream `sample` app emits, kept around as a regression
  baseline.

The pair (Truth42 + torquer) lets the dashboard show the FSW's
body torque commanded vs the 42 ground-truth attitude
response, which is the loop the supply-chain attack chains
need to render visibly.

## 4. ELK extraction

Logstash extracts:

- Torquer simulator log: `torquer_dipole_x/y/z`,
  `torquer_current_x/y/z`.
- Truth42 log: `truth_pos_x/y/z`, `truth_vel_x/y/z`,
  `truth_q0/q1/q2/q3`.

Kibana panels:

- "Torquer Commanded Dipole" on
  `nos3-std-telemetry-overview`.
- "Truth Attitude vs ADCS Estimate" on
  `nos3-std-mission-validation` (the Truth42 quaternion vs the
  ADCS HK quaternion).

## 5. Attack-surface relevance

- **Component supply**: a tampered torquer simulator can hide
  actuator failure (publish nominal dipole while drawing zero
  current).
- **Operations layer**: dropping the Truth42 filter from
  Logstash silences the cross-check that would flag forged
  ADCS estimates.

## 6. DTU divergence vs upstream

- Commit `6c67213e`: adds the Truth42, torquer, and sample
  Logstash filter sections; this is the ELK-side
  instrumentation that surfaces the torquer-and-attitude
  loop.

## 7. Verification

- Kibana: index-pattern field list contains
  `truth_q0..q3`, `torquer_dipole_x/y/z`. The
  index-pattern field-list refresh script
  (`nos3/elk/refresh_index_pattern_fields.py`) must run after
  the first ingest.
- "Truth Attitude vs ADCS Estimate" panel shows the two
  attitude time-series tracking each other within the
  expected ADCS estimation accuracy.
