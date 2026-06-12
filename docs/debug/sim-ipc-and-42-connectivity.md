# Simulator IPC and 42 connectivity

The 42 dynamics engine talks to the simulators over TCP sockets configured
in `nos3/cfg/InOut/Inp_IPC.txt` (build copy: `cfg/build/InOut/Inp_IPC.txt`).
At startup 42's `InitInterProcessComm` walks that file sequentially and
calls `accept()` (for TX/RX entries) on each socket in order. If any entry
is in a connecting mode but no client ever connects, `accept()` blocks
forever and 42 never reaches the entries below it (or `glutMainLoop`). Most
of the entries below are load-bearing for exactly this reason.

The 42 source referenced here is the runtime-extracted copy under
`~/.nos3/42/Source/`.

## Star Tracker IPC entry (port 4282) must stay OFF

- Entry 16 (port 4282, Star Tracker) IPC mode must remain `OFF`.
- Reason: the active sim XML uses `GENERIC_STAR_TRACKER_PROVIDER`, not the
  `_42_PROVIDER` variant, so nothing connects to `fortytwo:4282`. With this
  entry set to `TX` (its upstream value), 42 hangs indefinitely in
  `inet_csk_accept` and the whole sim stalls at startup.

## Thruster IPC entry (port 4280) must stay OFF

- The Thruster IPC entry (port 4280) IPC mode must remain `OFF`.
- Reason: `<thruster><enable>false</enable>` in
  `cfg/spacecraft/sc-mission-config.xml`, so the thruster sim exits early
  ("Simulator 'generic-thruster-sim' is not active") and nothing connects to
  `fortytwo:4280`. The source and build copies must agree: the build copy
  had been hand-patched to `OFF` in a prior session while the source was
  still `RX`, so the next `make config` would have regressed the build copy.
  Keeping the source `OFF` prevents that latent regression. `OFF` is parsed
  but bind/listen/accept is skipped, so the socket count is unchanged.
- Verification after `make config`:
  `diff cfg/InOut/Inp_IPC.txt cfg/build/InOut/Inp_IPC.txt` shows no
  Thruster-line difference.

## Blackboard IPC entry (port 4285) needs its sim running, or set it OFF

- Symptom: `make launch` brings up everything except FSW. 42's process state
  is `S (sleeping)` with `wchan = inet_csk_accept` indefinitely, no GLUT
  windows open, and `docker logs sc01-fortytwo` ends at "Server is listening
  on port 9999" without establishing. Decoding `/proc/1/net/tcp` in the 42
  container shows port 4285 stuck in LISTEN while 4286 and 4287 never start.
- Root cause: entry 18 is the Blackboard socket (port 4285), `TX` and marked
  `active=true`. `scripts/ci_launch.sh` did not include `blackboard-sim` in
  its sim startup loop even though `libblackboard_sim.so` is built and the
  XML marks the entry active. With no client on 4285, `accept()` blocks
  forever, so the TT&C (4286) and GNSS (4287) entries never listen and
  `generic_tt_c`/`generic_gnss` cannot stream. Same class as the 4282 and
  4280 hangs above.
- Fix: append `blackboard-sim` to the `scripts/ci_launch.sh` sim launch
  loop. Port 4285 then reaches ESTABLISHED within about 3 seconds and 42
  advances to 4286/4287. This is load-bearing until either the Blackboard
  IPC entry is set `OFF` or `blackboard-sim` is marked
  `<active>false</active>` in `nos3/cfg/sims/nos3-simulator*.xml`.

## TT&C (4286) and GNSS (4287) prefixes must stay tight

- TT&C lists exactly `"SC[0].GPS[0].PosW"` and `"SC[0].GPS[0].VelW"`
  (count 2); GNSS lists exactly `"SC[0].GPS[0].PosW"` (count 1). Do not
  widen these back to `"SC"` or `"Orb"`.
- Reason: 42's `WriteToSocket`
  (`~/.nos3/42/Source/AutoCode/TxRxIPC.c:464-468`) calls `write()` on a
  non-blocking socket and `exit(1)`s on -1. With broad prefixes, 42 emits
  about 5 KB per tick at 100 Hz to each socket; the simulator's byte-by-byte
  `rgetc` reader
  (`nos3/sims/sim_common/src/sim_data_42socket_provider.cpp:218-230`) cannot
  drain the kernel TCP send buffer fast enough, 42 hits EAGAIN within
  seconds and crashes. Tight prefixes drop the per-tick payload to about
  150 bytes and keep 42 alive.

## Inp_IPC.txt comment lines must stay short (under ~100 chars)

- Reason: 42's `InitInterProcessComm` (`~/.nos3/42/Source/42ipc.c:43`) uses
  `char junk[120]` with an unbounded `%[^\n]` `fscanf`; longer comment text
  overflows the buffer, corrupts the next field, and produces
  `Bogus input <garbled> in DecodeString (42init.c:249)` followed by exit.

## Simulator data-point parsers must strip 42's vector brackets

42 emits vector values as `[x y z]`. Scalar parsers that feed
`stringstream >> double` choke on the leading `[` and leave the data point
INVALID forever even when the line arrives.

- `nos3/components/generic_tt_c/sim/src/generic_tt_c_data_point.cpp` and
  `nos3/components/generic_gnss/sim/src/generic_gnss_data_point.cpp` strip
  `[` and `]` in the comma sanitiser at the start of `do_parsing`
  (`sP[i] == '[' || sP[i] == ']'`). Without the strip the data point stays
  INVALID. Truth42 avoids this by using
  `Sim42DataPoint::parse_double_vector()`.
- `nos3/components/blackboard/sim/src/blackboard_data_point.cpp` keeps the
  file-static `stof_safe` helper and the empty-frame deferral guard at the
  top of `do_parsing` (it probes `SC[N].svb`; if
  `_dp.get_value_for_key(...)` returns empty it logs DEBUG and returns). 42
  transiently returns empty strings during startup before any frame arrives;
  without these guards the parser threw on every tick for about 20 seconds
  at startup (about 5,500 `Error=stof` lines per run) and polluted Kibana via
  the `blackboard_error` tag in `nos3/elk/logstash.conf`. After the patch
  there are zero new ERROR docs: the deferral guard covers the brief startup
  window, then `stof_safe` carries the parse, and vector values continue to
  use `parse_double_vector` (which already strips brackets).

## fortytwo Docker-bridge connectivity (Connection timed out, port 4245)

- Symptom: `nos3-gps.log` loops on "Connection timed out, port 4245" to
  fortytwo. Every other `_42_PROVIDER` sim (rw0/1/2, torquer, css, mag, imu,
  fss, eps) shows the identical "Connection timed out" pattern.
- Diagnosis: this is a Docker-bridge / fortytwo connectivity issue, not an
  `Inp_IPC.txt` problem. (The Thruster `RX` entry was initially mistaken for
  the blocker; the disproof is that all `_42_PROVIDER` sims show the same
  pattern.)
- Runtime fix: `make stop && docker network rm nos3-sc01 && make launch`.
  This is an environment reset, not a code change.
