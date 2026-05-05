# cFS Resource Limits and CPU Boundary Analysis

This document set records the resource constraints, watchdog behaviour, CPU boundaries, and telemetry rate caps that govern the NOS3 build in this repository. Detail lives under [`layers/`](layers/) (constraints that apply to every app) and [`apps/`](apps/) (per-application surface area). Use the per-app pages as input for security-testing scope decisions.

The 2026-05-03 reorganisation split the original single-file README into:

- [`layers/`](layers/README.md) - SB, ES, EVS, SCH, memory, CPU/process limits.
- [`apps/`](apps/README.md) - one page per loaded app or component, grouped into `heritage/`, `draco-era/`, `dtu/`.
- [`ADDING_CFS_APPS.md`](ADDING_CFS_APPS.md) - integration guide for new apps + the EDS MID shim required by Draco-era apps.

## Investigative Methodology

cFS distributes its configuration across a hierarchy of header files. The layering is:

| Layer | Location | Description |
|---|---|---|
| Mission defaults | `nos3/fsw/cfe/modules/*/config/default_cfe_*_cfg.h` | Shipped with cFE; used if no override exists |
| Platform overrides | `nos3/cfg/nos3_defs/cpu1_platform_cfg.h` | Mission-specific overrides; take precedence over defaults |
| App-level overrides | `nos3/fsw/apps/<app>/fsw/platform_inc/<app>_platform_cfg.h` (or `*_internal_cfg.h`) | Per-application tightening of pipe depths and table sizes |

The audit used three search passes:

1. **System-wide constant search** - broad grep across `nos3/cfg/nos3_defs/` for all `CFE_PLATFORM_SB_*`, `CFE_PLATFORM_ES_*`, and `CFE_PLATFORM_EVS_*` constants to capture the platform override values.
2. **Per-app pipe depth search** - grep for `PIPE_DEPTH` across all `platform_cfg.h` / `internal_cfg.h` files under `nos3/fsw/apps/` to map how each application sizes its receive queue.
3. **OS-level constraint search** - examination of `nos3/cfg/build/launch.sh` and `nos3/fsw/build/exe/cpu1/cf/cfe_es_startup.scr` for Docker `--sysctl`, `--ulimit`, and `--cap-add` flags, and for the per-app exception action field.

## Loaded-app set (as of HEAD on 2026-05-03)

Source: `nos3/fsw/build/exe/cpu1/cf/cfe_es_startup.scr` (`CFE_APP` lines before the `!` EOF marker).

```
sch ci to ci_lab to_lab hs hk cs cf ds fm lc sc md mm sbn
generic_tt_c generic_gnss
generic_adcs generic_css generic_eps generic_fss generic_imu generic_mag generic_radio
generic_rw generic_torquer generic_st novatel_oem615 mgr sample
```

Off by default: `beacon_app`, `noisy_app`, `cpu_killer`. See [apps/README.md](apps/README.md) for the full matrix and per-app pages.

## Top-level findings

| Limit | Value | Where documented | Why it matters |
|---|---|---|---|
| SB default message limit | 4 msgs/pipe | [layers/software-bus.md](layers/software-bus.md) | Overflows silent after 16 events |
| ES app kill timeout | 5 seconds | [layers/executive-services.md](layers/executive-services.md) | Hung app runs undetected for up to 5 s |
| SCH lag threshold | 50 slots (500 ms) | [layers/scheduler.md](layers/scheduler.md) | `SCH_MULTI_SLOTS_EID` events signal CPU overload |
| EVS burst cap | 32 events | [layers/event-services.md](layers/event-services.md) | Event storms throttled silently after burst |
| EVS sustained rate | 8 events/sec | [layers/event-services.md](layers/event-services.md) | Throttling hides anomalous app behaviour in Kibana |
| HS app | **loaded** at priority 85 | [apps/heritage/hs.md](apps/heritage/hs.md) | Per-app exec-counter watchdog; AMT/EMT tampering disables it silently |
| Real-time priority range | 1 - 99 | [layers/cpu-and-process.md](layers/cpu-and-process.md) | Runaway app cannot starve SCH/CI/TO (priorities 40 - 42) |

## Source files examined

- `nos3/cfg/nos3_defs/cpu1_platform_cfg.h`
- `nos3/fsw/build/exe/cpu1/cf/cfe_es_startup.scr`
- `nos3/cfg/build/launch.sh`
- `nos3/fsw/apps/sch/fsw/platform_inc/sch_platform_cfg.h`
- `nos3/fsw/apps/{ds,fm,hk,hs,lc,md,sc,sbn}/fsw/inc/<app>_internal_cfg.h`
- `nos3/fsw/apps/cf/fsw/inc/cf_platform_cfg.h`
- `nos3/fsw/apps/sbn/fsw/platform_inc/sbn_platform_cfg.h`
- `nos3/fsw/apps/{ci,to}/fsw/examples/*/{ci,to}_platform_cfg.h`
- `nos3/components/*/fsw/platform_inc/*_topicids.h`
- `nos3/cfg/InOut/Inp_IPC.txt` (load-bearing OFF / tight-prefix invariants)
