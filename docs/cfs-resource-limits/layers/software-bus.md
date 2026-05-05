# Software Bus (SB) Limits

Source: `nos3/cfg/nos3_defs/cpu1_platform_cfg.h`.

| Constant | Value | Meaning |
|---|---|---|
| `CFE_PLATFORM_SB_MAX_MSG_IDS` | 256 | Unique MIDs in the routing table |
| `CFE_PLATFORM_SB_MAX_PIPES` | 64 | Total pipes across all apps |
| `CFE_PLATFORM_SB_MAX_DEST_PER_PKT` | 16 | Max subscribers to one MID |
| `CFE_PLATFORM_SB_DEFAULT_MSG_LIMIT` | **4** | Max queued copies per subscription |
| `CFE_PLATFORM_SB_BUF_MEMORY_BYTES` | 524 288 B (512 KB) | Total SB message buffer pool |
| `CFE_PLATFORM_SB_HIGHEST_VALID_MSGID` | 0x1FFF | Highest accepted MID |

## Pipe overflow behaviour

`DEFAULT_MSG_LIMIT = 4` is the tightest operational constraint. When an application does not drain its pipe fast enough, the SB drops the incoming message and fires event `CFE_SB_Q_FULL_ERR_EID` (Event ID 25). The event filter mask for this event is `CFE_EVS_FIRST_16_STOP`, meaning **only the first 16 overflow events are logged**. After that threshold, pipe overflows are completely silent: they will not appear in EVS or in Kibana.

Under CPU stress, this is the primary mechanism by which telemetry is lost without any visible error.

## Implicit subscription bound

```
256 MIDs x 16 destinations = 4 096 total subscription entries
```

No separate `CFE_PLATFORM_SB_MAX_SUBSCRIPTIONS` constant exists in this build.

## Per-application pipe depths

Detailed per-app values live under `../apps/`. Headlines:

| Application | Source file | Depth |
|---|---|---|
| SCH | `sch/fsw/platform_inc/sch_platform_cfg.h` | 12 |
| SC | `sc/fsw/inc/sc_internal_cfg.h` | 12 |
| LC | `lc/fsw/inc/lc_internal_cfg.h` | 12 |
| FM | `fm/fsw/inc/fm_internal_cfg.h` | 10 |
| HK | `hk/fsw/inc/hk_internal_cfg.h` | 40 |
| HS | `hs/fsw/inc/hs_internal_cfg.h` | 12 cmd / 32 evt / 1 wakeup |
| MD | `md/fsw/inc/md_internal_cfg.h` | 50 |
| DS | `ds/fsw/inc/ds_internal_cfg.h` | **45** |
| CF | `cf/fsw/inc/cf_platform_cfg.h` | 32 |
| SBN | `sbn/fsw/platform_inc/sbn_platform_cfg.h` | 32 (peer) / 32 (sub) |
| CI / TO | `*/fsw/examples/*/${name}_platform_cfg.h` | 10 - 32 |

DS has the deepest pipe (45) because it must absorb bursts of science and housekeeping telemetry without dropping records destined for downlink.

## Attack surface

- **Subscriber starvation:** an attacker who can flood a high-rate MID can exhaust any subscriber's pipe depth before it drains, and after 16 silent overflow events on that subscription the loss is invisible to the operator.
- **MID-space saturation:** at 256 MIDs total, adding noisy custom apps without budgeting is enough to push the system above the limit; the failure mode is `CFE_SB_NO_SUBSCRIBERS` errors at subscribe time.
- **Wide subscriptions:** the 16-destination cap on a single MID means a "broadcast" MID can be muted for late subscribers; useful to trace if telemetry suddenly disappears for one app while the publisher is healthy.
