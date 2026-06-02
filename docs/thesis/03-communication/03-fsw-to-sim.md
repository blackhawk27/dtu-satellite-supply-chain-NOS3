# Boundary 3: flight software to hardware simulator

This boundary is where a cFS driver talks to its peripheral. The
driver believes it is performing register reads or wire transactions
against real hardware; in the simulator setting, those calls are
intercepted by the hardware abstraction library (HWLIB) and
delivered to the corresponding component simulator over a NOS
Engine bus.

## The HWLIB layer

`fsw/apps/hwlib/` is the cFS hardware abstraction library. It
exposes the bus APIs that component drivers use:

- `hwlib_i2c.h` and the I2C transaction primitives.
- `hwlib_spi.h` and the SPI primitives.
- `hwlib_can.h` for CAN frames.
- `hwlib_uart.h` for UART byte streams.

Each driver app under `fsw/apps/` and `components/<name>/fsw/cfs/`
calls HWLIB. The library is compiled with one of two
implementations chosen by the cFS build target: a real-hardware
implementation that talks to the operating system's device nodes,
and a NOS-Engine-backed implementation that forwards every call as
a NOS Engine bus message. The simulator builds use the NOS Engine
backend; the same FSW source code can be flown on real hardware by
selecting the real-hardware backend.

The point of HWLIB is that the driver source on the spacecraft is
not aware of the simulation. A bit-banged I2C read in
`generic_eps_device.c` looks like an I2C read; the simulator
intercepts it before it reaches the kernel and routes it to
`generic_eps_sim` instead. This is what makes attack research on
this fork transferable: a malicious cFS app that publishes EPS
telemetry under EPS MIDs would do exactly the same thing on a real
spacecraft.

## NOS Engine as the carrier

NOS Engine is a separate process (one container on `nos3-core`)
that hosts named buses. Each bus models one physical interface
(`I2C_0`, `SPI_0`, `CAN_0`, `UART_0`, and so on). NOS Engine
clients (the HWLIB backend on one side, the component simulator on
the other) connect to it over TCP and exchange messages framed as
NOS Engine messages.

Three properties of NOS Engine matter at this boundary:

- **It is time-synchronised.** Every NOS Engine bus reads from the
  NOS Engine time driver's tick. A simulator that says "advance
  10 ms" advances exactly when the time driver says so, and the
  whole stack moves in lockstep. Without this, attack experiments
  could not be deterministic.
- **It is deterministic in delivery order.** For a given bus, NOS
  Engine maintains a per-bus message ordering across all clients.
  A reader sees messages in the order the time driver fired them.
- **It does not authenticate clients.** Anything that can reach the
  TCP socket can attach as a client. Inside the `nos3-sc01` Docker
  network this is moot, because every container on that network
  is part of the simulated spacecraft.

## How a component round-trips

Take a representative I2C transaction: the EPS driver reads battery
voltage.

1. The cFS `generic_eps` app, on its periodic tick, calls
   `hwlib_i2c_master_transaction(...)` to read register 0x0A from
   the EPS device at I2C address 0x40.
2. HWLIB serialises the transaction as a NOS Engine message on
   bus `I2C_0` and writes it to the NOS Engine TCP socket.
3. NOS Engine delivers the message to every subscriber of bus
   `I2C_0`. `generic_eps_sim` is subscribed, recognises the
   target address as its own, and synthesises a response.
4. The response is sent back over the same bus. HWLIB returns it
   to the EPS driver.
5. The driver decodes the register value and publishes the EPS
   telemetry MID on the Software Bus
   ([../03-communication/02-fsw-software-bus.md](02-fsw-software-bus.md)).

The same shape applies to SPI (star tracker, IMU), CAN (some
torquer configurations), and UART (the GNSS receiver and the
TT&C). The chosen bus per component is recorded in that
component's `<bus>` element in
`cfg/spacecraft/sc-mission-config.xml`.

## Authentication and the attacker

At this boundary the attacker is assumed to already be inside the
cFS process (boundary 2). From there, two attack shapes are
relevant and both are observable in the testbed:

- **Issue a wire transaction the legitimate driver would not
  issue.** The malicious app can call HWLIB directly and address
  any device on any bus. The component simulator on the other
  side cannot distinguish a malicious caller from the legitimate
  one. This is concretely the same problem as the Software Bus
  having no per-publisher ACL.
- **Forge telemetry without going through the wire.** The cheaper
  path is to skip the bus entirely and publish a telemetry MID on
  the Software Bus directly, bypassing the driver. This is the
  "EPS spoof" mechanism that `noisy_app` exercises
  (`nos3/fsw/apps/noisy_app/fsw/src/noisy_app.c:73-78`).
  Detecting it requires correlating the Software Bus document
  stream against the NOS Engine traffic for the underlying bus;
  this fork does not capture NOS Engine traffic directly, so the
  correlation runs against simulator logs and the sequence
  counter only.

## Deviation from upstream

The NOS Engine carrier and HWLIB are unchanged from the import
baseline. Two component simulators on this side carry load-bearing
edits that look like parser fixes but are functionally
defence-against-42-quirks rather than defence-against-attack:

- `generic_tt_c_data_point.cpp` and `generic_gnss_data_point.cpp`
  defer on empty input and then strip `,`, `[`, and `]` by inline
  character substitution before parsing the result through a
  `std::stringstream` (no helper function; the strip is in the
  loop body at `generic_tt_c_data_point.cpp:35-36` and
  `generic_gnss_data_point.cpp:28`). Without the strip, the data
  points stay `INVALID` and the FSW reads stale state because 42
  emits vector values as `[x y z]`.
- `blackboard_data_point.cpp` uses a different but equivalent
  shape: a file-static `stof_safe` helper for scalars (which
  strips `,`, `[`, and `]`) plus the upstream
  `parse_double_vector` for vectors, together with an
  empty-frame deferral guard at the top of `do_parsing` keyed on
  the `SC[N].svb` probe.

These edits are detailed in the per-app files under
[../04-apps/](../04-apps/) and their invariants are recorded
inline at each parser. They sit at this boundary in the
narrative because they are about the simulator side of the
HWLIB-equivalent interface, not because they touch HWLIB itself.
