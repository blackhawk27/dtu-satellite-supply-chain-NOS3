# DTU secured-fork notes (read this first)

The five threat/scenario documents in this directory were ported from the
**Draco baseline** repo (`blackhawk27/dtu-satellite-supply-chain-NOS3-Draco`,
commit `dd1d2cbb`) so the *same* supply-chain attack can be run on both testbeds
and compared. They describe the attack as designed against Draco; this note
records what is in scope and the one intended difference.

**The comparison axis is the cFS version (legacy vs new), with the attack and the
autonomous response held constant.** This fork runs **cFE 6.7.99** (the 6.7
"Bootes" line; last official release cfe v6.7.0), Draco runs **cFE 7.0.0** (the
"Caelum"/Draco line) - a real major-version gap. Draco-derived features in this
fork are backported "the legacy way" (implemented in 6.7 conventions), which is
why the parity port below uses this fork's 6.7 idioms rather than Draco's 7.0
code verbatim. The piggyback attack,
the injection path (DEBUG link, CI_LAB UDP 5012), and the battery-spoof response
chain (WP0 -> AP0 -> RTS 4: SAFE + comms downgrade) were made identical on both
(see "Response parity" below), so an observed behavioural difference is
attributable to the cFS version, not config drift.

## What is in scope here

The redesigned, covert-opcode "piggyback" `noisy_app` and these scenarios:

| Opcode | Scenario      | In scope? |
|--------|---------------|-----------|
| `0x02` | EPS_SPOOF     | yes |
| `0x04` | SB_BURST      | yes |
| `0x06` | EPS_OVERRIDE  | yes (drives the autonomous response) |
| `0x08` | SB_FLOOD      | yes (destructive; run last) |
| `0x0A` | IMU_BIAS      | **Draco-only** - needs a `generic_imu` backdoor not ported here |
| CDS persistence | -        | **Draco-only** - needs `LC_SAVE_TO_CDS` + AP/RTS companion edits |

## Response parity (made identical to Draco)

1. **LC battery autonomy - now matched.** Grafted from Draco into this fork:
   watchpoint **WP #0** (`BatteryVoltage < 14800` mV) -> actionpoint **AP #0**
   (`SAFE_ON_LOW_BAT`, ACTIVE) -> **RTS 4** = `MGR SET_MODE SAFE` + DS-disable +
   `TO_LAB SET_SAFE_TLM`. `drive_poc.py override` re-arms AP #0
   (`APNumber=0`). The 10000 mV spoof is below 14800 and trips WP #0. (This fork
   also retains its own WP #27/AP #28 SciMode actionpoints; AP #27/#28 stay
   `LC_APSTATE_DISABLED` by default, same as Draco, so they do not fire on the
   spoof.) See `nos3/cfg/nos3_defs/tables/lc_def_{wdt,adt}.c`, `sc_rts004.c`.
2. **Comms downgrade - mechanism matched, set differs by design.** `TO_LAB
   SET_SAFE_TLM` here drops the high-rate component streams and keeps the
   low-rate HK/command beacon, discriminating by the subscription `BufLimit`
   (`<= 4` kept, `>= 32` dropped) rather than Draco's positional 34-entry block -
   the two forks' subscription tables are ordered differently, so the exact
   retained stream set differs while the behaviour (downlink throughput drops,
   uplink stays alive, recover with `SET_NOMINAL_TLM`) is the same.
3. **Injection path.** Identical on both: the unauthenticated DEBUG link (CI_LAB
   UDP 5012). CryptoLib only guards the RADIO link, so it does not gate this
   attack on either testbed - which is why the path is held constant and the cFS
   version is the variable.
4. **Out of scope (Draco-only):** the IMU covert channel (`0x0A`, needs a
   `generic_imu` backdoor) and CDS persistence (`LC_SAVE_TO_CDS`).
5. **TO_LAB downlink dest.** This fork's TO_LAB self-resolves its dest and COSMOS
   auto-enables output (`to_enable_task.rb`), so the driver does not re-enable it
   by default (`ENABLE_DOWNLINK=1` to force).

## Where to look

- Quick run reference: `nos3/poc/piggyback_noisy/run_poc.md`
- Driver: `nos3/poc/piggyback_noisy/drive_poc.py`
- Comparison harness + results: `nos3/poc/piggyback_noisy/run_comparison.sh`,
  `docs/security/comparison-secured-vs-draco.md`
- Detection wiring: `nos3/elk/logstash.conf` (tags `attack_piggyback`,
  `attack_eps_spoof`, `attack_eps_override`, `attack_sb_burst`,
  `attack_sb_flood`, `attack_safe_mode`, `to_safe_downgrade`,
  `piggyback_carrier`, `piggyback_length_error`, `sb_pipe_overflow`).
