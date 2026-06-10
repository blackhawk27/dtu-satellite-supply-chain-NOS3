# Cyber Threat Analysis - Persistence via the Critical Data Store (CDS)

> **DTU secured-fork note.** Ported from the Draco baseline (commit `dd1d2cbb`). In this repo the LC battery autonomy is WP #27 (`< 24240` mV) -> AP #27 -> RTS 27 (not Draco's WP0/AP0 SAFE chain), and the DEBUG-path injection means CryptoLib does not gate the attack. Scenarios `0x02/0x04/0x06/0x08` are in scope; IMU (`0x0A`) and CDS persistence are Draco-only. See [`00-dtu-secured-fork-notes.md`](00-dtu-secured-fork-notes.md).


Analysis of a persistence technique that defeats the operator's reboot-based
recovery. The cFS Critical Data Store (CDS) preserves selected application state
across a processor (warm) reset. When the limit checker (LC) is configured to
save its state to CDS, an attacker who latches a safety actionpoint to PASSIVE
(via the EPS spoof of `docs/security/eps-spoof-comms-downgrade.md`) leaves that
disarmed state in the CDS. The operator's natural first-line recovery, a
processor reset, restores the poisoned state rather than clearing it: the safety
monitor comes back disabled and the recovery fails. The persistence is bounded
by cFE's processor-reset escalation to a power-on reset, which wipes the CDS.

- Subject: DTU NOS3 fork, cFS 6.7.99 / Draco, OSAL 5.0.0, PSP 1.4.0; profile
  `sc-mission-config.xml`.
- Methodology: STRIDE per trust boundary, mapped to the SPARTA matrix.
- Status: builds directly on `docs/security/eps-spoof-comms-downgrade.md`
  (the spoof that latches AP0) and `docs/security/threat-analysis.md` (the
  supply-chain delivery). This document adds the persistence / recovery-defeat
  trust boundary.

> Scope note: authorised defensive research on a simulator. `noisy_app` is an
> intentional, reversible attack-simulation fixture, OFF in the mission build.
> LC CDS persistence (`LC_SAVE_TO_CDS`) is a standard cFS option; this build
> enables it deliberately to study the risk (see 2.1 and Appendix C).

---

## 1. System decomposition and trust boundaries

```
   SPACE SEGMENT (cFS)                          PERSISTENCE PLANE
                                       |
 +---------+   spoof EPS HK  +------+  |   +-------------------------------+
 | noisy_  |---------------->| LC   |  |   | CDS (POSIX shared memory,     |
 | app     |   (0x06)        | WP0  |  |   |  PSP shmget, .cdskeyfile)     |
 +---------+                 | AP0  |--+-->|  LC_CDS_WRT / _ART / _AppData |
                             +--+---+  TB-5|  ART[0].CurrentState = PASSIVE|
                                |  RTS 4   +---------------+---------------+
                          SC <--+ SAFE                     | restore on warm boot
                                                           v
 operator recovery: CFE_ES_RESTART_CC (PROCESSOR) --> LC restores AP0 = PASSIVE
                    CFE_ES_RESTART_CC (POWER-ON)   --> CDS wiped, AP0 = ACTIVE
                    (auto after CFE_PLATFORM_ES_MAX_PROCESSOR_RESETS = 2)
```

Trust boundaries (extending `threat-analysis.md` TB-1..TB-4):

- **TB-2 Software Bus / autonomous trigger plane.** The spoof trips AP0 (see the
  companion doc). Reused here as the entry condition.
- **TB-5 Reset / persistence plane (new).** The CDS preserves LC state across a
  processor reset. The recovery assumption ("a reboot returns the vehicle to a
  known-good default") is violated when attacker-influenced state is what gets
  restored. The CDS blocks carry no integrity or authenticity binding to a
  trusted baseline.

---

## 2. Findings (enabling weaknesses)

### 2.1 LC persists actionpoint state to CDS, restored verbatim on warm boot (TB-5)

With `LC_SAVE_TO_CDS` enabled (`fsw/apps/lc/fsw/inc/lc_internal_cfg.h`, enabled
in this build) and `LC_STATE_WHEN_CDS_RESTORED = LC_STATE_FROM_CDS` (the
default), LC registers three CDS blocks at startup and refreshes them every
maintenance cycle: `LC_CDS_WRT` (watchpoint results), `LC_CDS_ART` (actionpoint
results, including each AP's `CurrentState`), and `LC_CDS_AppData`
(`lc_app.c:733-856`, `lc_utils.c:108-158`). On a processor reset, if all blocks
restore, LC keeps the restored state and emits `LC_CDS_RESTORED_INF_EID`
("Previous state restored from Critical Data Store", `lc_app.c:479-480`) instead
of loading defaults. There is no CRC or signature check binding the restored
ART to a trusted baseline; whatever `CurrentState` was last written is honoured.

STRIDE: Tampering, Elevation of Privilege. SPARTA: PER-0002 Backdoor (state),
EX-0012 Modify On-Board Values (persisted actionpoint state).

### 2.2 A latched-PASSIVE actionpoint disables a safety monitor (TB-2 -> TB-5)

When AP0 (`SAFE_ON_LOW_BAT`) fires RTS 4 it latches to PASSIVE (a one-shot, so a
single trip stops it re-evaluating until re-armed). PASSIVE is the state written
to CDS. After a warm boot AP0 returns PASSIVE, so the battery-low SAFE protection
stays disarmed: a subsequent genuine low-battery event would no longer trigger
SAFE. The attacker's transient spoof produces a durable hole in the autonomous
safety net.

STRIDE: Denial of Service (of protection), Tampering. SPARTA: EX-0012, IMP-0002
Disruption.

### 2.3 The reboot recovery path is the thing being defeated (TB-5)

The documented recovery from an anomalous SAFE is an operator-commanded reset
(`CFE_ES_RESTART_CC`). A processor reset preserves CDS; a power-on reset wipes
it. Operators reach for the gentler processor reset first. With 2.1 in effect,
that reset restores the poisoned LC state, so the standard recovery does not
work. The persistence is bounded: `CFE_PLATFORM_ES_MAX_PROCESSOR_RESETS = 2`
(`cpu1_platform_cfg.h:1211`), after which cFE escalates to a power-on reset that
clears the CDS and re-arms AP0. So the attack defeats the gentle recovery but not
a deliberate power-on reset (or an explicit `LC_SET_AP_STATE` re-arm).

STRIDE: Repudiation (recovery appears to succeed but does not), Denial of
Service. SPARTA: PER-0002, IMP-0002.

### 2.4 Supply-chain entry (TB-1/TB-2)

The latch is delivered by the same `noisy_app` covert-opcode piggyback and EPS
spoof as the companion analyses (`threat-analysis.md` 2.5,
`eps-spoof-comms-downgrade.md` 3.1). No new delivery primitive is required;
persistence is purely a consequence of the enabled CDS option plus the existing
spoof.

SPARTA: IA-0001 Compromise Supply Chain, PER-0002 Backdoor.

---

## 3. Proof of concept - spoof-latched AP0 survives the recovery reboot

### 3.1 Mechanism

1. **Latch (TB-2).** Drive `noisy_app` opcode `0x06` (EPS_OVERRIDE) so WP0 holds
   below 14800 mV; AP0 fails 3 samples, fires RTS 4 (SAFE + comms downgrade per
   the companion doc) and latches AP0 to PASSIVE.
2. **Persist (TB-5).** LC's maintenance cycle writes ART (AP0 = PASSIVE) to
   `LC_CDS_ART` within ~1 s (`lc_utils.c:108-158`).
3. **Operator recovery.** Ground commands a processor reset
   (`CFE_ES_RESTART_CC`, `RestartType = PROCESSOR`), expecting a clean slate and
   a re-armed safety net.
4. **Recovery defeated.** On warm boot LC finds the CDS already exists, restores
   ART, emits `LC_CDS_RESTORED_INF_EID`, and AP0 comes back PASSIVE. The
   battery-low SAFE protection is still disarmed; the reboot did not help.
5. **Bound.** A second processor reset trips the
   `CFE_PLATFORM_ES_MAX_PROCESSOR_RESETS = 2` limit; cFE forces a power-on reset,
   the CDS is wiped, LC reloads defaults, and AP0 returns to ACTIVE. A power-on
   reset (or an explicit AP re-arm) is the actual recovery.

Code/config: `fsw/apps/lc/fsw/inc/lc_internal_cfg.h` (`LC_SAVE_TO_CDS` enabled);
`fsw/apps/lc/fsw/src/lc_app.c:733-856` (CDS register/restore),
`lc_utils.c:108-158` (CDS update); `cfg/nos3_defs/tables/lc_def_adt.c:180-189`
(AP0 latch); reset semantics `fsw/cfe/modules/es/fsw/src/cfe_es_api.c`,
`cpu1_platform_cfg.h:1211`; delivery `fsw/apps/noisy_app/fsw/src/noisy_app.c`
(`0x06`).

### 3.2 Attack tree

```
GOAL: make a transient spoof's effect survive the operator's recovery reboot
|
+-- AND latch AP0 to PASSIVE
|     +-- EPS spoof drives WP0 < 14800 for 3 samples (eps-spoof doc 3.1)
|     +-- AP0 fires RTS 4 and latches PASSIVE (2.2)
|
+-- AND have LC persist that state to CDS
|     +-- LC_SAVE_TO_CDS enabled (2.1)                  [enabling, config]
|     +-- LC_STATE_WHEN_CDS_RESTORED = FROM_CDS (2.1)   [enabling, config]
|     +-- no integrity/authenticity check on CDS (2.1)
|
+-- AND the operator recovers with a PROCESSOR reset
|     +-- gentler reset is the natural first response (2.3)
|     +-- CDS preserved across processor reset           [enabling]
|
+-- THEN AP0 restored PASSIVE; safety monitor stays disarmed (2.2, 2.3)
      +-- PER-0002 persistence; IMP-0002 disruption of recovery
      +-- bounded by MAX_PROCESSOR_RESETS=2 -> power-on reset wipes CDS
```

### 3.3 STRIDE x trust boundary summary

| Boundary | S | T | R | I | D | E |
|---|---|---|---|---|---|---|
| TB-2 SB/triggers | spoof EPS HK | latch AP0 PASSIVE (2.2) | - | - | disarm safety AP | spoof -> AP state |
| TB-5 reset/persistence | - | poison CDS-restored ART (2.1) | recovery looks ok but isn't (2.3) | - | defeat reboot recovery (2.3) | persisted AP state survives reset |

### 3.4 SPARTA technique mapping

| SPARTA technique | Where in this PoC |
|---|---|
| IA-0001 Compromise Supply Chain | `noisy_app` delivery (2.4) |
| PER-0002 Backdoor / persistence | attacker state survives warm reset via CDS (2.1, 2.3) |
| EX-0012 Modify On-Board Values | persisted actionpoint `CurrentState` (2.1, 2.2) |
| IMP-0002 Disruption | safety monitor disarmed across recovery (2.2, 2.3) |

> Technique IDs are per the SPARTA matrix; sub-technique numbers should be
> cross-checked against the matrix version cited in the thesis. (SPARTA has no
> exact "persist via data store" leaf; PER-0002 is used as the closest persistence
> mapping for restored on-board state.)

---

## 4. Detection (ELK / Kibana, index `nos3-telemetry*`)

| Signal | KQL |
|---|---|
| LC restored state from CDS on warm boot | tag `lc_cds_restored` or `message:"restored from Critical Data Store"` |
| AP0 latched PASSIVE before the reset | `message:"Batt volt critical: SAFE"` then `message:"AP failed while passive"` |
| Reset count climbing | `hs_resets_performed` trend (HS HK) |
| Persistence tell: restore + still-PASSIVE AP0 after reboot | `lc_cds_restored` co-occurring with no `AP state change ... to PASS: AP = 0` afterwards |
| Bound reached (escalation) | cFE EVS `POWER ON RESET due to max proc resets` |

Detection insight: a healthy warm boot of a freshly-recovered vehicle should
re-arm AP0 (load defaults, AP0 ACTIVE). Seeing `lc_cds_restored` instead, with
AP0 never transitioning back to ACTIVE/PASS, means the reset restored prior state
rather than a clean baseline. Correlating `lc_cds_restored` with the earlier
spoof signature (the EPS-spoof doc's section 4) ties the persisted state to the
attack. The `lc_cds_restored` tag is emitted by `nos3/elk/logstash.conf`.

---

## 5. Mitigations

1. **Bind CDS to a trusted baseline.** Add a CRC/authenticator over each LC CDS
   block (HS already does an XOR-inverse integrity check on its CDS); reject and
   reload defaults on mismatch. Closes the verbatim-restore in 2.1.
2. **Restore safety actionpoints to ACTIVE, not FROM_CDS.** Set
   `LC_STATE_WHEN_CDS_RESTORED` to `LC_STATE_ACTIVE`, or special-case critical
   APs to re-arm on every reset, so a reboot always restores the safety net.
3. **Require ground confirmation to honour a restored safe latch.** On
   `LC_CDS_RESTORED`, alarm and hold critical APs ACTIVE pending operator
   acknowledgement rather than silently trusting restored PASSIVE state.
4. **Alarm on CDS restore.** Treat `lc_cds_restored` after a commanded reset as a
   security-relevant event in ELK, especially when correlated with a prior spoof
   signature.
5. **Prefer power-on reset for anomaly recovery in procedure.** Document that a
   processor reset preserves potentially-poisoned state; recovery from a suspected
   attack should use a power-on reset (or explicit AP re-arm).
6. **Supply-chain controls.** As in the companion docs (IA-0001): reviewed app
   manifest, flag attack-sim fixtures.

> Configuration trade-off: persisting LC state across resets preserves genuine
> monitoring continuity (the reason missions enable it). The mitigations keep
> that benefit while removing the verbatim-trust that makes it a persistence
> vector.

---

## Appendix A - Captured evidence (live run 2026-06-04)

Run on the rebuilt stack. The CDS-persistence enablement and the processor-reset
path are confirmed live; the full warm-boot restore observation is blocked by a
NOS Engine harness limitation (documented below).

**A.1 LC CDS persistence is now active (the config change, verified at boot).**
At cold start LC reports it saved state to the Critical Data Store, rather than
the upstream "CDS disabled" message:
```
42/1/LC 22: Default state loaded and written to CDS, activity mask = 0x00333BBB
42/1/LC  2: LC Initialized. Version 7.0.0.0
```
With `LC_SAVE_TO_CDS` undefined (upstream default) this line would instead be
`LC use of Critical Data Store disabled` (event 23). So WRT/ART/AppData are now
being written to CDS every maintenance cycle.

**A.2 AP0 latched after firing RTS 4.** The spoof tripped AP0 -> RTS 4 (see
`eps-spoof-comms-downgrade.md` A.2), after which AP0 is PASSIVE and that state is
the value LC persists to `LC_CDS_ART`.

**A.3 Processor reset path executes and enters warm-boot CDS init.** A commanded
`CFE_ES_RESTART_CC` (RestartType = 1, PROCESSOR; raw CCSDS to CI_LAB) ran:
```
CFE_ES_ResetCFE: PROCESSOR RESET called from CFE_ES_ResetCFE (Commanded).
CFE_PSP: Exiting cFE with PROCESSOR Reset status.
[fsw_respawn] FSW exited with code 0. Restarting in 3s...
CFE_PSP: Starting the cFE with a PROCESSOR reset.
CFE_ES_CreateObjects: Calling CFE_ES_CDSEarlyInit
```

**A.4 Warm-boot restore observation blocked (harness limitation).** The re-exec'd
cFS stalls in core early-init (`CFE_*_EarlyInit`) and never reaches LC's CDS
restore. The cause is the NOS Engine link: cFS time/bus are locked to the NOS
Engine ticker, and an in-place processor reset while the sim bus stays up does
not re-establish the connection (`NosEngine.Common ... failed to route confirmed
message`), so the warm boot hangs before app init. This is a property of the
simulator harness, not of the CDS change. Capturing the
`Previous state restored from Critical Data Store` event (tag `lc_cds_restored`)
and the post-reset AP0=PASSIVE state therefore requires a reset method that
preserves the NOS Engine connection while keeping the CDS shared-memory segment.
Options for a follow-up capture:
- Coordinate a stack restart that re-establishes the NOS Engine link for the FSW
  container while leaving the CDS POSIX shmem segment intact.
- Or exercise the LC CDS restore in a cFS unit/integration harness
  (`make test-fsw`) where the bus is mocked, asserting AP0 `CurrentState` is
  restored from CDS rather than reset to ACTIVE.

**A.5 Bound (by design, not yet exercised live).** After
`CFE_PLATFORM_ES_MAX_PROCESSOR_RESETS = 2` cFE escalates to a power-on reset that
wipes the CDS and reloads defaults (AP0 ACTIVE), ending the persistence. This is
the documented recovery and the upper bound on the attack.

## Appendix C - Reversibility

- `LC_SAVE_TO_CDS`: re-comment the block in
  `fsw/apps/lc/fsw/inc/lc_internal_cfg.h` to return to the upstream default (LC
  does a power-on init on every reset; no persistence).
- `noisy_app` toggles: `targets.cmake` (commented = excluded),
  `cpu1_cfe_es_startup.scr` (`OFF_APP`).
- ELK: the `lc_cds_restored` tag in `nos3/elk/logstash.conf` is additive and
  benign.
- The `LC_SAVE_TO_CDS` enable is recorded in `debug/journal.md`. No
  DO-NOT-REVERT guard-rail entry was touched.
