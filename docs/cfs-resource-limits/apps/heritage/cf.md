# cf (CFDP)

- **Source:** `nos3/fsw/apps/cf/`
- **Loaded by:** `cfe_es_startup.scr` line 18 (priority 50, stack 32768)
- **Pipe depth:** 32 (`cf_platform_cfg.h`)
- **MIDs:** CMD `0x18B3`, HK TLM `0x08B0`, EOT TLM `0x08B3`, send-HK `0x18B4`
- **Storage:** uses DS-managed file paths; CFDP transactions are journalled per `cf_def_config.c`

## Role

CCSDS File Delivery Protocol implementation. Sends and receives whole files between spacecraft and ground.

## Silent-failure modes

- CFDP transactions can stall waiting on EOFs; HK shows the live count but no per-transaction failure event is emitted unless explicitly enabled.
- Disk-full inside the container is reported by `ds`, not `cf`.

## Attack-surface notes

- File ingress is a primary path for table loads (`mm`/`tbl`) and for executable replacement; a CFDP transaction with a forged authentication context can deliver an attacker-controlled binary.
- Outbound: large file exfiltration is observable as sustained `cf` HK throughput.
