# generic_radio - DTU custom

- **Source:** `nos3/components/generic_radio/fsw/`, `nos3/components/generic_radio/sim/`
- **Loaded by:** `cfe_es_startup.scr` line 41 (priority 67, stack 32768)
- **Pipe depth:** 10
- **MIDs:** topic IDs resolved via component macro -> CMD `0x1956`, HK TLM `0x0956` (verify against build msgid table)

## Role

Radio sim + FSW app. Below `generic_tt_c` in priority; provides the physical-layer abstraction for the link.

## Silent-failure modes

- Link state changes are reported via `ttc_*` fields, not radio HK.

## Attack-surface notes

- Sim-level spoofing of received CCSDS frames is the entry path that `ci` then validates with CryptoLib. Ensure the sim's network exposure is restricted.
