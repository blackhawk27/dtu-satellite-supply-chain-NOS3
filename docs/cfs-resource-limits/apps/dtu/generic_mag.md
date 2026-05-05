# generic_mag - DTU custom

- **Source:** `nos3/components/generic_mag/fsw/`
- **Loaded by:** `cfe_es_startup.scr` line 40 (priority 66, stack 32768)
- **Pipe depth:** 10
- **MIDs:** topic IDs -> CMD `0x1944`, HK TLM `0x0944`

## Role

Magnetometer. Used by ADCS for bdot detumbling and the magnetic-field reference.

## Silent-failure modes

- Unwoken sensor publishes zeros; ADCS uses fallback control mode.

## Attack-surface notes

- Field-vector spoofing fools bdot into commanding torquers in the wrong direction; combined with [generic_torquer.md](generic_torquer.md) this can spin up the spacecraft.
