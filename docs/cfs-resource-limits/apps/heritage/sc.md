# sc (Stored Command)

- **Source:** `nos3/fsw/apps/sc/`
- **Loaded by:** `cfe_es_startup.scr` line 22 (priority 54, stack 32768)
- **Pipe depth:** 12 (`DEFAULT_SC_INTERNAL_PIPE_DEPTH`)
- **MIDs:** CMD `0x18A9`, HK TLM `0x08AA`
- **Tables:** ATS (absolute time), RTS (relative time) tables under `nos3/cfg/nos3_defs/tables/`

## Role

Dispatches pre-loaded command sequences on time-tagged or trigger-based schedules. Most autonomous mission triggers (LC actionpoints, MGR mode transitions) execute through SC's RTS machinery.

## Silent-failure modes

- A disabled RTS line is silently no-op; HK only counts dispatched lines.
- An RTS pointing at a non-existent MID emits an SB error but the RTS continues.

## Attack-surface notes

- ATS/RTS tampering rewrites autonomous behaviour. The most consequential target on the spacecraft after `mm`.
- The recently-added FOV-exit recovery RTS (commit `8af2fdea`) is a load-bearing autonomous response; verifying its integrity is part of the security baseline.
