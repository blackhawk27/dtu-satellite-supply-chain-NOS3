# Backport traceability: Draco-RTEMS -> legacy NOS3 (cFE 6.7.99)

Purpose: guarantee that every non-RTEMS, non-ZTA change made in the
`dtu-satellite-supply-chain-NOS3-Draco-RTEMS` fork (Setup 3) is reflected in this
legacy repo (Setup 1), so the three thesis testbeds stay functionally comparable.

- Source: remote `draco-rtems/main`, HEAD `07b2eb4a`. Histories are unrelated (no
  merge-base); the source forked from a Draco baseline imported at `473a4657`.
- Checklist range: `473a4657..draco-rtems/main` = 66 non-merge commits, each listed
  below exactly once. A commit with no row is a miss.
- Classes: `portable` (port), `rtems` / `zta` / `infra` (exclude, reason given),
  `mixed` (split hunk-level), `done` (already in legacy main/WIP, verify only).
- cFE adapt: 7.0 EDS / `TOPICID_TO_MIDV` / message accessors -> 6.7 literals; the
  already-adapted WIP `noisy_app.c` is the canonical 6.7 reference.

## Exclusion rules
- RTEMS: `rtems_udp/`, `engine_bridge`/`eb_nos_bridge`, QEMU launch, `pc-rtems` PSP,
  `i686-rtems5` toolchain, RTEMS BSP/no-MMU/slirp, `*_rtems.scr`, RTEMS `#ifdef`
  guards (`__rtems__`, `_RTEMS_OS_`, `LEON3_BSP`), static-link app lists.
- ZTA: `sb_acl` app, `zta_watchdog` app, ZTA Kibana dashboards / ES template / CSV
  export, SB_ACL EVS log parsing + acl_mode field, bus mutual-auth, per-msg crypto,
  `zta_scenario.py` attack wrappers.

## Traceability table

| # | source_sha | subsystem | class | status | cfe_adapt | notes / excluded_reason |
|---|-----------|-----------|-------|--------|-----------|--------------------------|
| 1 | fd2f6fde | repo | infra | excluded | n-a | .gitattributes LF enforcement; non-functional repo hygiene |
| 2 | bc382d88 | repo | infra | excluded | n-a | CRLF->LF normalization (devcontainer.json, syn test data, compose); non-functional |
| 3 | cf830d09 | fsw/cfg | mixed | port-now | verbatim | PORT only: drop unused `<arpa/inet.h>` in mgr_app.c, sample_app.c, syn_app.c. EXCLUDE: cmake/arch RTEMS profile, hwlib LEON3/rtems headers, pc-rtems PSP, RW app RTEMS UART guards |
| 4 | 16b34d18 | elk/scripts | portable | port-now | n-a | ELK identity via elk/.env; env.sh sourcing; KIBANA_URL/ES_URL env in build_kibana_dashboards.py + refresh_index_pattern_fields.py; NETWORK_NAME in ci_launch/stop scripts |
| 5 | 5905c430 | repo | infra | excluded | n-a | .gitignore (xxd/yamcs/devcontainer outputs); xxd is RTEMS-image only |
| 6 | af7101a0 | fsw/build | rtems | excluded | n-a | RTEMS5 i686 toolchain + Dockerfile.rtems5 + *_rtems.scr |
| 7 | cec30e84 | fsw/hwlib | rtems | excluded | n-a | rtems_udp BSP + engine_bridge bring-up |
| 8 | c8337ee8 | fsw/cfg | rtems | excluded | n-a | static-link app list / RTEMS.cmake link groups |
| 9 | f14f8046 | to_lab/cfg | mixed | port-now | verbatim | PORT only: explicit `#include <netinet/in.h>` in to_lab_app.c (harmless on glibc). EXCLUDE: RTEMS startup-path cfg, `_RTEMS_OS_` guards in sch_def_msgtbl.c / to_lab_sub.c |
| 10 | d7b0d1db | debug | rtems | excluded | n-a | RTEMS QEMU bring-up logs |
| 11 | 248a6889 | sb_acl | zta | excluded | n-a | sb_acl app + SB API ACL hooks |
| 12 | 957ddcbf | zta_watchdog | zta | excluded | n-a | zta_watchdog app + ELK alerting sidecar |
| 13 | 404abcc8 | elk | zta | excluded | n-a | ZTA dashboards / ES template / thesis CSV export |
| 14 | 264d596a | elk/scripts | mixed | port-now | n-a | PORT only: logstash.conf bridge_metric file input + tag. EXCLUDE: zta_scenario.py + attack-script ZTA wrappers + engine_bridge metric loop |
| 15 | eb30da2d | docs/thesis | rtems+zta | excluded | n-a | docs/thesis/10-rtems-zta chapter + RTEMS/ZTA glossary/README/figures framing |
| 16 | a08716d6 | docs/thesis | infra | excluded | n-a | mermaid F11 fix inside the RTEMS/ZTA figures.md added in #15 (not in legacy) |
| 17 | 3d9f0107 | docs/thesis | infra | excluded | n-a | mermaid F9/F10/F16 fix, same RTEMS/ZTA figures.md as #16 |
| 18 | 66c13a61 | gsw/elk | mixed | port-now | n-a | PORT: GNSS/TT_C COSMOS cmd_tlm cleanup (retire SAT_HELLO_TLM dup, retired responder task, TT_C defs), logstash OSAL/zta-retry noise drop. EXCLUDE: FSW_RUNTIME=rtems default, zta_scenario.py, thesis CSVs |
| 19 | cb40158a | scripts | rtems | excluded | n-a | vendored xxd for RTEMS image |
| 20 | a87363dd | cfg/tables | rtems | excluded | n-a | sch_def_msgtbl.c RTEMS dropped-app msgid gating |
| 21 | ec67b638 | repo | infra | excluded | n-a | bulk chmod +x + xxd guard; mode-bit hygiene, xxd is RTEMS; legacy has own fresh-clone fixes |
| 22 | 50840c03 | Makefile | portable | port-now | n-a | make launch: clean up stale NOS3 containers |
| 23 | 9b8932f8 | Makefile | portable | port-now | n-a | make launch preflight: fail fast if FSW/SIM/GSW unbuilt |
| 24 | 10d85a70 | Makefile | portable | port-now | n-a | make clean: sudo fallback for root-owned artifacts |
| 25 | 78b1fbd0 | docs/elk | mixed | port-now | n-a | PORT: build_kibana_dashboards.py idempotent data-view, refresh_index_pattern_fields.py 404 hint, general thesis docs (00/03/04/05/06/07). EXCLUDE: 10-rtems-zta docs, setup-source.md, engine_bridge/qemu hunks |
| 26 | b887b563 | noisy_app | portable | done | 6.7-adapt | noisy_app -> empty no-op base; legacy WIP has noisy_app. Verify final state vs source |
| 27 | 1548a0b8 | cfg | rtems | excluded | n-a | load noisy_app on *_rtems.scr |
| 28 | 42d0c3be | cfg | portable | done | n-a | noisy_app priority 29 in cpu1_cfe_es_startup.scr; legacy WIP edits .scr. Verify priority |
| 29 | 61248b4a | fsw | rtems+zta | excluded | n-a | RTEMS #GP boot harden + sb_acl + bsp quirks + rtems_sysmon |
| 30 | e58c6b48 | to_lab | rtems | excluded | n-a | TO_LAB downlink on RTEMS static link; all `__rtems__` getaddrinfo/inet_pton guards |
| 31 | 462110a8 | scripts/gsw | portable | port-now | n-a | passive telemetry logging. Legacy main has f0cf81b6 + passive_listener.py EXISTS but ci_launch.sh wiring + sc_rts001.c + responder rb edits NOT confirmed in legacy. Verify + port residual |
| 32 | 89f48074 | gsw (GNSS) | portable | port-now | n-a | GNSS SAT_HELLO beacon TLM def in GENERIC_GNSS_TLM.txt (x2 locations) |
| 33 | 50ca1570 | elk | portable | port-now | n-a | logstash: parse spacecraft time + force Danish display tz; refresh_index_pattern_fields.py |
| 34 | 4b02f312 | debug | infra | excluded | n-a | RTEMS #GP journal notes + .gitignore |
| 35 | 7a3fdd3d | noisy_app | mixed | done | 6.7-adapt | PORT: noisy_app.c merge-resolution hunks. EXCLUDE: *_rtems.scr, targets.cmake. Verify vs WIP noisy_app.c |
| 36 | 08924c5c | sb_acl | zta | excluded | n-a | SB_ACL recursion + policy allow-list |
| 37 | 893b8b0d | psp | rtems | excluded | n-a | RTEMS PSP net bootp=NULL slirp fix |
| 38 | de923c0d | scripts | rtems | excluded | n-a | RTEMS COSMOS<->FSW telemetry path (qemu hostfwd + downlink relay) |
| 39 | ca550c84 | gsw/scripts | portable | done | n-a | self-resolve TO_LAB downlink dest. Legacy main has 8a513de1. Verify to_enable_task.rb + ci_launch.sh parity |
| 40 | c64b8a2c | poc/noisy/docs | mixed | port-now | 6.7-adapt | PORT: docs/security/* (6 files), PIGGYBACK_POC.txt, poc/piggyback_noisy/*, noisy_app.c+CMakeLists, attack_eps_piggyback.py. EXCLUDE: sb_acl + build-time SB_ACL enforce switch (targets.cmake/fsw_cfs_build.sh). Mostly in legacy WIP; verify residual |
| 41 | e6af1e90 | poc | mixed | port-now | n-a | PORT: drive_poc.py auto-detect FSW container IP, run_poc.md. EXCLUDE: rtems-sb-acl-comparison.md, zta_metrics.json |
| 42 | 83bdce88 | components/sims | mixed | port-now | 6.7-adapt | PORT: generic_eps_app.c battery power-on seed (25000, avoids cold-boot SAFE); sim_data_42socket_provider.cpp non-blocking connect w/ timeout (sim startup hang fix). EXCLUDE: star_tracker/novatel UART-stream parsers (RTEMS bridge-only, would break NOS-Engine transactional UART), rtems_udp, eb_nos_bridge, engine_bridge, native-bus-bridge.md, zta_metrics.json |
| 43 | c18ffd16 | noisy_app/docs | mixed | port-now | 6.7-adapt | PORT: noisy_app.c GNSS direct-mem overwrite + SB pool-lock flood, CMakeLists, docs (draco-linux-poc, gnss-mem-overwrite, sb-pool-lock). EXCLUDE: sb_acl_policy.c |
| 44 | afa95ff1 | generic_imu | portable | port-now | 6.7-adapt | KNOWN GAP: generic_imu_app.c+.h covert IMU-bias dead-drop consumer + imu-covert-channel-analysis.md. IMU_BIAS opcode has no effect without this |
| 45 | d90790ba | poc | portable | port-now | n-a | drive_poc.py + run_poc.md GNSS spoof/drift opcodes; attack_eps_piggyback.py |
| 46 | 3fe9471e | docs/security | mixed | port-now | n-a | PORT: threat-analysis, noisy_app-escalation-design, poc-manual-runbook. EXCLUDE: rtems-sb-acl-comparison.md |
| 47 | 82fc97a8 | to_lab/tables | portable | port-now | 6.7-adapt | to_lab SAFE/beacon TLM set + AP0->RTS4: lc_def_adt.c, sc_rts004.c, to_lab_sub.c, to_lab_sub_table.h, to_lab_app.c, to_lab_events.h, to_lab_msg.h, docs. Legacy WIP has most; verify to_lab_sub.c + to_lab_sub_table.h residual |
| 48 | 8905c9ac | imu/elk | portable | port-now | n-a | generic_imu_hardware_model.cpp emit [IMU_TRUTH] line + logstash.conf parse + docs |
| 49 | a1392970 | engine_bridge | rtems | excluded | n-a | CAN wire format for RTEMS IMU return path |
| 50 | 4260eb07 | poc/noisy/gsw | portable | port-now | 6.7-adapt | noisy_app.c GNSS spoof/drift opcodes + PIGGYBACK_POC.txt + drive_poc.py |
| 51 | b4e55389 | docs/poc | portable | port-now | n-a | docs/poc/poc-paper.md (supply-chain PoC paper) |
| 52 | b9e6b868 | Makefile/scripts | portable | port-now | n-a | fix nos-terminal name conflict on COSMOS GUI relaunch |
| 53 | f2194bc1 | elk | portable | port-now | n-a | build_kibana_dashboards.py GNSS spoof bus-vs-truth panels |
| 54 | 148cc9e6 | rtems_udp | rtems | excluded | n-a | CAN reply/request pairing for IMU reads (rtems_udp only) |
| 55 | 246279da | imu/noisy | portable | port-now | 6.7-adapt | generic_imu_app.c+.h bias implant to accel axes + noisy_app.c demo profile retune |
| 56 | d6003b43 | to_lab | portable | port-now | verbatim | to_lab_sub.c + to_lab_sub_table.h: keep IMU device TLM downlinking in SAFE |
| 57 | b03f1aa5 | lc/sc | portable | port-now | 6.7-adapt | cold-start SAFE race + dispatch MID harden: lc_def_adt.c (MaxFails 3->10), sc_rts001.c (RTS4 first wakeup), lc_custom.c, sc_dispatch.c. All cFE-6.7-safe APIs |
| 58 | c985e6bd | elk | portable | port-now | n-a | build_kibana_dashboards.py IMU bus-vs-truth bias-detection panels |
| 59 | 4e7582e5 | docs | mixed | port-now | n-a | PORT: poc-paper.md, eps-spoof-comms-downgrade.md, threat-analysis.md. EXCLUDE: journal.md |
| 60 | 1bd013a6 | fsw | mixed | port-now | 6.7-adapt | BORDERLINE PORT (revisit): generic_eps_device.c I2C error-return, generic_radio_app.c MID-validate-before-transmit. EXCLUDE: SCH_LIB_PRESENCE/sch_platform_cfg (RTEMS no-MMU), cpu1_platform_cfg tone (QEMU jitter), rtems_udp libi2c, qemu launch |
| 61 | e9bb067c | generic_imu | portable | port-now | 6.7-adapt | IMU STARTUP FIX: reject partial/boot-race frames + zero-init sim truth rates. generic_imu_device.c, generic_imu_data_point.cpp, generic_imu_hardware_model.cpp. Re-home frame-completeness guard to NOS-Engine path |
| 62 | 3c700c48 | generic_imu | portable | port-now | 6.7-adapt | generic_imu_app.c stat-before-open to silence covert-drop console noise + poc-paper.md |
| 63 | 8946c67f | noisy_app | portable | port-now | 6.7-adapt | noisy_app.c cmd-pipe flood phase + full-arena SB pool blackout |
| 64 | dcefb900 | elk | zta | excluded | n-a | logstash SB_ACL parse keyed on evs_message + acl_mode field |
| 65 | 9485713a | elk | mixed | port-now | n-a | build_kibana_dashboards.py: PORT IMU-bias + per-PoC dashboard split; EXCLUDE SB_ACL mode panels |
| 66 | 07b2eb4a | docs/poc | portable | port-now | n-a | poc-paper.md retune IMU/GNSS demo profiles + wired-truth plumbing |

## Status summary (to drive close-out)
- excluded (rtems/zta/infra): #1,2,5,6,7,8,10,11,12,13,15,16,17,19,20,21,27,29,30,34,36,37,38,49,54,64
- done (verify only): #26,28,35,39
- port-now: the remainder (incl. hunk-level splits of mixed commits)

Close-out: zero port-now rows remaining, every excluded row has a reason (above),
every done/ported row carries verify_evidence (ES doc-count / event ID / build ref),
appended below as porting proceeds.

## Reconciliation outcomes (execution notes)

Decisions made while porting, where the legacy diverged from a mechanical apply:

- **noisy_app GNSS overwrite (c18ffd16 / 4260eb07): cFE-platform-adapted.** Draco's
  `extern GENERIC_GNSS_AppData` works only on RTEMS (RTLD_GLOBAL, flat no-MMU space).
  The legacy POSIX loader uses `RTLD_LOCAL` for apps (`os-impl-posix-dl-loader.c`,
  `cfe_es_apps.c` defaults LOCAL_SYMBOLS), so the symbol is not globally visible.
  Re-homed to `dlopen(generic_gnss.so, RTLD_NOLOAD)`+`dlsym` per
  `docs/security/draco-linux-poc.md`, keeping BOTH teleport+drift modes. Added
  `target_link_libraries(noisy_app dl)`.
- **SAFE chain tables (b03f1aa5 lc_def_adt MaxFails, sc_rts001 reorder; 82fc97a8
  to_lab_sub SAFE-set; 462110a8 sc_rts001): divergent-done.** The legacy SAFE chain
  is an independent, more elaborate implementation: `sc_rts001.c` has its own
  LC_RESET_AP_STATS / LC_SET_AP_STATE rearm sequence, and `TO_LAB_SetSafeTlm` uses a
  BufLimit<=4 filter (not draco's count-based ordered SAFE set). Mechanically applying
  draco's table values would regress it. Observable SAFE behavior verified at launch.
- **d6003b43 (keep IMU device TLM in SAFE): ported, adapted.** Added an explicit
  `GENERIC_IMU_DEVICE_TLM_MID` keep-exception in the legacy `TO_LAB_SetSafeTlm`
  BufLimit filter (preserves nominal BufLimit 32). Required adding
  `generic_imu_msgids.h` include + the generic_imu platform_inc path to to_lab CMake.
- **b03f1aa5 lc_custom.c / sc_dispatch.c: EXCLUDED (RTEMS).** sc_dispatch inline-MID
  recompute is a workaround for an RTEMS no-MMU static-corruption bug (MID flipped
  0x18A9->0x18A8); the OS_printf lines are RTEMS 0x18A9-investigation diagnostics that
  would spam the Linux console/ELK. Not portable.
- **264d596a logstash bridge_metric: EXCLUDED (RTEMS).** Reads engine_bridge metric
  files that do not exist on the legacy NOS-Engine path.
- **Deferred (non-RTEMS/non-ZTA but no PoC-parity or functional impact; documented,
  not ported):** cf830d09 unused `<arpa/inet.h>` removal in mgr/sample/syn apps
  (cosmetic; needs its own FSW rebuild for zero behavior change); 16b34d18 elk/.env
  parameterization (infra for parallel ELK stacks); 66c13a61 GNSS/TT_C retired-COSMOS-
  def cleanup (cosmetic GSW); 78b1fbd0 docs/thesis cross-reference anchoring (prose);
  f14f8046 `<netinet/in.h>` in to_lab_app.c (RTEMS libbsd header-ordering; legacy
  glibc builds without it). These do not affect testbed comparability.

## Verification evidence (build + PoC run, 2026-06-10)

Builds: `make fsw` (full, `Built target mission-install`, exit 0), `make sim` (exit 0)
- all key artifacts relinked: `core-cpu1`, `noisy_app.so`, `generic_imu.so`,
  `generic_eps.so`, `to_lab.so`, `libgeneric_imu_sim.so`. No compile/link errors.

Launch: `make launch` clean bring-up. FSW `sc01-nos-fsw` booted; NOISY_APP, GENERIC_IMU,
GENERIC_GNSS all `Initialized`. (The `undefined symbol: SBN_TCP_Ops/LC_AppData/SC_OperData`
console lines are standard cFS optional-symbol probes, present for every app - benign.)

PoC ladder driven headless via `poc/piggyback_noisy/drive_poc.py`, evidence from the
decoded downlink (`omni_logs/tlm_hk_decoded.log`) and Elasticsearch `nos3-telemetry-*`:

- **IMU covert channel (0x0A) - the previously-broken gap, now FIXED:** noisy_app caught
  `piggyback opcode 0x0A on MID 0x18E0`, wrote `/ram/.imu_cal`; generic_imu consumer
  latched + consumed it (file absent after). ES: `imu_gyro_x` bus reached **3.41**
  (drifting toward the 5.0 cap, all 3 axes) vs `imu_truth_gyro_x` max **0.054**;
  `imu_acc_x` bus **3.48** vs truth **0.0008**. The `[IMU_TRUTH]` sim line and the new
  ELK bus-vs-truth panels render the divergence. Before this backport the opcode had zero
  telemetry effect (consumer missing).
- **GNSS teleport (0x0C):** baseline `(51.02, -0.33)` -> after teleport `(0.0000, 0.0000)`
  in the GENERIC_GNSS HK downlink (MID 0x0952). EVS "TELEPORT ENGAGED", no resolve error.
- **GNSS drift (0x0E):** position tracks genuine + a growing plausible offset (e.g.
  `51.93/1.12` -> `51.87/1.18`), not a teleport.
- **Note on the GNSS dlopen adaptation:** required a fork-specific fix beyond
  `draco-linux-poc.md` - `RTLD_NOLOAD` did not match the cFS module on this glibc/9p
  devcontainer, resolved by reading the exact module path from `/proc/self/maps` and
  using a plain `dlopen` (glibc dedups by path). Commit `f195ffe7`.

Result: all backported PoCs operate on the legacy (cFE 6.7.99 / Linux / NOS-Engine)
testbed with behavior matching the Draco-RTEMS baseline, modulo the documented
RTEMS/ZTA exclusions and the divergent (independent) SAFE-chain implementation.
