# `blackboard`

## Purpose

`blackboard` is the internal data-exchange component on the
simulator side. It pulls a curated set of spacecraft-body-frame
state vectors out of the 42 truth stream (sun vector in body
frame, magnetic field in body frame, momentum in body frame,
per-gyro true rates, per-CSS validity flags, and a few related
fields) and exposes them to other simulators as a shared
"blackboard". Components that need a quick read of the truth
without subscribing to 42 directly read from the blackboard. The
mission manager and the ADCS rely on this indirection.

The component predates the DTU fork; it was present at the
import baseline. It is documented here because the parser on the
sim side carries a load-bearing edit (DTU commit `a0d796f6`).

## Source files

- `nos3/components/blackboard/sim/src/blackboard_data_point.cpp`
  is the file that parses 42 truth strings into typed fields.
  This is the file the DTU fork has changed.
- `nos3/components/blackboard/sim/src/blackboard_42_data_provider.cpp`
  is the upstream TCP client to 42; it pumps lines into the data
  point.
- `nos3/components/blackboard/sim/src/blackboard_hardware_model.cpp`
  exposes the parsed fields to other simulators.
- `nos3/components/blackboard/fsw/cfs/` carries the cFS
  app-side interface (not modified by the fork; included here
  for completeness).

## Wire-level surface

The blackboard does not own a Software Bus MID range. It is a
simulator-side service: other component simulators query it for
truth values rather than going to 42 themselves. The values it
exposes correspond to 42's per-spacecraft variables in
`SC[N].*`. The set the parser pulls in the current revision:

- `SC[N].svb`: sun vector in body frame, three doubles, bracket-
  delimited.
- `SC[N].bvb`: magnetic field in body frame, three doubles.
- `SC[N].Hvb`: angular momentum in body frame, three doubles.
- `SC[N].Gyro[i].TrueRate` for `i` in `0..2`: per-gyro true
  body rate, scalar.
- `SC[N].CSS[i].Valid` for `i` in `0..5`: per-coarse-sun-sensor
  validity flag, scalar.

Vector values use 42's bracket form (`[x y z]`); scalar values
are bare doubles. The parser uses `parse_double_vector` for the
brackets and `stof_safe` for the scalars.

## Telemetry it emits

Nothing on the Software Bus. The blackboard's outputs are
consumed in-process by other simulators and reach Logstash only
through whatever those downstream simulators choose to log.

## What was changed from upstream

The DTU edit is commit `a0d796f6` ("fix(blackboard sim): guard
parser against empty/bracketed 42 frames"). Three concrete
changes in `blackboard_data_point.cpp`:

- **A file-static `stof_safe` helper.** Replaces direct
  `std::stof` calls on every scalar field. The helper strips
  `,`, `[`, and `]` characters before calling `std::stof` and
  returns `0.0` on an all-whitespace string. The reason is
  upstream-42 behaviour: 42 emits scalar values without
  brackets but its truth-string lines occasionally contain the
  bracketed vector form when 42 has not yet populated its
  state, and any bracket inside the input throws
  `std::invalid_argument` from `std::stof`. Vector fields go
  through `parse_double_vector` (which already strips brackets
  via `Sim42DataPoint::parse_double_vector()`) and do not need
  the helper.
- **An empty-frame deferral guard at the top of `do_parsing`.**
  Probes `SC[N].svb`; if `_dp.get_value_for_key(probe_key)`
  returns an empty string, the function logs a DEBUG line and
  returns. The reason is that 42 transiently returns empty
  strings during sim startup, before any frame has arrived;
  without the guard, the parser threw on every tick for the
  first roughly twenty seconds of every run and produced
  thousands of error log lines.
- **An `error` tag side-effect mitigation.** The thrown
  exceptions had been caught and logged with `sim_logger->error`,
  which was tagged by `nos3/elk/logstash.conf` lines 815 through
  816 as `blackboard_error`. With the deferral and `stof_safe`
  in place, the catch block is no longer reached during startup
  and the `blackboard_error` tag does not appear in clean runs.
  Verified on 2026-05-09: zero new ERROR documents in
  Elasticsearch from blackboard during the first thirty seconds
  of launch.

The two upstream files (`blackboard_42_data_provider.cpp` and
`blackboard_hardware_model.cpp`) are unchanged.

## Load-bearing invariants

The constraints on this file are encoded inline at the
relevant sites (`blackboard_data_point.cpp:8` for the helper,
`:27-44` for the deferral guard, `:49-160` for the vector
path). The summary:

- The `stof_safe` helper must keep stripping all three of `,`,
  `[`, and `]`. Earlier drafts of the patch only stripped
  brackets and produced silent comma-handling regressions when
  42's locale settings produced comma-delimited vectors.
- The empty-frame deferral must remain an early `return`, not a
  `throw`. A `throw` is caught by the outer block, logged as
  ERROR, and tagged by Logstash; the run looks broken in
  Kibana even though every later tick parses correctly.
- Vector fields must continue to use `parse_double_vector` (not
  `stof_safe`). The former already handles brackets and
  preserves the three-element output structure; the latter
  returns one scalar per call.

## Note on `to_lab_sub.c`

Earlier drafts of these documents referred to "`to_lab_sub.c` in
`components/blackboard/fsw/cfs/configs/`". The file does not live
there. The canonical location is
`nos3/cfg/nos3_defs/tables/to_lab_sub.c` (mission-level table),
copied into `nos3/cfg/build/nos3_defs/tables/to_lab_sub.c` by
`make config`. The DTU edit that expanded it is commit
`8cad7cc8` ("Added all msgid's to to_lab_sub.c, so now they all
show up i Kibana"); the upstream sample-app default also exists
at `nos3/fsw/apps/to_lab/fsw/tables/to_lab_sub.c` but is
overridden by the mission table at config time. The table is
discussed at the FSW-to-GSW boundary in
[../03-communication/05-fsw-to-gsw.md](../03-communication/05-fsw-to-gsw.md);
its content is not part of the blackboard component despite the
historical mis-attribution.
