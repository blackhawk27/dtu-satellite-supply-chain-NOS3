# Boundary 4: hardware simulator to 42 dynamics engine

Each component simulator that needs to know spacecraft truth (orbit
position, attitude, sun vector, magnetic field, wheel momentum)
connects directly to the 42 dynamics container over TCP. This is
the only wire boundary inside the spacecraft network that is not
mediated by NOS Engine, and it is by some distance the most
fragile.

## The wire protocol

42 is a TCP server. It listens on one port per IPC entry; the
mapping is defined by `nos3/cfg/InOut/Inp_IPC.txt`, which is
copied into `cfg/build/InOut/Inp_IPC.txt` by `make config` and
read by the 42 container at startup. Each entry declares:

- An IPC mode: `RX`, `TX`, `TXRX`, `ACS`, `WRITEFILE`, `READFILE`,
  or `OFF`.
- A socket role (always `SERVER` on this fork).
- A hostname and port: `fortytwo` and a 42xx port.
- A boolean for blocking semantics (always `FALSE`).
- A list of prefix strings the client wants to receive.

The simulators are clients. Each one opens a TCP connection to the
appropriate `fortytwo:42xx` port on the `nos3-sc01` network and
sends back a list of subscription prefixes. 42 responds by sending
that subset of its truth-state lines on every tick.

The line format is plain ASCII, one truth field per line, like
`SC[0].Whl[0].H = 0.01234567` or
`SC[0].GPS[0].PosW = [1234.5 -678.9 100.0]`. Vector values are
bracket-delimited. The per-tick payload to a given socket is the
concatenation of every line whose prefix matches one of the
client's subscription prefixes.

The relevant 42xx ports, drawn from `Inp_IPC.txt`:

- `4277`, `4377`, `4477` (RW0, RW1, RW2 outgoing).
- `4278`, `4378`, `4478` (RW0, RW1, RW2 incoming actuator commands).
- `4279` (Torquer).
- `4280` (Thruster, mode `OFF` on this fork).
- `4282` (Star Tracker, mode `OFF` on this fork).
- `4245` (GPS truth to GNSS sim).
- `4286` (TT&C, narrow `PosW`/`VelW` subscription).
- `4287` (GNSS, narrow `PosW` subscription).

The full table is in `Inp_IPC.txt`; the entries above are the ones
this fork has touched relative to the import baseline.

## Why this boundary is fragile

42's outbound write is the source of all the operational pain here.
The relevant code lives in upstream 42 at
`/home/vscode/.nos3/42/Source/AutoCode/TxRxIPC.c`, around line 464.
Two properties matter:

- The socket is set non-blocking on the 42 side.
- On a failed `write()` (typically `EAGAIN` because the kernel
  send buffer is full), 42 calls `exit(1)`. It does not retry, does
  not back off, does not log a recoverable warning. The whole 42
  process dies.

The reader (each simulator) uses a byte-by-byte `rgetc`-style
parser (`sim_data_42socket_provider.cpp` around line 218). At the
sim tick rate, an attached simulator can comfortably drain a few
hundred bytes per tick. At 42's 100 Hz emission rate the safe
budget per socket is therefore in the low hundreds of bytes per
tick.

When a subscription prefix is broad (`"SC"` or `"Orb"` instead of
`"SC[0].GPS[0].PosW"`), 42 emits roughly 5 KB of matching state
per tick to that socket. The kernel send buffer fills within
seconds, 42's non-blocking `write()` returns -1 with EAGAIN, 42
exits, and the entire simulation freezes. The first observation in
the system is the cFS clock stopping; the diagnosis requires
reading 42's container logs.

This is why the TT&C entry on port 4286 lists exactly
`"SC[0].GPS[0].PosW"` and `"SC[0].GPS[0].VelW"` (count `2`) and
why the GNSS entry on port 4287 lists exactly
`"SC[0].GPS[0].PosW"` (count `1`). Widening either prefix back
to `"SC"` or `"Orb"`, which were the upstream defaults, crashes
42 within seconds at 100 Hz. The reasoning is captured inline
at the port-4286 and port-4287 entries in `cfg/InOut/Inp_IPC.txt`.

## Why some IPC modes must stay `OFF`

A separate failure mode applies to entries whose corresponding
simulator is not running. The star tracker (port 4282) and the
thruster (port 4280) are disabled in `sc-mission-config.xml`. If
the Inp_IPC.txt entry for either is set to `TX` (its upstream
default), 42 enters its `inet_csk_accept` waiting for a client that
will never connect. The sim init blocks indefinitely; nothing
upstream of 42 makes progress.

Setting the mode to `OFF` is the operational fix: 42 then skips
that entry entirely. The two affected entries
(`cfg/InOut/Inp_IPC.txt` port 4282 and port 4280) carry inline
comments explaining why the mode is `OFF`. The structural fix
would be to generate `Inp_IPC.txt` from the spacecraft profile
at `make config` time so that mode follows `<enable>`. That is
a worthwhile future change but is out of scope here; the
documented constraint is the working state.

## The comment-buffer overflow trap

`InitInterProcessComm` in upstream 42 (`42ipc.c:43`) reads each IPC
entry's trailing comment with `fscanf("%[^\n]")` into a
fixed-size `char junk[120]` buffer. Comments longer than the buffer
overflow into the next field, producing
`Bogus input <garbled> in DecodeString (42init.c:249)` and an
immediate exit. The constraint is that every comment line on a
non-`OFF` entry must stay under roughly 100 characters. The
constraint is recorded inline at the top of `cfg/InOut/Inp_IPC.txt`.

## Authentication and threat model

There is no authentication on this boundary. Any process on the
`nos3-sc01` network can open a TCP connection to any of the 42xx
ports and subscribe to any prefix. In a malicious-component
scenario, a tampered hardware simulator could subscribe to the
attitude truth stream and report falsified sensor readings back to
the FSW.

This boundary therefore extends the supply-chain threat model from
the FSW side onto the simulator side: a compromised component
simulator can lie about its instrument, and the FSW has no way to
detect it through this channel. Detection is only possible by
cross-checking the FSW's published telemetry MIDs against an
independent truth source. The Logstash pipeline does not currently
attempt that correlation; it is a candidate addition.

## Deviation from upstream

The wire protocol, the port allocations, and 42 itself are
unchanged from the import baseline. What this fork changes are the
contents of `Inp_IPC.txt`:

- Port 4282 (Star Tracker): `TX` upstream, `OFF` here. Reason: the
  active sim XML uses `GENERIC_STAR_TRACKER_PROVIDER`, not the
  `_42_PROVIDER` variant; nothing was ever going to connect.
- Port 4280 (Thruster): `RX` upstream, `OFF` here. Reason:
  `<thruster><enable>false</enable>` in the spacecraft profile.
- Port 4286 (TT&C): prefix narrowed from `"SC"` to
  `"SC[0].GPS[0].PosW"` plus `"SC[0].GPS[0].VelW"`.
- Port 4287 (GNSS): prefix narrowed from `"SC"` (or `"Orb"`) to
  `"SC[0].GPS[0].PosW"`.
- Comment trimming on every touched line so the 120-byte buffer in
  `InitInterProcessComm` does not overflow.

All five entries carry inline comments at their corresponding
lines in `cfg/InOut/Inp_IPC.txt` recording the diagnostic
reason.
