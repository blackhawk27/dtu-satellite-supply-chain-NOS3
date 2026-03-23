# cFS Resource Limits and CPU Boundary Analysis

This document records the resource constraints, process watchdog behaviour, CPU scheduling boundaries, and telemetry rate caps that govern the NOS3 build in this repository. It also describes the investigative methodology used to derive these findings from the source tree.

---

## Table of Contents

1. [Investigative Methodology](#investigative-methodology)
2. [Software Bus Message Limits](#software-bus-message-limits)
3. [Process Management and App Dropping](#process-management-and-app-dropping)
4. [CPU Scheduling and Priority Hierarchy](#cpu-scheduling-and-priority-hierarchy)
5. [Scheduler Timing Boundaries](#scheduler-timing-boundaries)
6. [EVS Event Rate Limiting](#evs-event-rate-limiting)
7. [Memory Resource Limits](#memory-resource-limits)
8. [Summary and Monitoring Implications](#summary-and-monitoring-implications)

---

## Investigative Methodology

cFS distributes its configuration across a hierarchy of header files. The layering is:

| Layer | Location | Description |
|---|---|---|
| Mission defaults | `nos3/fsw/cfe/modules/*/config/default_cfe_*_cfg.h` | Shipped with cFE; used if no override exists |
| Platform overrides | `nos3/cfg/nos3_defs/cpu1_platform_cfg.h` | Mission-specific overrides; take precedence over defaults |
| App-level overrides | `nos3/fsw/apps/<app>/fsw/platform_inc/<app>_platform_cfg.h` | Per-application tightening of pipe depths and table sizes |

The analysis used three search passes:

1. **System-wide constant search** — broad grep across `nos3/cfg/nos3_defs/` for all `CFE_PLATFORM_SB_*`, `CFE_PLATFORM_ES_*`, and `CFE_PLATFORM_EVS_*` constants to capture the platform override values.
2. **Per-app pipe depth search** — grep for `PIPE_DEPTH` across all `platform_cfg.h` files under `nos3/fsw/apps/` to map how each application sizes its receive queue.
3. **OS-level constraint search** — examination of `nos3/cfg/build/launch.sh` and `nos3/cfg/nos3_defs/cpu1_cfe_es_startup.scr` for Docker `--sysctl`, `--ulimit`, and `--cap-add` flags, and for the per-app exception action field.

A dedicated search was also made for the Health and Safety (`hs`) application using patterns `HS_CPU_UTILIZATION_MAX`, `HS_MAX_MONITORED_APPS`, and `HS_AppMonTable`. All returned empty. The binary is also absent from the startup script. This absence is itself a significant finding documented below.

---

## Software Bus Message Limits

All constants from `nos3/cfg/nos3_defs/cpu1_platform_cfg.h`.

| Constant | Value | Meaning |
|---|---|---|
| `CFE_PLATFORM_SB_MAX_MSG_IDS` | 256 | Unique MIDs in routing table |
| `CFE_PLATFORM_SB_MAX_PIPES` | 64 | Total pipes across all apps |
| `CFE_PLATFORM_SB_MAX_DEST_PER_PKT` | 16 | Max subscribers to one MID |
| `CFE_PLATFORM_SB_DEFAULT_MSG_LIMIT` | **4** | Max queued copies per subscription |
| `CFE_PLATFORM_SB_BUF_MEMORY_BYTES` | 524 288 B (512 KB) | Total SB message buffer pool |
| `CFE_PLATFORM_SB_HIGHEST_VALID_MSGID` | 0x1FFF | Highest accepted MID |

### Pipe overflow behaviour

`DEFAULT_MSG_LIMIT = 4` is the tightest operational constraint. When an application does not drain its pipe fast enough, the SB drops the incoming message and fires event `CFE_SB_Q_FULL_ERR_EID` (Event ID 25). The event filter mask for this event is `CFE_EVS_FIRST_16_STOP`, meaning **only the first 16 overflow events are logged**. After that threshold, pipe overflows are completely silent — they will not appear in EVS or in Kibana. Under CPU stress, this is the primary mechanism by which telemetry is lost without any visible error.

The maximum number of subscription slots is bounded implicitly:

```
256 MIDs × 16 destinations = 4 096 total subscription entries
```

No separate `CFE_PLATFORM_SB_MAX_SUBSCRIPTIONS` constant exists in this build.

### Per-application pipe depths

| Application | Source file | Depth |
|---|---|---|
| SCH | `sch/fsw/platform_inc/sch_platform_cfg.h` | 12 |
| SC | `sc/fsw/platform_inc/sc_platform_cfg.h` | 12 |
| LC | `lc/fsw/inc/lc_platform_cfg.h` | 12 |
| FM | `fm/fsw/inc/fm_platform_cfg.h` | 10 |
| CI | `ci/fsw/examples/*/ci_platform_cfg.h` | 10 |
| TO | `to/fsw/examples/*/to_platform_cfg.h` | 10–32 |
| DS | `ds/fsw/inc/ds_platform_cfg.h` | **45** |
| CF | `cf/fsw/inc/cf_platform_cfg.h` | 32 |
| SBN | `sbn/fsw/platform_inc/sbn_platform_cfg.h` | 32 |

DS has the deepest pipe (45) because it must absorb bursts of science and housekeeping telemetry without dropping records destined for downlink.

---

## Process Management and App Dropping

All constants from `nos3/cfg/nos3_defs/cpu1_platform_cfg.h`.

```c
/* Milliseconds between ES health scans */
#define CFE_PLATFORM_ES_APP_SCAN_RATE      1000

/* Scan cycles before forceful kill of a non-responsive app */
#define CFE_PLATFORM_ES_APP_KILL_TIMEOUT   5

/* Maximum concurrently loaded applications */
#define CFE_PLATFORM_ES_MAX_APPLICATIONS   32
```

With a 1-second scan rate and a 5-cycle kill timeout, an application that stops calling `CFE_ES_RunLoop()` will be terminated after **5 seconds**.

### Exception action

The last field on each line of `nos3/cfg/nos3_defs/cpu1_cfe_es_startup.scr` controls what ES does when an app crashes:

```
CFE_APP, noisy_app, NOISY_APP_Main, NOISY_APP, 160, 16384, 0x0, 0;
#                                                                 ^
#                             0 = restart the app on exception
#                             Non-zero = full processor reset
```

Every application in this build uses `0`, so a crashing app is silently restarted rather than triggering a processor reset.

### Absence of the Health and Safety application

The cFS `hs` application provides automated CPU utilisation monitoring, per-app execution count watchdogs, and configurable response actions. A thorough search of the entire source tree found no `hs` binary, no `HS_CPU_UTILIZATION_MAX`, no `HS_AppMonTable`, and no `hs` entry in the startup script.

**Consequence:** this build has no automated CPU watchdog at the flight software level. There is no mechanism that will detect a runaway application consuming excessive CPU time. The only CPU enforcement is the Linux scheduler operating through the POSIX real-time priority hierarchy set at Docker launch time.

---

## CPU Scheduling and Priority Hierarchy

The FSW Docker container is launched with the following OS-level parameters (`nos3/cfg/build/launch.sh`, line 109):

```bash
docker run ... \
  --sysctl fs.mqueue.msg_max=10000 \
  --ulimit rtprio=99               \
  --cap-add=sys_nice
```

| Flag | Value | Effect |
|---|---|---|
| `fs.mqueue.msg_max` | 10 000 | POSIX MQ kernel limit — far above the 512 KB SB pool, so the pool is always the binding constraint |
| `--ulimit rtprio=99` | 99 | Grants the full Linux real-time priority range (1–99) |
| `--cap-add=sys_nice` | — | Permits `sched_setscheduler()` inside the container; OSAL uses this when creating tasks |

### Task priority hierarchy

Lower numeric priority = higher scheduling precedence.

| Task | Constant | Priority |
|---|---|---|
| TIME Tone / 1Hz | `CFE_PLATFORM_TIME_TONE_TASK_PRIORITY` | **25** |
| EVS | `CFE_PLATFORM_EVS_START_TASK_PRIORITY` | 61 |
| SB | `CFE_PLATFORM_SB_START_TASK_PRIORITY` | 64 |
| ES | `CFE_PLATFORM_ES_START_TASK_PRIORITY` | 68 |
| TBL | `CFE_PLATFORM_TBL_START_TASK_PRIORITY` | 70 |
| SCH (startup script) | — | 40 |
| CI (startup script) | — | 41 |
| TO (startup script) | — | 42 |
| CI_LAB / TO_LAB | — | 80 / 81 |
| BEACON_APP | — | 82 |
| NOISY_APP | — | 160 |

SCH, CI, and TO are assigned priorities 40–42, higher than all cFE core services except the TIME task. Mission payload apps (NOISY_APP at 160) are placed well below all infrastructure, so they cannot starve the command and telemetry paths under a CPU spike.

No `CFE_PLATFORM_ES_MAX_PRIORITY` constant exists in this build. Priorities are passed directly to `OS_TaskCreate()` in OSAL, which maps them to Linux real-time priorities within the `rtprio=99` ulimit.

---

## Scheduler Timing Boundaries

From `nos3/fsw/apps/sch/fsw/platform_inc/sch_platform_cfg.h`:

```c
#define SCH_TOTAL_SLOTS            100     /* minor frames per major frame — 100 Hz */
#define SCH_MICROS_PER_MAJOR_FRAME 1000000 /* 1-second major frame */
#define SCH_ENTRIES_PER_SLOT       5       /* activities per 10 ms slot */
#define SCH_MAX_LAG_COUNT          50      /* slots before catch-up abandoned */
#define SCH_MAX_SLOTS_PER_WAKEUP   5       /* catch-up burst size */
```

The system runs at **100 Hz** — each major frame is 1 second divided into 100 minor frames of 10 ms. If the scheduler accumulates more than `SCH_MAX_LAG_COUNT = 50` unprocessed slots (i.e. the system is more than **500 ms behind schedule**), it abandons catch-up and resumes from the current slot.

This is the scheduler's CPU overload boundary. Under sustained high load it manifests as `SCH_SAME_SLOT_EID` and `SCH_MULTI_SLOTS_EID` events in the EVS log, which propagate into the ELK pipeline and can be correlated with spikes in the `TOTAL_CPU_PCT` metric from `cpu_monitor.sh`.

---

## EVS Event Rate Limiting

From `nos3/cfg/nos3_defs/cpu1_platform_cfg.h`:

```c
/* Maximum events in a single burst from one application */
#define CFE_PLATFORM_EVS_MAX_APP_EVENT_BURST   32

/* Sustained event rate cap (events per second, per application) */
#define CFE_PLATFORM_EVS_APP_EVENTS_PER_SEC     8
```

An application may emit up to 32 events in a burst before EVS begins enforcing the 8 events/second sustained cap. Events beyond this threshold are silently discarded. In Kibana, EVS throttling appears as an abrupt drop in event throughput for the affected app rather than as an explicit error, making it easy to misinterpret as normal behaviour.

---

## Memory Resource Limits

From `nos3/cfg/nos3_defs/cpu1_platform_cfg.h`.

| Constant | Purpose | Size |
|---|---|---|
| `CFE_PLATFORM_SB_BUF_MEMORY_BYTES` | SB message buffer pool | 512 KB |
| `CFE_PLATFORM_TBL_BUF_MEMORY_BYTES` | Table services buffer | 512 KB |
| `CFE_PLATFORM_ES_CDS_SIZE` | Critical Data Store | 128 KB |
| `CFE_PLATFORM_ES_USER_RESERVED_SIZE` | User reserved memory | 1 MB |
| `CFE_PLATFORM_ES_RESET_AREA_SIZE` | Reset area | 170 KB |
| `CFE_PLATFORM_ES_MAX_BLOCK_SIZE` | Max single ES pool block | 80 KB |
| `CFE_PLATFORM_ES_PERF_DATA_BUFFER_SIZE` | cFS performance log entries | 10 000 |

---

## Summary and Monitoring Implications

| Limit | Value | Monitoring implication |
|---|---|---|
| SB default message limit | 4 msgs/pipe | Overflows silent after 16 events; loss invisible in Kibana |
| ES app kill timeout | 5 seconds | Hung app runs undetected for up to 5 s before restart |
| SCH lag threshold | 50 slots (500 ms) | `SCH_MULTI_SLOTS_EID` events signal CPU overload |
| EVS burst cap | 32 events | Event storms throttled silently after burst |
| EVS sustained rate | 8 events/sec | Throttling hides anomalous app behaviour in Kibana |
| No HS app | — | No automated CPU watchdog; `cpu_monitor.sh` is the only observer |
| Real-time priority range | 1–99 | Runaway app cannot starve SCH/CI/TO (priorities 40–42) |

The most consequential finding is the absence of the Health and Safety application. In a production cFS build, HS would detect an application consuming abnormally high CPU time and trigger corrective actions. Without it, the only visibility into CPU anomalies is the external `cpu_monitor.sh` script, which polls the Docker container via `docker stats` and `docker exec ps` and writes key-value log lines into the ELK pipeline.

This external instrumentation compensates for the missing HS watchdog but operates at container granularity rather than at the cFS task level. The closest available approximation to task-level attribution is the per-thread `CPU_THREAD` log entries produced by `ps -eLo` sampling inside the container. Correlating these thread names with the application names in the startup script provides thread-level CPU attribution for each cFS app.

---

*Source files examined:*
- `nos3/cfg/nos3_defs/cpu1_platform_cfg.h`
- `nos3/cfg/nos3_defs/cpu1_cfe_es_startup.scr`
- `nos3/cfg/build/launch.sh`
- `nos3/fsw/apps/sch/fsw/platform_inc/sch_platform_cfg.h`
- `nos3/fsw/apps/ds/fsw/inc/ds_platform_cfg.h`
- `nos3/fsw/apps/lc/fsw/inc/lc_platform_cfg.h`
- `nos3/fsw/apps/fm/fsw/inc/fm_platform_cfg.h`
- `nos3/fsw/apps/cf/fsw/inc/cf_platform_cfg.h`
- `nos3/fsw/apps/sbn/fsw/platform_inc/sbn_platform_cfg.h`
- `nos3/fsw/apps/sc/fsw/platform_inc/sc_platform_cfg.h`
- `nos3/fsw/apps/ci/fsw/examples/*/ci_platform_cfg.h`
- `nos3/fsw/apps/to/fsw/examples/*/to_platform_cfg.h`
