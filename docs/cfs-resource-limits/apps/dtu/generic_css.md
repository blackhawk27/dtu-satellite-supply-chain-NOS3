# generic_css - DTU custom

- **Source:** `nos3/components/generic_css/fsw/`
- **Loaded by:** `cfe_es_startup.scr` line 34 (priority 62, stack 32768)
- **Pipe depth:** 10
- **MIDs:** topic IDs -> CMD `0x1960`, HK TLM `0x0960`, Device TLM `0x0961`

## Role

Coarse Sun Sensor (six-face). Provides body-frame sun vector for sun-pointing.

## Silent-failure modes

- Frozen sensor publishes zeros; bdot/sun-pointing degrade silently.

## Attack-surface notes

- Sun-vector spoofing makes ADCS point antenna away from ground or solar array away from sun.
