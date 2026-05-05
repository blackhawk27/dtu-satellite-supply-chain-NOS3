# 03.03 Flight Software to Simulators (NOS Engine Buses)

This document describes how the cFS Hardware Library (HWLIB) talks
to the per-component simulator containers. It pins to figure F4 in
[`../08-figures/figures.md`](../08-figures/figures.md).

## 1. The HWLIB contract

cFS apps that drive a hardware peripheral do not call OS-level
device drivers directly. They call the Hardware Library, an
abstraction that exposes a small set of bus-flavoured APIs:

- `hwlib_i2c_*` for I2C peripherals (EPS, IMU, MAG).
- `hwlib_spi_*` for SPI peripherals (reaction wheel).
- `hwlib_can_*` for CAN peripherals (Arducam).
- `hwlib_uart_*` for UART peripherals (GNSS, TT&C, radio).

Each call has two implementations linked into the FSW image:

- A real-hardware backend that talks to a Linux kernel device
  (`/dev/i2c-N`, `/dev/spidev*`, `/dev/can*`, `/dev/ttyS*`).
  This is the path that runs on the flight target.
- A NOS Engine backend that publishes the same call as a
  message on a NOS Engine bus URI. This is the path that runs
  in the testbed container.

The selection happens at link time. The testbed FSW is built
with the NOS Engine backend; the flight FSW would be built with
the real-hardware backend. The application code above HWLIB is
byte-identical in both cases. This is the core property that
makes the testbed a meaningful surrogate for flight.

## 2. NOS Engine in the testbed

NOS Engine is a message-bus middleware. It runs as a separate
process on `nos3-core` (the time-driver container) and exposes
named buses that publishers and subscribers connect to. The
testbed creates one bus per peripheral type, and each
simulator subscribes to its own bus URI.

The bus-URI assignments live in
`nos3/cfg/sims/nos3-simulator.xml` (the active configuration is
selected by `nos3/cfg/nos3-mission.xml`); the same file declares
each simulator container, the bus the simulator joins, and the
peripheral address (I2C address, SPI chip select, etc.) the FSW
should use.

The two co-existing transports for NOS Engine:

- **Sockets**: each bus is a TCP socket on `nos3-core`. This is
  the default for the EO1 mission and is used in
  `nos3-simulator.sockets.xml`.
- **Shared memory**: a faster local-only transport exists for
  benchmark runs and is used in `nos3-simulator.shmem.xml`.
  Not used in the EO1 mission set.

## 3. Per-bus topology

### 3.1 I2C

The I2C bus is the busiest in the testbed. Three components
sit on it:

- `generic_eps_sim` at I2C address `0x2B`. The FSW driver lives
  at `nos3/components/generic_eps/fsw/cfs/`; the simulator at
  `nos3/components/generic_eps/sim/`.
- `generic_imu_sim` at I2C address `0x68`.
- `generic_mag_sim` at I2C address `0x0C`.

Each simulator is a long-running process that polls its bus URI
for a read/write call, services it, and emits a response.
Hardware-style timing is approximated; real I2C timing is faster
than the testbed cares to model.

### 3.2 SPI

`generic_reaction_wheel_sim` is the only SPI peripheral in the
EO1 mission. It uses the SPI bus URI declared in the simulator
XML and responds to FSW reads with a 4-wheel momentum vector
the ADCS app uses.

### 3.3 CAN

`arducam_sim` (the imager simulator) uses the CAN bus. The CAN
backend is included in HWLIB but the EO1 mission rarely
exercises it; the imager is enabled only in the
`sc-research-config.xml` profile.

### 3.4 UART

The UART bus carries the higher-bandwidth components:

- `generic_gnss_sim`: GNSS receiver, UART NMEA-style frames.
- `generic_tt_c_sim`: TT&C radio, UART frames.
- `generic_radio_sim`: legacy `generic_radio` placeholder; the
  EO1 mission uses `generic_tt_c` for the comms link.

Each UART simulator carries its own line discipline (line-mode
or framed); the FSW driver knows which to expect because the
simulator declares it in the simulator XML.

## 4. How a packet crosses

The data-flow contract is symmetric for all four bus types.
Walk through a representative I2C read for EPS:

1. `generic_eps` cFS app pipes a read command into the EPS
   driver in `nos3/components/generic_eps/fsw/cfs/`.
2. The driver calls `hwlib_i2c_master_transaction` with the
   target I2C address `0x2B`. The NOS Engine backend translates
   this into a publish on the I2C bus URI.
3. NOS Engine routes the message to `generic_eps_sim`, which
   has subscribed to that URI with the same address.
4. The simulator computes the response (battery voltage, solar
   power, etc.) inside its hardware model
   (`nos3/components/generic_eps/sim/src/generic_eps_hardware_model.cpp`)
   and publishes a reply.
5. NOS Engine delivers the reply to the FSW driver. The HWLIB
   call returns. The cFS app proceeds.

The simulators that need orbital ground truth (attitude,
position, time of flight) read it from 42 over the IPC sockets
described in the next document
([`04-sim-to-42-dynamics.md`](04-sim-to-42-dynamics.md)) before
they synthesise the bus reply. The TT&C and GNSS simulators
follow this pattern: 42 publishes the spacecraft state vector,
the simulator parses it, and the bus reply to the FSW carries
GNSS- or TT&C-shaped data derived from that state.

## 5. The simulator base classes

Every component simulator inherits from
`Sim_Hardware_Model` in
`nos3/sims/sim_common/inc/sim_hardware_model.hpp`. The base class
registers the simulator with NOS Engine and provides the
publish/subscribe glue. The simulators that ingest 42 ground
truth additionally instantiate a `Sim42SocketProvider` (its
implementation lives at
`nos3/sims/sim_common/src/sim_data_42socket_provider.cpp`).

The single most important detail of `Sim42SocketProvider` for
the testbed is the byte-by-byte read loop, called `rgetc`,
spanning lines 218 through 230 of
`nos3/sims/sim_common/src/sim_data_42socket_provider.cpp`. That
function is the consumer at the receiving end of the 42 IPC
sockets and is the kernel of the backpressure pathology
documented in the next layer document.

## 6. Failure modes at this layer

- **Bus-server not joined**: the simulator container starts but
  fails to subscribe (typically because `nos3-simulator.xml`
  has not been regenerated after a `make config`). The FSW
  driver then sees timeouts on every read and the corresponding
  HK packet stays at zero. The fix is `make config` followed by
  `make` to rebuild the simulator XML and the FSW.
- **First-tick parse failures**: several simulators dropped
  the first 42 IPC packet because their parser called
  `std::stof("")` on an empty pre-tick string. Commit
  `962b61e7` added an `if (line.empty()) continue;` guard in
  the affected ADCS-cluster simulators. The deep-dive
  documents under [`../04-apps/`](../04-apps/) cite the
  specific files.
- **Address collisions**: I2C address collisions (two
  components claiming the same address) have shown up after
  copy-paste mistakes during component additions. Detection is
  manual: the misbehaving simulator either produces zero
  responses or interleaved garbled ones. The check is to grep
  the simulator XML for duplicate `<address>` entries before
  rebuilding.

## 7. Cross-references

- The simulator XML schema:
  `nos3/cfg/sims/nos3-simulator.xml` (and the
  `.shmem.xml`/`.sockets.xml` siblings).
- The shared base class:
  `nos3/sims/sim_common/inc/sim_hardware_model.hpp`.
- The 42 ingest layer:
  [`04-sim-to-42-dynamics.md`](04-sim-to-42-dynamics.md).
- Per-component simulator details:
  [`../04-apps/`](../04-apps/) deep-dive set.
