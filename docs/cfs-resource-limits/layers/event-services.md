# Event Services (EVS) Limits

Source: `nos3/cfg/nos3_defs/cpu1_platform_cfg.h`.

```c
/* Maximum events in a single burst from one application */
#define CFE_PLATFORM_EVS_MAX_APP_EVENT_BURST   32

/* Sustained event rate cap (events per second, per application) */
#define CFE_PLATFORM_EVS_APP_EVENTS_PER_SEC     8
```

An application may emit up to 32 events in a burst before EVS begins enforcing the 8 events/second sustained cap. Events beyond this threshold are silently discarded.

## Silent throttling

In Kibana, EVS throttling appears as an abrupt drop in event throughput for the affected app rather than as an explicit error, making it easy to misinterpret as normal behaviour.

The freshness tile row added on 2026-05-03 surfaces this indirectly: an app whose `last_seen_ts` is older than `now - 30s` is either dead, hung, or being throttled. Consult `docs/thesis/05-elk/03-kibana-dashboards.md` for the dashboard layout.

## Per-app filter masks

Many event IDs are filtered with `CFE_EVS_FIRST_N_STOP` (e.g. `CFE_SB_Q_FULL_ERR_EID` is `FIRST_16_STOP`). After the configured count, the event is dropped permanently for that app until cFS restarts. Per-app filter tables live in each app's `*_events.h`.

## Attack surface

- **EVS muting:** triggering 32 throttle-eligible events in a burst silences subsequent events from that app for the rest of the second. Combined with `FIRST_N_STOP` masks, an attacker can permanently suppress specific error events by deliberately inducing them at startup.
- **Storm masking:** an attacker generating events from a separate app cannot directly mute another app's stream (the rate cap is per-app), but can saturate the global EVS pipe and indirectly cause the SB pipe overflow described in [software-bus.md](software-bus.md).
