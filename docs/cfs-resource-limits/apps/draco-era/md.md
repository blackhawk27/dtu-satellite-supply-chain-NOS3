# md (Memory Dwell) - Draco-era

- **Source:** `nos3/fsw/apps/md/`
- **Loaded by:** `cfe_es_startup.scr` line 23 (priority 60, stack 16384)
- **Pipe depth:** 50 (`DEFAULT_MD_INTERNAL_PIPE_DEPTH`)
- **MIDs (shim):** CMD `0x1890`, HK TLM `0x0890`, dwell TLM range
- **Tables:** four dwell tables (one per dwell stream) under `nos3/cfg/nos3_defs/tables/`

## Draco-era integration note

Same shim pattern as CS; see [../../ADDING_CFS_APPS.md](../../ADDING_CFS_APPS.md).

## Role

Periodically samples arbitrary memory locations and emits the values as telemetry. Used for engineering diagnostics and post-launch debugging.

## Silent-failure modes

- A dwell entry pointing at unmapped memory is silently skipped.
- Sample rate is per-table; a high-rate dwell on a busy region adds steady SB load that other apps must drain.

## Attack-surface notes

- Reads arbitrary memory by ground command. With auth bypass, this becomes an arbitrary-read primitive over the downlink.
- Combined with `mm` (Memory Manager), `md` provides the read half of memory access; `mm` provides the write half.
