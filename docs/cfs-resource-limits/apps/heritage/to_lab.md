# to_lab (Telemetry Output Lab)

- **Source:** `nos3/fsw/apps/to_lab/`
- **Loaded by:** `cfe_es_startup.scr` line 10 (priority 81, stack 32768)
- **Pipe depth:** 10
- **MIDs:** CMD `0x1880`, HK TLM uses `to_lab_msgids.h` mapping
- **Telemetry rate:** subscribes to MIDs listed in its routing table; drains at SB pace

## Role

Reference telemetry-output implementation. Writes raw CCSDS-formatted telemetry to UDP port 1235 (default) for COSMOS / OpenC3 / Yamcs ingestion. The 2026-05-03 work extended this path: `god_view_capture.py` mirrors the same UDP stream to a second destination on `NOS3_TLM_MIRROR_DEST` so the COSMOS DEBUG listener (port 5013) inside its netns receives every frame.

## Silent-failure modes

- A MID that is *not* in `to_lab`'s subscription set never reaches the ground; the publisher sees a normal SB publish.
- The mirror socket is a duplication path; if the env var is unset, COSMOS sees nothing and the GS Hello-Flow does not close. See `gnss_gs_responder_task.rb` ECHO_TIMEOUT_S behaviour.

## Attack-surface notes

- Subscription-table tampering controls what the operator sees; an attacker that drops the subscription for `0x09F8` (MGR_HK) hides mode changes from the dashboards.
- The mirror socket duplicates traffic; if it is misconfigured to a third-party host, telemetry leaks externally.
