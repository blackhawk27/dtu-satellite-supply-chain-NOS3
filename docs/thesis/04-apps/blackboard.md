# 04. blackboard - Shared Telemetry Bulletin

This document describes the `blackboard` simulator, a shared
data-point bulletin board used by other simulators when they need
to read state another simulator publishes. The DTU-relevant
change is the empty-data-point skip from commit `6cff4102`.

## 1. Role

Several simulators in the testbed need to read state another
simulator publishes; for example, the EPS simulator needs the
solar-array status and the magnetic torquer's commanded dipole.
Rather than each consumer subscribing to each producer
individually, the blackboard simulator acts as a shared
bulletin: producers publish their data points to the
blackboard, and consumers read whichever entries they care
about.

The blackboard runs on the per-spacecraft network and
participates in the simulator XML the rest of the components
use.

## 2. App overview

The component lives at `nos3/components/blackboard/sim/`:

- `src/blackboard_42_data_provider.cpp`: 42 IPC consumer.
- `src/blackboard_data_point.cpp`: data-point handler.
- `src/blackboard_hardware_model.cpp`: bus simulator.

There is no flight-software counterpart; blackboard is
sim-only.

## 3. Headline change: empty-data-point skip

Commit `6cff4102` adds an early-return guard in
`blackboard_data_point.cpp`:

```
if (get_lines().empty()) {
    return;
}
```

Without this guard, an empty `get_lines()` result reached the
parser, which then attempted to tokenise an empty buffer. The
downstream parse error propagated up and crashed the simulator.
The early return is small but turns a hard failure into a
silent skip, which is the correct behaviour for an empty
bulletin entry.

## 4. ELK extraction

Blackboard log lines tag-strip into the same `system_log`
ingest path as the other simulator logs. The field set is
mostly intermediate state; no Kibana panel is dedicated to
blackboard, but its health is visible through the absence of
"empty buffer" parser-crash lines after the fix.

## 5. Attack-surface relevance

- **Component supply**: a tampered blackboard simulator can
  forward forged values to consumers without changing any
  consumer's source. The consumers trust whatever the
  blackboard publishes.
- **Robustness**: the empty-data-point case is the exception;
  intentional injection of malformed bulletin entries is the
  attack.

## 6. DTU divergence vs upstream

- Commit `6cff4102`: empty-data-point early return. Two-line
  fix; turns a downstream parser crash into a silent skip on
  the empty case.

## 7. Verification

- `nos3-blackboard.log` post-launch: no "parse error on empty
  buffer" lines.
- The simulator container stays up across an entire run; no
  crash-and-restart cycle is visible in `docker ps` uptime.
