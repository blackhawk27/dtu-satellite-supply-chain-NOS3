# Piggyback PoC: legacy vs new cFS (this fork vs Draco)

> **DTU comparison artifact (thesis).** The *same* covert-opcode piggyback attack
> (`nos3/poc/piggyback_noisy/`) is run on both testbeds over the same
> unauthenticated DEBUG link (CI_LAB UDP 5012), with the same autonomous-response
> chain. The one intended variable is the cFS version. See
> [`00-dtu-secured-fork-notes.md`](00-dtu-secured-fork-notes.md).

## Methodology

- **Held constant (made identical on both testbeds):**
  - Attack artifact: the piggyback `noisy_app` and `drive_poc.py`.
  - Injection path: CI_LAB UDP 5012 (DEBUG). CryptoLib only guards the RADIO
    link, so it does not gate this attack on either side - which is exactly why
    the path is held constant rather than used as the variable.
  - Autonomous response: battery-low chain WP #0 (`< 14800` mV) -> AP #0
    (`SAFE_ON_LOW_BAT`) -> RTS 4 (`MGR SET_MODE SAFE` + DS-disable + `TO_LAB
    SET_SAFE_TLM` comms downgrade). Grafted from Draco into this fork; `drive_poc.py
    override` re-arms AP #0.
- **The variable:** the cFS version. This fork = **cFE 6.7.99** (the 6.7 "Bootes"
  line, legacy; Draco-derived features backported in 6.7 conventions);
  Draco = **cFE 7.0.0** (the "Caelum"/Draco line, new) - a major-version gap. Any
  behavioural difference under the identical attack is attributable to the cFS
  code, not configuration.
- **Baseline column** is from Draco's `debug/journal.md` live-run results.
  **This-fork column** is produced by `run_comparison.sh` (counts in
  `nos3/poc/piggyback_noisy/results_secured.md`). Cells marked `MEASURED:` are
  filled from that run.

> Status: the parity FSW (piggyback `noisy_app` + WP0/AP0/RTS4 + TO_LAB
> SET_SAFE_TLM) is built/compile-verified. The live run that fills the `MEASURED:`
> cells must be launched from a terminal with an X display (the agent shell used
> for the work had no X server, so `make launch` aborts at 42's `xhost`). Run
> `make launch` then `bash nos3/poc/piggyback_noisy/run_comparison.sh --with-flood
> --save` and paste in the counts. See `debug/journal.md`.

## Per-scenario comparison (responses now identical by construction)

### Scenario A - EPS_SPOOF (opcode 0x02, one-shot)

| Aspect | Draco (cFE 7.0.0, new) | This fork (cFE 6.7.99, legacy) |
|---|---|---|
| Delivered to bus? | yes - CI_LAB rejects the over-length NOOP on 0x18E0, sniffer still reads the opcode | yes - same mechanism (`piggyback_carrier` + `piggyback_length_error`) |
| Forged EPS HK seen? | yes - `EPS HK SPOOF ... 10000 mV` on 0x091A | expected yes; MEASURED: `attack_eps_spoof` = ___ |
| Autonomous response? | one-shot unlikely to be the sampled packet -> use override | same (AP #0 ACTIVE, but a single forged packet rarely the sampled value); MEASURED: `attack_safe_mode` EVS = ___ |
| Detected in ELK? | EVS spoof event | `attack_eps_spoof`, `piggyback_length_error`; MEASURED = ___ |

### Scenario B - EPS_OVERRIDE (opcode 0x06, persistent) - the autonomous SAFE trip

| Aspect | Draco (cFE 7.0.0, new) | This fork (cFE 6.7.99, legacy) |
|---|---|---|
| Autonomous response | `Batt volt critical: SAFE ... RTS = 4`; TO_LAB SAFE downlink downgrade engaged | **identical chain**: WP #0 -> AP #0 -> RTS 4 -> `MGR SET_MODE SAFE` + `TO: SAFE-mode downlink downgrade engaged`; MEASURED: `attack_safe_mode` + `to_safe_downgrade` EVS = ___ |
| Link after trip | degraded (HK beacon), uplink alive | expected same (high-rate streams dropped, uplink alive); MEASURED: downlinked-MID count drop = ___ |
| Holds under override? | yes - shadow wins every LC sample, SAFE holds | expected yes - shadow wins WP #0 sample; MEASURED = ___ |
| Detected in ELK? | EVS SAFE/RTS events | `attack_eps_override`, `attack_safe_mode`, `to_safe_downgrade`, `eps_battery_mv < 14800`; MEASURED = ___ |
| Clears on 0x00? | yes | expected yes; MEASURED = ___ |

Difference to watch: the response *configuration* is identical, so any divergence
(timing, RTS execution, SAFE-beacon contents, table-load behaviour, error paths)
is a property of cFE 6.7.99 vs 7.0.0. Note the SAFE beacon *contents* differ by
construction: Draco keeps a positional 34-stream block; this fork keeps the
low-rate (`BufLimit <= 4`) HK/command streams and drops the high-rate component
telemetry - a table-ordering difference, not a behavioural one.

### Scenario C - SB_BURST (opcode 0x04, bounded)

| Aspect | Draco | This fork |
|---|---|---|
| Effect | 64-packet capped burst on 0x08F2; sim stays responsive | same; MEASURED: `attack_sb_burst` = ___, `noisy_app_spam_target` (0x08F2) = ___ |

### Scenario D - SB_FLOOD (opcode 0x08, destructive) - run last

| Aspect | Draco | This fork |
|---|---|---|
| Effect | ~15 max-size buffers parked -> 512 KB SB pool locked -> every app's `TransmitMsg` fails -> blackout; needs restart | expected same (shared SB pool unchanged); MEASURED: `sb_pipe_overflow` = ___ |
| Recovery | `make stop` + relaunch | same |

### Scenario E - CDS persistence / IMU covert channel (0x0A) - Draco-only

| Aspect | Draco | This fork |
|---|---|---|
| CDS persistence | `LC_SAVE_TO_CDS` on; PASSIVE latch designed to survive a warm reboot (Draco journal: live verify blocked by a NOS-Engine warm-boot harness limitation) | **out of scope** - companion edits not ported |
| IMU bias (0x0A) | `generic_imu` backdoor latches `/ram/.imu_cal`, drifts gyro/accel TM with zero SB/EVS evidence | **out of scope** - `0x0A` drops the file but has no telemetry effect here |

## Headline finding (for the thesis)

With the attack, the injection path, and the autonomous-response chain held
identical across both testbeds, the comparison isolates the **cFS version** (cFE
6.7.99 vs 7.0.0) as the sole variable. Two framings the data supports:

1. **Supply-chain robustness across cFS versions.** Does the same co-resident
   piggyback attack produce the same SAFE trip, comms downgrade, and SB-flood
   outcome on both cFE builds, or does the newer cFE change any error-handling /
   table-load / SB behaviour that alters the attack's effect or its observability?
2. **Transport security is not enough.** On the unauthenticated DEBUG link and
   Software Bus, neither testbed's RADIO-boundary CryptoLib prevents the attack -
   it is detected (ELK tags the full lifecycle) and triggers autonomous safing
   (LC AP #0 -> RTS 4), but not prevented. This motivates controls at the
   **Software Bus** boundary (sender authentication, per-app quotas, multi-source
   corroboration for autonomy), which transport-layer crypto does not provide.
