# generic_eps - DTU custom

- **Source:** `nos3/components/generic_eps/fsw/`, `nos3/components/generic_eps/sim/`
- **Loaded by:** `cfe_es_startup.scr` line 35 (priority 63, stack 32768)
- **Pipe depth:** 10
- **MIDs:** topic IDs `0x1A`/`0x1B` -> CMD `0x191A`, HK TLM `0x091A`, REQ_HK `0x191B`
- **Storage:** battery SOC seed in CDS

## Role

Electrical Power Subsystem. Emits per-app load attribution, battery SOC, bus voltage, solar-array power. Mode-aware load model + per-app load deque + bus-voltage scaling. Hysteresis band 90 - 100 % SOC to prevent eclipse-edge oscillation; tuning landed in commit `1b441712` (2026-05-03).

## ELK fields

`eps_solar_power_w`, `eps_power_used_w`, `eps_battery_wh`, `eps_battery_mv`, `eps_solar_array_enabled`. Composite EVS tick-summary every wakeup.

## Silent-failure modes

- A misconfigured per-app load entry silently distorts the load attribution but the bus-voltage curve still tracks aggregate load.
- The hysteresis band (90 - 100 %) hides small oscillations; outside this band the mode-shutoff is fast.

## Attack-surface notes

- An EPS race fix shipped in commit `59b1cbf5` (pre-thesis) is load-bearing. See `docs/thesis/04-apps/eps.md` for the EPS-vs-noisy_app dashboard panel.
- Spoofing low SOC triggers SAFE mode (battery-protect autonomous response in MGR/SC); useful denial-of-service.
- Spoofing high SOC during real eclipse keeps SCIENCE mode active and drains the battery.
