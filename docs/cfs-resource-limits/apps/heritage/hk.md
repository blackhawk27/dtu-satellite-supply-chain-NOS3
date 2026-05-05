# hk (Housekeeping)

- **Source:** `nos3/fsw/apps/hk/`
- **Loaded by:** `cfe_es_startup.scr` line 14 (priority 90, stack 16384)
- **Pipe depth:** 40 (`DEFAULT_HK_INTERNAL_PIPE_DEPTH` in `hk_internal_cfg.h`)
- **MIDs:** CMD `0x189B`, HK TLM `0x089B`
- **Tables:** Copy Table (CPT) + Runtime Table (RTT) under `nos3/cfg/nos3_defs/tables/`
- **Telemetry rate:** combined HK packet at 1 Hz (one combined MID per output)

## Role

Subscribes to per-app HK packets, copies subsets of fields into combined output packets per the copy table, and republishes the combined packets on SB for `to`/`to_lab` to downlink. Reduces HK bandwidth by deduplicating frequently-updated fields.

## Silent-failure modes

- Pipe depth 40 is the largest of the heritage apps but can still overflow under burst HK; once 16 overflow events fire, further drops are silent.
- A copy-table entry that points at an unmapped source MID silently emits zero bytes for that field.

## Attack-surface notes

- Copy-table tampering is a quiet exfiltration path: an attacker can re-route fields from any HK packet into the combined output, surfacing values the operator never asked to see (or hiding values the operator expects).
- A malicious app that publishes its own MID frequently can crowd out HK's pipe and trigger silent drops on legitimate combined packets.
