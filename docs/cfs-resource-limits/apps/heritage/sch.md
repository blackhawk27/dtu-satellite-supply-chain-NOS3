# sch (Scheduler)

- **Source:** `nos3/fsw/apps/sch/`
- **Loaded by:** `cfe_es_startup.scr` line 5 (priority 40, stack 16384)
- **Pipe depth:** 12 (`SCH_PIPE_DEPTH` in `sch_platform_cfg.h`)
- **MIDs:** CMD `0x1895`, HK TLM `0x0897`, send-HK `0x1896`
- **Telemetry rate:** 100 Hz tick drives all wakeups; HK at 1 Hz
- **Tables:** SDT (Schedule Definition) + MDT (Message Definition) under `nos3/cfg/nos3_defs/tables/`
- **Storage:** none beyond TBL buffer
- **Crypto / link:** none

## Why it matters for security testing

SCH is the timing root. Every periodic activity (HK request, NAV poll, ADCS sample) is dispatched from one of `SCH_TOTAL_SLOTS = 100` minor frames. Tampering with the SDT redirects wakeups; tampering with the MDT changes which MID is published.

## Silent-failure modes

- Lag past 50 slots (500 ms) abandons catch-up; missed wakeups are *not* re-fired.
- Five activities per slot is a hard cap; a sixth fails table load.
- See [../../layers/scheduler.md](../../layers/scheduler.md) for the full lag/burst story.

## Attack-surface notes

- An SDT entry pointing at an attacker-controlled MID makes that MID part of the 100 Hz stream.
- An attacker who can pause SCH for >5 s induces ES kill-and-restart of every app that subscribes to its wakeup MIDs (cascade restart).
