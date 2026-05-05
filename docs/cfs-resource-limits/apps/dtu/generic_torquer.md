# generic_torquer - DTU custom

- **Source:** `nos3/components/generic_torquer/fsw/`
- **Loaded by:** `cfe_es_startup.scr` line 45 (priority 69, stack 32768)
- **Pipe depth:** 10
- **MIDs:** topic IDs -> CMD `0x1948`, HK TLM `0x0948`

## Role

Magnetorquer driver. Converts ADCS bdot output into per-axis dipole moments.

## Silent-failure modes

- A failed torquer reports zero moment in HK but ADCS does not re-allocate authority.

## Attack-surface notes

- Direct dipole-command primitive. With control of the cmd MID, an attacker can tumble the spacecraft using only torquers (no wheels needed).
