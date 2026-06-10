# Piggyback PoC - run and observe

> For a full click-by-click operator walkthrough (COSMOS GUI launch, sending the
> commands by hand, finding the evidence in Kibana and building a dashboard,
> pcap capture + analysis, and saving/replaying the run) see
> `docs/security/poc-manual-runbook.md`. This file is the quick reference.


A covert opcode byte is piggybacked onto the unused tail of a legitimate CI_LAB
NOOP, sent manually from the COSMOS ground station, and read off the Software Bus
by the co-resident `noisy_app`, which then runs a selected scenario.

> DTU parity note (legacy vs new cFS): this fork now matches the Draco baseline's
> battery-spoof response. Watchpoint **WP #0** (`BatteryVoltage < 14800` mV) is
> wired to actionpoint **AP #0** (`SAFE_ON_LOW_BAT`, ACTIVE) -> **RTS 4**:
> `MGR SET_MODE SAFE` + DS-disable + `TO_LAB SET_SAFE_TLM` (downlink downgraded to
> the low-rate HK beacon; CI_LAB uplink stays alive). The 10000 mV spoof is below
> 14800 and trips WP #0. The attack + injection path (DEBUG link, port 5012) are
> identical on both testbeds, so the only variable is the cFS version
> (this fork cFE 6.7.99 "Bootes" / legacy vs Draco cFE 7.0.0 "Caelum" / new). See
> `docs/security/00-dtu-secured-fork-notes.md`.

## What was changed

- `nos3/fsw/apps/noisy_app/` - empty skeleton + covert-opcode listener
  (subscribes to its own MID `0x18F2` and sniffs the carrier `CI_LAB_CMD_MID
  0x18E0`).
- `nos3/cfg/nos3_defs/targets.cmake`, `cpu1_cfe_es_startup.scr` - noisy_app made
  active (reversible block / `CFE_APP` row).
- `nos3/gsw/cosmos/config/targets/CI_DEBUG/cmd_tlm/PIGGYBACK_POC.txt` - the
  manual COSMOS command `CI_DEBUG CI_DEBUG_PIGGYBACK_NOOP_CC` with an `OPCODE`
  dropdown (DORMANT / EPS_SPOOF / SB_BURST / EPS_OVERRIDE / SB_FLOOD / IMU_BIAS /
  GNSS_SPOOF / GNSS_DRIFT).

## Opcode map (the piggybacked trailing byte)

| OPCODE | Scenario     | Effect |
|--------|--------------|--------|
| `0x00` | DORMANT      | nothing (stealth); also CLEARS a running EPS_OVERRIDE |
| `0x02` | EPS_SPOOF    | forge ONE `GENERIC_EPS_HK` (`0x091A`) with BatteryVoltage=10000 mV (< LC WP #0 threshold 14800 -> can drive RTS 4 / SAFE) |
| `0x04` | SB_BURST     | 64-packet capped Software Bus burst (pool/pipe stress; not the legacy 32k storm) |
| `0x06` | EPS_OVERRIDE | PERSISTENT: subscribe to `0x091A` and shadow EVERY real EPS HK so the spoof always wins LC's sample (AP #0 fires RTS 4: SAFE + comms downgrade, and holds it). Send `0x00` to clear. See `docs/security/noisy_app-escalation-design.md` |
| `0x08` | SB_FLOOD     | DESTRUCTIVE: self-hosted SB pool lock (~15 max-size buffers parked unread) -> every app's TransmitMsg fails -> total blackout. Needs a sim restart. Gated research mode |
| `0x0A` | IMU_BIAS     | COVERT CHANNEL: drop `/ram/.imu_cal` (off-bus file dead-drop). Requires the companion `generic_imu` backdoor to have telemetry effect (see note at end) |
| `0x0C` | GNSS_SPOOF   | OFF-BUS: write the peer GNSS app's cached position (LastBusLat/Lon) directly in memory -> downlinked position teleports to Null Island (0,0). Never on the SB, so SB_ACL cannot block it. Send `0x00` to clear |
| `0x0E` | GNSS_DRIFT   | OFF-BUS: same direct-memory vector but a slow plausible offset (genuine position + a growing delta) that a range check will not catch; only a truth-vs-bus divergence does. Send `0x00` to clear |

## Build

```bash
cd nos3
make config && make fsw     # noisy_app must appear as CFE_APP in the build
grep noisy cfg/build/nos3_defs/cpu1_cfe_es_startup.scr   # expect CFE_APP, noisy_app
```

## Launch

```bash
cd nos3 && make launch
```

Confirm noisy_app loaded (EVS): `NOISY_APP: Initialized. CMD MID 0x18F2,
sniffing carrier 0x18E0.`

## Send from COSMOS (manual)

In the COSMOS Command Sender:
- Target `CI_DEBUG`, Command `CI_DEBUG_PIGGYBACK_NOOP_CC`.
- Set `OPCODE` to the scenario (start with `DORMANT`, then `EPS_SPOOF`, then `SB_BURST`).
- Send.

Or from a COSMOS script / ruby console:
```ruby
cmd("CI_DEBUG CI_DEBUG_PIGGYBACK_NOOP_CC with OPCODE 2")   # EPS_SPOOF
cmd("CI_DEBUG CI_DEBUG_PIGGYBACK_NOOP_CC with OPCODE 4")   # SB_BURST
```

Control / baseline (no piggyback, noisy_app stays silent):
```ruby
cmd("CI_DEBUG CI_DEBUG_NOOP_CC")
```

## Send headless (no COSMOS GUI)

`drive_poc.py` sends raw CCSDS straight to CI_LAB UDP 5012. Run it from the
docker host: by default it auto-resolves the `sc01-nos-fsw` container IP on the
`nos3-sc01` bridge (override with the `FSW_IP` arg or `FSW_CONTAINER` env). It
does NOT re-enable the TO_LAB downlink (COSMOS `to_enable_task.rb` does that and
TO_LAB self-resolves its dest here); set `ENABLE_DOWNLINK=1` to force the legacy
enable.
Modes:
```bash
python3 nos3/poc/piggyback_noisy/drive_poc.py            # full ladder (control, 0x02, 0x04)
python3 nos3/poc/piggyback_noisy/drive_poc.py override   # re-arm AP #0 + persistent EPS override -> sustained RTS 4 (SAFE)
python3 nos3/poc/piggyback_noisy/drive_poc.py off        # clear the override (opcode 0x00)
python3 nos3/poc/piggyback_noisy/drive_poc.py spoof      # one-shot EPS_SPOOF (0x02)
python3 nos3/poc/piggyback_noisy/drive_poc.py burst      # one-shot SB_BURST (0x04)
python3 nos3/poc/piggyback_noisy/drive_poc.py flood      # DESTRUCTIVE: SB pool lock (0x08), needs restart
python3 nos3/poc/piggyback_noisy/drive_poc.py gnss       # OFF-BUS: GNSS teleport to (0,0) (0x0C)
python3 nos3/poc/piggyback_noisy/drive_poc.py gnss_drift # OFF-BUS: GNSS slow plausible drift (0x0E)
python3 nos3/poc/piggyback_noisy/drive_poc.py <FSW_IP> <DEST_IP> <mode>   # explicit endpoints
```
Note on EPS_SPOOF vs EPS_OVERRIDE: a single `0x02` spoof is unlikely to be the
exact packet LC samples, so it may not fire RTS 4 on its own. The `override` mode
does the robust thing: it re-arms AP #0 and engages opcode `0x06`, which shadows
every real EPS HK so WP #0 stays FAIL and RTS 4 (SAFE + comms downgrade) fires and
holds - no rate-racing needed.

## Observe

- EVS (passive_listener + ELK system_log):
  - `NOISY_APP: piggyback opcode 0x02 on MID 0x18E0 ...`
  - `NOISY_APP: EPS HK SPOOF sent ...` or `NOISY_APP: SB BURST sent 64 packets ...`
  - CI_LAB length-error event (`Invalid msg length: ID = 0x18E0 ...`) + CI_LAB HK
    `CommandErrorCounter++` - the endpoint defense the sniffer bypasses.
- EPS_SPOOF: a `GENERIC_EPS_HK` record with BatteryVoltage 10000 in
  `omni_logs/tlm_hk_decoded.log` / ELK; under `override`, once it crosses WP #0,
  LC AP #0 fires RTS 4 -> `Batt volt critical: SAFE` (LC EVS), MGR enters SAFE,
  and TO_LAB logs `SAFE-mode downlink downgrade engaged` (high-rate streams
  dropped, HK beacon kept, uplink alive).
- pcaps under `attack_logs/tcpdump/` contain the 9-byte piggyback datagram on 5012.

## KQL (Kibana, nos3-telemetry*)

```
message:"piggyback opcode"
message:"EPS HK SPOOF"
message:"Invalid msg length" and message:"0x18E0"
message:"Batt volt critical: SAFE"
message:"SAFE-mode downlink downgrade"
eps_battery_mv < 14800
```

## Revert to mission-truth

- Set the `noisy_app` `cpu1_cfe_es_startup.scr` row back to `OFF_APP` (and, to drop
  the binary entirely, remove `noisy_app` from `targets.cmake`); delete
  `PIGGYBACK_POC.txt`. Rebuild. noisy_app is then absent and the PoC command is a
  no-op. (Note: the mission/shipped config keeps the row `OFF_APP`; `CFE_APP` is
  the experiment toggle.)

## Opcode 0x0A (IMU_BIAS) - Draco-only, out of scope here

The IMU covert-channel opcode requires a companion backdoor in the `generic_imu`
FSW app (latch `/ram/.imu_cal` and drift its telemetry). That backdoor is NOT
ported into this repo, so `0x0A` writes the dead-drop file but has no telemetry
effect. Treat it as Draco-only unless the `generic_imu` side is also ported.
