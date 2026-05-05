# ci_lab (Command Ingest Lab)

- **Source:** `nos3/fsw/apps/ci_lab/`
- **Loaded by:** `cfe_es_startup.scr` line 9 (priority 80, stack 16384)
- **Pipe depth:** 10
- **MIDs:** CMD `0x18E0`, HK TLM `0x08E0`, send-HK `0x18E1`
- **Telemetry rate:** HK only
- **Storage:** none

## Role

Reference command-ingest implementation. Listens on UDP port 1234 (default) for raw CCSDS-formatted commands and republishes on SB. Used for development and the COSMOS / OpenC3 lab integrations.

## Silent-failure modes

- No SDLS / CryptoLib path; an attacker reachable on the UDP port can publish any MID.
- Malformed packets are dropped silently with a counter increment.

## Attack-surface notes

- **High-risk** if the UDP port is reachable from outside the container. The COSMOS netns isolation (see `docs/missions/NOS3-DTU-EO1-mission-implementation.md`) keeps it container-internal in normal use, but any port-mapping that exposes it bypasses the production CryptoLib path entirely.
- Combined with the `2026-05-03` `CmdTlmServerHeadless` change, `ci_lab` is the canonical command path during the GS Hello-Flow validation.
