# Customisations relative to the import baseline

This document is the authoritative divergence list of the DTU
fork against the upstream NOS3 state captured at the import
baseline. Every entry below answers three questions: *what
changed*, *where in the tree the change lives*, and *why*. The
purpose is to give a thesis examiner (or a future maintainer
reading the diff in isolation) the rationale for every
non-trivial edit without forcing them to walk seventy commits
of history.

## Import baseline

- **Commit hash:** `55ad2148b0f52e5aac0a98cd4bb3b47ebe3e4410`
- **Subject:** `chore: imported and flattened NOS3 environment
  with all submodules`
- **Position:** Earliest commit on the `main` branch of this
  repository. Every later commit is a DTU divergence from this
  state.

The baseline captures the upstream NOS3 tree at the time of the
DTU import, with every submodule de-referenced into a single
flat tree. All paths below are relative to the repository root.

## Categories

Entries are grouped by category, not by file. The categories,
in the order they appear:

1. Build and toolchain
2. Networking and host setup
3. Mission configuration and spacecraft profile
4. Flight software apps: added
5. Flight software apps: modified
6. Hardware-simulator components
7. Ground software configuration
8. Telemetry pipeline (ELK)
9. Security and threat-model surface
10. Documentation and operator tooling

The same commit appears in only one category. When a commit
bundled changes across categories, the entry sits under the
category that motivated the change; the bundle is noted in the
entry text.

## 1. Build and toolchain

- **Empty gitlinks replaced with full app sources.** Commits
  `3b456032` (HK, HS), `76709983` (CS, MD, MM), `f7ac1730`
  (CF, DS, FM, LC, SBN, SC). The upstream NOS3 referenced these
  cFS apps as git submodules; the flattening import left them
  as empty directories. The DTU replacement filled in the cFS
  app trees so the build resolves them by file rather than by
  submodule fetch. Without this, `make fsw` fails on missing
  `CMakeLists.txt` for half the heritage cFS apps.
- **Draco-era CMake helpers replaced.** Commits `677b5e99` and
  `b0322760`. The CS / MD / MM apps used
  `generate_configfile_set()`, a helper that exists in the
  Draco cFE branch but not in the cFE this fork targets. The
  DTU edit substitutes `generate_config_includefile()` (the
  legacy-cFE equivalent) in both the per-app CMake and the
  shared `arch_build.cmake`.
- **Draco-era app APIs ported back to legacy cFE.** Commit
  `b2947fea` (CS), `9309e3a6` (MD, MM port from
  `TOPICID_TO_MIDV` to hardcoded MIDs), `449e904a`
  (`OS_strnlen` to `strnlen` in MM). The apps shipped in the
  Draco style; the cFE in this fork is legacy. Each rewrite
  is the smallest change that compiles against the legacy
  API surface.
- **`noisy_app` build hardening for cFS 7.0.** Commit
  `32257387`. The original add-commit landed before cFS 7.0
  stabilised; the include layout had to be corrected to
  resolve the cross-app EPS header import.
- **C++ include paths configured.** Commit `e72f9424`.
  Resolves the noisy_app save conflict and configures the
  C++ include paths so the simulator side links cleanly
  against the EPS message struct.
- **Missing umbrella headers added.** Commit `1bb69bf8`. The
  MD app's table system expected a `default_md_tbl.h`
  umbrella header that the Draco source had but the import
  had not surfaced; the file is restored.
- **Shell scripts made executable in git.** Commit
  `2595970d`. Every shell script invoked from the Makefile
  has `+x` set in the git index. Earlier clones intermittently
  lost the bit and `make launch` failed with a permission
  error; setting the bit in-index removes that class of
  failure.
- **Devcontainer scaffolding.** Commit `7441371f` adds the
  initial devcontainer, `40bbd718` adds the X11 dependency
  bootstrap and the bridge-`netfilter` sysctl tweak. Without
  the sysctl, intra-bridge container traffic on `nos3-sc01`
  is silently filtered and the simulator-to-42 IPC flows are
  dropped.

## 2. Networking and host setup

- **42 IPC port and prefix narrowing.** Commit `0d529bce`. The
  load-bearing edit to `nos3/cfg/InOut/Inp_IPC.txt` documented
  at length in
  [03-communication/04-sim-to-42.md](03-communication/04-sim-to-42.md).
  Star tracker (port 4282) and thruster (port 4280) IPC modes
  switched to `OFF` because the active sim XML uses providers
  that never connect; TT&C (port 4286) and GNSS (port 4287)
  prefixes narrowed to specific position/velocity keys so 42's
  non-blocking `write()` does not exit on EAGAIN at the
  100 Hz tick rate.
- **Bridge-`netfilter` sysctl reasserted at every launch.**
  Part of commits `40bbd718` and `e70fac92`.
  `scripts/ci_launch.sh` calls `sysctl
  net.bridge.bridge-nf-call-iptables=0` early in its run; the
  Docker daemon flips it back to `1` on its own restart, so
  the reassertion is necessary on every launch.
- **`DISPLAY` guard at launch.** Part of `ci_launch.sh`'s
  setup. Without the guard, a `DISPLAY`-less shell (cron,
  side-container `docker exec`) runs ~180 lines into the
  launch before failing on `xhost +local:*`; with the guard,
  it fails fast with an actionable message.

## 3. Mission configuration and spacecraft profile

- **Orbit oriented over Denmark.** Commit `555dc567`. Edits
  `nos3/cfg/InOut/Orb_LEO.txt` to 60 degree inclination, 346
  degree RAAN, 61.6 degree true anomaly so the spacecraft
  overflies Denmark on every orbit. Adds the Svalbard and DTU
  ground stations to the GS roster.
- **Generic GNSS and TT&C enabled.** Commits `2a0bd473` and
  `154d653d`. `cfg/spacecraft/sc-mission-config.xml` enables
  the two new components; cFS startup scripts and the LC
  autonomy table gain Denmark-field-of-view entries.
- **Star tracker switched to synthetic.** Same commits. The
  `_42_PROVIDER` star-tracker variant required an extra 42
  IPC connection that was disabled in commit `0d529bce`; the
  configuration now points at `GENERIC_STAR_TRACKER_PROVIDER`,
  which synthesises a unit-quaternion output instead.
- **Mission epoch bumped.** Same `2a0bd473`. The default
  mission start time was advanced so the Denmark overfly
  happens within the first orbit of the simulation rather
  than several hours in.
- **cFS startup script and target list expanded.** Commits
  `535c7709`, `1e9bf761`, `f7ac1730`. `cpu1_cfe_es_startup.scr`
  and `targets.cmake` gain entries for HK, HS, CS, MM, MD,
  CF, DS, FM, LC, SBN, SC. Earlier startup scripts only
  enabled a minimal subset.
- **SCH tables widened.** Commits `2111b907`, `b5055809`,
  `6bba4737`. The scheduler tables now drive CS, DS, FM, LC,
  MD, MM, SC and the hardware-HK aggregator. Without this,
  several apps loaded but never received their periodic
  request packets.

## 4. Flight software apps: added

- **`noisy_app`.** Commits `dd750790` (broadcast storm),
  `c1642e3c` (OFF_APP menu, OS handshake), `e9465af8` (EPS HK
  spoof). Documented in detail in
  [04-apps/noisy_app.md](04-apps/noisy_app.md). The
  centrepiece of the supply-chain threat model: a cFS app
  that ships through the same build path as legitimate apps
  and arms by sniffing the CI_LAB command carrier (MID
  `0x18E0`) for a covert opcode byte.
- **`beacon_app`.** Commit `90e024c1`. A ground-commandable
  ping / pong telemetry app on MID `0x18F0`. It is an
  independent, legitimate app and is NOT part of `noisy_app`'s
  trigger path; `noisy_app` does not subscribe to it. The
  threat-model framing of the passive trigger lives entirely
  with `noisy_app`'s carrier sniffing (see
  [04-apps/noisy_app.md](04-apps/noisy_app.md)): an attacker
  need not have direct ground access; a single unauthenticated
  command on the sniffed carrier is sufficient to arm.
- **`generic_gnss`.** Commits `ee347c91` (initial), `f766be00`
  (restore on main after a merge mishap). Documented in
  [04-apps/generic_gnss.md](04-apps/generic_gnss.md).
- **`generic_tt_c`.** Commits `2b7dad40` (initial), `db78b8b1`
  (restore on main). Documented in
  [04-apps/generic_tt_c.md](04-apps/generic_tt_c.md).

## 5. Flight software apps: modified

- **`mgr` reboot policy and EVS hygiene.** Commit `8161e160`.
  Documented in [04-apps/mgr.md](04-apps/mgr.md).
- **`lc`, `md`, `sc` health-monitoring autonomy.** Commit
  `bdf010d3`. Adds the limit-checker watchpoints, the memory-
  dwell tables, and the stored-command sequences that
  together implement automated safe-mode response. The
  active set is the one the IDE has open
  (`cfg/build/nos3_defs/tables/lc_def_wdt.c`).
- **`cs` disabled on-power checksum tables on 64-bit POSIX.**
  Commit `af0fc6f4`. The checksum app's default table set
  assumed a 32-bit cFE platform; on the 64-bit `amd64-nos3`
  target the tables triggered alignment faults at startup.
  The fix disables the at-power table set; the runtime
  checksum machinery remains available for explicit
  commanding.

## 6. Hardware-simulator components

- **`blackboard` sim parser hardening.** Commit `a0d796f6`.
  Documented in [04-apps/blackboard.md](04-apps/blackboard.md).
- **`generic_rw` empty-frame guard.** Commit `19cebabb`. The
  reaction-wheel simulator's 42 data-point parser carries
  the same empty-frame deferral pattern as the
  `generic_tt_c` and `generic_gnss` parsers. Also normalises
  line endings so a CRLF input does not throw on the trailing
  CR.
- **`generic_star_tracker` synthetic quaternion.** Commit
  `c08b0169`. With the `_42_PROVIDER` variant disabled, the
  star tracker sim emits a unit quaternion as its synthetic
  output rather than waiting for a 42 IPC line that never
  arrives.
- **`generic_eps` per-app load model.** Commit `02f08c68`.
  The EPS sim's power state is now driven by a per-app,
  per-MID load model rather than a constant draw. The
  practical effect is that `noisy_app`'s storm phase shows
  up as a transient load increase in the EPS telemetry,
  which is the signal the "EO1 power budget" Kibana
  dashboard correlates against.
- **`generic_eps` first-tick race fix.** Commit `59b1cbf5`.
  The EPS sim emitted a transient battery-voltage overflow
  on the first scheduler tick because the model integrator
  read state before the integrator initialised. Fixed by
  deferring the first publish to the second tick.

## 7. Ground software configuration

- **COSMOS `SIM_CMDBUS_BRIDGE` target.** Commits `b9786e9e`,
  `68641ded`, `be5a6bb9`. Adds a COSMOS target that exposes
  the simulator-side command and telemetry bus as a COSMOS
  interface, so operator scripts can drive the simulator
  directly without going through the FSW. Used by attack
  scripts that need to manipulate simulator state from the
  ground side.
- **GENERIC_GNSS COSMOS target and responder.** Commit
  `eb80ddd4`. Registers a COSMOS target for the new GNSS
  component and starts the ground-responder background
  task (`gnss_gs_responder_task.rb`), which writes the
  Layer 4 `omni_logs/gnss_uplinks.log` file the ELK
  pipeline ingests.

## 8. Telemetry pipeline (ELK)

This entire category is a DTU addition relative to the import
baseline. The upstream NOS3 has no aggregator and no
observability beyond stdout.

- **ELK stack and god-view capture.** Commit `08fe63c3`.
  Introduces `nos3/elk/docker-compose.yml`, the initial
  `logstash.conf`, and the god-view capture script. This is
  the seed commit for the whole pipeline.
- **Logstash filter evolution.** Commits `c1fbebe3` (kv
  filter), `dab4f0ce` (numeric type conversions), `195ad837`
  (EPS / GPS / RW / cpu_monitor parsing), `a3ed42ad` (full
  sensor parsing and attack taxonomy), `2bac4f44` (HS / HK
  MID classification), `6194182e` (CS / MD / MM extraction).
  The pipeline grew incrementally as each new component was
  enabled; the current shape is described in
  [05-elk/01-logstash-filters.md](05-elk/01-logstash-filters.md).
- **CPU monitor and EVS / sim capture scripts.** Commits
  `0a840579`, `bdcb5986`, `0d0f9cb5`, `0f847818`. The four
  host-side capture surfaces and their integration into
  `make launch` / `make stop`.
- **Save / load run targets.** Commit `8131144a`. Archives
  a run's logs and metadata; the corresponding `load-run`
  target replays them into Kibana without restarting the
  sim. Documented in
  [05-elk/04-build-and-load.md](05-elk/04-build-and-load.md).
- **`to_lab` subscription widening.** Commit `8cad7cc8`.
  Expanded `nos3/cfg/nos3_defs/tables/to_lab_sub.c` to
  enumerate every component MID so the full bus surface
  becomes visible in `cfs_god_view.json` and the
  `software_bus` Kibana documents.
- **MID-registry centralisation and seed.** Commits
  `e70fac92` (centralisation), `076dad6c` (seed). The
  generator targets Draco-era topic macros; the seed under
  `nos3/elk/seed_mid/` is the long-term fallback.
- **Kibana dashboard builder.** Commit `d66a1b38`. Rebuilds
  the pipeline and introduces
  `elk/build_kibana_dashboards.py` plus
  `elk/load_dashboards.py`. The Python builder is now the
  source of truth for the saved-object set; the older
  ndjson is retained for backwards compatibility.
- **Kibana data view auto-creation.** Commit `0e76500f`.
  On fresh clones the `nos3-telemetry*` index pattern saved
  object did not exist, and dashboards rendered against an
  empty pattern. The builder now creates the pattern if
  absent.
- **`make doctor`.** Commit `36e32fb6`. The end-to-end
  telemetry-chain diagnostic described in
  [05-elk/04-build-and-load.md](05-elk/04-build-and-load.md).
- **SB transmit-path JSON write removed.** Commit `1ab8a90d`.
  An earlier iteration of the god view wrote JSON directly
  from `cfe_sb`'s transmit path; the refactor moved capture
  entirely out of the FSW process and into the host-side
  `god_view_capture.py`. Eliminates the FSW-side write
  amplification and keeps the bus boundary clean.

## 9. Security and threat-model surface

- **Research attack scripts.** Commit `584db4dd`. Adds the
  scenario scripts under `nos3/scripts/attack/` that
  reproduce the FSW vulnerability scenarios developed in
  the Threat Model chapter.
- **Carrier sniffing as the passive trigger.** The threat
  model assumes the attacker arms `noisy_app` by sniffing the
  CI_LAB command carrier (MID `0x18E0`) for a covert opcode
  byte riding a legitimate command, rather than issuing a
  custom command on a dedicated trigger MID. `beacon_app`
  (commit `90e024c1`, MID `0x18F0`) is an independent,
  legitimate ping / pong app and is NOT part of this trigger
  path. A co-resident app reading a covert opcode off a
  legitimate carrier on the unauthenticated Software Bus is
  the canonical shape of a passive-trigger supply-chain attack.

## 10. Documentation and operator tooling

- **cFS resource limits and attack event signatures.**
  Commit `1c95bb80`. Adds `docs/cfs-resource-limits/`,
  which contains the resource-limit reference and the
  signature table that the Kibana attack-radar dashboard
  surfaces from.
- **Developer guide for new cFS apps.** Commit `f2a1249c`.
  Operator-facing instructions for adding a new cFS app with
  resource-limit considerations.
- **Working notes.** Commit `d138ada9`. Adds a chronological
  investigation log for the load-bearing fixes (outside the
  thesis tree; not load-bearing for reproducibility). The
  invariants themselves are recorded inline in the affected
  source / config files.
- **Centralised launch under `ci_launch.sh`.** Commit
  `e70fac92`. Consolidates the prior per-target launch
  scripts into a single entry point; bundled with the MID
  registry centralisation in the same commit.

## What is intentionally unchanged

For completeness, three categories of upstream surface that
the fork has deliberately left alone:

- **`cfe_sb` and the Software Bus trust model.** The bus has
  no authentication and no per-publisher provenance. The
  threat model attacks exactly that property; changing it
  here would invalidate the experiment.
- **CryptoLib's internal security associations and key
  material.** CryptoLib is treated as a fixed property of
  the FSW-to-GSW boundary, not as an attack surface, so the
  internal SA / MC / KEY flags are pinned in the Makefile
  but the implementation is not touched.
- **The Software Bus Network bridge (`sbn`) and its client
  (`sbn_client`).** Multi-spacecraft is not in scope for the
  current research. The apps are loaded for completeness so
  the FSW image is structurally complete.

These exclusions are part of the divergence record as much as
the changes are; a future fork can choose to drop the
exclusions if its research question is different.
