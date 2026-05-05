# CPU Scheduling and Process Limits

The FSW Docker container is launched with the following OS-level parameters (`nos3/cfg/build/launch.sh` line ~109):

```bash
docker run ... \
  --sysctl fs.mqueue.msg_max=10000 \
  --ulimit rtprio=99               \
  --cap-add=sys_nice
```

| Flag | Value | Effect |
|---|---|---|
| `fs.mqueue.msg_max` | 10 000 | POSIX MQ kernel limit; far above the 512 KB SB pool, so the pool is always the binding constraint |
| `--ulimit rtprio=99` | 99 | Grants the full Linux real-time priority range (1 - 99) |
| `--cap-add=sys_nice` | - | Permits `sched_setscheduler()` inside the container; OSAL uses this when creating tasks |

## Task priority hierarchy

Lower numeric priority = higher scheduling precedence.

| Task | Constant | Priority |
|---|---|---|
| TIME Tone / 1 Hz | `CFE_PLATFORM_TIME_TONE_TASK_PRIORITY` | **25** |
| EVS | `CFE_PLATFORM_EVS_START_TASK_PRIORITY` | 61 |
| SB | `CFE_PLATFORM_SB_START_TASK_PRIORITY` | 64 |
| ES | `CFE_PLATFORM_ES_START_TASK_PRIORITY` | 68 |
| TBL | `CFE_PLATFORM_TBL_START_TASK_PRIORITY` | 70 |
| SCH (startup script) | - | 40 |
| CI (startup script) | - | 41 |
| TO (startup script) | - | 42 |
| CF | - | 50 |
| DS | - | 51 |
| FM | - | 52 |
| LC | - | 53 |
| SC | - | 54 |
| CS | - | 55 |
| MD / generic_adcs | - | 60 |
| MM | - | 61 |
| SBN | - | 63 |
| generic_eps | - | 63 |
| MGR | - | 75 |
| generic_tt_c | - | 76 |
| generic_gnss | - | 77 |
| HS | - | 85 |
| HK | - | 90 |
| CI_LAB / TO_LAB | - | 80 / 81 |
| BEACON_APP (OFF) | - | 82 |
| NOISY_APP (OFF) | - | 20 |

Source: `nos3/fsw/build/exe/cpu1/cf/cfe_es_startup.scr`.

SCH, CI, and TO are assigned priorities 40 - 42, higher than all cFE core services except the TIME task. Mission payload apps are placed below the infrastructure tier so they cannot starve the command and telemetry paths under a CPU spike.

No `CFE_PLATFORM_ES_MAX_PRIORITY` constant exists. Priorities are passed directly to `OS_TaskCreate()` in OSAL, which maps them to Linux real-time priorities within the `rtprio=99` ulimit.

## CPU watchdog status

This build *does* load the cFS Health & Safety (`hs`) app at priority 85; see [../apps/heritage/hs.md](../apps/heritage/hs.md). The `hs` app handles per-app execution-count watchdogs and configurable response actions.

For container-level CPU monitoring, the external `cpu_monitor.sh` script polls `docker stats` and `docker exec ps -eLo` and writes key-value log lines (`TOTAL_CPU_PCT`, `CPU_PCT`, `NUM_PIDS`, etc.) into the ELK pipeline. The two views are complementary: `hs` watches per-cFS-task execution counts inside the container, `cpu_monitor.sh` watches container-level CPU utilisation from outside.

## Attack surface

- **Priority inversion:** the priority hierarchy assumes well-behaved apps. An attacker who can re-prioritise an app (via `OS_TaskSetPriority` or by editing the startup script) can park a malicious task at priority 40 and starve the operational tier.
- **rtprio 99 escape:** with `--ulimit rtprio=99` and `--cap-add=sys_nice`, any compromised app inside the FSW container has the kernel privilege needed to set itself to RT priority 99, above all cFS tasks.
- **HS bypass:** the HS watchdog tables (`hs_def_amt.c`, `hs_def_emt.c`, etc.) decide which app missing wake-ups triggers a response. Tampering with these tables disables the watchdog without any other observable effect.
