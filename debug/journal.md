# debug/journal

Investigation log for load-bearing fixes. Entries are append-only;
oldest first. Entry header: `## YYYY-MM-DD HH:MM UTC summary`.

## 2026-05-07 02:00 UTC: blackboard-sim missing from ci_launch.sh sim list

### Symptom

`make launch` brings up everything except FSW. 42 process state shows
`State: S (sleeping)` with `wchan = inet_csk_accept` indefinitely. No
GLUT windows ever open even though `Inp_Sim.txt` has
`Graphics Front End? = TRUE` and `DISPLAY=:0` is plumbed into the
container. `xwininfo -root -tree` only shows the WSLg `Weston WM`
window, no `42 Cam` / `42 Map` / `42 Unit Sphere Viewer`.

`docker logs sc01-fortytwo` ends with:

```
Server is listening on port 4234
Server side of socket established.
Server is listening on port 9999     <-- last line, never establishes
```

### Diagnosis

`/proc/1/net/tcp` inside the 42 container, decoded:

```
4227 ESTAB | 4234 ESTAB | 4245 ESTAB | 4277..4477 ESTAB | 4279 ESTAB
4281 ESTAB | 4283 ESTAB | 4284 ESTAB | 4285 LISTEN          <-- stuck
9999 ESTAB
```

42's `InitInterProcessComm` walks `Inp_IPC.txt` sequentially and calls
`accept()` on each TX/RX socket. Entry 18 is the Blackboard socket
(port 4285) which is `TX` mode and listed `active=true` in
`cfg/sims/nos3-simulator.sockets.xml`. With no client connecting to
4285, accept() blocks forever and the next two entries (TT&C 4286 and
GNSS 4287) never get to listen, so FSW's `generic_tt_c` and
`generic_gnss` apps cannot stream data and 42's main loop never
reaches `glutMainLoop`.

`ci_launch.sh`'s sim startup loop did NOT include `blackboard-sim`,
even though the simulator binary `libblackboard_sim.so` is built and
the simulator XML marks the entry `active=true`. The pattern is
identical to the port-4282 (Star Tracker) and port-4280 (Thruster)
hangs already documented in CLAUDE.md.

### Fix

`nos3/scripts/ci_launch.sh`: append `blackboard-sim` to the sim launch
loop. Comment in the script captures the failure mode so it does not
get dropped again. With the sim included, port 4285 transitions to
ESTABLISHED within ~3 seconds of launch and 42 advances to listen on
4286 and 4287.

This change is load-bearing on `main` until either (a) the Blackboard
IPC entry in `nos3/cfg/InOut/Inp_IPC.txt` is set to `OFF` or
(b) `<active>false</active>` is set on `blackboard-sim` in
`nos3/cfg/sims/nos3-simulator*.xml`. Reverting the launch script
without one of those mitigations restores the 42 hang.

## 2026-05-07 02:30 UTC: FSW SIGSEGV - CS app on 64-bit POSIX

### Symptom

`sc01_nos_fsw` exits with code 139 (SIGSEGV) ~33 seconds after start,
right after `EVS Port1 EVS Port1 42/1/SC 73: RTS Number 001 Started`.
Container has no further EVS output. dmesg captures:

```
CS[77628]: segfault at 85e7c000 ip 00005c1c85e7fbb6 sp 000071b25bdc69b0
           error 4 in core-cpu1[5c1c85e7c000+47000]
core-cpu1: CS: potentially unexpected fatal signal 11.
```

The crashing thread is named `CS` (cFS Checksum app). The fault
address `0x85e7c000` is exactly the lower 32 bits of the executable
mapping base `0x5c1c85e7c000` - a smoking-gun pattern for a 64-bit
pointer truncated to 32 bits.

### Root cause

`addr2line -e core-cpu1 -i 0x1abb6` resolves the crashing IP to
`CFE_ES_CalculateCRC` at `cfe/modules/es/fsw/src/cfe_es_api.c:1628`,
the `ByteValue = *BufPtr;` byte-load inside the CRC-16 loop.

Tracing the BufPtr origin: cs_compute.c:361 sets
`State.BufferAddr = CFE_ES_MEMADDRESS_TO_PTR(ResultsEntry->StartAddress);`.
`CS_Res_EepromMemory_Table_Entry_t::StartAddress` is declared
`CFE_ES_MemAddress_t` in
`fsw/apps/cs/config/default_cs_tbldefs.h`, and that typedef is
`uint32` in `fsw/cfe/modules/es/config/default_cfe_es_extern_typedefs.h:409`.

`cs_init.c:254` does `CodeSeg->StartAddress = CFEAddress;` where
`CFEAddress` is the cpuaddr (uintptr_t = 64-bit on amd64) returned by
`CFE_PSP_GetCFETextSegmentInfo`. On a PIE-loaded cFS process the cFE
text segment lives at a high canonical address (~0x5c1c85e7c000), so
the assignment silently truncates the upper 32 bits. The macro
`CFE_ES_MEMADDRESS_TO_PTR(x) ((void *)(cpuaddr)(x))` then
zero-extends the truncated value back to 64-bit, producing a pointer
into unmapped low memory. When CS_BackgroundCfeCore eventually fires
a CRC pass, the first byte-load page-faults.

The same truncation hits all four runtime tables that are populated
from cFS-level pointers:

- CFE Text Segment (cs_init.c)
- OS Text Segment (cs_init.c)
- App Code Segments (CFE_ES_GetAppInfo)
- Tables (CFE_TBL_GetAddress)

User-supplied EEPROM and Memory definition tables are unaffected
because their ground-loaded `StartAddress` values fit in 32 bits.

### Fix

`nos3/fsw/apps/cs/fsw/inc/cs_internal_cfg.h`: change four
`DEFAULT_CS_INTERNAL_*_STATE` macros from `CS_ChecksumState_ENABLED`
to `CS_ChecksumState_DISABLED` so the CS background loop does not
walk those four tables on power-on. The CS app still loads,
publishes housekeeping, and accepts ground commands; only the
auto-checksum path that dereferences truncated pointers is suppressed.
Each define carries a comment explaining the truncation so a future
edit does not silently re-enable it.

A clean fix would be to widen `CFE_ES_MemAddress_t` to a 64-bit type
or split the runtime field from the telemetry field, but both touch
the cFS upstream wire format and ground tooling. Defer that to a
proper upstream PR.

This change is load-bearing on amd64. Re-enabling any of the four
states without first widening the StartAddress field will reproduce
the SIGSEGV inside ~30 seconds of boot.

## 2026-05-07 02:30 UTC: cryptolib hostname-alias mismatch

### Symptom

`sc01-cryptolib` exits 255 with:

```
socket_init: Failed to resolve hostname 'radio-sim'
crypto_standalone_socket_init tc_apply.write failed with status -1
```

### Diagnosis

`ci_launch.sh` sim loop alias guard reads
`if [[ "$sim" == "generic_radio_sim" ]]` (underscore) but the loop
variable values use hyphens (`generic-radio-sim`). The match never
fires, so the `radio-sim` network alias is never attached to the
radio-sim container. CryptoLib's standalone wrapper hard-codes
`radio-sim` as its peer hostname.

Not yet patched - cryptolib is not on the dashboard critical path
for this validation pass. Tracked here for the next launch-script
sweep.

## 2026-05-07 02:50 UTC: components/generic_{gnss,tt_c}/fsw missing on main

### Symptom

`make build-fsw` aborts on `main`:

```
** Module generic_gnss/fsw/cfs NOT found **
** Module generic_tt_c/fsw/cfs NOT found **
CMake Error at cmake/mission_build.cmake:240 (message):
  Target build incomplete, source for 2 module(s) not found.
```

### Diagnosis

`cfg/build/nos3_defs/targets.cmake` references both modules in
`MISSION_GLOBAL_APPLIST`, and `cpu1_cfe_es_startup.scr` lists
`CFE_APP, generic_gnss, ...` and `CFE_APP, generic_tt_c, ...`.

`git ls-tree HEAD -- nos3/components/generic_gnss
nos3/components/generic_tt_c` shows only the `sim/` subtrees on main.
The full FSW sources (`fsw/cfs/CMakeLists.txt`,
`platform_inc/`, `mission_inc/`, `src/`) live on
`feat/port-draco-features` but were never landed on main. The
already-built `cf/generic_gnss.so` and `cf/generic_tt_c.so` in
`fsw/build/exe/cpu1/cf/` are stale artifacts from the last build on
that branch.

### Workaround used today

```
git checkout feat/port-draco-features -- \
    nos3/components/generic_gnss/fsw \
    nos3/components/generic_tt_c/fsw
```

Adds the two `fsw/cfs` trees to the working tree (staged as `A` in
the index, NOT committed). Build then completes. Decision per the
user: stay on main but pull in the missing source so the
TT&C-Downlink and GNSS-to-GS dashboards can populate. Do not commit
those files to main without an explicit user instruction; the
dual-testbed strategy (per `.ai-memory/MEMORY.md`) keeps Draco-only
features on the feat branch.

## 2026-05-07 09:00 UTC: sim_logs_capture container-name drift

### Symptom

`omni_logs/nos3-*.log` were 60-byte files containing only:

```
Error response from daemon: No such container: sc01-eps-sim
```

Every dashboard panel that reads from a `subsystem:nos3-eps`
(or `nos3-gps`, `nos3-imu`, etc.) document was DARK because the
sim-side debug stream never reached Logstash.

### Diagnosis

`scripts/sim_logs_capture.sh` hard-coded container names like
`sc01-eps-sim` and `sc01-rw-sim0`. `ci_launch.sh` actually creates
sim containers as `sc01_<sim-name>` for everything in its sim
loop, where `<sim-name>` itself uses hyphens. The set was also
incomplete: it listed only nine sims, missing torquer, blackboard,
truth42, css, radio, tt_c, and gnss.

### Fix

`scripts/sim_logs_capture.sh`: replace the SIMS map with the actual
running container names and extend it to cover the full set
(eps, fss, css, imu, gps, mag, startrk, radio, torquer, rw0/1/2,
tt_c, gnss, blackboard, truth42). Comment in the file warns about
the `sc01_` vs `sc01-` distinction so the next refactor does not
regress.

After the fix and a `pkill -f sim_logs_capture.sh && ./scripts/
sim_logs_capture.sh &` cycle, every log went from 60 B to
100 KB+ within seconds and Logstash started extracting per-sim
fields (gps_lat, eps_battery_wh, gnss_solution_type, etc.).

## 2026-05-07 09:15 UTC: ELK pipeline + dashboards on main

### Symptom

Even with FSW alive and 42 publishing IPC, the user-shipped 15
dashboards mostly came up DARK or PARTIAL. Inspection found two
gaps:

1. Logstash on main inlined a 138-entry MID dictionary inside
   `logstash.conf`. cfs_god_view.json on main now emits 180+ MIDs
   (TT_C, GNSS, MGR, NOVATEL_DEVICE_TLM, SB_ALLSUBS, TIME_DATA,
   etc.) that all flowed into ES with `msg_name=UNKNOWN_MSG`,
   `subsystem=UNKNOWN`, `layer=UNKNOWN`. Every panel filtered by
   `subsystem.keyword:TT_C` or `:GNSS` therefore returned 0 docs.
2. Only 6 of the 15 dashboards the user named exist as Kibana
   saved objects on main. The remaining 9 (the EO1 and Standard
   set, Spacecraft Mode Changes, Mission Denmark, etc.) live in
   `nos3/elk/dashboards/nos3-eo1-dashboards.ndjson` on
   `feat/port-draco-features` and have never been imported into
   the main testbed's Kibana volume.

### Fix

`nos3/elk/logstash.conf`: replaced with the feat-branch version
(1407 lines vs 854). The new pipeline reads MID dictionaries from
`/etc/logstash/mid/mid_to_{name,layer,subsystem}.yml` and adds
TT_C and GNSS grok extractors that pull
`gnss_{solution_type,num_sats_*,pdop,hdop,vdop}` and
`tt_c_{elevation_deg,doppler_hz,slant_range_km,pkt_count,...}`
out of the simulator stdout. The YAMLs already shipped on main
under `cfg/build/elk/` (181-entry mid_to_name.yml) and stay in
sync with `cfg/build/mid_registry.json`.

`nos3/elk/docker-compose.yml`: bind-mount
`../cfg/build/elk:/etc/logstash/mid:ro` into the logstash
container so the new `dictionary_path` references resolve. Mount
is read-only because the YAMLs are build-output.

Dashboards: `curl -X POST .../api/saved_objects/_import?
overwrite=true --form file=@/tmp/feat-dashboards.ndjson` against a
copy of `nos3/elk/dashboards/nos3-eo1-dashboards.ndjson` from the
feat branch. Imports 25 saved objects (15 dashboards plus their
visualizations and lens panels). Idempotent under
`overwrite=true`.

### Verification on this run

- Today's index `nos3-telemetry-2026.05.07` has 480 K+ docs.
- `msg_name=UNKNOWN_MSG` count: 0 (was 6807 of every 10 K docs).
- 10 K docs carry a populated `gps_lat` field; max altitude
  ~400 km matches the LEO orbit; avg lat traverses Denmark.
- 1788 GNSS docs, avg `gnss_num_sats_total=9.86` (3-D fix).
- 1140 TT_C docs across HK_TLM and REQ_HK MIDs.
- All 15 named dashboards present in Kibana saved objects.
- FSW (`sc01_nos_fsw`) uptime 47 minutes and counting (versus the
  ~33 second SIGSEGV cliff on the unpatched build).
