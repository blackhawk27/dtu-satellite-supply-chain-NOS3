# 07. Customizations vs. Upstream NOS3

This document is the authoritative list of every change DTU
made to the NOS3 testbed since the import baseline. It is
generated against commit `55ad2148 chore: imported and
flattened NOS3 environment with all submodules`, which is
the snapshot of upstream NOS3 with submodules expanded into
the in-tree layout this fork uses. `origin/main` is not a
useful baseline; the user's fork pushes back to its own
`origin/main`, so the divergence is captured by the commit
range `55ad2148..HEAD`.

A reader who wants to regenerate the raw stats this document
is built from should run:

```bash
git log --oneline 55ad2148..HEAD                # 128 commits at the time of writing
git diff --stat 55ad2148..HEAD                  # file-level divergence
git diff --diff-filter=A --name-only 55ad2148..HEAD -- nos3/components/  # new component apps
git diff --diff-filter=A --name-only 55ad2148..HEAD -- nos3/fsw/apps/    # new FSW apps
git diff --diff-filter=M --name-only 55ad2148..HEAD -- nos3/components/  # modified component apps
```

The headline numbers as of authoring: 128 commits ahead of the import; 1202 new files under `nos3/fsw/`; 166 new files under `nos3/gsw/`; 79 new files under `nos3/components/`; roughly 11000 modifications spread across `nos3/fsw/`, `nos3/gsw/`, `nos3/components/`, and the build configuration.

**Post-thesis update (2026-05-03):** the thesis baseline was frozen at commit `2b3af0fc docs: add thesis documentation tree and 42 GUI postmortem` on 2026-05-01. Sixteen commits landed after that baseline (HEAD is `7afa0107` as of authoring this section); they are catalogued in section 8 below. Most additions are in the ELK pipeline, the GSW (cosmos) tooling, and the LC / SC / MGR autonomous-trigger machinery; no new components were added.

The categories below organise these into a tractable list.

## 1. ELK telemetry pipeline (entirely new)

Path: `nos3/elk/`. Did not exist upstream. Added in commit
`08fe63c3 feat: integrated ELK stack and cFS god view
logging` and refined across roughly 12 follow-up commits.

| File | Purpose |
|---|---|
| `nos3/elk/docker-compose.yml` | Brings up Elasticsearch, Logstash, and Kibana containers on the `nos3-core` Docker network. |
| `nos3/elk/logstash.conf` | Single ingest pipeline. Tails `cfs_god_view.json` (tagged `software_bus`) and `omni_logs/*.log` (tagged `system_log`); runs grok and mutate filters; outputs structured docs to Elasticsearch. |
| `nos3/elk/index_template.json` | Elasticsearch index template that fixes field types (numeric, keyword, date) so KQL panels render correctly. |
| `nos3/elk/build_kibana_dashboards.py` | Idempotent dashboard generator. Re-creates the two curated dashboards (`nos3-std-telemetry-overview`, `nos3-std-mission-validation`) via the Kibana Saved Objects API. |
| `nos3/elk/load_dashboards.py` | NDJSON importer. Used after a fresh Kibana volume to restore the checked-in dashboard set. |
| `nos3/elk/refresh_index_pattern_fields.py` | Re-populates the Kibana field cache after daily index rollover. |
| `nos3/elk/dashboards/nos3-eo1-dashboards.ndjson` | Checked-in NDJSON export of the curated dashboards. |

The deep-dive lives at [`05-elk/`](05-elk/).

## 2. Newly-added cFS apps

### 2a. New under `nos3/components/`

| App | Source | Status |
|---|---|---|
| `generic_tt_c` | `nos3/components/generic_tt_c/` | NEW. Added in commit `a385a187 feat(components): add generic_tt_c and generic_gnss component sources`. cFS app + simulator. Wired into cFE startup script and 42 IPC port table. Deep-dive: [`04-apps/tt_c.md`](04-apps/tt_c.md). |
| `generic_gnss` | `nos3/components/generic_gnss/` | NEW. Same commit as `generic_tt_c`. cFS app + simulator. Deep-dive: [`04-apps/gnss.md`](04-apps/gnss.md). |
| `mgr` | `nos3/components/mgr/` | NEW. DTU mission manager. cFS app, no simulator (it is a logical service, not hardware). Deep-dive: [`04-apps/mgr.md`](04-apps/mgr.md). |

### 2b. New under `nos3/fsw/apps/`

| App | Source | Status |
|---|---|---|
| `beacon_app` | `nos3/fsw/apps/beacon_app/` | NEW. Added in commit `90e024c1 feat: add BEACON_APP ground-commandable ping/pong telemetry application`. Used as a baseline for telemetry-injection attack experiments. Deep-dive: [`04-apps/beacon_app.md`](04-apps/beacon_app.md). |
| `noisy_app` | `nos3/fsw/apps/noisy_app/` | NEW. Added in commit `dd750790 feat: implemented and staged noisy_app broadcast storm attack`. Spoofs EPS housekeeping; used as the in-band realisation of a tampered hardware component. No deep-dive page per scope decision; documented here only. Function code 3 spoofs EPS HK (`6bed8787 feat(noisy_app): add EPS HK telemetry spoofing capability (function code 3)`). |
| `cs`, `ds`, `fm`, `hk`, `hs`, `lc`, `md`, `mm`, `sc` | `nos3/fsw/apps/<name>/` | These were not present in the import baseline as full sources (the flattened import omitted them) but are stock cFS service apps that the EO1 mission requires. They are upstream cFS code, vendored into the tree. |

## 3. Load-bearing fixes to existing apps and configuration

### 3a. ADCS-cluster first-tick stof guard

Affected apps: `generic_adcs`, `generic_reaction_wheel`,
`generic_imu`, `generic_mag`. Commit: `962b61e7 fix(sim):
empty-string guard on first-tick std::stof/stod for IMU,
MAG, RW`. Symptom (pre-fix): the simulator's `do_parsing`
function crashed on the first tick because 42 had not yet
emitted any data and `std::stof("")` throws
`std::invalid_argument`.

A separate but related fix in `generic_css` is commit
`26279b3b fix(elk+css): clear ERROR sweep for LC AP
events, thruster CMD port, CSS stof, no-cmd-port narrative,
HS_SysMon`.

### 3b. EPS load model + first-tick race

Affected app: `generic_eps`. Commits:

- `2976dc74 feat(sim+elk): EPS load model, ADCS resilience,
  LC throttling, ELK noise sweep` (the load-model extension
  itself)
- `59b1cbf5 fix: resolve EPS sim race condition causing
  battery overflow on first tick` (the race fix)

Deep-dive: [`04-apps/eps.md`](04-apps/eps.md).

### 3c. 42 IPC port table

Path: `nos3/cfg/InOut/Inp_IPC.txt`. Four entries are
load-bearing:

| Port | Subsystem | Setting | Reason |
|---|---|---|---|
| 4280 | Thruster | `OFF` | The thruster simulator is disabled in the EO1 spacecraft config. With `RX`, no client connects and 42 hangs in `inet_csk_accept`. Commit `d16c89e3 fix(ipc): set Thruster IPC OFF (port 4280)`. |
| 4282 | Star Tracker | `OFF` | The active sim XML registers the synthetic ST provider, not the 42 provider. Same hang mechanism as port 4280. |
| 4286 | TT&C TX | tight prefix (2 keys) | 42's `WriteToSocket` is non-blocking and exits on EAGAIN. Wide prefixes overflow the kernel TCP send buffer at 100 Hz. |
| 4287 | GNSS TX | tight prefix (1 key) | Same EAGAIN-on-overflow rationale. |

The bracket-stripping parser fix that goes with the
tight-prefix invariant lives in
`generic_tt_c_data_point.cpp` and
`generic_gnss_data_point.cpp`; commit `4e2022fb feat(elk/sim):
integrate TT&C and GNSS sims with status codes and
optimized dashboards` introduced it.

### 3d. Comment-buffer overflow protection

Path: `nos3/cfg/InOut/Inp_IPC.txt` comment lines must stay
under 100 characters. 42's `InitInterProcessComm` uses
`char junk[120]` with unbounded `%[^\n]` `fscanf`. Longer
comments overflow the buffer and corrupt the next field,
producing `Bogus input ... in DecodeString (42init.c:249)`.

### 3e. cFE Draco computed-MID API

Files: `nos3/cfg/nos3_defs/cfe_msgid_api.h`,
`nos3/cfg/nos3_defs/nos3_component_msgid_mapping.h`. Commit
`778f1ec8 feat(msgids): introduce computed MID API for cFS
Draco v7.0.0 compatibility`. The headers split the heritage
NASA app MID base (`0x1800` / `0x0800`) from the new
component-app base (`0x1900` / `0x0900`) so both can
coexist in a single Draco build.

### 3f. MGR persistence and message-header init

Files: `nos3/components/mgr/fsw/cfs/src/mgr_app.c`. Commits:

- `e0a93250 fix(mgr): initialize CFE message header before
  writing persistence file`
- `87e0bdc2 fix(fsw): clamp bogus NOS Engine startup ticks
  and harden MGR persistence restore`

Deep-dive: [`04-apps/mgr.md`](04-apps/mgr.md).

### 3g. MD dwell-table baseline

Files: `nos3/fsw/apps/md/fsw/tables/md_dw01.c`,
`md_dw02.c`. The two dwell tables are EO1-specific
configurations that read LC/SC/CFE_ES_Global state for
attack-detection panels. Used by `attack_md_jam.py` as the
exfiltration vehicle in Scenario 2 Phase B.

### 3h. SC stored-command tables

Files: `nos3/cfg/nos3_defs/tables/sc_rts003.c` and
related. RTS 003 implements reaction-wheel desaturation:
LC AP2 fires when `DeviceEnabled_RW0 == 0`, the RTS
enables the magnetic torquer for 10 SCH ticks, then
disables it.

### 3i. SCH message table

Files: `nos3/fsw/apps/sch/fsw/tables/sch_def_msgtbl.c`.
Wakeup entries for the new component apps (TT&C, GNSS,
ADCS, MGR), plus the LC SAMPLE_AP wakeup at 1 Hz and the
MD dwell wakeup at 1 Hz.

## 4. Mission profile and orbit

### 4a. Orbit

Path: `nos3/cfg/InOut/Orb_LEO.txt`. Tuned for Denmark
overfly: 60 deg inclination, 346 deg RAAN, 61.6 deg true
anomaly. The 42 propagator uses these values to set initial
conditions; ground-station passes are timed against the
EO1 mission's autonomous SAFE -> SCIENCE transition.

### 4b. Ground stations

Commit `94f840af feat(42): add Svalbard (SvalSat) and DTU
Lyngby ground stations`. Both stations were added to 42's
`Inp_Sim.txt` and the related Gateway and STF1 inputs.

### 4c. Spacecraft profiles

Files: `nos3/cfg/spacecraft/sc-mission-config.xml`,
`sc-research-config.xml`, `sc-fprime-config.xml`,
`sc-minimal-config.xml`. The default for the EO1 mission
is `sc-mission-config.xml`. Modifications include the
OnAIR enablement (`5afaa4fa feat(cfg): enable OnAIR
framework in default mission spacecraft config`) and the
new component registrations
(`0f0b65ac feat(sim): register generic-tt_c-sim and
generic-gnss-sim with 42 providers`).

## 5. Build and tooling

### 5a. Coverage tooling

Files: `nos3/Makefile` (targets `code-coverage`,
`code-coverage-clean`, `code-coverage-internal`),
`nos3/scripts/coverage.sh`. Commit `a844d23d build(make):
add code-coverage-clean target and make coverage
incremental`. Output lands at `nos3/docs/coverage/`.

### 5b. Save-run / load-run

Files: `nos3/Makefile` (targets `save-run`, `load-run`).
Used to archive a simulation run with all logs and
re-ingest it later for offline analysis without re-running
the simulation.

### 5c. CI launch hardening

Files: `nos3/scripts/ci_launch.sh`,
`nos3/scripts/cfs_evs_capture.sh`,
`nos3/scripts/cpu_monitor.sh`,
`nos3/scripts/sim_logs_capture.sh`,
`nos3/scripts/god_view_capture.py`. Commit
`ac912da4 feat(scripts): overhaul launch infrastructure;
add FSW respawn and OnAIR`.

### 5d. Devcontainer

File: `.devcontainer/devcontainer.json`. The file is
load-bearing for X11 forwarding on WSL2 with Docker
Desktop: no `runArgs` block, no manual `containerEnv`
DISPLAY/WAYLAND_DISPLAY entries, and the `xhost +local:*`
inside `ci_launch.sh` must remain unwrapped. The
post-mortem at `docs/postmortem_42_gui.md` is the
canonical reference.

## 6. Attack instrumentation and research scripts

Path: `nos3/scripts/attack/`. New in this fork.

| Script | Purpose |
|---|---|
| `attack_md_jam.py` | Scenario 2 Phase B: redirects MD Dwell Table 2 to exfiltrate the EPS BatteryVoltage field. |
| `attack_noisy_trigger.py` | Scenario 1 Phase B: arms `noisy_app` via the `BEACON_PING_FC` command. |
| `attack_syn_leak.py` | Scenario 1 Phase A/C: floods `SYN_RESET_CC` to exercise the heap leak. |
| `investigate_mm_traversal.sh` | Phase 2 / Scenario 3A: surface audit for the `MM` path-traversal CVE-2025-25371. |
| `resolve_eps_address.sh` | Phase 3 / Scenario 2 Phase B helper. |
| `run_scenario1.sh` | Combined SYN-leak + noisy_app OOM attack scenario. |

These scripts are part of the thesis evaluation harness;
each runs against a live `make launch` and produces
telemetry that the curated Kibana dashboards visualise.

## 7. Research and documentation artefacts

| Path | Purpose |
|---|---|
| `debug/audits/20260428T232636Z/` | Numbered audit run from 2026-04-28 with `INVENTORY.md`, `FIX_PROPOSALS.md`, `PROGRESS.md`. |
| `debug/journal.md` | Chronological discovery journal. Captures load-bearing decisions in their original context. |
| `debug/FOLLOWUPS.md` | Open-item list. |
| `debug/EPS_*.md`, `debug/ADCS_*.md` | Per-subsystem design and validation notes. |
| `docs/missions/NOS3-DTU-EO1-mission-implementation.md` | Canonical mission spec. Updated by [`06-mission-doc-update.md`](06-mission-doc-update.md) when that slice ships. |
| `docs/postmortem_42_gui.md` | The 42 GUI startup post-mortem. |
| `docs/draco-upgrade-walkthrough.md` | Notes from the cFS Draco upgrade work. |
| `docs/cfs-resource-limits/` | Resource-limit documentation. |
| `docs/noise-filters.md` | Authoritative list of legitimate Logstash drop-rules and their rationale. |
| `docs/telemetry-and-fsw-reference.md` | Companion to the mission spec: packet-level MID byte layouts and FSW function bodies. |

## 8. Post-thesis additions (2026-05-01 to 2026-05-03)

Sixteen commits landed after the thesis baseline `2b3af0fc`. They cluster into six themes; the table records the commit hash, the date, the one-line subject as it lives in `git log`, and the thesis pages that were updated to match.

| Theme | Commits | Thesis pages updated |
|---|---|---|
| **GS Hello-Flow loop** | `c6c79140 feat(gnss/tt_c): add UART telemetry parser, science-mode gating, GS ping, and COSMOS targets` (2026-05-03); `8a45426f fix(gsw/gnss): bump GS responder echo timeout to 5s based on empirical p90` (2026-05-03); `7eaca741 docs(debug): journal the ECHO_TIMEOUT_S=5.0 tuning rationale` (2026-05-03); `bc6369c8 feat(elk/make): add GNSS GS validation AND-gate panels and harden launch reset` (2026-05-03) | [`04-apps/gnss.md`](04-apps/gnss.md), [`04-apps/tt_c.md`](04-apps/tt_c.md), [`04-apps/beacon_app.md`](04-apps/beacon_app.md), [`03-communication/05-fsw-to-gsw.md`](03-communication/05-fsw-to-gsw.md), [`05-elk/03-kibana-dashboards.md`](05-elk/03-kibana-dashboards.md) |
| **Headless COSMOS + mirror socket** | `f45ad877 feat(scripts): expand god_view capture and ci_launch for new telemetry` (2026-05-03); `c2a8c8db feat(make): add cosmos-gui target and rebuild kibana dashboards on launch` (2026-05-03); `0ecfbea1 chore(cosmos): align CFS target tlm definitions with current Draco struct layouts` (2026-05-03) | [`03-communication/05-fsw-to-gsw.md`](03-communication/05-fsw-to-gsw.md), [`05-elk/03-kibana-dashboards.md`](05-elk/03-kibana-dashboards.md) |
| **ELK dashboards + freshness** | `0dcccd58 feat(elk): rebuild kibana dashboards from python source and extract new gnss and rw fields` (2026-05-03); `7afa0107 feat(elk/dashboards): add freshness tiles and spacecraft-mode context` (2026-05-03) | [`03-communication/06-telemetry-to-elk.md`](03-communication/06-telemetry-to-elk.md), [`05-elk/02-elasticsearch-mappings.md`](05-elk/02-elasticsearch-mappings.md), [`05-elk/03-kibana-dashboards.md`](05-elk/03-kibana-dashboards.md) |
| **MGR mode-driven sensors** | `b8da042d feat(mgr): drive ADCS modes from spacecraft mode and auto-enable sensors at boot` (2026-05-03) | [`04-apps/mgr.md`](04-apps/mgr.md), [`04-apps/adcs.md`](04-apps/adcs.md) |
| **EPS hysteresis + ADCS sweep** | `1b441712 fix(eps/sim): add battery hysteresis band and tune capacity for visible eclipse drop` (2026-05-03); `8af2fdea feat(tables): add ADCS sensor wakeup slots, FOV-exit recovery RTS, and AP4 retargeting` (2026-05-03) | [`04-apps/eps.md`](04-apps/eps.md), [`04-apps/adcs.md`](04-apps/adcs.md) |
| **Audit / chore** | `7e740c16 docs(debug): record adcs, eps, gnss diagnosis and validation sessions` (2026-05-03); `5199d3a3 chore: untrack local skill notes and tidy comments` (2026-05-03); `aa4fa8ca fix(docs): unbreak Mermaid in figures.md by quoting dotted-edge labels` (2026-05-01) | [`08-figures/figures.md`](08-figures/figures.md) |

**Headline-number deltas:**

- Commits ahead of import: 128 -> 144.
- New thesis-tracked source files: GS responder + headless wrapper + 7 new index-template fields + 2 new dashboard rows.
- No new components or FSW apps; the loaded-app set is unchanged.

The cfs-resource-limits doc tree was also reorganised in this window: the previous single `docs/cfs-resource-limits/README.md` was split into `layers/` (six pages) and `apps/{heritage,draco-era,dtu}/` (one page per loaded app or component) to support the upcoming security-testing phase. That reorganisation is independent of the testbed code; the layer / app set is derived from `cfe_es_startup.scr` and the per-app platform headers.

## 9. Vendored upstream that is NOT modified

The following directories contain vendored upstream code
that DTU did not modify; the audit explicitly excludes
them from any writing-rules sweep:

- `nos3/fsw/fprime/` (F' framework, including vendored
  googletest under `nos3/fsw/fprime/fprime-nos3/lib/fprime/googletest/`)
- `nos3/gsw/yamcs/`

Modifications to these directories would diverge from the
upstream maintainers' tree and break future upstream
merges. They are left intact.
