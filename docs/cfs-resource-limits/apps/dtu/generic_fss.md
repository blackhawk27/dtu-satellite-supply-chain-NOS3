# generic_fss - DTU custom

- **Source:** `nos3/components/generic_fss/fsw/`
- **Loaded by:** `cfe_es_startup.scr` line 36 (priority 64, stack 32768)
- **Pipe depth:** 10
- **MIDs:** topic IDs -> CMD `0x1962`, HK TLM `0x0962`, Device TLM `0x0963`

## Role

Fine Sun Sensor. Higher-precision counterpart to CSS; provides the high-accuracy sun vector for science-mode pointing.

## Silent-failure modes

- Stale data is averaged with CSS; degraded but not flagged.

## Attack-surface notes

- Same vector-spoofing primitive as CSS, with finer steering authority.
