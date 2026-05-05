# beacon_app - DTU custom

- **Source:** `nos3/fsw/apps/beacon_app/`
- **Loaded by:** `cfe_es_startup.scr` line 11 as `OFF_APP` (priority 82). **Not loaded by default.** Enabled for the GS Hello-Flow loop tests.
- **Pipe depth:** 10 (typical default; check `beacon_app_platform_cfg.h`)
- **MIDs:** CMD `0x18F0`, send-HK `0x18F1`, HK TLM `0x08F0`
- **Storage:** in-RAM counters (pingcounter, cmdcounter, errcounter)

## Role

Liveness probe and the round-trip ack source for the GS Hello-Flow. Handles `BEACON_PING_CC` from the GNSS GS responder and emits a key=value EVS line with the incremented `pingcounter`. Without `beacon_app`, the GS responder loop never closes and `ping_count` in the dashboard never advances.

## ELK fields

`pingcounter`, `cmdcounter`, `errcounter` (extracted via Logstash kv filter); also `last_ping_seq`, `last_ping_time`, `ping_count` populated in the index template (added 2026-05-03).

## Silent-failure modes

- A flat counter is the link-down signal. The dashboard alerts on `last_ping_seq` not advancing within the window.
- If the EVS line is throttled (32-event burst cap, then 8/s sustained), the counter advances but the log line is dropped; this is invisible to the responder.

## Attack-surface notes

- Small surface (one cmd code), but the round-trip ack is the canonical liveness check. Spoofing replies on the responder side or muting the EVS lines via filter-mask tampering both make the dashboard show a healthy link when it is not.
- Cross-ref [../../layers/event-services.md](../../layers/event-services.md) for the EVS muting story.
