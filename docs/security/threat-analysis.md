# Cyber Threat Analysis - DTU NOS3 (cFS / Draco)

> **DTU secured-fork note.** Ported from the Draco baseline (commit `dd1d2cbb`). In this repo the LC battery autonomy is WP #27 (`< 24240` mV) -> AP #27 -> RTS 27 (not Draco's WP0/AP0 SAFE chain), and the DEBUG-path injection means CryptoLib does not gate the attack. Scenarios `0x02/0x04/0x06/0x08` are in scope; IMU (`0x0A`) and CDS persistence are Draco-only. See [`00-dtu-secured-fork-notes.md`](00-dtu-secured-fork-notes.md).


Command-chain security analysis of the DTU NOS3 research fork, centred on a
demonstrated proof-of-concept: a **covert opcode byte piggybacked onto the
unused tail of a legitimate command**, sent from the ground station and acted on
by a co-resident malicious application (`noisy_app`).

- Subject: DTU NOS3 fork, NASA Core Flight System (cFS) 6.7.99 / Draco, OSAL
  5.0.0, PSP 1.4.0; ground software COSMOS 4.5; spacecraft profile
  `sc-mission-config.xml`.
- Methodology: **STRIDE** per trust boundary, mapped to the **SPARTA** (Space
  Attack Research and Tactic Analysis) matrix from The Aerospace Corporation.
- Status of the prior research notes (2026-03-31): re-validated as a
  status table in Appendix B; this document supersedes it for the command-chain
  threat class.

> Scope note: this is authorised defensive research on a simulator. `noisy_app`
> is an intentional, clearly-reversible attack-simulation fixture, OFF in the
> mission build (see Appendix C).

> Companion analyses (the autonomous-effect half of this threat class):
> `docs/security/eps-spoof-comms-downgrade.md` refines the SAFE comms effect into
> a realistic throughput downgrade (the old hard blackout was removed: AP0 now
> drives RTS 4 -> MGR SAFE + TO_LAB `SET_SAFE_TLM` beacon, uplink left alive).
> `docs/security/cds-persistence.md` shows how enabling LC Critical Data Store
> persistence makes a spoof-latched actionpoint survive the operator's recovery
> reboot. Note: the RTS 001 references in section 3.1 and Appendix A.6 below are
> from an earlier run, before AP0 was retargeted from RTS 1 to RTS 4
> (`lc_def_adt.c:180-189`).

---

## 1. System decomposition and trust boundaries

```
        GROUND SEGMENT                |              SPACE SEGMENT (cFS)
                                      |
 +---------------------+   UDP 5012   |  +-----------+        Software Bus
 | COSMOS GS           |  (TC, plain) |  | CI_LAB    |  (no sender auth, multicast)
 | Command Sender,     |------------->|  | ingest    |---+------------------------+
 | background tasks    |   TB-1       |  +-----------+   |                        |
 +---------------------+              |   length-bounds  v                        v
                                      |   only      +---------+   +---------+  +---------+
 +---------------------+   UDP 5013   |             | target  |   | LC      |  | noisy_  |
 | passive_listener,   |<-------------|  +--------+ | app     |   | watch/  |  | app     |
 | tcpdump, ELK        |  (TM)        |  | TO_LAB |<-| (HK/TM) |   | action  |  | (sniffer|
 +---------------------+   TB-3       |  +--------+ +---------+   +----+----+  | +switch)|
                                      |                                |        +----+----+
                                      |        SC (RTS/ATS) <-----------+             |
                                      |        triggers, MGR mode, sims  <------------+
                                      |   TB-2 (autonomous trigger plane) TB-4 (sim/42)
```

Trust boundaries:

- **TB-1 Ground -> FSW uplink (UDP 5012, CI_LAB).** The primary command ingress.
  In the active configuration commands cross this boundary **unauthenticated**
  (see 2.1).
- **TB-2 Software Bus and the autonomous trigger plane.** cFE SB multicasts every
  message to all subscribers with **no sender identity check**; LC watchpoints ->
  actionpoints -> SC RTS sequences and MGR mode logic act on telemetry values
  without provenance.
- **TB-3 FSW -> Ground downlink (TO_LAB / UDP 5013).** Telemetry egress; the ELK
  pipeline and passive listener observe here.
- **TB-4 Simulator / 42 dynamics <-> sims.** Sensor/actuator data feeding FSW.

---

## 2. Findings (enabling weaknesses)

### 2.1 Unauthenticated, unvalidated command ingress (TB-1)

The active uplink app is **CI_LAB** (`fsw/apps/ci_lab/fsw/src/ci_lab_app.c`). On
each datagram (UDP 5012) it checks only that the size is between
`sizeof(CFE_MSG_CommandHeader_t)` and `CI_LAB_MAX_INGEST`, then calls
`CFE_SB_TransmitBuffer` straight onto the Software Bus (`ci_lab_app.c:329-376`).
There is **no checksum verification** (`CFE_MSG_ValidateChecksum` exists but is
never called on ingest), **no MID allow-list**, and **no cryptographic
authentication** - CryptoLib is compiled but not on the CI_LAB path. An attacker
who can reach UDP 5012 needs only a well-formed CCSDS header to get a command
executed.

STRIDE: Spoofing, Tampering. SPARTA: enables Initial Access (rogue command
ingress) and Execution.

### 2.2 Software Bus has no sender authentication; it multicasts (TB-2)

`CFE_SB_TransmitMsg`/`TransmitBuffer` validate MsgId and size only and broadcast
to every subscriber of that MID. The CCSDS primary header carries an APID
(MsgId), not a source identity. Consequences:
- Any app that can transmit can impersonate any other app's telemetry MID.
- Any app may **subscribe to another app's command MID** and observe its traffic.
This is the structural property the PoC exploits.

STRIDE: Spoofing, Information Disclosure. SPARTA: **EX-0014 Spoofing** (bus
traffic / sensor data), lateral observation on the bus.

### 2.3 Endpoint length checks do not protect the bus (TB-2)

Core apps (e.g. CI_LAB) call `CFE_MSG_VerifyCmdLength` and reject malformed
commands (`ci_lab_app.c:384-410`: length mismatch -> `CI_LAB_LEN_ERR_EID` event +
`CommandErrorCounter++`). That defence is **per-endpoint**: it stops the
legitimate target from acting, but does not prevent a co-subscribed sniffer from
reading the same message off the bus. Length validation is necessary but not
sufficient against a bus-resident adversary.

### 2.4 Autonomous trigger plane is driven by unauthenticated telemetry (TB-2)

The rewritten LC/SC machinery makes a single telemetry value autonomously drive
command sequences:
- `lc_def_wdt.c` WP0 watches `GENERIC_EPS_HK` BatteryVoltage (watchpoint offset 20);
  `< 14800 mV` -> actionpoint AP0 -> **RTS001 (enter SAFE)**.
- WP6 watches `GENERIC_GNSS_HK` `InDenmarkBox` (offset 55); `== 1` -> AP3 ->
  **RTS002 (SCIENCE: enable recording + CFDP downlink + rearm AP4)**.
Because these inputs are unauthenticated bus telemetry (2.2), a single **spoofed
HK packet** can drive the SAFE or SCIENCE command chain off-nominal.

STRIDE: Tampering, Elevation of Privilege. SPARTA: **EX-0014 Spoofing**,
**EX-0012 Modify On-Board Values** (scheduling/actionpoint state).

### 2.5 Supply-chain: a co-resident malicious app shipped in the build

`noisy_app` demonstrates that an application delivered in the flight build, even
one that looks like an empty skeleton, can lie dormant and act on a covert
trigger. This is the project's namesake concern: trust in the build/app supply
chain.

STRIDE: Tampering (supply chain), Elevation of Privilege. SPARTA: **IA-0001
Compromise Supply Chain** (.01 Software Dependencies & Development Tools),
**PER-0002 Backdoor** (.02 Software).

---

## 3. Proof of concept - covert-opcode piggyback

### 3.1 Mechanism

1. **Carrier:** a `CI_LAB` NOOP (`CI_LAB_CMD_MID 0x18E0`, `CI_LAB_NOOP_CC 0`). A
   normal NOOP is header-only (8 bytes).
2. **Piggyback:** the operator (here, the attacker on the GS) sends the NOOP with
   **one extra trailing byte** in the otherwise-unused payload tail (9 bytes,
   `CCSDS_LENGTH = 2`). The trailing byte is a covert opcode.
3. **Ingress (TB-1):** CI_LAB accepts the 9-byte datagram (size within bounds, no
   checksum/auth) and transmits it to the Software Bus.
4. **Endpoint rejects, sniffer reads (TB-2):** CI_LAB's own command handler runs
   `VerifyCmdLength` (expected 8, actual 9) -> length-error event +
   `CommandErrorCounter++`. **`noisy_app`, co-subscribed to `0x18E0`, receives the
   same message, sees FC==NOOP and size > header, reads the trailing byte and
   dispatches a scenario.**
5. **Scenario `switch`:**
   - `0x00` DORMANT - nothing (stealth).
   - `0x02` EPS_SPOOF - forge one `GENERIC_EPS_HK` (`0x091A`) with
     BatteryVoltage = 10000 mV (< WP0 14800) -> can drive **RTS001 (SAFE)**.
     Links the injection to the autonomous trigger plane (2.4).
   - `0x04` SB_BURST - a hard-capped 64-packet Software Bus burst (pool/pipe
     stress), deliberately not the legacy 32k-packet storm.

Code: `fsw/apps/noisy_app/fsw/src/noisy_app.c`; GS command
`gsw/cosmos/config/targets/CI_DEBUG/cmd_tlm/PIGGYBACK_POC.txt`; run guide
`nos3/poc/piggyback_noisy/run_poc.md`; headless driver
`nos3/poc/piggyback_noisy/drive_poc.py`; step-by-step operator walkthrough
(COSMOS GUI, Kibana dashboard, pcap capture/analysis, save/replay)
`docs/security/poc-manual-runbook.md`.

### 3.2 Attack tree

```
GOAL: cause an unauthorised on-board effect via a benign-looking GS command
|
+-- AND reach the command uplink (UDP 5012)            [TB-1]
|     +-- access to the GS / ground network
|
+-- AND get a crafted command onto the Software Bus
|     +-- no auth/checksum on CI_LAB ingest (2.1)      [enabling]
|
+-- AND have a bus-resident actor read the covert byte
|     +-- malicious app in the build (2.5)             [SPARTA IA-0001 / PER-0002]
|     +-- SB multicast, no sender auth (2.2)           [enabling]
|     +-- endpoint length check does not stop it (2.3)
|
+-- THEN effect:
      +-- EX-0014 Spoof EPS HK -> EX-0012 drive RTS001 SAFE (2.4)
      +-- EX-0013 Flooding / IMP-0003 Denial (SB burst)
```

### 3.3 STRIDE x trust boundary summary

| Boundary | S | T | R | I | D | E |
|---|---|---|---|---|---|---|
| TB-1 uplink | forge cmd (2.1) | piggyback byte (3.1) | no cmd echo (no repudiation trail) | - | flood ingress | exec via injection |
| TB-2 SB/triggers | spoof HK MID (2.2) | modify AP/RTS state (2.4) | no sender log | sniff carrier (2.2) | SB pool burst (3.1) | spoof->autonomous RTS (2.4) |
| TB-3 downlink | - | - | partial: EVS captures effects | covert effects observable in ELK | - | - |
| TB-4 sims | spoof sensor | - | - | - | - | - |

### 3.4 SPARTA technique mapping

| SPARTA technique | Where in this PoC |
|---|---|
| IA-0001 Compromise Supply Chain (.01 SW deps & dev tools) | malicious `noisy_app` in the build (2.5) |
| PER-0002 Backdoor (.02 Software) | dormant co-subscribed listener (2.5) |
| EX-0014 Spoofing (bus traffic / sensor data) | forged EPS HK; SB impersonation (2.2, 3.1) |
| EX-0012 Modify On-Board Values (scheduling/AP tables) | spoof -> LC AP/RTS state change (2.4) |
| EX-0013 Flooding | capped SB burst (3.1, `0x04`) |
| EX-0001 Replay | a recorded NOOP+tail can be replayed (no anti-replay on CI_LAB) |
| IMP-0002 Disruption / IMP-0003 Denial | SAFE-mode forcing; SB pool stress |

> Technique IDs are per the SPARTA matrix (sparta.aerospace.org); sub-technique
> numbers should be cross-checked against the matrix version cited in the thesis.

---

## 4. Detection (ELK / Kibana, index `nos3-telemetry*`)

| Signal | KQL |
|---|---|
| Covert opcode read | `message:"piggyback opcode"` |
| Spoofed EPS HK effect | `message:"EPS HK SPOOF"` or `eps_battery_mv < 14800` |
| Capped SB burst | `message:"SB BURST sent"` |
| Endpoint length rejections (the tell-tale of a piggyback) | `message:"Invalid msg length" and message:"0x18E0"` |
| Rising CI_LAB command-error counter | CI_LAB HK `CommandErrorCounter` trend |
| Unexpected SAFE transition | LC actionpoint / SC RTS001 EVS during a non-anomalous power state |

Detection insight: the piggyback is **loud at the endpoint** (CI_LAB length
error) even though it is **silent at the covert reader**. A bus/EVS monitor that
correlates "length-error on 0x18E0" with "EPS HK anomaly within N ticks" detects
the chain that per-endpoint length checking alone misses.

These detections are wired into a dedicated Kibana dashboard, **NOS3 Security -
Piggyback PoC** (`nos3-piggyback-poc`, built by `elk/build_kibana_dashboards.py`,
auto-created on `make launch`): metric tiles + EPS-battery-vs-threshold line +
attack-chain EVS-by-app bar + a per-detection KQL filter bar. See
`docs/security/poc-manual-runbook.md` section 3.

---

## 5. Mitigations

1. **Authenticate the uplink (TB-1).** Put CryptoLib (SDLS/TC authentication +
   anti-replay) on the active CI/TO path instead of CI_LAB plain UDP; reject
   unauthenticated TCs. Closes 2.1 and EX-0001.
2. **Verify checksums on ingest.** Call `CFE_MSG_ValidateChecksum` before
   `TransmitBuffer`.
3. **Enforce exact length and reject extra payload at ingress**, not only at the
   endpoint, so over-length frames never reach the bus (mitigates 2.3 / 3.1).
4. **Constrain SB subscriptions.** Treat the subscriber table as security-
   relevant: an app subscribing to another app's command MID should be a
   build-time review gate (mitigates 2.2 / 2.5).
5. **Provenance on autonomous triggers (TB-2).** LC/RTS decisions on critical
   values (battery, geofence) should require corroboration (multiple sources /
   rate-of-change sanity) rather than a single unauthenticated HK field (2.4).
6. **Supply-chain controls.** App allow-listing in `targets.cmake` /
   startup.scr as a reviewed manifest; flag attack-sim fixtures; reproducible
   builds (IA-0001).
7. **Deploy the correlation detection in section 4** as a standing ELK rule.

> Escalations (design): the shipped PoC is deliberately bounded (one spoof, a
> 64-packet capped burst). `docs/security/noisy_app-escalation-design.md`
> analyses two worst-case escalations with code - a *reactive* EPS override that
> defeats the real sensor by shadowing every genuine packet (so the spoof always
> wins LC's sample), and a *full SB denial* that exhausts the 512 KB pool and
> floods critical apps' command pipes so comms stop - each paired with its
> detection and mitigation. Those modes are gated off by default.

---

## Appendix A - Captured evidence (live run 2026-06-03)

Live end-to-end run on the rebuilt stack (FSW + 28 sim/GSW containers + ELK).
Commands were delivered to CI_LAB UDP 5012 as raw CCSDS exactly as COSMOS would
emit them, using `nos3/poc/piggyback_noisy/drive_poc.py` (a COSMOS-GUI-free
driver). All times UTC.

**A.1 Malicious app loaded at boot (EVS).**
```
CFE_ES_ParseFileEntry: Loading file: /cf/noisy_app.so, APP: NOISY_APP
42/1/NOISY_APP 1: NOISY_APP: Initialized. CMD MID 0x18F2, sniffing carrier 0x18E0.
```

**A.2 Control (negative test).** A normal 8-byte CI_LAB NOOP
(`hex 18e0c00000010000`) produced no `noisy_app` event and no CI_LAB length
error: the sniffer stays silent on legitimate traffic. (Confirmed by absence of
any NOISY_APP / length-error EVS between the enable and the first piggyback.)

**A.3 Piggyback core demonstration (dual path on one packet).** The 9-byte
over-length NOOP (`hex 18e0c0000002000002`, trailing opcode `0x02`) is rejected
by the legitimate endpoint *and* read by the co-resident sniffer:
```
42/1/CI_LAB_APP 16: Invalid msg length: ID = 0x18E0,  CC = 0, Len = 9, Expected = 8
42/1/NOISY_APP  2: NOISY_APP: piggyback opcode 0x02 on MID 0x18E0 (len 9, count 4).
42/1/NOISY_APP  3: NOISY_APP: EPS HK SPOOF sent on 0x091A (BatteryVoltage=10000 mV).
```
This is 2.3 made concrete: the endpoint defence (length check) fires while the
bus-resident reader still acts.

**A.4 SB burst (opcode 0x04).**
```
42/1/NOISY_APP 2: NOISY_APP: piggyback opcode 0x04 on MID 0x18E0 (len 9, count 5).
42/1/NOISY_APP 4: NOISY_APP: SB BURST sent 64 packets on 0x08F2 (capped).
```
Hard-capped at 64 with a yield every 8 packets; the simulator stayed alive.

**A.5 Spoofed telemetry on the bus (downlinked + decoded).** The forged
`GENERIC_EPS_HK` (MID 0x091A, BatteryVoltage = 10000 mV) appears in
`omni_logs/tlm_hk_decoded.log`:
```
{"app":"EPS","msg_id":"0x091A","sequence":221,"battery_mv":10000,"solar_array_mv":0}
```
This is the only sub-14800 battery value in the run (real EPS HK reads ~25189
mV), so the spoof is unambiguous in telemetry.

**A.6 Injection-to-trigger link (EX-0014 -> EX-0012 -> autonomous SAFE).**
The autonomous chain (WP0 battery < 14800 -> AP0, MaxFailsBeforeRTS=3 -> RTS 1
SAFE) is genuinely exercised. It fired first at cold start on the real low
battery:
```
42/1/LC 1000: Batt volt critical: SAFE : AP = 0, FailCount = 3, RTS = 1
42/1/SC   86: RTS 001 Execution Completed
```
LC then latches AP0 to PASSIVE (one-shot), so a single later spoof logs
`AP failed while passive`. Driving the spoof at ~8 Hz held WP0 in FAIL and the
passive fail counter climbed to 356, proving the spoof reaches the same
watchpoint LC trusts (`lc_def_wdt.c:123-129`, WatchpointOffset 16 matches the
forged field). Re-arming AP0 (a single `LC_SET_AP_STATE` command, AP0 ->
ACTIVE) and continuing the spoof re-fired the SAFE RTS, now attributable to the
injection:
```
42/1/LC   33: Set AP state command: AP = 0, New state = 1
42/1/LC 1000: Batt volt critical: SAFE : AP = 0, FailCount = 356, RTS = 1
42/1/SC   73: RTS Number 001 Started
```
Finding: the only thing standing between a single spoofed HK field and repeated
autonomous SAFE-mode forcing is LC's one-shot passive latch - not any
authentication or provenance check (reinforces 2.4 and mitigation 5).

**A.7 Wire capture (pcap).** The passive tcpdump sidecar
(`attack_logs/tcpdump/sc01_5012_*.pcap`) recorded the uplink. UDP payload-length
distribution on 5012: **104 x len 9** (piggyback NOOPs), 2 x len 8 (control
NOOPs), 9 x len 28 (TO_LAB enable), 1 x len 12 (the AP re-arm). The 9-byte
datagram tail carries the covert opcode in the clear, e.g. trailing bytes
`...0002 0000 02` (len field 2, FC 0, checksum 0, opcode 0x02) and
`...0002 0000 04` (opcode 0x04).

**A.8 ELK / Kibana detections** (index `nos3-telemetry-2026.06.03`; 15
dashboards rebuilt during launch; counts via `match_phrase`, the Kibana KQL
equivalent):

| Detection (KQL `message:"..."`) | Hits |
|---|---|
| `piggyback opcode 0x02` | 48 |
| `piggyback opcode 0x04` | 1 |
| `EPS HK SPOOF sent` | 48 |
| `SB BURST sent` | 1 |
| `Invalid msg length` AND `0x18E0` (the piggyback tell-tale) | 95 |
| `Batt volt critical: SAFE` | 3 |
| `RTS Number 001 Started` / `RTS 001 Execution Completed` | 3 / 3 |
| `battery_mv < 14800` (all at the spoofed value 10000) | 101 |

Note on counts: 101 spoofed HK packets reached the bus and downlinked
(`battery_mv:10000` x101), but only ~48 `EPS HK SPOOF` EVS events are indexed -
cFS EVS per-event squelching suppressed the rest during the high-rate bursts.
The correlation rule from section 4 (length-error on 0x18E0 co-occurring with an
EPS-HK anomaly) is satisfied: 95 length-errors on 0x18E0 alongside the spoofed
battery records.

## Appendix B - Prior-findings re-validation (2026-03-31 -> 2026-06-03)

The 2026-03-31 static audit (prior research notes, `phase3_*`, `phase4_*`)
is re-validated against the current Draco tree by a fresh
source read. Status legend: Fixed / Still-valid / Latent (present but not
reachable in the active build) / Patched (was never open in this tree).

| ID | Finding | Original (2026-03-31) | Current status | Evidence (current source) |
|---|---|---|---|---|
| F1 | OSAL path traversal (CVE-2025-25371) | VULNERABLE - `OS_TranslatePath` did not strip `..` | **Fixed** | `osal/src/os/shared/src/osapi-filesys.c:709-713` now does `result = strstr(VirtualPath, "..")` and returns `OS_FS_ERR_PATH_INVALID` before any path assembly. The MM file-op surfaces (LOAD/DUMP/SYM_TBL) inherit the fix. |
| F3 | SYN heap leak / NULL-malloc (H3, H4) | Still-valid in source | **Latent (not in build)** | Source unchanged: `components/syn/fsw/cfs/src/syn_app.c:189` still `malloc` with no NULL check; `:366-367` still leaks per RESET; `:90` `//SYN_Disable()` still commented. BUT `cfg/spacecraft/sc-mission-config.xml` sets `<syn><enable>false</enable>`; `syn.so` is not built and the SYN app does not load. Note: the build-copy `cpu1_cfe_es_startup.scr:68` still carries a `CFE_APP, syn` row and `targets.cmake` still lists `syn/fsw/cfs` - a stale-manifest smell (config disables the sim/app but the startup row was not flipped to `OFF_APP`). Harmless now (no `.so`), but it is exactly the supply-chain manifest hygiene called out in mitigation 6. |
| F8 | MD `PktOffset` dwell overrun | Still-valid | **Still-valid** | `apps/md/fsw/src/md_dwell_pkt.c:195` still `memcpy(...Payload.Data[TblPtr->PktOffset]...)` with no bounds check; `:199` still `TblPtr->PktOffset += NumBytes` unconditionally. Reachable via MD JAM with uplink access; needs `MD_INTERFACE_DWELL_TABLE_SIZE >= 26` and is bounded by the missing symbol allow-list (phase3 sec 4). |
| PSP | Non-volatile port casts (LICM) | Still-valid, latent at -O0 | **Still-valid (latent)** | `psp/fsw/modules/port_direct/cfe_psp_port_direct.c:63,117,178` (and the Write peers) still cast `cpuaddr` to plain `uint8/16/32 *`. `CMakeCache.txt` still `CMAKE_BUILD_TYPE=debug` (-O0), so the read-once LICM hazard does not activate in this build; it would at `-O2`/release. One-token fix per line (`volatile`). |
| - | CVE-2025-25373 (MM RCE) | Patched | **Patched (unchanged)** | `apps/mm/fsw/src/mm_utils.c:134,148,163,187,211` still gate every write path on `CFE_PSP_MemValidateRange`. |
| - | CVE-2025-25372 (MM DoS) | Patched | **Patched (unchanged)** | `apps/mm/fsw/src/mm_dispatch.c:41` `MM_VerifyCmdLength` still called for every command before dispatch. |

Takeaways: the one finding that was OPEN at the OS layer (F1 traversal) is now
closed. The memory-safety finding F8 and the compiler-latent PSP finding persist
unchanged. F3 is code-unchanged but not loaded in the mission build; the stale
`syn` startup-script / targets-manifest rows are a concrete instance of the
supply-chain manifest-hygiene gap (mitigation 6). The command-chain threat class
documented in sections 1-5 is independent of all of these and is the live focus
of this analysis.

## Appendix C - Reversibility

- `noisy_app` source: `nos3/fsw/apps/noisy_app/` (skeleton + listener).
- Enable toggles: `targets.cmake` (commented = excluded) and
  `cpu1_cfe_es_startup.scr` (`OFF_APP` = not loaded).
- GS command: delete `PIGGYBACK_POC.txt`.
- After reverting + rebuild, `noisy_app` is absent and the PoC command is a no-op.
- No DO-NOT-REVERT guard-rail entry was touched.
