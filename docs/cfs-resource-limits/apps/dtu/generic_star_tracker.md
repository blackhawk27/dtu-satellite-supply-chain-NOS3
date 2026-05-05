# generic_star_tracker (generic_st) - DTU custom

- **Source:** `nos3/components/generic_star_tracker/fsw/`
- **Loaded by:** `cfe_es_startup.scr` line 44 (priority 72, stack 32768)
- **Pipe depth:** 10
- **MIDs:** topic IDs -> CMD `0x196A`, HK TLM `0x096A`

## Role

Star tracker (provides quaternion attitude). Sim variant in use is `GENERIC_STAR_TRACKER_PROVIDER` (NOT the `_42_PROVIDER` variant).

## Load-bearing invariant

- IPC port 4282 in `nos3/cfg/InOut/Inp_IPC.txt` **must remain `OFF`**. The active sim XML uses `GENERIC_STAR_TRACKER_PROVIDER`, so nothing connects to fortytwo:4282; if the entry is `TX`, 42 hangs in `inet_csk_accept` and the sim stalls at startup. See `debug/journal.md` for the full investigation.

## Silent-failure modes

- Quaternion freeze is hard to spot in raw HK; ADCS-side outlier detection catches sustained freeze.

## Attack-surface notes

- Quaternion spoofing fools the ADCS controller into thinking the spacecraft is in a different attitude; combined with FSS/CSS spoofing, hard to disambiguate.
