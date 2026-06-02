# `generic_gnss`

## Purpose

`generic_gnss` is the GNSS receiver simulator. It consumes the
GPS truth stream from 42 (position only, in the world frame) and
publishes a corresponding cFS telemetry packet. The fork uses it
in preference to the upstream `novatel_oem615` component for the
default mission profile because its message structs are
deliberately minimal and its Logstash filter coverage is the most
complete.

The component is a DTU addition. It was introduced in commit
`ee347c91` ("feat(generic_gnss): add new GNSS sim component, FSW
app, and COSMOS target"). The FSW source was lost from `main`
during a later merge and restored in commit `f766be00`
("fix(generic_gnss): restore missing cFS app source on main");
the recovery is mentioned because the original commit's
provenance is otherwise the only record of when the component
appeared.

## Source files

- `nos3/components/generic_gnss/sim/src/generic_gnss_42_data_provider.cpp`
  is the 42 TCP client. It connects to `fortytwo:4287` and
  subscribes to the single narrow prefix
  `"SC[0].GPS[0].PosW"`.
- `nos3/components/generic_gnss/sim/src/generic_gnss_data_point.cpp`
  is the parser; it carries the same empty-frame deferral and
  bracket-handling pattern as the TT&C parser, with the
  position-only key set.
- `nos3/components/generic_gnss/sim/src/generic_gnss_data_provider.cpp`
  and `..._hardware_model.cpp` are the standard sim-side wiring.
- `nos3/components/generic_gnss/fsw/cfs/src/generic_gnss_app.c`,
  `generic_gnss_device.c`, and the sibling headers are the cFS-
  side app. The `device.c` file is the per-receiver driver
  shim that fills in the device-telemetry payload from the sim
  output; the file restored in `f766be00` was this one.

## Wire-level surface

`generic_gnss` allocates five CCSDS MIDs (declared in
`generic_gnss_msgids.h`):

- **`0x1952` (`GENERIC_GNSS_CMD_MID`).** Ground commands.
- **`0x1953` (`GENERIC_GNSS_REQ_HK_MID`).** Periodic HK request.
- **`0x0952` (`GENERIC_GNSS_HK_TLM_MID`).** Housekeeping
  telemetry.
- **`0x0953` (`GENERIC_GNSS_DEVICE_TLM_MID`).** Device-level
  telemetry: the position fix in world-frame metres, the
  derived geodetic latitude / longitude / altitude, and an
  ECEF representation.
- **`0x0954` (`GENERIC_GNSS_SAT_HELLO_TLM_MID`).** A periodic
  "satellite hello" packet used by the fork's `gnss_gs_responder`
  COSMOS script as a ping target.

On the simulator side the wire is the TCP IPC line to 42 (port
4287). The parser reads a single prefix
`SC[0].GPS[0].PosW` (three-element position vector). Unlike the
TT&C component, no velocity vector is consumed; the receiver
model exposes position only.

## Telemetry it emits

The DEVICE_TLM packet (`0x0953`) is the canonical GPS source for
the Logstash pipeline. The relevant filter block extracts:

- `gps_lat`, `gps_lon`, `gps_alt`: geodetic position.
- `gps_ecef_x`, `gps_ecef_y`, `gps_ecef_z`: Earth-centred
  Earth-fixed position.
- `gps_abs_time`: GPS time at the fix.

These fields back the orbit-track view in Kibana (Figure F5 in
[../08-figures/figures.md](../08-figures/figures.md)) and the
`science_overfly` tagging in `mgr.md`'s filter block.

The SAT_HELLO packet (`0x0954`) is paired with the COSMOS
ground-station responder script
(`components/generic_gnss/gsw/cosmos/procedures/gnss_gs_responder.rb`)
to model a ground-station ping-pong handshake. The Logstash
pipeline tags command packets keyed off this MID as
`gnss_ping_uplink` so a saved search can find every
ground-issued ping in the run.

## What was changed from upstream

The whole component is a DTU addition. The upstream NOS3 ships
`novatel_oem615` as its GNSS reference; this fork keeps that
component in the tree but uses `generic_gnss` for the default
mission profile because its message structs are simpler to
filter against in Logstash and its `device.c` is easier to read
than the Novatel binary-protocol shim.

Two specifics carry across from the import:

- The parser pattern (lines 17 to 37 of
  `generic_gnss_data_point.cpp`) is the position-only twin of
  the TT&C pattern. It computes
  `keyP = "SC[" + sc + "].GPS[0].PosW"`, reads the value from
  the data point, defers with a DEBUG log if the string is
  empty, then iterates every character substituting `,`,
  `[`, and `]` with whitespace
  (`generic_gnss_data_point.cpp:28`), and parses with a
  `std::stringstream` directly. It does NOT call
  `parse_double_vector` (an earlier draft of this document
  claimed it did; that was wrong). The same defensive-parser
  invariant applies to this file and the TT&C file.
- Port 4287 in `cfg/InOut/Inp_IPC.txt` configures one
  subscription prefix
  (`"SC[0].GPS[0].PosW"`, count `1`). Widening triggers the
  42 non-blocking-write crash hazard.

## Load-bearing invariants

- **Port 4287 stays at one subscription prefix.** Count `1`,
  prefix `"SC[0].GPS[0].PosW"`. Adding a second prefix or
  widening to `"SC"` crashes 42.
- **The parser must defer on empty input rather than throw.**
  Same reasoning as for `generic_tt_c`.
- **The `0x095x` and `0x195x` MID slots must stay allocated to
  this component.** The Logstash pipeline keys its GPS
  extraction off `0x0953`; a different MID would silently
  break every dashboard that depends on `gps_lat`,
  `gps_lon`, `gps_alt`.
- **`generic_gnss_device.c` must stay present on `main`.** The
  `f766be00` restoration commit fixed a missing-on-main
  condition that broke the FSW build with a confusing CMake
  error rather than an obvious "missing file". The recurrence
  prevention is to keep this file under explicit test, but
  that test is not currently in CI; the file's existence is
  therefore a load-bearing invariant in the same sense as the
  `Inp_IPC.txt` entries.
