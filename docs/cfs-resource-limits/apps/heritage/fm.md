# fm (File Manager)

- **Source:** `nos3/fsw/apps/fm/`
- **Loaded by:** `cfe_es_startup.scr` line 20 (priority 52, stack 32768)
- **Pipe depth:** 10 (`DEFAULT_FM_INTERNAL_APP_PIPE_DEPTH`)
- **MIDs:** CMD `0x188C`, HK TLM `0x088C`
- **Tables:** `fm_def_table.c` (free space monitoring volumes)

## Role

File-system commands: copy, move, delete, list, mkdir, rmdir, free-space queries on the cFS-mounted volumes. Operates on ground-issued commands.

## Silent-failure modes

- Errors are reported as event IDs but the calling sequence sees only a counter increment in HK.
- A delete on a CFDP transaction's working file mid-transfer is not blocked.

## Attack-surface notes

- **High-risk** by design: every FM command takes a path string from the uplink. Path traversal (`../../`) is the obvious vector if input validation is incomplete.
- Combined with `mm` (Memory Manager), `fm` is the file-side half of arbitrary-write capability.
