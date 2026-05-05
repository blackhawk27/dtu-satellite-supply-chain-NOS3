# Scheduler (SCH) Timing Boundaries

Source: `nos3/fsw/apps/sch/fsw/platform_inc/sch_platform_cfg.h`.

```c
#define SCH_TOTAL_SLOTS            100     /* minor frames per major frame - 100 Hz */
#define SCH_MICROS_PER_MAJOR_FRAME 1000000 /* 1-second major frame */
#define SCH_ENTRIES_PER_SLOT       5       /* activities per 10 ms slot */
#define SCH_MAX_LAG_COUNT          50      /* slots before catch-up abandoned */
#define SCH_MAX_SLOTS_PER_WAKEUP   5       /* catch-up burst size */
```

The system runs at **100 Hz**: each major frame is 1 second divided into 100 minor frames of 10 ms. If the scheduler accumulates more than `SCH_MAX_LAG_COUNT = 50` unprocessed slots (i.e. the system is more than **500 ms behind schedule**), it abandons catch-up and resumes from the current slot.

This is the scheduler's CPU overload boundary. Under sustained high load it manifests as `SCH_SAME_SLOT_EID` and `SCH_MULTI_SLOTS_EID` events in the EVS log, which propagate into the ELK pipeline and can be correlated with spikes in the `TOTAL_CPU_PCT` metric from `cpu_monitor.sh`.

## Activities per slot

`SCH_ENTRIES_PER_SLOT = 5` caps each 10 ms slot at five distinct activities (typically each emits a single MID into SB). Adding a sixth wakeup to an already-full slot fails at table-load time; the schedule definition table (SDT) and message definition table (MDT) live under `nos3/cfg/nos3_defs/tables/`.

## ADCS sensor wakeup additions (2026-05-03)

Commit `8af2fdea` added ADCS sensor wakeup slots. The current SDT now schedules CSS / FSS / IMU / MAG / NAV samples within budget; details in `docs/thesis/04-apps/adcs.md`.

## Attack surface

- **Slot starvation:** a CPU-bound app on slot N can push SCH beyond `MAX_LAG_COUNT`; once the catch-up budget is exhausted the dropped activities are *not* re-scheduled, so per-slot work simply does not happen for the duration of the overload.
- **Slot saturation by table edit:** an attacker who can modify the schedule definition table can fill a slot with five pointer activities to a malicious app, monopolising 1/100 of every second.
- **Sustained jitter:** under-budgeted apps cause cascading lag without ever crossing the kill timeout; only `SCH_*_EID` events expose this, and EVS rate-caps may swallow them.
