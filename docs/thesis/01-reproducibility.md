# Reproducibility: clone to launch

This document is the contract between this repository and a future
reader who only has a fresh machine and the URL to the git remote.
Every phase below is a single command (or a single Makefile target),
followed by a verification step the reader can actually run. If a
verification step fails, the phase failed; do not proceed.

All `make` commands are issued from the `nos3/` subdirectory, because
that is where the top-level Makefile lives. Everything else is run
from the repository root unless explicitly noted.

The host requirements assumed throughout: a Linux box (this repo is
developed in a VS Code devcontainer on top of WSL2; native Linux and
macOS with a Linux VM also work), a Docker daemon the user can talk
to without sudo, roughly 60 GB of free disk for build artefacts and
images, and at least 8 GB of RAM (Elasticsearch alone reserves 512
MB and the build process is parallel). An X server is needed only
for the COSMOS GUI; the rest of the stack runs headless.

## Phase 1: clone and pin

**Goal.** Get a working tree that matches the state these documents
describe.

**Command.**
```
git clone <remote-url> dtu-satellite-supply-chain-NOS3
cd dtu-satellite-supply-chain-NOS3
```

**Verification.** Confirm the import-baseline commit is present at
the root of history. This documentation set is written against the
commit recorded in
[07-customizations-vs-upstream.md](07-customizations-vs-upstream.md).
```
git log --reverse --oneline | head -1
```
The first line must read
`55ad2148 chore: imported and flattened NOS3 environment with all submodules`.
If the hash differs, the local clone is on a different fork; the
documents in this directory may not apply.

## Phase 2: prepare the host environment

**Goal.** Pull the build container image, create the per-user
directories the launch scripts expect, and apply the host-side Linux
kernel tweaks NOS3 needs (specifically, disabling
`net.bridge.bridge-nf-call-iptables` so intra-bridge container
traffic on the `nos3-sc01` network is not silently filtered).

**Command.**
```
cd nos3
make prep
```

This wraps `scripts/cfg/prepare.sh`, which pulls the pinned NOS3
build image (`$DCALL image pull $DBOX`), sets up the per-user
directories, and builds the 42 dynamics engine inline. The image
tag is `ivvitc/nos3-64:20251107`; nothing else in the build matters
if this image is not local.

**Verification.**
```
docker images ivvitc/nos3-64 --format '{{.Repository}}:{{.Tag}}'
test -d $HOME/.nos3
```
The first command must print `ivvitc/nos3-64:20251107`. The second
must exit 0 (the prep step creates `~/.nos3/42/` for the 42 dynamics
engine, among other things). If either fails, re-run `make prep`
with `VERBOSE=1` and inspect the script output.

## Phase 3: configure the mission

**Goal.** Expand the mission XML
(`nos3/cfg/nos3-mission.xml`) and the active spacecraft profile
(`nos3/cfg/spacecraft/sc-mission-config.xml` by default) into
generated launch scripts and per-app definitions under
`nos3/cfg/build/`. This is the step that selects cFS over F-prime,
COSMOS over OpenC3 / YAMCS, and which component simulators run.

**Command.**
```
make config
```

To override the spacecraft profile without editing XML, pass
`SC1_CFG=spacecraft/sc-minimal-config.xml` (or any other profile in
`nos3/cfg/spacecraft/`). The default profile is the one these
documents describe.

**Verification.**
```
test -f cfg/build/current_config_path.txt
test -s cfg/build/elk/mid_to_name.yml
```
The first file is `make config`'s success marker. The second is the
ELK MID-registry YAML; on this fork it is produced either by
`scripts/mid/gen_mid_registry.py` (which often silently fails
against the current cFE topic-macro layout) or, failing that, copied
from `nos3/elk/seed_mid/` by the `start-elk` target. Either way,
the file must be non-empty before Logstash can start; without it
the Logstash container crash-loops on missing pipeline
dictionaries.

## Phase 4: build the flight software, the simulators, and ground software

**Goal.** Compile the cFS image, every simulator, the ground software
(COSMOS plus CryptoLib), and install everything into the locations
the launch scripts read from.

**Command.**
```
make
```

This is shorthand for `make all`, which runs `make config` if
needed, then sequentially `make fsw`, `make sim`, and `make gsw`.
Each of those is a small wrapper that bind-mounts the working tree
into a fresh build container and invokes CMake there. On a cold
clone with no Docker layer cache this takes thirty to forty minutes
on the reference devcontainer; on an incremental build it is
seconds.

Two operator-side rules are load-bearing for build velocity but
are not enforced by the Makefile. First, do not remove the
`nos_build_fsw` container between attempts; the bind-mounted build
directory persists on the host but cmake's view of the container
does not, so the next run treats every dependency as cold and
rebuilds from scratch. Second, do not bypass
`cfg/build/fsw_build.sh` to "work around" the `-it` TTY flag; a
shell without a TTY makes the build serial on the 9p bind mount,
which drags a 35-minute build into 4-plus hours. The recovery
from a failed build is to fix the source error and re-run
`make fsw`; cmake recompiles only what changed.

**Verification.**
```
ls fsw/build/exe/cpu1/core-cpu1
ls sims/build/bin/ | head
ls gsw/build/bin/crypto_standalone
```
The cFE executable, at least one simulator binary, and the
CryptoLib standalone must all exist. If any are missing, the build
phase has not completed; the second-most-common cause is the silent
serial-build slowdown described above, the most common is a syntax
error in a recent edit that the build container surfaces but the
host shell scrolls past.

## Phase 5: launch the full stack

**Goal.** Bring up the ELK stack on the shared `nos3-legacy-core`
Docker network, start every simulator and the FSW container on
`nos3-sc01` (with the shared services on `nos3-legacy-core`),
attach the four
telemetry-capture scripts (`god_view_capture.py`,
`cfs_evs_capture.sh`, `sim_logs_capture.sh`, `cpu_monitor.sh`), and
import the twenty Kibana saved objects (nineteen dashboards and one
saved search) through the Saved Objects API.

**Command.**
```
make launch
```

The target is idempotent: if the ELK containers are already up they
are reused; if the capture scripts are already running they are
killed and restarted; the `nos3-telemetry-*` indices are deleted at
the start of each launch so Kibana opens against a fresh dataset
unless the previous run was archived through `make save-run`.

**Verification.**
```
docker ps --format '{{.Names}}' | sort | grep -E 'nos3-(elastic|logstash|kibana)|nos_'
curl -fsS http://localhost:9203/_cluster/health | python3 -c 'import sys,json; print(json.load(sys.stdin)["status"])'
curl -fsS http://localhost:5604/api/status | python3 -c 'import sys,json; print(json.load(sys.stdin)["status"]["overall"]["level"])'
```
The first command must list `nos3-legacy-elasticsearch`, `nos3-legacy-logstash`,
`nos3-legacy-kibana`, and at least one `nos_*` build or runtime container.
The second must print `yellow` or `green` (a single-node ES never
goes green; yellow is the steady state). The third must print
`available`. After roughly thirty seconds, opening
`http://localhost:5604` shows the dashboards under "Analytics /
Dashboard".

## Phase 6: confirm telemetry is flowing

**Goal.** Prove that the simulator is actually publishing, that
Logstash is parsing, and that Elasticsearch has indexed at least one
document of each major type.

**Command.**
```
make doctor
```

This runs `scripts/elk_doctor.sh`, which walks the entire chain
(containers up, capture scripts attached, log files non-empty,
Logstash pipeline healthy, Elasticsearch indices present and
non-empty, Kibana data view present). It is the diagnostic of
record when Kibana shows no data after `make launch`.

**Verification.**
```
curl -s 'http://localhost:9203/_cat/indices/nos3-telemetry-*?v'
curl -s 'http://localhost:9203/nos3-telemetry-*/_count?q=type:software_bus' | python3 -c 'import sys,json; print(json.load(sys.stdin)["count"])'
curl -s 'http://localhost:9203/nos3-telemetry-*/_count?q=type:system_log'    | python3 -c 'import sys,json; print(json.load(sys.stdin)["count"])'
```
The index listing must show at least one `nos3-telemetry-YYYY.MM.DD`
index in `yellow` or `green` health. The two counts must both be
greater than zero within roughly a minute of launch. If either is
zero, run `make doctor` again; if `software_bus` is zero in
particular, the most likely cause is that `god_view_capture.py` is
not running or that its TO_LAB UDP subscription has been broken by a
config change to `nos3/components/blackboard/fsw/cfs/configs/to_lab_sub.c`.

## Phase 7: archive a run

**Goal.** Snapshot a complete run (every Software Bus message,
every simulator log, the dynamics engine state, the metadata header)
so that it can be replayed into Kibana later without re-launching
the sim.

**Command.**
```
make save-run RUN_LABEL=baseline_clean
make stop
```

This copies `attack_logs/cfs_god_view.json` and `omni_logs/*.log`
into `$HOME/nos3_runs/baseline_clean/`, writes a metadata header
(label, save timestamp, git commit), and stops the sim while
preserving the `nos3-telemetry-*` indices so this run remains
inspectable in Kibana.

**Verification.**
```
test -f $HOME/nos3_runs/baseline_clean/cfs_god_view.json
test -f $HOME/nos3_runs/baseline_clean/metadata.txt
docker ps --format '{{.Names}}' | grep -E '^(nos_|cosmos-)' | wc -l
```
The first two files must exist. The third command must print `0`
(the sim has stopped). The ELK containers remain running on
purpose, so Kibana still serves the archived run on
`http://localhost:5604`.

To replay later, on a different machine or after a `make stop`, run
`make load-run RUN=baseline_clean`; the equivalent verification is
the same `_count` queries as Phase 6.

## What "reproducible" actually means here

The seven phases above produce an outcome that is deterministic in
two senses. First, the sim is time-locked to the NOS Engine ticker
rather than to the host wall clock, so two runs with the same start
time and the same orbit produce the same FSW behaviour. Second, the
ELK index pattern uses a deterministic daily-rolled name
(`nos3-telemetry-YYYY.MM.DD`), and `make stop` deletes those indices
by default; this means consecutive runs do not contaminate each
other's Kibana view unless `KEEP_TLM=1` is set (which `save-run`
does explicitly).

What is not reproducible across machines is the per-tick wall-clock
latency: a slower disk or a slower CPU produces the same telemetry
content but with different timestamps in the system-log stream. The
Software Bus stream is unaffected because its timestamps are
derived from the simulated mission elapsed time, not from
`gettimeofday`. This distinction matters when comparing attack
runs across hosts; see
[05-elk/00-pipeline.md](05-elk/00-pipeline.md) for the precise field
semantics.
