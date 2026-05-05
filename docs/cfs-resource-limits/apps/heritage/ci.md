# ci (Command Ingest)

- **Source:** `nos3/fsw/apps/ci/`
- **Loaded by:** `cfe_es_startup.scr` line 6 (priority 41, stack 16384)
- **Pipe depth:** 10 (per `ci_platform_cfg.h`)
- **MIDs:** CMD `0x18B0`, HK TLM `0x08B0`
- **Telemetry rate:** HK only (1 Hz via SCH)
- **Storage:** none

## Role

Receives uplinked CCSDS frames from the ground (via UDP into the FSW container), verifies SDLS authentication via CryptoLib, and republishes the contained command on the SB.

## Silent-failure modes

- Auth-fail counts increment in HK but no per-frame event is emitted by default.
- Pipe overflow on the CCSDS-in side is silent after 16 events (see [../../layers/software-bus.md](../../layers/software-bus.md)).

## Attack-surface notes

- Primary command-injection surface. Any flaw in CryptoLib's frame validation or in the SDLS key state allows arbitrary MIDs to be injected.
- Replay attacks: defended by SDLS sequence numbers; verify `crypto_config.json` ARSN configuration.
- See [../../../missions/NOS3-DTU-EO1-mission-implementation.md](../../../missions/NOS3-DTU-EO1-mission-implementation.md) IPC port map for the host-side socket.
