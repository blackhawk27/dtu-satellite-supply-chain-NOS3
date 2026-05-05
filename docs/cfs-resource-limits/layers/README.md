# Layer-Wide cFS Resource Limits

Each page documents the limits and silent-failure modes for one cFS / OSAL / platform layer. These constraints apply to every loaded application; per-app tightening lives under `../apps/`.

| Layer | Page | What it bounds |
|---|---|---|
| Software Bus | [software-bus.md](software-bus.md) | Pipe depths, msg-IDs, subscription slots, drop behaviour |
| Executive Services | [executive-services.md](executive-services.md) | App scan rate, kill timeout, max apps, exception action |
| Event Services | [event-services.md](event-services.md) | Burst cap, sustained rate, silent throttling |
| Scheduler | [scheduler.md](scheduler.md) | Slot count, lag threshold, catch-up policy |
| Memory | [memory.md](memory.md) | SB / TBL / CDS / reset-area / pool sizes |
| CPU & Process | [cpu-and-process.md](cpu-and-process.md) | rtprio, sys_nice, priority hierarchy, HS/cpu_monitor split |

All values are pulled from `nos3/cfg/nos3_defs/cpu1_platform_cfg.h`, `nos3/fsw/build/exe/cpu1/cf/cfe_es_startup.scr`, and per-app `*_platform_cfg.h` / `*_internal_cfg.h` headers. Methodology is documented at [../README.md](../README.md).
