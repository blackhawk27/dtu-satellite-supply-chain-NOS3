# generic_adcs - DTU custom

- **Source:** `nos3/components/generic_adcs/fsw/`
- **Loaded by:** `cfe_es_startup.scr` line 33 (priority 60, stack 32768)
- **Pipe depth:** 10
- **MIDs:** topic IDs resolved via component macro -> CMD `0x195E`, HK TLM `0x095E` (verify against build msgid table)
- **Sub-modes:** sun-pointing, nadir, recovery; driven by MGR since commit `b8da042d`

## Role

Attitude Determination & Control. Aggregates inputs from CSS / FSS / IMU / MAG / NAV; closes the loop through reaction wheels and torquers. The first-tick `std::stof` crash fix (commit `962b61e7`) is load-bearing for ADCS startup.

## Silent-failure modes

- A sensor that reports stale data (e.g. CSS frozen) is averaged with healthy ones; pointing degrades gracefully without a discrete event.
- FOV-exit recovery RTS (commit `8af2fdea`) auto-fires when the science target leaves the field; verify it is in the SC RTS table.

## Attack-surface notes

- ADAC update MID is the high-rate control path; tampering steers the spacecraft.
- Sub-mode selection is driven by MGR; see [mgr.md](mgr.md) for the cascade.
