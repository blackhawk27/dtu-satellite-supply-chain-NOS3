# cs (Checksum) - Draco-era

- **Source:** `nos3/fsw/apps/cs/`
- **Loaded by:** `cfe_es_startup.scr` line 15 (priority 55, stack 16384)
- **Pipe depth:** 12 (typical Draco default; check `cs_internal_cfg.h`)
- **MIDs (hardcoded shim):** CMD `0x189F`, send-HK `0x18A0`, HK TLM `0x08A4`
- **Tables:** memory / app / table / EEPROM definitions under `nos3/cfg/nos3_defs/tables/`

## Draco-era integration note

CS uses the EDS topic-ID system (`CFE_PLATFORM_CMD_TOPICID_TO_MIDV()`). NOS3's older cFE does not implement this macro, so CS ships in this repo with a hand-written `cs_msgids.h` that hardcodes the resolved values. See [../../ADDING_CFS_APPS.md](../../ADDING_CFS_APPS.md).

## Role

Computes running CRCs over registered memory regions, app text segments, tables, and EEPROM. Reports mismatches as events; can trigger configurable response actions.

## Silent-failure modes

- Disabled regions are silent (no event); an attacker who can issue `CS_DISABLE_*_CC` muffles the integrity check.
- A modified region whose new CRC has been re-baselined via `CS_RECOMPUTE_*_CC` is also silent.

## Attack-surface notes

- The integrity-check hardpoint of the build. Disabling or recomputing the baseline is a quiet bypass.
- Region table tampering moves the watched range; a region pointing at zero-padded memory always passes.
