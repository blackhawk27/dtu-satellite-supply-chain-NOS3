# `generic_tt_c` (TT&C subsystem)

## Purpose

`generic_tt_c` is the telemetry, tracking, and command subsystem
simulator. It consumes the GPS truth stream from 42 (position and
velocity in the world frame) and publishes a corresponding cFS
telemetry packet so the FSW behaves as it would on an orbit where
TT&C is taking position and velocity fixes from a real receiver.
The component is a DTU addition; the upstream baseline did not
ship a TT&C simulator. It was introduced in commit `2b7dad40`
("feat(generic_tt_c): add TT&C sim component consuming 42 GPS
truth stream") and restored on `main` in commit `db78b8b1`
("fix(generic_tt_c): restore missing cFS app source on main")
after a branch-management mishap removed the FSW source.

## Source files

- `nos3/components/generic_tt_c/sim/src/generic_tt_c_42_data_provider.cpp`
  is the 42 TCP client. It connects to `fortytwo:4286` and
  subscribes to the narrow `"SC[0].GPS[0].PosW"` and
  `"SC[0].GPS[0].VelW"` prefixes; see
  [../03-communication/04-sim-to-42.md](../03-communication/04-sim-to-42.md)
  for why the prefixes must stay tight.
- `nos3/components/generic_tt_c/sim/src/generic_tt_c_data_point.cpp`
  is the parser. This is the file that carries the load-bearing
  bracket-handling edit.
- `nos3/components/generic_tt_c/sim/src/generic_tt_c_data_provider.cpp`
  and `..._hardware_model.cpp` are the standard sim-side wiring.
- `nos3/components/generic_tt_c/fsw/cfs/src/generic_tt_c_app.c`
  and its sibling headers are the cFS-side app, scheduled
  alongside the other component cFS apps.

## Wire-level surface

`generic_tt_c` allocates four CCSDS MIDs (declared in
`generic_tt_c_msgids.h`):

- **`0x1950` (`GENERIC_TT_C_CMD_MID`).** Ground commands.
- **`0x1951` (`GENERIC_TT_C_REQ_HK_MID`).** Periodic HK request.
- **`0x0950` (`GENERIC_TT_C_HK_TLM_MID`).** Housekeeping
  telemetry.
- **`0x0951` (`GENERIC_TT_C_DEVICE_TLM_MID`).** Device-level
  telemetry, including the position and velocity vectors derived
  from 42's truth.

On the simulator side, the wire is the TCP IPC line to 42 (port
4286). The data-point parser reads two prefixes:
`SC[0].GPS[0].PosW` (three-element position vector in
world-frame metres) and `SC[0].GPS[0].VelW` (three-element
velocity vector in world-frame metres per second). Both arrive
in 42's bracket-delimited form (`[x y z]`).

## Telemetry it emits

The DEVICE_TLM packet (`0x0951`) carries the position and
velocity vectors as six doubles. These are not extracted by the
Logstash pipeline directly (the GNSS component is the canonical
GPS source for Kibana); they are emitted so the FSW-side TT&C
logic can build link-budget calculations that depend on
position.

The HK packet (`0x0950`) carries the standard cFS app
housekeeping counters (command counter, command error counter,
sim-message counter).

Both MIDs are in the fork's `to_lab_sub.c`
(`nos3/cfg/nos3_defs/tables/to_lab_sub.c`, expanded in DTU
commit `8cad7cc8`), so they reach `cfs_god_view.json` and
Kibana.

## What was changed from upstream

The whole component is a DTU addition; there is no upstream
counterpart to diff against. Two specifics of the implementation
are load-bearing.

First, the parser's empty-frame and bracket-handling pattern.
The relevant block (lines 18 to 48 of
`generic_tt_c_data_point.cpp`) is structurally similar to the
blackboard edit but uses an inline char-substitution rather than
the helper-function form:

- Compute `keyP = "SC[" + sc + "].GPS[0].PosW"` and
  `keyV = "SC[" + sc + "].GPS[0].VelW"`.
- Read both strings from the data point.
- If either is empty, log a DEBUG line ("empty
  SC[%d].GPS[0].Pos/VelW frame, deferring") and return.
- Otherwise iterate every character of each string in place,
  substituting `,`, `[`, and `]` with whitespace
  (`generic_tt_c_data_point.cpp:35-36`).
- Parse with two `std::stringstream` objects, reading three
  doubles each into `_posW[0..2]` and `_velW[0..2]`. Set
  `_valid` based on the combined `fail()` flags.
- Wrap the whole thing in a try / catch and on exception log an
  ERROR via `sim_logger->error`.

The blackboard parser at `blackboard_data_point.cpp` uses the
helper `parse_double_vector` for vectors and a file-static
`stof_safe` for scalars; the TT&C parser inlines its own
bracket-strip and uses `stringstream` directly. Both achieve the
same effect (tolerate bracket-form vectors from 42 without
throwing). Earlier drafts of this document claimed
`parse_double_vector` was the helper here too; that was wrong
and has been corrected.

The same reasoning that makes the blackboard parser
defensive applies here: 42 emits empty strings transiently at
startup and bracket-form vectors on every tick, so without
both the deferral and the bracket-aware vector parser the data
point stays `INVALID` indefinitely.

Second, the IPC port. `cfg/InOut/Inp_IPC.txt` configures port
4286 as a TX entry with subscription prefixes
`"SC[0].GPS[0].PosW"` and `"SC[0].GPS[0].VelW"` and count `2`.
The narrow prefixes are deliberate: widening to `"SC"` or
`"Orb"` triggers 42's non-blocking-write crash hazard described
in [../03-communication/04-sim-to-42.md](../03-communication/04-sim-to-42.md).
The constraint is captured by the inline comment on the
port-4286 entry in `cfg/InOut/Inp_IPC.txt`.

## Load-bearing invariants

- **Port 4286 stays at exactly two subscription prefixes.**
  `Inp_IPC.txt` is the file; the entry's count value must read
  `2`, and the prefixes must be the two narrow forms above.
- **The parser must defer on empty input rather than throw.**
  Logged as DEBUG, not ERROR; the empty-frame condition is a
  normal startup transient, not a fault. A throw here cascades
  into the `tt_c_error` Logstash tag and pollutes Kibana.
- **MIDs `0x1950` / `0x1951` / `0x0950` / `0x0951` must stay
  unique in the fork's MID space.** They were allocated to slot
  into the `0x19xx` command and `0x09xx` telemetry space
  cleanly; collisions with another fork-added component would
  silently route the wrong subscriber.
- **The component is enabled in
  `sc-mission-config.xml`.** The default profile has it on;
  switching to `sc-minimal-config.xml` disables it. The narrow
  prefix constraint only applies while it is enabled.
