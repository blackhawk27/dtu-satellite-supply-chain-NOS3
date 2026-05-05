# 01. Reproducibility: Clone to Launch

This document is the canonical step-by-step build and run guide
for the testbed. Every phase below maps to a single Makefile
target (or a small group of them), runs to completion in under
30 minutes total on a modern Linux host with Docker, and ends
with a verification step the reader can run to confirm the
phase succeeded before moving on.

The phases align to the way the testbed grew historically: a
reader who follows them in order is replaying the same sequence
that the original integrator went through. Where a phase
involves a load-bearing decision (a configuration value that
must not be reverted to its upstream default), the phase notes
say so and points to the matching deep-dive.

## Prerequisites

| Requirement | Tested with | Notes |
|---|---|---|
| Linux x86_64 host | Ubuntu 22.04, Debian 12, WSL2 with Docker Desktop | macOS and native Windows are not supported. |
| Docker Engine | 24.x or 25.x | The build runs entirely inside containers, so no host-side toolchain is required. |
| `git`, `make`, `curl`, `python3` | Distribution defaults | These run on the host; everything else runs in containers. |
| ~30 GB free disk | | Roughly 8 GB images, 5 GB build artefacts, the rest for telemetry indices and saved runs. |

For local development inside VSCode, the repository ships with
a devcontainer at `.devcontainer/devcontainer.json` that pulls
the same toolchain image and configures the WSLg X11 forward
needed by 42's GUI windows. The devcontainer is the
recommended development environment but is not required for
headless runs (`make ci-launch`).

The load-bearing notes file at the project root flags two
classes of edits that must not be reverted under any
circumstances: the `Inp_IPC.txt` port table (entries 4280,
4282, 4286, 4287) and the `.devcontainer/devcontainer.json`
WSLg integration. Both are explained in the relevant phase
below; the short version is "do not add an X11 mount to the
devcontainer, do not widen the IPC prefixes back to upstream
defaults".

## Phase 1: Acquire

```bash
git clone https://github.com/blackhawk27/dtu-satellite-supply-chain-NOS3-Draco.git
cd dtu-satellite-supply-chain-NOS3-Draco
```

The repository is self-contained. It does not use git submodules
(the original NOS3 submodules were flattened in commit
`55ad2148` so the fork has a single tree). All upstream NOS3
content is already in-tree at the import baseline; every commit
after `55ad2148` is DTU-authored.

**Verification:** `git log --oneline | tail -1` reports
`55ad2148 chore: imported and flattened NOS3 environment with
all submodules`. `git status` is clean.

## Phase 2: Configure

All `make` commands run from the `nos3/` subdirectory, where
the top-level Makefile lives.

```bash
cd nos3
make config
```

`make config` runs `scripts/cfg/config.sh`, which invokes the
Python `configure.py` driver and reads the active mission XML
at `cfg/nos3-mission.xml`. The output is `cfg/build/`, a tree
of generated build scripts and definition files that the
later phases depend on. The default mission XML selects:

- `<flight-software>cfs</flight-software>`: the cFS flight stack (not F').
- `<ground-software>cosmos</ground-software>`: COSMOS as the operator console.
- `<sc-1-cfg>spacecraft/sc-mission-config.xml</sc-1-cfg>`: the standard EO1 spacecraft profile.

To override the spacecraft profile at config time:

```bash
make config SC1_CFG=spacecraft/sc-research-config.xml
```

The four available profiles live under `cfg/spacecraft/`:
`sc-minimal-config.xml` (heritage cFS apps + sample only),
`sc-mission-config.xml` (default; full generic component set
plus ADCS), `sc-research-config.xml` (adds Arducam, thruster,
research integrations), `sc-fprime-config.xml` (F' variant,
not used in this thesis).

**Verification:** `cfg/build/current_config_path.txt` exists
and points at the spacecraft profile selected. `cfg/build/`
contains `fsw_build.sh`, `gsw_build.sh`, `launch.sh`, and a
populated `nos3_defs/` tree.

## Phase 3: Prepare

```bash
make prep
```

`make prep` runs `scripts/cfg/prepare.sh`, which pulls the
build container image (`ivvitc/nos3-64:20251107`), creates
the `~/.nos3/` host-side state directory, and stages the 42
dynamics simulator under `~/.nos3/42/` if it is not already
present. The script is idempotent; running it twice is a
no-op.

This phase is the first one that touches Docker. It pulls
roughly 8 GB across three images: the build image, the
runtime spacecraft image, and the COSMOS ground-software
image. ELK images are pulled lazily by the launch target.

**Verification:** `docker images | grep nos3-64` returns the
build image at the pinned tag. `ls ~/.nos3/42/` shows the
42 sources.

## Phase 4: Build

```bash
make
```

The bare `make` invokes the `all` target, which sequentially
builds the flight software, the simulators, and the ground
software:

1. `make fsw`: drives `cfg/build/fsw_build.sh` inside the
   build container. Output is `fsw/build/amd64-nos3/default_cpu1/`
   (CMake build tree) and `fsw/build/exe/cpu1/` (installed
   `core-cpu1` binary plus all loadable cFS apps). Roughly 5
   minutes on a modern host.
2. `make sim`: builds every hardware-bus simulator under
   `components/<name>/sim/` and the common simulators under
   `sims/`. Output is `sims/build/`. Roughly 3 minutes.
3. `make gsw`: builds CryptoLib (the FSW-side crypto bridge)
   and the COSMOS ground-software image. Roughly 4 minutes.

The build runs fully containerised; the host toolchain is
not used. The build container's `/workspaces` is bind-mounted
to the repository root, so artefacts written by the
container are visible immediately on the host.

**Verification:** `ls fsw/build/exe/cpu1/cf` lists the
loadable cFS apps (one `.so` per app: `mgr.so`, `beacon_app.so`,
`generic_eps.so`, etc.). `ls sims/build/bin` lists the
simulator binaries (`generic-eps-sim`, `generic-tt_c-sim`,
`generic-gnss-sim`, etc.).

## Phase 5: Launch

```bash
make launch
```

`make launch` is the integration target. It does, in order:

1. Stops any prior capture-script processes (`god_view_capture.py`,
   `cfs_evs_capture.sh`, `sim_logs_capture.sh`, `cpu_monitor.sh`).
2. Recreates the live log directories `omni_logs/` and
   `attack_logs/` if missing, with the cloning user's ownership.
3. Truncates yesterday's capture files (`make purge-logs`).
4. Brings up the ELK stack (`make start-elk`), waits for
   Elasticsearch to leave RED, and deletes any prior
   `nos3-telemetry-*` indices so the run starts clean.
5. Calls `scripts/ci_launch.sh` to start the FSW container,
   the simulator containers, the COSMOS GSW container, and
   the 42 dynamics container. The script also runs
   `xhost +local:*` so 42's GUI windows can attach to the
   X server.
6. Starts the four capture scripts in the background.
7. Waits 15 seconds for the capture scripts to write their
   first lines, then restarts Logstash so its file-tail
   inputs see the freshly-created files.
8. Refreshes Kibana's index-pattern field cache so newly
   indexed fields show up immediately in Lens panels.

The result is a running stack: cFS in the FSW container,
NOS Engine bus simulators in their containers, 42 propagating
the orbit and attitude in `sc01-fortytwo`, COSMOS listening
for commands on the GSW network, ELK ingesting both the
god-view JSON and the per-component simulator logs.

**Verification:** `docker ps` shows roughly 10 running
containers including `nos3-elasticsearch`, `nos3-logstash`,
`nos3-kibana`, `nos3-cosmos`, the per-component simulator
containers, and `sc01-fortytwo`. The 42 GUI windows (42 Cam,
42 Map, 42 Unit Sphere Viewer) appear on the host display.
Kibana at `http://localhost:5601/` shows the two curated
dashboards under "Dashboards":

- `nos3-std-telemetry-overview`: shows non-zero Software-Bus
  message rate, MID donuts, busiest subsystems.
- `nos3-std-mission-validation`: shows current mission mode
  (MGR), heartbeat rate (HS), CPU usage, battery state of
  charge, GPS altitude.

## Phase 6: Headless variant

For automated testing or thesis-result reproduction without
the GUI windows, use `make ci-launch` instead. It runs
`scripts/ci_launch.sh` followed by `scripts/system_tests.sh`
and `scripts/stop.sh`, exits with a non-zero status if the
system test harness fails, and produces no GUI windows. This
is the path that the thesis evaluation scripts invoke.

## Phase 7: Save and replay a run

```bash
make save-run RUN_LABEL=my_attack_scenario
```

`save-run` archives `omni_logs/*.log` and
`attack_logs/cfs_god_view.json` to
`$HOME/nos3_runs/<label>/`, captures the git commit hash,
and stops the simulation. The Elasticsearch indices are
preserved on disk across this stop; the `make stop` invoked
internally uses `KEEP_TLM=1` to skip the index-deletion step.

To replay a saved run later:

```bash
make load-run RUN=my_attack_scenario
```

`load-run` deletes the live `omni_logs/` and the
`cfs_god_view.json`, copies the saved files in their place,
deletes the live telemetry indices, and starts the ELK stack
so Logstash re-ingests the saved logs into fresh time-stamped
indices. The simulation is not restarted; only the telemetry
pipeline runs. This is the workflow used to reproduce thesis
attack scenarios on a clean stack without re-running the
simulation.

## Phase 8: Tear down

```bash
make stop          # stop containers, keep build artefacts
make clean         # also remove cfg/build/ and build trees
make uninstall     # also remove Docker images and containers
```

`make stop` is the quick reset between runs. `make clean`
forces the next launch through Phase 2 (configure) and Phase
4 (build) again. `make uninstall` returns the host to a
state equivalent to "after Phase 1, before Phase 2", at the
cost of having to re-pull the Docker images on the next run.

## Common failure modes

| Symptom | Likely cause | Resolution |
|---|---|---|
| `make launch` finishes but no 42 GUI windows appear | Stale Docker container left from an earlier run, or the X11 forward broke | `make stop && make launch`; run `xeyes` inside the devcontainer to confirm X is healthy. Do NOT add `runArgs` or `containerEnv` blocks to `devcontainer.json`; the upstream-clean configuration is correct. The post-mortem in `docs/postmortem_42_gui.md` lists the diagnostic ladder. |
| Logstash receives no events | Capture scripts not running, or `cfs_god_view.json` not being written | Check `docker ps` for the FSW container; check the capture-script logs under `omni_logs/`; restart Logstash with `docker restart nos3-logstash`. |
| Elasticsearch RED on launch | Disk full or prior run did not stop cleanly | `docker volume prune` after stopping all containers; rerun `make launch`. |
| 42 hangs at startup, sim containers stall | `Inp_IPC.txt` was reset to upstream defaults | Verify the load-bearing tight-prefix invariants on ports 4286 (TT&C) and 4287 (GNSS), and the OFF state on ports 4280 (thruster) and 4282 (star tracker). The deep-dive lives in [`03-communication/04-sim-to-42-dynamics.md`](03-communication/04-sim-to-42-dynamics.md). |
| Build fails inside container with `redefined` errors | A header is being re-included against upstream cFE Draco | The fix is in `nos3/cfg/nos3_defs/cfe_msgid_api.h`: the `#ifndef` guards around the topic-id-to-MIDV macros must be present. |

## Reproducibility commit-hash anchor

This guide was written against commit `0fda60f5 fix(elk):
prevent Kibana hangs and implement orphan object purge`
(latest at the time of authoring). Future commits may add
new phases (for example, an FPGA bitstream simulator step or
an additional ground station); the eight-phase backbone is
expected to remain stable.

For a record of every change since the import baseline, see
[`07-customizations-vs-upstream.md`](07-customizations-vs-upstream.md).
