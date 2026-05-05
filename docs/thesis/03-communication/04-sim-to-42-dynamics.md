# 03.04 Simulators to 42 Dynamics (IPC Sockets)

This document describes how the per-component simulators read
ground-truth state from the 42 dynamics simulator. It pins to
figure F5 in [`../08-figures/figures.md`](../08-figures/figures.md).
This is the layer that carries the most testbed-specific scar
tissue; two non-obvious invariants live here, and both are listed
in the project root notes file under the load-bearing-edits
register.

## 1. 42 in the testbed

42 is a NASA Goddard spacecraft dynamics simulator that runs as
its own container, `sc01-fortytwo`, on the per-spacecraft
network. It propagates orbital and attitude state at 100 Hz and
exposes that state to the rest of the testbed through TCP
sockets. The configuration lives under
`nos3/cfg/InOut/`, with the per-mission overrides for the EO1
orbit (60 deg inclination, 346 deg RAAN, 61.6 deg true anomaly,
tuned for Denmark overflies) layered on top of the upstream 42
defaults.

The configuration is split into multiple files; the one that
matters for this layer is `nos3/cfg/InOut/Inp_IPC.txt`, which
declares each TCP socket 42 should open and what it should
publish on it.

## 2. The Inp_IPC.txt port table

Every IPC entry in `Inp_IPC.txt` is one TCP socket. The schema
per entry is:

- A header banner (`********** RW 0 from 42 ************`).
- An IPC mode (`OFF`, `TX`, `RX`, `TXRX`, `ACS`, `WRITEFILE`,
  `READFILE`).
- A file name (used only for `WRITEFILE`/`READFILE` modes).
- A socket role (`SERVER` or `CLIENT`).
- A host name and port. The host name is the Docker DNS name
  of the 42 container, `fortytwo`.
- Whether the socket allows blocking on `RX`.
- An echo-to-stdout flag.
- A count `N` of TX prefixes.
- `N` prefix strings, each a 42 expression like
  `SC[0].PosN` or `SC[0].GPS[0].PosW`.

42 looks at every published state-vector field per tick; for
each socket in `TX` mode, if any prefix matches the field name,
42 emits the field to that socket. The prefixes are therefore
the filter that controls how much data flows out per tick on
each port.

The EO1 mission's port table, after the DTU corrections:

| Port | Direction | Component | Mode | Prefix(es) |
|------|-----------|-----------|------|------------|
| 4277, 4278 | RW0 to/from 42 | reaction wheel 0 | TX, RX | `SC[0].Whl[0].H`, `SC` |
| 4279 / 4378 / 4478 / 4578 | RW1-3 to/from 42 | reaction wheels 1-3 | TX, RX | per-wheel |
| 4280 | thruster | `OFF` (load-bearing) | - | - |
| 4281 | IMU TX | TX | `SC[0].B[0].wn`, `SC[0].B[0].DCM` |
| 4282 | star tracker | `OFF` (load-bearing) | - | - |
| 4283 | mag TX | TX | `SC[0].B[0].MagModel.Field` |
| 4284 | CSS TX | TX | `SC[0].svn` |
| 4285 | FSS TX | TX | `SC[0].svn` |
| 4286 | TT&C TX | TX, tight prefix | `SC[0].GPS[0].PosW`, `SC[0].GPS[0].VelW` |
| 4287 | GNSS TX | TX, tight prefix | `SC[0].GPS[0].PosW` |

Port numbers and prefixes above are the active values in
`nos3/cfg/InOut/Inp_IPC.txt`. The two invariants stated in the
project root notes file follow.

## 3. Invariant 1: ports 4280 and 4282 must stay OFF

Port 4280 is the thruster port. The EO1 spacecraft profile at
`nos3/cfg/spacecraft/sc-mission-config.xml` sets
`<thruster><enable>false</enable>`, so the thruster simulator
exits early during initialisation. No client connects to port
4280.

Port 4282 is the star tracker port. The active sim XML uses
`GENERIC_STAR_TRACKER_PROVIDER`, the synthetic provider, not the
`_42_PROVIDER` variant; nothing in the EO1 mission connects to
port 4282 either.

If either entry is set to `TX` or `RX`, 42 opens the listening
socket and waits for a client. The block happens at the
`accept(2)` call in 42's `InitInterProcessComm`; in Linux's
kernel-level wait queue the call sits in `inet_csk_accept`. With
no client, 42 hangs indefinitely and the whole sim stalls at
startup. The fix is to leave both entries `OFF`. The build copy
of `Inp_IPC.txt` had been hand-patched to `OFF` once before the
source was; the source is now aligned, which prevents
`make config` from regenerating the broken state.

## 4. Invariant 2: tight prefixes on 4286 and 4287

Ports 4286 (TT&C) and 4287 (GNSS) carry data the EO1 mission
actively consumes. The published prefixes must stay tight:

- 4286 carries exactly two prefixes: `SC[0].GPS[0].PosW` and
  `SC[0].GPS[0].VelW`. Count line `2`.
- 4287 carries exactly one prefix: `SC[0].GPS[0].PosW`. Count
  line `1`.

The reason is the kernel TCP send buffer at the writer side and
the byte-by-byte reader at the consumer side. Walk through:

1. 42's writer is `WriteToSocket` at
   `~/.nos3/42/Source/AutoCode/TxRxIPC.c:464-468`. The socket is
   non-blocking and the writer calls `write(2)` once. If the
   call returns `-1` (typically `EAGAIN` because the kernel
   send buffer is full), `WriteToSocket` calls `exit(1)` and
   the entire 42 process dies.
2. The consumer is `Sim42SocketProvider::rgetc` at
   `nos3/sims/sim_common/src/sim_data_42socket_provider.cpp:218-230`.
   It reads one byte at a time with no batched buffering.
3. With wide prefixes (for example the upstream defaults `SC`
   or `Orb`), 42 emits roughly 5 KB of state per tick at 100 Hz.
   That is half a megabyte per second per socket. The
   byte-by-byte rgetc cannot drain the kernel send buffer at
   that rate; the buffer fills, `write(2)` returns `EAGAIN`,
   and 42 exits.
4. With the tight prefixes, the per-tick payload drops to
   roughly 150 bytes per socket. The send buffer no longer
   fills; 42 stays alive.

The two invariants together (OFF on 4280/4282, tight on
4286/4287) are the configuration that keeps 42 stable through
mission durations. Both are explicit entries in the project
root notes file's load-bearing-edits register.

## 5. The bracket-stripping parser

42 emits vector values as space-separated triples wrapped in
square brackets, for example `[1.234e6 -5.678e5 9.012e4]`. The
TT&C and GNSS simulators consume this through their data-point
parsers at:

- `nos3/components/generic_tt_c/sim/src/generic_tt_c_data_point.cpp`
- `nos3/components/generic_gnss/sim/src/generic_gnss_data_point.cpp`

The parsers' `do_parsing` routine includes a sanitiser that
strips `[`, `]`, and the comma separator before feeding the
remainder to a `std::stringstream` `>>` chain. Without the
strip, `>> double` fails on the leading `[`, the
DataPoint::valid flag stays false, and the corresponding
HK packet stays `INVALID` forever. Truth42 (the canonical
upstream consumer) avoids this by using
`Sim42DataPoint::parse_double_vector`; the DTU TT&C and GNSS
sims chose to keep the simpler `do_parsing` path and add the
strip, which is what the load-bearing-edits register
documents.

## 6. The comment-length quirk

42's `InitInterProcessComm` uses `char junk[120]` together with
an unbounded `%[^\n]` `fscanf` to swallow the trailing comment
on each `Inp_IPC.txt` line (see
`~/.nos3/42/Source/42ipc.c:43`). Comment text longer than
roughly 100 characters overflows the buffer and corrupts the
next field, producing the diagnostic
`Bogus input <garbled> in DecodeString (42init.c:249)` and
exit. The convention enforced by the load-bearing-edits register
is to keep all comments in `Inp_IPC.txt` under that limit.

## 7. Why this layer is the most fragile

Three properties combine here:

- The writer crashes the process on the first failed write
  rather than retrying or buffering.
- The reader is byte-by-byte and has no flow control.
- The kernel send buffer is finite (Linux defaults around
  208 KB) and not configurable from inside the simulator.

The result is a configuration whose stability depends on the
ratio of writer rate to reader rate, mediated by the kernel
buffer. The DTU configuration sits inside the safe ratio for
the EO1 mission tick budget; widening the prefixes back to the
upstream defaults pushes the system across the line and fails
within seconds. This is why edits to `Inp_IPC.txt` are flagged
as load-bearing.

## 8. Cross-references

- The IPC table source: `nos3/cfg/InOut/Inp_IPC.txt`.
- 42 writer: `~/.nos3/42/Source/AutoCode/TxRxIPC.c:464-468`.
- 42 IPC init: `~/.nos3/42/Source/42ipc.c:43`.
- Consumer: `nos3/sims/sim_common/src/sim_data_42socket_provider.cpp:218-230`.
- TT&C parser:
  `nos3/components/generic_tt_c/sim/src/generic_tt_c_data_point.cpp`.
- GNSS parser:
  `nos3/components/generic_gnss/sim/src/generic_gnss_data_point.cpp`.
- Project root notes file load-bearing-edits register, entries
  for ports 4280, 4282, 4286, 4287, the bracket-stripping
  parser, and the comment-length quirk.
