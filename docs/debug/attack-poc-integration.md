# Supply-chain PoC integration (this fork vs Draco)

This fork and the Draco baseline
(`blackhawk27/dtu-satellite-supply-chain-NOS3-Draco`) run the same covert
supply-chain attack so the thesis can compare them. The two testbeds differ
in cFE version: this fork is cFE 6.7.99 (the 6.7 "Bootes" line) and
backports Draco features the 6.7 way; Draco is cFE 7.0.0 ("Caelum"). The
comparison is meant to isolate the cFS-version delta, so the porting was
done deliberately to keep the two testbeds as replicas for the attack.

## Piggyback PoC port

The Draco "piggyback" covert-opcode test was ported into this fork. The
attack is injected on the unauthenticated DEBUG path, so the measured delta
is detection plus autonomous response, not prevention.

- FSW: `nos3/fsw/apps/noisy_app/fsw/src/noisy_app.c` is the piggyback
  sniffer (carrier `0x18E0`; covert opcodes `0x02` EPS_SPOOF, `0x04`
  SB_BURST, `0x06` EPS_OVERRIDE, `0x08` SB_FLOOD, `0x0A` IMU_BIAS, `0x0C`
  GNSS teleport, `0x0E` GNSS drift), with
  `fsw/platform_inc/noisy_app_msgids.h` (CMD `0x18F2` / HK `0x08F2`).
- Startup toggle: in `cfg/nos3_defs/cpu1_cfe_es_startup.scr`, the
  `noisy_app` row ships as `CFE_APP` (priority 29), so it loads on every
  boot. The row is a toggle: set it to `OFF_APP` to leave the app present in
  the image but not loaded. See `04-apps/noisy_app.md`.
- Driver and COSMOS: `nos3/poc/piggyback_noisy/{drive_poc.py,run_poc.md,
  run_comparison.sh}` plus `CI_DEBUG/cmd_tlm/PIGGYBACK_POC.txt`. `drive_poc`
  uses CI_LAB 5012, auto-resolves the FSW IP from `sc01-nos-fsw`, makes the
  TO_LAB re-enable opt-in (`ENABLE_DOWNLINK=1`), and in override mode re-arms
  this repo's battery actionpoint.
- ELK: `logstash.conf` tags these scenarios from the EVS strings the app
  emits: `piggyback_opcode`, `eps_spoof`, and `sb_pool_lock`, plus the
  msg-id-keyed `noisy_app_spam_target` and `beacon_cmd`.

## Attack-response parity: battery-low SAFE chain

Draco's battery-low SAFE response was grafted into this fork, written in the
6.7 idiom rather than copied verbatim from 7.0.

- `cfg/nos3_defs/tables/lc_def_wdt.c`: watchpoint BATTERY_LOW
  (`GENERIC_EPS_TLM_MSG`, offset 20 = this fork's BatteryVoltage field,
  UWORD_LE, LT, < 14800 mV). This fork's EPS HK struct uses offset 20, not
  Draco's offset 16.
- `cfg/nos3_defs/tables/lc_def_adt.c`: actionpoint SAFE_ON_LOW_BAT (ACTIVE,
  RTSId 4, RPN {WP0}).
- `cfg/nos3_defs/tables/sc_rts004.c` (new): MGR SET_MODE SAFE, DS disable,
  TO_LAB SET_SAFE_TLM. Written in this fork's RTS idiom (`.TimeTag`,
  `DS_DestStateCmd_t`, `CFE_MSG_CMD_HDR_INIT`, `TO_LAB_SetSafeTlmCmd_t`), not
  Draco's (`.WakeupCount` + `DS_SetDestStateCmd_t`). Tables are globbed, so
  the file builds without a list edit.
- TO_LAB app: added SET_SAFE_TLM / SET_NOMINAL_TLM command codes, events,
  and handlers. SET_SAFE_TLM drops high-rate streams and keeps the low-rate
  HK/command beacon by `BufLimit` (kept `<= 4`, dropped `>= 32`), which is
  order-independent, so `to_lab_sub.c` is left untouched. EVS still reaches
  ELK via the FSW-log capture path, so the downgrade does not blind
  forensics.

Compatibility note: the LC/SC/TO_LAB table and command-struct layouts for
the fields used are compatible across 6.7 and 7.0, but the conventions
differ and this port follows the 6.7 conventions. (An earlier draft
mislabelled this fork as v7.0.0-rc4 from a stale `CFE_BUILD_BASELINE`
git-tag string; the actual version macros are MAJOR 6 / MINOR 7 /
REVISION 99.)

## Cross-app symbol resolution on Linux/glibc: use /proc/self/maps + plain dlopen

Found while porting the GNSS direct-memory-overwrite PoC (opcodes 0x0C
teleport, 0x0E drift) from the Draco-RTEMS fork. Fixed in commit f195ffe7.

- Symptom: after arming the GNSS spoof, EVS either logged `GNSS spoof could
  not resolve generic_gnss.so:GENERIC_GNSS_AppData` or fired "TELEPORT
  ENGAGED" while the downlinked GENERIC_GNSS HK position (MID 0x0952) never
  moved off the genuine orbit. The IMU covert-channel PoC (opcode 0x0A)
  worked because it biases the IMU app's own AppData in-process and needs no
  cross-app lookup.
- Root cause: the shadow task must write the victim GNSS app's cached
  position (`GENERIC_GNSS_AppData.LastBusLat/Lon`, copied into
  `DeviceHK.GnssLat/Lon` each cycle). cFE on Linux runs all apps as pthreads
  in one process, but the POSIX OSAL loader loads each app `.so` with
  `RTLD_LOCAL` (`cfe_es_apps.c` defaults `OS_MODULE_FLAG_LOCAL_SYMBOLS`,
  mapped to `RTLD_LOCAL` in `os-impl-posix-dl-loader.c`). So
  `GENERIC_GNSS_AppData` is not in the process-global scope: a link-time
  `extern` (the RTEMS approach, which relies on RTEMS's flat `RTLD_GLOBAL`
  loader) and `dlsym(RTLD_DEFAULT)` both fail. The Draco-Linux recipe used
  `dlopen("generic_gnss.so", RTLD_NOLOAD) + dlsym`, but `RTLD_NOLOAD` did
  not match the resident module here: this devcontainer is a 9p (WSL2)
  bind-mount whose synthetic inode numbers make glibc's dev/ino fallback
  unreliable, and CFE_ES records the module under its full absolute path
  (confirmed via `/proc/<pid>/maps`), so a bare or cwd-relative name does
  not string-match the recorded `l_name`.
- Fix: in `NOISY_ResolveGnssAppData()`
  (`nos3/fsw/apps/noisy_app/fsw/src/noisy_app.c`), read the exact loaded
  path from `/proc/self/maps` (the line containing `generic_gnss.so`) and
  `dlopen(path, RTLD_NOW)` without `RTLD_NOLOAD`. glibc dedups loaded
  libraries by resolved path, so dlopen of the already-resident path returns
  the same `link_map` (refcount incremented), giving the live AppData. A
  bare-name fallback remains; CMake links `dl`.
- Verification: on a clean `make stop && make launch`, GNSS teleport (0x0C)
  drives the HK downlink to (0.0000, 0.0000), GNSS drift (0x0E) produces a
  slowly growing plausible offset, and IMU bias (0x0A) latches `/ram/.imu_cal`
  and diverges bus gyro/accel from the truth signal in Elasticsearch.
- Gotcha: do not verify with `docker restart sc01-nos-fsw`. That kills the
  host-side capture pipeline (`passive_listener.py`, `cfs_evs_capture.sh`,
  started by the Makefile `launch` target) and confuses RTLD module
  identity, producing misleading "could not resolve" readings. Use a clean
  `make stop && make launch`.

## cryptolib hostname-alias mismatch (resolved)

- Symptom (historical): `sc01-cryptolib` exited 255 with `socket_init:
  Failed to resolve hostname 'radio-sim'`.
- Root cause: the `ci_launch.sh` sim-loop alias guard read
  `if [[ "$sim" == "generic_radio_sim" ]]` (underscore) but the loop values
  use hyphens (`generic-radio-sim`), so the `radio-sim` network alias was
  never attached to the radio-sim container, while CryptoLib's standalone
  wrapper hard-codes `radio-sim` as its peer hostname.
- Status: resolved in code. The `ci_launch.sh` sim-loop guard now matches
  `generic-radio-sim` (hyphen, consistent with the loop values) and attaches
  `-h radio-sim --network-alias=radio-sim`, so the `radio-sim` alias that
  CryptoLib resolves is present and `sc01-cryptolib` no longer fails to
  resolve its peer hostname.

## generic_gnss/tt_c FSW sources absent on main (build module not found)

- Symptom: `make build-fsw` aborts with `Module generic_gnss/fsw/cfs NOT
  found` and `Module generic_tt_c/fsw/cfs NOT found`, then
  `Target build incomplete, source for 2 module(s) not found`.
- Root cause: `targets.cmake` and `cpu1_cfe_es_startup.scr` reference both
  modules, but only their `sim/` subtrees exist on `main`; the full FSW
  sources live on `feat/port-draco-features`. The prebuilt `.so` files in
  `fsw/build/exe/cpu1/cf/` are stale artifacts from a build on that branch.
- Workaround (not committed to main):
  `git checkout feat/port-draco-features -- nos3/components/generic_gnss/fsw
  nos3/components/generic_tt_c/fsw` to stage the two `fsw/cfs` trees so the
  build completes. The dual-testbed strategy keeps the feat-only features off
  main unless explicitly landed.
