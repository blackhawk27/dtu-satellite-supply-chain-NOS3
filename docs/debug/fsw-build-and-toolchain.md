# FSW build and toolchain

All `make` commands run from `nos3/`. The build runs inside the Docker
image `ivvitc/nos3-64:20251107`; the host toolchain is irrelevant. Use
`make debug` for a shell inside that container.

## Build-time expectations (do not waste hours rebuilding)

A clean `make fsw` takes about 30-40 minutes on this devcontainer; an
incremental build takes seconds. If a build runs for more than about an
hour, stop and find out why rather than powering through. Causes seen in
practice:

- Do not `docker rm -f nos_build_fsw` between attempts. The bind-mounted
  build directory on the host survives, but destroying the container
  invalidates cmake's view and the next run treats every dependency as cold
  and rebuilds from scratch. To recover from a failed build: fix the source
  error and re-run `make fsw`; cmake recompiles only what changed.
- Do not hand-roll `docker run -i ...` to work around the `-it` TTY flag in
  non-interactive shells. Losing the TTY makes `make` fall back to serial
  install on the 9p bind mount, turning a 35-minute build into 4+ hours. Run
  `make fsw` from a VS Code integrated terminal (which has a TTY), or in a
  no-TTY shell wrap it as `script -qc 'make fsw' /dev/null` so the underlying
  `docker run` still sees a TTY.
- Watch parallelism. During a real build,
  `docker exec nos_build_fsw pgrep -c cc1` should be 4 or more (ideally
  `nproc`). If it sits at 1, make has gone serial; kill it and diagnose
  (missing TTY, invalidated cache, or a mission-install loop).
- A table-only edit (`sc_rts*.c`, `lc_def_*.c`, `sch_def_*.c`,
  `to_lab_sub.c`) should rebuild only that table object library and re-run
  `elf2cfetbl`. If cmake rebuilds all of cFE for a table-only change, the
  dependency graph is corrupted; find what invalidated it before re-running.

## Build parallelism is capped via NUM_CPUS (avoids OOM)

`nos3/scripts/env.sh` sets `NUM_CPUS="${NUM_CPUS:-$( nproc )}"`. The default
is still `nproc`, but callers can override it:

```bash
cd nos3 && NUM_CPUS=6 make fsw   # or make sim
```

This affects `fsw_build.sh`, `build_sim.sh`, and `debug.sh` (all source
`env.sh`), and Docker's `--cpus=$NUM_CPUS` flag, so the container is capped
too.

- Symptom: `make[4]: *** [... amd64_nos3_default_cpu1-install] Terminated`
  and `make: *** [Makefile:152: fsw] Error 137`, typically at about 70%
  through the mission-install tree while compiling `cs_cmds.c`. Sibling jobs
  show `Terminated`.
- Root cause: on a 7.6 GiB host with `nproc=20`, the default `-j20` ran
  twenty concurrent gcc processes (~300 MB peak each) with no `--memory`
  cap, exceeding the host budget (less the memory held by the VS Code
  server). The kernel OOM-killer SIGKILLs one gcc (exit 137) and make
  propagates SIGTERM to the siblings.
- Fix: the `NUM_CPUS` override above. Use `NUM_CPUS=6`, or 4 if ELK is
  running. During the build `docker stats` should show `nos_build_fsw` peak
  memory under about 5 GiB; artifacts land at
  `fsw/build/amd64-nos3/default_cpu1/exe/cpu1/core-cpu1` and
  `fsw/build/exe/cpu1/cf/*.so`.

## make config must precede make

Running `make` without first running `make config` fails or uses stale
`cfg/build/` artifacts. Always run `make config` after changing
`cfg/nos3-mission.xml`. The spacecraft-config override is a config-time
choice, not a build-time one:

```bash
make config SC1_CFG=spacecraft/sc-minimal-config.xml
```

## Build output locations

- CMake artifacts: `fsw/build/amd64-nos3/default_cpu1/`
- Installed executables: `fsw/build/exe/cpu1/`

## ctest must run from the build dir

```bash
cd fsw/build/amd64-nos3/default_cpu1 && ctest --output-on-failure -O ctest.log
```

Running ctest from the repo root does nothing.

## Housekeeping targets (opt-in)

- `make clean-docker` removes exited/created containers, dangling images,
  and build cache. It keeps volumes and in-use images and does not touch
  `elk_es_data_vol`. It is not part of `make clean` because it destroys
  exited containers that may still be useful for `docker logs` forensics.
- `make purge-logs` truncates `attack_logs/cfs_god_view.json` and
  `omni_logs/*.log` to zero bytes, leaving `~/nos3_runs/*` archives alone.
  Use it before `make launch` to start a run with clean logs.

## Historical compile-error fixes (Draco-era API drift)

These came up when porting Draco-baseline apps onto this fork's cFE
(6.7.99) and are kept for reference; all are already applied.

- MM app, `OS_strnlen` undefined: Draco-era OSAL API not present in the
  current cFE. Replaced with standard `strnlen` (commit 449e904).
- MD/MM apps, wrong MID macro: `TOPICID_TO_MIDV` is Draco-era. Replaced
  with hardcoded MID values matching the `nos3-mission.xml` assignments
  (commit 9309e3a).
- CS app, legacy cFE API: ported to the legacy cFE API surface
  (commit b2947fe).
- CS/MD/MM, `arch_build.cmake`: `generate_configfile_set` was removed from
  the current cmake scripts. Removed the call and used direct file
  references (commit b032276).
