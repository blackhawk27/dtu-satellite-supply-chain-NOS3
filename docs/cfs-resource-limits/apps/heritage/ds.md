# ds (Data Storage)

- **Source:** `nos3/fsw/apps/ds/`
- **Loaded by:** `cfe_es_startup.scr` line 19 (priority 51, stack 32768)
- **Pipe depth:** **45** (`DEFAULT_DS_INTERNAL_APP_PIPE_DEPTH` in `ds_internal_cfg.h`) - largest of all loaded apps
- **MIDs:** CMD `0x18BB`, HK TLM `0x08B8`
- **Tables:** filter table + destination file table under `nos3/cfg/nos3_defs/tables/`

## Role

Subscribes to MIDs and writes them as on-disk records, partitioned by destination file. Used for science telemetry archiving and post-flight downlink.

## Silent-failure modes

- File-full conditions roll over to the next file; old records are *not* deleted until quota cleanup.
- A destination file that does not exist yet is auto-created on first write; permission failures are reported in HK but not as discrete events per packet.
- Pipe depth 45 is large but not infinite; sustained 100 Hz on multiple MIDs can still overflow.

## Attack-surface notes

- Filter-table tampering decides which MIDs reach disk; selectively dropping packets from an attack window from disk hides forensic evidence.
- An attacker who can fill the data partition forces rollover and wraps over older records.
