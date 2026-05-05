# generic_reaction_wheel (generic_rw) - DTU custom

- **Source:** `nos3/components/generic_reaction_wheel/fsw/`
- **Loaded by:** `cfe_es_startup.scr` line 42 (priority 68, stack 32768)
- **Pipe depth:** 10
- **MIDs:** topic IDs -> CMD `0x1992`, HK TLM `0x0992`

## Role

Reaction-wheel actuator + sim. Reports `rw_momentum` per wheel; consumed by the ADCS controller and the dashboard "Reaction Wheel Momentum" panel.

## Silent-failure modes

- Wheel saturation is reported as momentum-near-max in HK but no discrete event by default.

## Attack-surface notes

- Commanding all wheels to peak momentum simultaneously triggers ADCS pointing loss.
- Wheel speed/momentum spoofing fools the controller into adding control torque that destabilises the spacecraft.
