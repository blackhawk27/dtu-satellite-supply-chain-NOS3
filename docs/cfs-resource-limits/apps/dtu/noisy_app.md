# noisy_app - DTU custom test fixture

- **Source:** `nos3/fsw/apps/noisy_app/`
- **Loaded by:** `cfe_es_startup.scr` line 30 as `OFF_APP` (priority 20 / 160 in different mission profiles). **Not loaded by default.**
- **Pipe depth:** 10 (typical default)
- **MIDs:** see `noisy_app_msgids.h`
- **Storage:** none

## Role

Test fixture for ELK pipeline noise floor and EPS load model validation. Emits configurable EVS bursts and SB packets at a configured rate. The EPS sim's per-app load model includes a `noisy_app` profile; toggling `noisy_app` shows up as the bottom row of the EPS HK-vs-noisy_app dashboard panel.

## Silent-failure modes

- Disabled at runtime via cmd; HK still emits but at zero rate.

## Attack-surface notes

- Low value as an attack vector itself (turned off by default), but a useful red-team primitive for stressing SB pipe overflow, EVS rate caps, and the EPS bus-voltage scaling model.
