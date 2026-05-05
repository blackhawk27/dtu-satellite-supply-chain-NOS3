# syn - DTU research (NOT LOADED in mission profile)

- **Source:** `nos3/components/syn/fsw/`
- **Loaded by:** *not* in the active CFE_APP set for `sc-mission-config.xml`. Activated only by `sc-research-config.xml`.

## Role

Synthetic-telemetry research app. Produces test patterns for ELK pipeline development.

## When enabled

- Pipe depth 10, standard component pattern.
- Generates configurable HK at requested rate; useful as a Logstash filter regression fixture.

## Attack-surface notes (if enabled)

- Low value as an attack vector itself.
- Useful as a **noise generator** in security-testing scenarios.
