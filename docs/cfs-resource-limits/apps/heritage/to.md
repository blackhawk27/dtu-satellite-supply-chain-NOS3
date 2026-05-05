# to (Telemetry Output)

- **Source:** `nos3/fsw/apps/to/`
- **Loaded by:** `cfe_es_startup.scr` line 7 (priority 42, stack 32768)
- **Pipe depth:** 10 to 32 (per `to_platform_cfg.h`; varies by example variant)
- **MIDs:** CMD `0x1880`, HK TLM `0x0880`
- **Telemetry rate:** drains every subscribed MID; rate bounded by SB and ground link
- **Storage:** none

## Role

Egresses telemetry from SB to the ground link. Wraps each downlink frame in CCSDS, applies SDLS auth/encryption via CryptoLib, and forwards to the radio sim or to a UDP socket on the host.

## Silent-failure modes

- A MID that is published to SB but not subscribed by `to` simply does not downlink; no error is raised.
- Subscriber-pipe overflow (e.g. when CPU lag prevents `to` from draining) silently drops messages after 16 events.
- The on-2026-05-03 mirror-socket addition (see `nos3/scripts/god_view_capture.py` `NOS3_TLM_MIRROR_DEST`) was added to side-step COSMOS netns isolation; it is not visible to `to` itself.

## Attack-surface notes

- Subscription table (per-app filter) decides which MIDs reach the ground; tampering enables exfiltration of MIDs the operator cannot see.
- Bandwidth saturation: `to` does not implement priority-based shedding, so a high-rate attacker MID can crowd out genuine HK.
