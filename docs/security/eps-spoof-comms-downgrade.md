# Cyber Threat Analysis - EPS Spoofing to Force a Comms Downgrade

> **DTU secured-fork note.** Ported from the Draco baseline (commit `dd1d2cbb`). In this repo the LC battery autonomy is WP #27 (`< 24240` mV) -> AP #27 -> RTS 27 (not Draco's WP0/AP0 SAFE chain), and the DEBUG-path injection means CryptoLib does not gate the attack. Scenarios `0x02/0x04/0x06/0x08` are in scope; IMU (`0x0A`) and CDS persistence are Draco-only. See [`00-dtu-secured-fork-notes.md`](00-dtu-secured-fork-notes.md).


Analysis of a cyberattack that spoofs the Electrical Power System (EPS)
housekeeping telemetry to trip the on-board limit checker into a false SAFE
mode, whose autonomous response downgrades the downlink to a low-rate
housekeeping beacon. The attacker denies the ground full-rate telemetry (and
the science/device data it carries) without ever taking the link down, using
the same covert-opcode piggyback delivery demonstrated in
`docs/security/threat-analysis.md`.

- Subject: DTU NOS3 fork, NASA Core Flight System (cFS) 6.7.99 / Draco, OSAL
  5.0.0, PSP 1.4.0; ground software COSMOS 4.5; spacecraft profile
  `sc-mission-config.xml`.
- Methodology: STRIDE per trust boundary, mapped to the SPARTA (Space Attack
  Research and Tactic Analysis) matrix from The Aerospace Corporation.
- Status: this document refines the comms-effect half of
  `docs/security/threat-analysis.md`. The earlier work modelled the SAFE
  response as a hard comms blackout (downlink disabled + uplink self-latched
  off). That is not how real spacecraft safe modes behave: they reduce data
  rate / drop to a beacon or housekeeping-only posture and keep the command
  receiver alive. This analysis supersedes the blackout framing with a realistic
  throughput downgrade.

> Scope note: authorised defensive research on a simulator. `noisy_app` is an
> intentional, clearly-reversible attack-simulation fixture, OFF in the mission
> build (see Appendix C).

---

## 1. System decomposition and trust boundaries

```
        GROUND SEGMENT                 |            SPACE SEGMENT (cFS)
                                       |
 +--------------------+   UDP 5012     |  +---------+      Software Bus
 | COSMOS GS          |  (TC, plain)   |  | CI_LAB  |  (no sender auth, multicast)
 | Command Sender     |--------------->|  | ingest  |---+----------------+--------+
 +--------------------+   TB-1         |  +---------+   |                |        |
                                       |   length only  v                v        v
 +--------------------+   UDP 5013     |            +---------+    +---------+  +--------+
 | passive_listener,  |<---------------|  +-------+ | EPS app |    | LC      |  | noisy_ |
 | tcpdump, ELK       |  (TM, beacon   |  |TO_LAB |<| (HK/TM) |    | watch/  |  | app    |
 +--------------------+   in SAFE)     |  +---+---+ +---------+    | action  |  |(sniffer|
                                       |      |                   +----+----+  |+spoof) |
                                       | SET_SAFE_TLM (RTS 4)           |       +---+----+
                                       | reduces TO_LAB subs  SC RTS 4 <+           |
                                       |   TB-3                MGR mode <-----------+
                                       |                  TB-2 (autonomous trigger plane)
```

Trust boundaries:

- **TB-1 Ground -> FSW uplink (UDP 5012, CI_LAB).** Commands cross unauthenticated
  (see 2.1). The piggyback carrier enters here.
- **TB-2 Software Bus and the autonomous trigger plane.** cFE SB multicasts every
  message to all subscribers with no sender identity check; LC watchpoints ->
  actionpoints -> SC RTS sequences act on telemetry values without provenance
  (see 2.2, 2.3).
- **TB-3 FSW -> Ground downlink (TO_LAB / UDP 5013).** Telemetry egress. The
  attack's effect (reduced-rate beacon) is observed here by ELK.

---

## 2. Findings (enabling weaknesses)

### 2.1 Unauthenticated, unvalidated command ingress (TB-1)

CI_LAB (`fsw/apps/ci_lab/fsw/src/ci_lab_app.c`) accepts any UDP 5012 datagram
whose size is between `sizeof(CFE_MSG_CommandHeader_t)` and `CI_LAB_MAX_INGEST`
and transmits it straight onto the Software Bus. No checksum verification, no MID
allow-list, no cryptographic authentication. This is the delivery boundary for
the covert opcode. (Detailed in `threat-analysis.md` 2.1.)

STRIDE: Spoofing, Tampering. SPARTA: IA Initial Access, EX Execution.

### 2.2 Software Bus has no sender authentication (TB-2)

`CFE_SB_TransmitMsg` broadcasts to every subscriber of a MID; the header carries
an APID, not a source identity. Any app can publish another app's telemetry MID,
so a co-resident app can publish a forged `GENERIC_EPS_HK` that LC cannot
distinguish from the real EPS app's packet. (Detailed in `threat-analysis.md`
2.2.)

STRIDE: Spoofing, Information Disclosure. SPARTA: EX-0014 Spoofing.

### 2.3 Autonomous SAFE trigger is driven by a single unauthenticated value (TB-2)

`lc_def_wdt.c` WP0 watches `GENERIC_EPS_HK` BatteryVoltage (wire offset 16);
`< 14800 mV` -> actionpoint AP0 (`lc_def_adt.c:180-189`, `SAFE_ON_LOW_BAT`,
`MaxFailsBeforeRTS = 3`) -> **RTS 4** (`sc_rts004.c`). One spoofed HK field, held
true for three LC sample cycles, autonomously forces SAFE. There is no
corroboration against a second power source and no rate-of-change sanity check.

STRIDE: Tampering, Elevation of Privilege. SPARTA: EX-0014 Spoofing, EX-0012
Modify On-Board Values (actionpoint state).

### 2.4 SAFE-mode comms posture is policy-driven and reversible, but its trigger is not trustworthy (TB-2 -> TB-3)

RTS 4 (`sc_rts004.c`) is the Critical Health Response. On trigger it issues, one
wakeup apart: MGR `SET_MODE SAFE`; DS `SET_DEST_STATE` (stop instrument
recording); and **TO_LAB `SET_SAFE_TLM`**, which unsubscribes the high-rate
component device/science streams and keeps the leading housekeeping beacon block
(`TO_LAB_SAFE_BEACON_COUNT = 34` of 57 subscribed streams,
`to_lab_app.c:TO_LAB_SetSafeTlm`). The beacon is the full set of `*_HK_TLM_MID`
housekeeping (cFE-core + flight-software + every subsystem HK: EPS power, MGR
mode, HS health, GNSS, RW, etc., plus the IMU device stream), so the ground keeps
full spacecraft-health visibility; only the high-rate device/science/ADCS/
diagnostic streams are dropped. The downlink stays up at reduced throughput
and the CI_LAB uplink is untouched, so the posture is fully ground-recoverable
(TO_LAB `SET_NOMINAL_TLM`). The weakness is not the response, which is a
reasonable power-conserving safe mode; it is that an unauthenticated spoof (2.3)
can invoke it at will, denying full-rate telemetry on demand.

STRIDE: Tampering, Denial of Service (degraded). SPARTA: EX-0012, IMP-0003
Denial (of full-rate service).

### 2.5 Supply-chain: a co-resident malicious app shipped in the build

`noisy_app` is delivered in the flight build, lies dormant, and acts on the
covert trigger. (Detailed in `threat-analysis.md` 2.5.)

STRIDE: Tampering (supply chain), Elevation of Privilege. SPARTA: IA-0001
Compromise Supply Chain, PER-0002 Backdoor.

---

## 3. Proof of concept - spoofed low battery forces a false SAFE downgrade

### 3.1 Mechanism

1. **Delivery (TB-1).** A `CI_LAB` NOOP (`0x18E0`, `NOOP_CC 0`) is sent with one
   trailing covert opcode byte (9-byte datagram). CI_LAB accepts it and transmits
   it onto the bus; its own length check rejects it (`CI_LAB_LEN_ERR_EID`), but
   `noisy_app`, co-subscribed to `0x18E0`, reads the trailing byte.
2. **Spoof (TB-2).** Opcode `0x02` (EPS_SPOOF) forges one `GENERIC_EPS_HK`
   (`0x091A`) with BatteryVoltage = 10000 mV; opcode `0x06` (EPS_OVERRIDE) is the
   persistent variant that shadows every genuine EPS HK so the spoof always wins
   LC's sample race.
3. **Autonomous trigger (TB-2).** LC WP0 sees 10000 < 14800 -> AP0 FAIL; after
   `MaxFailsBeforeRTS = 3` consecutive samples, LC requests RTS 4.
4. **Response (TB-2 -> TB-3).** SC runs RTS 4: MGR SAFE, DS recording disabled,
   TO_LAB `SET_SAFE_TLM`. The downlink drops to the 34-stream all-housekeeping
   beacon; the component device/science telemetry stops. The uplink stays alive.
5. **Effect.** The ground loses full-rate telemetry (a denial of the science and
   device data) on the strength of a forged power reading, while the vehicle
   still appears nominally powered. Recovery requires a ground command
   (`SET_NOMINAL_TLM` + re-arm AP0 + MGR back to nominal); the attacker can
   re-trigger at will with another spoof.

Code: `fsw/apps/noisy_app/fsw/src/noisy_app.c` (opcodes `0x02` / `0x06`);
trigger chain `cfg/nos3_defs/tables/lc_def_wdt.c` (WP0),
`cfg/nos3_defs/tables/lc_def_adt.c` (AP0), `cfg/nos3_defs/tables/sc_rts004.c`
(RTS 4); downgrade handler `fsw/apps/to_lab/fsw/src/to_lab_app.c`
(`TO_LAB_SetSafeTlm` / `TO_LAB_SetNominalTlm`); GS command
`gsw/cosmos/config/targets/CI_DEBUG/cmd_tlm/PIGGYBACK_POC.txt`; headless driver
`nos3/poc/piggyback_noisy/drive_poc.py`.

### 3.2 Attack tree

```
GOAL: deny the ground full-rate telemetry via a forged power reading
|
+-- AND reach the command uplink (UDP 5012)             [TB-1]
|
+-- AND get the piggyback NOOP onto the Software Bus
|     +-- no auth/checksum on CI_LAB ingest (2.1)       [enabling]
|
+-- AND have a bus-resident actor forge EPS HK
|     +-- malicious app in the build (2.5)              [IA-0001 / PER-0002]
|     +-- SB multicast, no sender auth (2.2)            [enabling]
|
+-- AND hold WP0 below threshold for 3 LC samples
|     +-- one-shot spoof (0x02) for a single trip, or
|     +-- persistent override (0x06) to win the sample race (2.3)
|
+-- THEN AP0 -> RTS 4 -> SAFE + TO_LAB SET_SAFE_TLM (2.4)
      +-- EX-0012 actionpoint state change
      +-- IMP-0003 denial of full-rate downlink (beacon-only)
      +-- recoverable: uplink alive, SET_NOMINAL_TLM restores it
```

### 3.3 STRIDE x trust boundary summary

| Boundary | S | T | R | I | D | E |
|---|---|---|---|---|---|---|
| TB-1 uplink | forge cmd (2.1) | piggyback byte (3.1) | no cmd echo | - | - | exec via injection |
| TB-2 SB/triggers | spoof EPS HK (2.2) | drive AP0/RTS 4 (2.3) | no sender log | sniff carrier (2.2) | - | spoof -> autonomous SAFE (2.3) |
| TB-3 downlink | - | reduce TO_LAB subs (2.4) | EVS captures the downgrade | beacon still observable in ELK | denial of full-rate TM (2.4) | - |

### 3.4 SPARTA technique mapping

| SPARTA technique | Where in this PoC |
|---|---|
| IA-0001 Compromise Supply Chain (.01 SW deps & dev tools) | malicious `noisy_app` in the build (2.5) |
| PER-0002 Backdoor (.02 Software) | dormant co-subscribed listener (2.5) |
| EX-0014 Spoofing (sensor data / bus traffic) | forged `GENERIC_EPS_HK` (2.2, 3.1) |
| EX-0012 Modify On-Board Values (actionpoint state) | spoof -> LC AP0 -> RTS 4 (2.3) |
| EX-0001 Replay | a recorded NOOP+tail can be replayed (no anti-replay on CI_LAB) |
| IMP-0003 Denial (degraded) | SAFE-mode downlink downgrade to a beacon (2.4) |

> Technique IDs are per the SPARTA matrix (sparta.aerospace.org); sub-technique
> numbers should be cross-checked against the matrix version cited in the thesis.

---

## 4. Detection (ELK / Kibana, index `nos3-telemetry*`)

| Signal | KQL |
|---|---|
| Covert opcode read | `message:"piggyback opcode"` |
| Spoofed EPS HK effect | `message:"EPS HK SPOOF"` or `eps_battery_mv < 14800` |
| Endpoint length rejections (piggyback tell-tale) | `message:"Invalid msg length" and message:"0x18E0"` |
| Autonomous SAFE trip | `message:"Batt volt critical: SAFE"` or tag `lc_actionpoint_fired` |
| SAFE-mode downlink downgrade | `message:"SAFE-mode downlink downgrade"` (tag `to_lab_safemode` if added) |
| Mode change to SAFE | `mgr_mode:"SAFE"` |
| Downlinked MID set shrinks to the beacon | distinct downlinked `msg_id` in `nos3-telemetry*` drops (measured 36 -> 29) and packet rate roughly halves |
| High-rate device/science streams stop | `GENERIC_*_DEVICE_TLM_MID`, `GENERIC_ADCS_DI/AD/GNC/AC/DO`, GNSS device `0x0953` etc. absent after the downgrade |
| All housekeeping still flows (downgrade, not blackout) | every `*_HK_TLM_MID` incl. EPS `0x091A`, MGR `0x08F8`, HS `0x08AD`, plus IMU device `0x0926` still present and counting > 0 |
| Uplink still alive | CI_LAB HK `CommandCounter` still increments after the trip |

Detection insight: the authoritative downlink observable is **`cfs_god_view.json`
itself**, which `scripts/passive_listener.py` populates by capturing TO_LAB's
real output on **UDP 5013** (`cfgTLM_PORT`) - it is the downlink ground truth, not
the Software Bus. So the downgrade is visible directly in the `nos3-telemetry*`
index as a drop in the set of distinct downlinked `msg_id`/`msg_name`: the
high-rate component device/science streams stop appearing while every
housekeeping stream (incl. EPS power, MGR mode, HS health, and the IMU) keeps
flowing (reduced rate, not zero). The full attack signature is the
conjunction of (a) the piggyback tell-tale (length-error on `0x18E0`), (b) a
sub-14800 `eps_battery_mv` with no corroborating drop in the real EPS stream's
`eps_power_balance_w` / `eps_battery_soc_pct`, (c) the `TO: SAFE-mode downlink
downgrade engaged` EVS event, and (d) the downlinked MID set collapsing to the
beacon while CI_LAB `CommandCounter` keeps moving (uplink alive). Note: the TT&C
link-budget simulator's `packets_downlinked` is a fixed placeholder rate and does
NOT reflect the cut - do not use it as the gauge.

---

## 5. Mitigations

1. **Authenticate the uplink (TB-1).** CryptoLib SDLS/TC authentication +
   anti-replay on the active CI path; reject unauthenticated TCs. Closes 2.1.
2. **Corroborate the SAFE trigger (TB-2).** AP0 should require a second power
   indicator (battery SOC, power balance) or a rate-of-change sanity check, not a
   single unauthenticated HK field (2.3).
3. **Rate-limit autonomous SAFE.** Cap RTS 4 re-fires (for example once per
   orbit) so a sustained spoof cannot hold the vehicle in a degraded posture.
4. **Constrain SB subscriptions.** Treat an app subscribing to another app's
   telemetry/command MID as a build-time review gate (2.2 / 2.5).
5. **Alarm on the downgrade.** Wire the section-4 correlation (length-error on
   `0x18E0` + spoofed EPS value + TO_LAB downgrade) as a standing ELK rule.
6. **Supply-chain controls.** App allow-listing in `targets.cmake` /
   startup.scr as a reviewed manifest; flag attack-sim fixtures (IA-0001).

> Follow-on: enabling LC Critical Data Store persistence turns the transient AP0
> trip in this scenario into a state that survives the operator's recovery
> reboot. That escalation is analysed in `docs/security/cds-persistence.md`.

---

## Appendix A - Captured evidence (live run 2026-06-04)

Live end-to-end run on the rebuilt stack (FSW + 28 sim/GSW containers + ELK).
Commands delivered to CI_LAB UDP 5012 as raw CCSDS via
`poc/piggyback_noisy/drive_poc.py` (spoof, then override). All EVS times are the
cFS epoch shown by the sim.

**A.1 Piggyback delivery + spoof (dual path on one packet).** The 9-byte
over-length NOOP is rejected by the legitimate endpoint and read by the
co-resident sniffer:
```
42/1/CI_LAB_APP 16: Invalid msg length: ID = 0x18E0,  CC = 0, Len = 9, Expected = 8
42/1/NOISY_APP  2: NOISY_APP: piggyback opcode 0x02 on MID 0x18E0 (len 9, count 1).
42/1/NOISY_APP  3: NOISY_APP: EPS HK SPOOF sent on 0x091A (BatteryVoltage=10000 mV).
```

**A.2 Spoof drives AP0 -> RTS 4 (persistent override).** With opcode 0x06 holding
WP0 below threshold, AP0 fires RTS 4:
```
42/1/LC 1000: Batt volt critical: SAFE : AP = 0, FailCount = 141, RTS = 4
```

**A.3 RTS 4 downgrades the downlink (the new behaviour).** RTS 4 reaches its
TO_LAB step and the new handler runs:
```
42/1/TO_LAB_APP 20: TO: SAFE-mode downlink downgrade engaged; 34 housekeeping streams kept (full set dropped)
```

**A.4 Degraded, not blacked out (real downlink, measured).** The authoritative
gauge is TO_LAB's actual output on UDP 5013, captured by
`scripts/passive_listener.py` into `attack_logs/cfs_god_view.json` (the
`nos3-telemetry*` index). Distinct downlinked `msg_id` and packet count were
sampled over equal 22 s windows, before and after the
`TO: SAFE-mode downlink downgrade engaged` event:

| Downlink (UDP 5013) | distinct `msg_id` | packets / 22 s |
|---|---|---|
| Full-rate (after `SET_NOMINAL_TLM`) | 36 | 418 |
| After downgrade (beacon) | 29 | 188 |

The beacon carries the full housekeeping set, confirmed present in the beacon
window: EPS HK `0x091A`, MGR-mode HK `0x08F8`, HS health HK `0x08AD`, IMU HK
`0x0925` and the IMU device stream `0x0926`, plus GNSS/RW/ST HK (`0x0920`,
`0x0993`, `0x0950`/`0x0952`) and the cFE-core/FSW HK. The high-rate device/
science streams are gone, confirmed absent: GNSS device `0x0953`, the ADCS
sub-streams (`0x0941`...), and the other `*_DEVICE_TLM`. Packet rate falls ~55 %
(418 -> 188 per 22 s) while every housekeeping stream keeps flowing - a downgrade
that preserves spacecraft-health visibility, not a blackout. (The IMU device
stream `0x0926` is carried in SAFE as a normal beacon entry in
`to_lab_sub.c` so attitude-sensor data stays available during detumble.)

(Do NOT use the TT&C `packets_downlinked` counter: `nos3-ttc.log` shows it kept
climbing - `link_state=ACQUIRED ... packets_downlinked=15744` - because the
link-budget sim models throughput as a fixed placeholder rate
(`generic_tt_c_hardware_model.cpp:220-230`) decoupled from the real TO_LAB flow.
Making that counter safe-mode-aware is an optional sim-side enhancement; it is
not the gauge.)

**A.5 Recovery command works (uplink alive).** `SET_NOMINAL_TLM` (TO_LAB CC 8,
sent over the still-open uplink) restored full telemetry:
```
42/1/TO_LAB_APP 21: TO: full-rate downlink restored; 57 streams subscribed
```
and a clean 8-byte NOOP sent after a SAFE trip was still ingested and executed:
```
42/1/CI_LAB_APP 5: CI: NOOP command
```
Under the old hard-blackout (CI_LAB DISABLE_UPLINK) both would have been dropped;
the downgrade keeps the recovery path reachable.

## Appendix C - Reversibility

- `noisy_app` source and toggles: `targets.cmake` (commented = excluded) and
  `cpu1_cfe_es_startup.scr` (`OFF_APP` = not loaded).
- The TO_LAB downgrade commands (`TO_LAB_SET_SAFE_TLM_CC` /
  `TO_LAB_SET_NOMINAL_TLM_CC`) and RTS 4's use of `SET_SAFE_TLM` are benign
  safe-mode policy; they replaced the prior hard-blackout commands
  (`TO_LAB_OUTPUT_DISABLE_CC`, `CI_LAB_DISABLE_UPLINK_CC`), which were removed.
- GS command: delete `PIGGYBACK_POC.txt`.
- The blackout->downgrade rework is recorded in `debug/journal.md`. No
  DO-NOT-REVERT guard-rail entry was touched.
