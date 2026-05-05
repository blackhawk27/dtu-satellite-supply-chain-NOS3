# generic_imu - DTU custom

- **Source:** `nos3/components/generic_imu/fsw/`
- **Loaded by:** `cfe_es_startup.scr` line 38 (priority 65, stack 32768)
- **Pipe depth:** 10
- **MIDs:** topic IDs -> CMD `0x1942`, HK TLM `0x0942`

## Role

Inertial Measurement Unit (gyro + accel). Wakeup added in the 2026-05-03 ADCS sensor sweep (commit `8af2fdea`).

## Silent-failure modes

- Unwoken sensor publishes zeros; ADCS averages with other sensors so pointing degrades quietly.

## Attack-surface notes

- Bias injection: rate spoof on a single axis becomes a slow drift in attitude knowledge.
