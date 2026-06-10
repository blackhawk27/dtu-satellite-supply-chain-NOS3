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

## 2026-06-06 - Integrate Draco piggyback PoC for secured-vs-baseline comparison

Ported the Draco baseline's covert-opcode "piggyback" supply-chain test
(`blackhawk27/dtu-satellite-supply-chain-NOS3-Draco`, commit `dd1d2cbb`) into
this secured fork so the SAME attack runs on both for a thesis comparison.
Decisions (with user): replace+reconcile the existing legacy noisy_app; inject
on the unauthenticated DEBUG path (so the measured delta is detection +
autonomous response, not prevention); add a comparison harness.

Changed:
- FSW: replaced `nos3/fsw/apps/noisy_app/fsw/src/noisy_app.c` (legacy 3-ping
  beacon + 32k SB storm) with the piggyback sniffer (carrier `0x18E0`, opcodes
  `0x02/0x04/0x06/0x08/0x0A`); added `fsw/platform_inc/noisy_app_msgids.h`
  (CMD 0x18F2 / HK 0x08F2); ported the CMakeLists include layout.
- Startup: `cpu1_cfe_es_startup.scr` noisy_app row `OFF_APP` -> `CFE_APP`,
  priority 20 -> 29. NOTE (CLAUDE.md DO-NOT-REVERT context): the shipped/mission
  profile keeps this row `OFF_APP`; `CFE_APP` is the experiment toggle. Flip it
  back to `OFF_APP` to return to mission-truth.
- Driver/COSMOS: `nos3/poc/piggyback_noisy/{drive_poc.py,run_poc.md,
  run_comparison.sh}` + `CI_DEBUG/cmd_tlm/PIGGYBACK_POC.txt`. drive_poc adapted
  for this repo: CI_LAB 5012 (unchanged), FSW_IP auto-resolves `sc01-nos-fsw`,
  TO_LAB re-enable is opt-in (ENABLE_DOWNLINK=1), and override mode re-arms
  AP #27 (this repo's battery actionpoint) not Draco's AP0.
- ELK: `logstash.conf` retagged for piggyback signatures (attack_piggyback,
  attack_eps_spoof/override, attack_sb_burst/flood, piggyback_carrier,
  piggyback_length_error; spam-target MID -> 0x08F2). Dashboard description
  refreshed.
- Docs: `docs/security/` (5 Draco docs + `00-dtu-secured-fork-notes.md` +
  `comparison-secured-vs-draco.md`); rewrote `docs/thesis/04-apps/noisy_app.md`.

Build: VERIFIED. `make config && make fsw` clean, 0 compile errors,
`fsw/build/exe/cpu1/cf/noisy_app.so` produced. Build defs regenerate with
`CFE_APP, noisy_app` and noisy_app in targets.cmake.

Live run: NOT completed in the agent shell. Two environment blockers, neither an
integration defect:
  1. No X server in this shell (no /tmp/.X11-unix socket, DISPLAY empty, no
     Xvfb). `ci_launch.sh` line ~197 `xhost +local:*` (before the 42 docker run)
     is unguarded and aborts `make launch` with "unable to open display" right
     after the ELK + core-services + COSMOS containers come up, before FSW/42/
     sims start. (The NOS3_HEADLESS branch described in CLAUDE.md is not present
     in this ci_launch.sh.)
  2. Memory: ELK alone ~3.2 GB; only ~0.6 GB free at the time. The full
     29-container stack would OOM. (Stopping ELK frees it, but the X blocker
     remains and the comparison still needs ELK or raw-log grep.)
Partial stack was stopped cleanly (`make stop`); system left at ~4 GB free, 0
containers.

To finish (from a VS Code integrated terminal that has DISPLAY=:0 + the X tunnel,
where `make launch` has worked before; FSW is already built so skip rebuild):
  cd nos3 && make launch
  # confirm EVS: "NOISY_APP: Initialized. CMD MID 0x18F2, sniffing carrier 0x18E0."
  python3 nos3/poc/piggyback_noisy/drive_poc.py            # ladder
  python3 nos3/poc/piggyback_noisy/drive_poc.py override   # -> RTS 27
  bash nos3/poc/piggyback_noisy/run_comparison.sh --with-flood --save
  # fill the measured column in docs/security/comparison-secured-vs-draco.md
If ELK memory is tight, the same signatures are greppable in omni_logs/cfs_evs.log
and omni_logs/tlm_hk_decoded.log without Elasticsearch.

## 2026-06-07 - Full attack-response parity (legacy vs new cFS)

Per user: make the two testbeds replicas for the attack so the comparison
isolates the cFS version (this fork cFE 6.7.99, the 6.7 "Bootes" line / legacy,
vs Draco cFE 7.0.0 "Caelum" / new - a major-version gap; this fork backports
Draco features "the legacy way" in 6.7 conventions), not config drift. Grafted
Draco's battery-low SAFE chain into this fork. (Correction: an earlier draft
mislabelled this fork as v7.0.0-rc4 from the stale CFE_BUILD_BASELINE git-tag
string; the actual version macros are MAJOR 6 / MINOR 7 / REVISION 99.)

Load-bearing edits (justification: deliberate parity port, approved):
- `cfg/nos3_defs/tables/lc_def_wdt.c`: WP #0 was unused -> now BATTERY_LOW
  (`GENERIC_EPS_TLM_MSG`, offset 20 = this fork's BatteryVoltage field, UWORD_LE,
  LT, < 14800 mV). Used this fork's offset 20 (NOT Draco's 16: different EPS HK
  struct). WP #27/#28 left intact.
- `cfg/nos3_defs/tables/lc_def_adt.c`: AP #0 was unused -> now SAFE_ON_LOW_BAT
  (ACTIVE, RTSId=4, RPN {WP0}). AP #27/#28 left DISABLED (as in Draco).
- `cfg/nos3_defs/tables/sc_rts004.c`: NEW. MGR SET_MODE SAFE + DS disable idx 3
  + TO_LAB SET_SAFE_TLM. Written in THIS fork's RTS idiom (`.TimeTag`,
  `DS_DestStateCmd_t`, `CFE_MSG_CMD_HDR_INIT`, `TO_LAB_SetSafeTlmCmd_t`), NOT
  Draco's verbatim (Draco uses `.WakeupCount` + `DS_SetDestStateCmd_t`). Tables
  are globbed, so the file builds without a list edit.
- TO_LAB app: `to_lab_msg.h` (+CC 7/8 + typedefs), `to_lab_events.h` (+EID 20/21),
  `to_lab_app.c` (+prototypes, +dispatch cases, +`TO_LAB_SetSafeTlm` /
  `TO_LAB_SetNominalTlm`). SET_SAFE_TLM drops high-rate streams and keeps the
  low-rate HK/command beacon by `BufLimit` (`<= 4` kept, `>= 32` dropped) - this
  fork's `to_lab_sub.c` is ordered by subsystem, not as Draco's leading 34-block,
  so BufLimit is the order-independent discriminator. `to_lab_sub.c` therefore
  left UNTOUCHED (lower risk than a reorder; same observable outcome). EVS still
  reaches ELK via the FSW-log capture path, so the downgrade does not blind
  forensics.
- Driver/ELK/docs: `drive_poc.py` override re-arms AP #0 (was AP #27);
  `logstash.conf` +`attack_safe_mode` / `to_safe_downgrade` tags;
  `run_poc.md`, `docs/security/*`, thesis `noisy_app.md` reframed to
  legacy-vs-new-cFS with identical responses.

Compatibility note: this fork is cFE 6.7.99, Draco is cFE 7.0.0. The LC/SC/TO_LAB
table + command struct layouts for the fields used are compatible across the two,
but the conventions differ and the port was written THIS fork's (6.7) way, not
Draco's (7.0) verbatim: SC RTS entries use `.TimeTag` (not Draco's `.WakeupCount`),
DS uses `DS_DestStateCmd_t` (not `DS_SetDestStateCmd_t`), and the TO_LAB no-args
struct keeps this fork's `CmdHeade` member. sc_rts004.c also mirrors
`TO_LAB_SET_SAFE_TLM_CC` locally to avoid depending on TO_LAB's src include path.

Build: compile-verify pending (this session). Live verify deferred (agent shell
has no X display): expect `make launch` then `drive_poc.py override` to log
`Batt volt critical: SAFE` (LC AP0) -> RTS 4 -> `TO: SAFE-mode downlink downgrade
engaged`, matching Draco.

## 2026-06-10 - RTLD_NOLOAD does not match cFS modules on the glibc/9p devcontainer; use /proc/self/maps + plain dlopen

Found while porting the noisy_app GNSS direct-memory-overwrite PoC (piggyback
opcodes 0x0C teleport / 0x0E drift) from the Draco-RTEMS fork into this legacy
(cFE 6.7.99 / Linux / NOS-Engine) fork.

### Symptom
After arming the GNSS spoof, the EVS either logged
`NOISY_APP: GNSS spoof could not resolve generic_gnss.so:GENERIC_GNSS_AppData`
or the StartGnssSpoof "TELEPORT ENGAGED" event fired but the downlinked GENERIC_GNSS
HK position (MID 0x0952) never moved off the genuine orbit. The IMU covert-channel
PoC (opcode 0x0A, same off-bus class) worked fine - because it biases the IMU app's
OWN AppData in-process and needs no cross-app symbol lookup.

### Root cause
The shadow task must write the victim GNSS app's cached position
(`GENERIC_GNSS_AppData.LastBusLat/Lon`, which `GENERIC_GNSS_UpdatePositionAndFlags`
copies into the downlinked `DeviceHK.GnssLat/Lon` each cycle). cFE on Linux runs all
apps as pthreads in ONE process, but the POSIX OSAL loader loads each app `.so` with
`RTLD_LOCAL` (`cfe_es_apps.c` defaults `OS_MODULE_FLAG_LOCAL_SYMBOLS`;
`os-impl-posix-dl-loader.c` maps that to `RTLD_LOCAL`). So `GENERIC_GNSS_AppData`
is NOT in the process-global scope: a link-time `extern` (the Draco/RTEMS approach,
which relies on RTEMS's `RTLD_GLOBAL` flat no-MMU loader) and `dlsym(RTLD_DEFAULT)`
both fail to reach it. The Draco-Linux recipe (`docs/security/draco-linux-poc.md`)
used `dlopen("generic_gnss.so", RTLD_NOLOAD) + dlsym`, but `RTLD_NOLOAD` did NOT
match the resident module here:
- This devcontainer is a 9p (WSL2) bind-mount, whose synthetic inode numbers make
  glibc's dev/ino fallback for `RTLD_NOLOAD` matching unreliable.
- CFE_ES records the module under its full ABSOLUTE path
  (`/workspaces/.../fsw/build/exe/cpu1/cf/generic_gnss.so`, confirmed via
  `/proc/<pid>/maps`), so a bare `generic_gnss.so` or cwd-relative `cf/generic_gnss.so`
  does not string-match the recorded `l_name` either.

### Fix (commit f195ffe7)
In `NOISY_ResolveGnssAppData()` (nos3/fsw/apps/noisy_app/fsw/src/noisy_app.c): read
the EXACT loaded path from `/proc/self/maps` (the line containing `generic_gnss.so`)
and `dlopen(path, RTLD_NOW)` WITHOUT `RTLD_NOLOAD`. glibc dedups loaded libraries by
resolved path, so a dlopen of the already-resident path returns the SAME `link_map`
(refcount++), never a second copy - giving the live `GENERIC_GNSS_AppData`. A
cwd-relative / bare-name candidate list remains as a fallback. CMake links `dl`.

Verified on a clean `make stop && make launch`: GNSS teleport (0x0C) drives the
HK downlink to lat/lon (0.0000, 0.0000); GNSS drift (0x0E) produces a slowly growing
plausible offset off the genuine track; IMU bias (0x0A) latches `/ram/.imu_cal` and
diverges bus gyro/accel (~3.4) from `[IMU_TRUTH]` (~0.05) in ES.

### Gotcha for future debugging
Do NOT verify by `docker restart sc01-nos-fsw`. That is not the normal lifecycle:
it kills the host-side capture pipeline (`passive_listener.py`, `cfs_evs_capture.sh`,
started by the Makefile `launch` target) and confuses RTLD module identity, which
produced misleading "could not resolve" readings mid-session. Use a clean
`make stop && make launch`; the authoritative results above came from that.
