# mgr (Mission Manager) - DTU custom

- **Source:** `nos3/components/mgr/fsw/`
- **Loaded by:** `cfe_es_startup.scr` line 39 (priority 75, stack 32768)
- **Pipe depth:** 10 (typical for DTU app; check `mgr_platform_cfg.h`)
- **MIDs:** topic IDs `0xF8` (CMD/HK), `0xF9` (REQ_HK). Resolved via `0x1900 | topic` macro: CMD `0x19F8`, HK TLM `0x09F8`
- **Tables:** mode-table + per-mode app-load profile
- **Storage:** mode state is broadcast on HK; CDS not used

## Role

Drives spacecraft mode (BOOT, SAFE, NOMINAL, SCIENCE, RECOVERY). Issues mode-change commands to other apps, gates the LC actionpoints by mode, and (since commit `b8da042d`, 2026-05-03) auto-enables ADCS sensors at boot and drives ADCS sub-modes from the spacecraft mode.

## ELK fields

`spacecraft_mode`, `mgr_mode_id` (extracted by Logstash); the `in_science_mode` boolean (added 2026-05-03) is derived from `spacecraft_mode == SCIENCE`.

## Silent-failure modes

- A mode change that is rejected by another app (e.g. EPS refusing SCIENCE during eclipse) is logged once but the global mode HK still shows the requested value.
- The auto-enable-on-boot path (commit `b8da042d`) will silently no-op if the sensor app is not yet started; race only visible in EVS.

## Attack-surface notes

- Mode is the single most consequential autonomous control variable. An attacker who can set mode to SCIENCE during eclipse drains the battery; setting RECOVERY can mask other anomalies.
- The MGR HK MID (`0x09F8`) gates the in_science_mode flag in the dashboard; tampering with the published value affects the GNSS GS validation AND-gate.
