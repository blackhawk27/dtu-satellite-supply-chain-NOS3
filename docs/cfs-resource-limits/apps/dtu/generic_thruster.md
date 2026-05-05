# generic_thruster - DTU custom (NOT LOADED)

- **Source:** `nos3/components/generic_thruster/fsw/`
- **Loaded by:** *not* in the active CFE_APP set in `sc-mission-config.xml`. Listed in the post-`!` template region of `cfe_es_startup.scr` only.
- **MIDs:** topic IDs -> CMD `0x19??`, HK TLM `0x09??` (resolved via macro)

## Why this page exists

The thruster is **disabled** in the current mission profile. Per `nos3/cfg/spacecraft/sc-mission-config.xml`, `<thruster><enable>false</enable>`. The simulator on `cfg/InOut/Inp_IPC.txt` port 4280 is also `OFF` and **must remain OFF** (load-bearing invariant): if 42 tries to write to a non-listening socket, it `exit(1)`s and the whole sim stalls.

## If enabled in the future

- Pipe depth 10, standard component pattern.
- Direct delta-V command primitive: any compromise of the cmd MID is a manoeuvring authority. Highest-impact actuator on the spacecraft.
- The IPC port 4280 must be set back to TX coordinated with `<enable>true</enable>` in the spacecraft config.
