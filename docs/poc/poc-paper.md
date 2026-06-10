# Supply-chain attack proof-of-concepts on a NOS3 / cFS (Linux / NOS-Engine) satellite

A self-contained technical paper covering every proof-of-concept (PoC) attack
scenario built on this testbed: how each one works in depth, where and why the
code is written the way it is, what was tested and found, the limitations, how to
build and run each PoC (including the headless path used during development), and
exactly how to verify each result (Kibana dashboards, KQL queries, log greps, and
pcap filters).

Everything here is authorised defensive research on a simulator. The malicious
application, `noisy_app`, is an intentional, clearly reversible attack-simulation
fixture that is OFF in the mission build (see section 12, Reversibility).

This document is self-contained: every mechanism, command, value, and
verification step is inlined below, so you can read it start to finish without
opening anything else. All concrete values (message IDs, offsets, thresholds,
buffer sizes, event strings) were verified by reading the actual source, not the
older prose notes. The SAFE response on this repo is both a spacecraft mode change
and a telemetry beacon downgrade: AP0 fires RTS 4, which runs `MGR SET_MODE SAFE`,
a DS recording-disable, and `TO_LAB SET_SAFE_TLM` (see section 3.1). Section 13
lists where each piece of code lives; Appendix A is the per-PoC file manifest.

Contents:

1. Introduction and threat model
2. Common machinery (the piggyback channel, the implant layout)
3. PoC 1: EPS spoof and persistent EPS override (autonomous SAFE mode)
4. PoC 2: Software Bus pool-lock flood (denial of service)
5. PoC 3: IMU bias injection over a file dead-drop covert channel
6. PoC 4: GNSS direct-memory overwrite (teleport and drift)
7. Running the PoCs (build, launch, headless drivers, COSMOS GUI)
8. Operational recovery (destructive scenarios, relay latency)
9. Verification reference (dashboards, KQL, truth-vs-bus, log greps, pcap filters)
10. Per-PoC verification playbook
11. Reversibility
12. References
13. Appendix A: per-PoC file manifest

---

## 1. Introduction and threat model

### 1.1 The system under test

- Subject: DTU NOS3 research fork, NASA Core Flight System (cFS) 6.7.99,
  OSAL 5.0.0, PSP 1.4.0; ground software COSMOS 4.5; spacecraft profile
  `cfg/spacecraft/sc-mission-config.xml`.
- Flight target: `amd64-nos3` (Linux). cFS apps are loaded as per-app `.so`
  modules in one process; every app is a pthread sharing one address space.
- Methodology for the analysis: STRIDE per trust boundary, mapped to the SPARTA
  (Space Attack Research and Tactic Analysis) matrix from The Aerospace
  Corporation.

### 1.2 Trust boundaries

```
        GROUND SEGMENT                |              SPACE SEGMENT (cFS)
                                      |
 +---------------------+   UDP 5012   |  +-----------+        Software Bus
 | COSMOS GS           |  (TC, plain) |  | CI_LAB    |  (no sender auth, multicast)
 | Command Sender,     |------------->|  | ingest    |---+------------------------+
 | drivers             |   TB-1       |  +-----------+   |                        |
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

- TB-1 Ground to FSW uplink (UDP 5012, CI_LAB). The primary command ingress.
  Commands cross unauthenticated.
- TB-2 Software Bus and the autonomous trigger plane. cFE SB multicasts every
  message to all subscribers with no sender identity check; LC watchpoints feed
  actionpoints, which feed SC RTS sequences and MGR mode logic, all acting on
  telemetry values without provenance.
- TB-3 FSW to Ground downlink (TO_LAB / UDP 5013). The ELK pipeline and the
  passive listener observe here.
- TB-4 Simulator / 42 dynamics to sims. Sensor and actuator data feeding FSW.

### 1.3 Enabling weaknesses

These are the structural properties every PoC builds on:

1. Unauthenticated, unvalidated command ingress (TB-1). CI_LAB
   (`fsw/apps/ci_lab/fsw/src/ci_lab_app.c`) accepts any UDP 5012 datagram whose
   size is between `sizeof(CFE_MSG_CommandHeader_t)` and `CI_LAB_MAX_INGEST`,
   then transmits it straight onto the Software Bus. No checksum verification, no
   MID allow-list, no cryptographic authentication. A well-formed CCSDS header is
   enough to get a command executed.
2. The Software Bus has no sender authentication and it multicasts (TB-2).
   `CFE_SB_TransmitMsg` validates MsgId and size only, then broadcasts to every
   subscriber of that MID. Any app can publish another app's telemetry MID, and
   any app may subscribe to another app's command MID and read its traffic.
3. Endpoint length checks do not protect the bus (TB-2). CI_LAB calls
   `CFE_MSG_VerifyCmdLength` and rejects malformed commands, but that defence is
   per-endpoint: it stops the legitimate target from acting, it does not stop a
   co-subscribed sniffer reading the same message off the bus.
4. The autonomous trigger plane is driven by unauthenticated telemetry (TB-2).
   `lc_def_wdt.c` WP0 watches `GENERIC_EPS_HK` BatteryVoltage (wire offset 16);
   below 14800 mV drives actionpoint AP0, which fires a SAFE RTS. One spoofed HK
   field can drive the SAFE command chain.
5. Single shared address space, no inter-app memory protection (the off-bus
   enabler). Every app is a pthread in one process (`os-impl-tasks.c`); a pointer
   in one app dereferences anywhere in the shared address space, and the loaded
   module's symbols are resolvable at runtime (`dlopen`/`dlsym`, see PoC 4). The
   volatile RAM disk at `/ram` has no per-app file ownership, so any app can read
   or write any file.
6. Supply chain: a co-resident malicious app shipped in the build. `noisy_app`
   demonstrates that an application delivered in the flight build, even one that
   looks like an empty skeleton, can lie dormant and act on a covert trigger.
   SPARTA IA-0001 Compromise Supply Chain, PER-0002 Backdoor.

### 1.4 On-bus versus off-bus attacks

The spine of this paper is the distinction between attacks that act *on* the
Software Bus (a Subscribe or a Transmit) and attacks that route *around* the bus
entirely (a direct memory write, a file dead-drop):

| PoC | Primitive | On the Software Bus? |
|-----|-----------|----------------------|
| EPS spoof / override | forge `0x091A` publish + subscribe to carrier | Yes |
| SB pool-lock flood | self-subscribe to `0x08F2` to retain parked buffers | Yes |
| IMU bias dead-drop | file write/read on `/ram` | No (off-bus) |
| GNSS memory overwrite | direct store into peer globals | No (off-bus) |

The on-bus attacks operate where a Software Bus mediation control could observe
and gate them; the off-bus attacks do not, so a bus-level access control is blind
to them. The off-bus PoCs exist to draw that boundary: a message-bus ACL is
necessary but not sufficient against an in-process implant, because without memory
and filesystem isolation an implant simply acts off the bus. CryptoLib (CCSDS
SDLS) also exists in this repo, but it protects the ground RF link, not the
intra-spacecraft bus, so it is not the relevant control against a co-resident app.

---

## 2. Common machinery

Read this once; every PoC reuses it.

### 2.1 The covert-opcode piggyback delivery channel

All scenarios are dispatched by a single trailing "covert opcode" byte. The
delivery mechanism:

1. Carrier: a CI_LAB NOOP (`CI_LAB_CMD_MID 0x18E0`, `CI_LAB_NOOP_CC 0`). A normal
   NOOP is header-only (8 bytes).
2. Piggyback: the attacker sends the NOOP with one extra trailing byte in the
   otherwise-unused payload tail (9 bytes, `CCSDS_LENGTH = 2`). The trailing byte
   is the covert opcode.
3. Ingress (TB-1): CI_LAB accepts the 9-byte datagram (size within bounds, no
   checksum or auth) and transmits it to the Software Bus.
4. Endpoint rejects, sniffer reads (TB-2): CI_LAB's own command handler runs
   `VerifyCmdLength` (expected 8, actual 9), emits a length-error event, and
   bumps its `CommandErrorCounter`. `noisy_app`, co-subscribed to `0x18E0`,
   receives the same message, sees FC equal to NOOP and size greater than the
   header, reads the trailing byte, and dispatches a scenario.

This is the "loud at the endpoint, silent at the covert reader" property: the
piggyback produces a visible CI_LAB length error on `0x18E0` even though the
covert reader acts without raising its own alarm.

The implant subscribes to three MIDs in `NOISY_APP_Main`:

```c
CFE_SB_Subscribe(CFE_SB_ValueToMsgId(NOISY_APP_CMD_MID), CmdPipe);     /* 0x18F2 own cmd */
CFE_SB_Subscribe(CFE_SB_ValueToMsgId(CARRIER_CMD_MID), CmdPipe);       /* 0x18E0 carrier sniff */
CFE_SB_Subscribe(CFE_SB_ValueToMsgId(GENERIC_EPS_HK_TLM_MID), CmdPipe);/* 0x091A EPS observe */
```

The third subscription, to genuine EPS HK, is itself an indicator of compromise:
a co-resident app reading another subsystem's telemetry. It exists so the
persistent EPS override (PoC 1) can shadow each genuine packet.

The opcode-to-scenario map (the switch in `NOISY_APP_RunScenario`):

| Opcode | Name | Effect | On-bus? |
|--------|------|--------|---------|
| `0x00` | DORMANT | nothing; also clears a running EPS override and a running GNSS spoof | n/a |
| `0x02` | EPS_SPOOF | forge ONE `GENERIC_EPS_HK` (`0x091A`), BatteryVoltage = 10000 mV | Yes |
| `0x04` | SB_BURST | benign hard-capped 64-packet burst (pool/pipe stress, deliberately not the destructive flood) | Yes |
| `0x06` | EPS_OVERRIDE | persistent: shadow every real EPS HK so the spoof always wins LC's sample | Yes |
| `0x08` | SB_FLOOD | destructive: self-hosted SB pool lock (total blackout) | Yes |
| `0x0A` | IMU_BIAS | covert channel: drop `/ram/.imu_cal` for the backdoored IMU app | No |
| `0x0C` | GNSS_SPOOF | persistent: teleport peer GNSS to Null Island (0,0) | No |
| `0x0E` | GNSS_DRIFT | persistent: slow plausible GNSS position drift | No |

### 2.2 The implant source layout

Everything lives in one file,
`nos3/fsw/apps/noisy_app/fsw/src/noisy_app.c`, structured as:

- Opcode defines and tuning constants.
- Per-scenario state (`NOISY_OverrideOn`, `NOISY_GnssMode`/`NOISY_GnssSpoofOn`,
  `NOISY_GnssTaskUp`).
- Per-scenario helper functions (`NOISY_SendEpsHk`, `NOISY_SpoofEpsHk`,
  `NOISY_SbBurst`, `NOISY_SbPoolLock`, `NOISY_WriteImuDeadDrop`,
  `NOISY_GnssShadowTask`, `NOISY_StartGnssSpoof`).
- The dispatcher `NOISY_APP_RunScenario(uint8 opcode)`.
- The main loop `NOISY_APP_Main`, which subscribes, handles the reactive EPS
  override on every genuine EPS HK, and detects the piggyback.

The build toggles:

- Compiled or not: `cfg/nos3_defs/targets.cmake` MISSION_GLOBAL_APPLIST (listed
  means compiled).
- Loaded at startup or not: `cfg/nos3_defs/cpu1_cfe_es_startup.scr`, the
  `noisy_app` row (`CFE_APP` = loaded, `OFF_APP` = present but not loaded).

`make config` regenerates the `cfg/build/...` copies from these sources. Edit the
sources, not the copies.

---

## 3. PoC 1: EPS spoof and persistent EPS override

### 3.1 Mechanism

The attacker forges a `GENERIC_EPS_HK` telemetry packet (MID `0x091A`) carrying a
low BatteryVoltage. Because the bus has no sender authentication and is
last-writer-wins for any later sampler, the forged packet is what the Limit
Checker (LC) samples at its watchpoint:

- WP0 (`cfg/nos3_defs/tables/lc_def_wdt.c:120-129`): watches `GENERIC_EPS_HK`
  (MID `0x091A`) at `WatchpointOffset = 16`, comparison `Unsigned16 < 14800`.
- AP0 (`cfg/nos3_defs/tables/lc_def_adt.c:180-196`): `SAFE_ON_LOW_BAT`,
  `RTSId = 4`, `EventText = "Batt volt critical: SAFE"`.
- RTS 4 (`cfg/nos3_defs/tables/sc_rts004.c`): the shared force-SAFE RTS. At wakeup
  0 it sends `MGR SET_MODE` with `MGR_SAFE_MODE`; at wakeup 1 it disables DS
  instrument recording; at wakeup 2 it sends `TO_LAB SET_SAFE_TLM`, which
  collapses the downlink to the beacon/housekeeping stream set.

So the chain is: forged battery below 14800 -> WP0 FAIL for the configured number
of consecutive LC samples -> AP0 -> RTS 4 -> `MGR SET_MODE SAFE` + DS
recording-disable + `TO_LAB SET_SAFE_TLM` comms downgrade. The spacecraft mode
(MGR) goes SAFE and the downlink drops to a beacon on the strength of one forged,
unauthenticated field.

The comms downgrade: the TO_LAB step (`TO_LAB_SetSafeTlm`,
`fsw/apps/to_lab/fsw/src/to_lab_app.c`) unsubscribes the high-rate device / raw /
diagnostic streams and keeps only the leading `TO_LAB_SAFE_BEACON_COUNT` beacon /
housekeeping streams from the SAFE-first subscription table
(`cfg/nos3_defs/tables/to_lab_sub.c`). The command path stays alive; recovery is a
ground `TO_LAB SET_NOMINAL_TLM` command, which re-subscribes the full set. So the
observable effect of PoC 1 is both the MGR mode transition to SAFE and the
downlink beacon downgrade. The detailed downgrade model is in
`docs/security/eps-spoof-comms-downgrade.md`.

Two opcodes:

- `0x02` EPS_SPOOF (one-shot): forge a single low-battery packet. Good for a
  single demonstration trip, but a single later spoof only logs a passive AP
  failure once LC has latched AP0 to passive.
- `0x06` EPS_OVERRIDE (persistent): the reactive variant. Every time a genuine
  EPS HK arrives, the implant immediately emits a shadow packet with its own
  value, so it always wins the sample race regardless of the real EPS publish
  rate. This holds WP0 in FAIL and forces and holds SAFE. Send `0x00` to clear.

### 3.2 Implementation and rationale

Forge primitive (`NOISY_SendEpsHk`). The implant carries a self-contained mirror
of the EPS HK wire layout (`NOISY_EpsHkMimic_t`) rather than linking the victim's
struct. Only `BatteryVoltage` (wire offset 16) matters for tripping WP0. It builds
the packet, time-stamps it, and calls `CFE_SB_TransmitMsg` on
`GENERIC_EPS_HK_TLM_MID`. Consumers (LC, COSMOS, DS, ELK) see it as a normal EPS HK.

Why mirror the layout instead of including the EPS header: the forge only needs
the one field at a known offset, and hardcoding the wire format keeps the implant
decoupled from the victim's build. (Contrast with the GNSS PoC, which includes
the real header because it needs exact struct offsets for an in-memory write.)

The reactive override (in the main loop). When `NOISY_OverrideOn` is set, every
received genuine EPS HK triggers an immediate shadow:

```c
if (NOISY_OverrideOn && CFE_SB_MsgIdToValue(MsgId) == GENERIC_EPS_HK_TLM_MID)
{
    const NOISY_EpsHkMimic_t *in = (const NOISY_EpsHkMimic_t *)BufPtr;
    if (in->BatteryVoltage != OVERRIDE_BATTERY_MV)
    {
        NOISY_SendEpsHk(OVERRIDE_BATTERY_MV);
    }
    continue;
}
```

The design reason it subscribes to the victim's own telemetry MID: a periodic
self-timed forge would race the real EPS HK and sometimes lose LC's sample. By
reacting to each genuine packet and emitting a shadow right after it, the spoof is
always the last writer before the next sampler reads. The `BatteryVoltage !=
OVERRIDE_BATTERY_MV` guard stops the implant reacting to its own shadow (it is
subscribed to `0x091A`), which would otherwise loop.

`OVERRIDE_BATTERY_MV` is 10000 (below the 14800 threshold) to force SAFE. Setting
it at or above 14800 would instead MASK a real low battery so the protective SAFE
never fires, the inverse attack. The value is one constant.

The SAFE chain itself lives in the config tables, not the implant. The implant
only has to put the forged value on the bus; the unauthenticated autonomous
trigger plane does the rest.

### 3.3 What was tested and found

From a high-rate run: 101 spoofed HK packets reached the bus at
`battery_mv = 10000` against a real EPS baseline of about 25189 mV, with 95
CI_LAB length-errors on `0x18E0` co-occurring (so the correlation detection,
length-error on the carrier plus a sub-14800 battery, is satisfied). The LC EVS
`Batt volt critical: SAFE` and the `RTS 004 ... Started` / `Execution Completed`
events accompany the trip, and the MGR mode reads SAFE afterwards. RTS 4's
`TO_LAB SET_SAFE_TLM` step also collapses the downlink from the full set to the
leading beacon/HK streams, so expect both the MGR mode flip to SAFE and a
downlink-rate drop, plus the SAFE-telemetry EVS event.

### 3.4 Limitations

- LC latches AP0 to passive after the first trip (a one-shot latch), so a single
  later one-shot spoof (`0x02`) only logs `AP failed while passive`. Re-firing
  SAFE from the spoof requires re-arming AP0 (`LC SET_AP_STATE`, the `--rearm`
  driver flag) and then driving the spoof faster than the real EPS HK so the
  required consecutive LC samples see the low value. The persistent override
  (`0x06`) sidesteps this by winning every sample.
- The forged value is a constant; the implant does not attempt to track or mask
  the real battery trend, so a trend-aware ground rule that compares against a
  second power indicator would flag the discontinuity.

---

## 4. PoC 2: Software Bus pool-lock flood (denial of service)

### 4.1 Mechanism

Opcode `0x08` runs the self-hosted SB buffer-pool lock. cFE allocates every
Software Bus message from one shared arena (`CFE_PLATFORM_SB_BUF_MEMORY_BYTES =
524288`, 512 KB, `cpu1_platform_cfg.h:156`). A buffer returns to the pool only
after the last subscriber's pipe has received and released it; if a subscriber
never receives, the buffer stays out of the pool. There is no per-app quota. The
implant creates a deep pipe it never drains, subscribes to a MID it owns with a
high per-MID limit, then parks buffers in it until the arena is dry. After that,
every other app's `CFE_SB_TransmitMsg` fails with `CFE_SB_BUF_ALOC_ERR`: HK,
telemetry, CI_LAB uplink-forwarding, and LC all stall at once. A silent, total
Software Bus blackout; recovery requires a sim restart.

The benign `0x04` SB_BURST is the bounded sibling: a hard-capped 64-packet burst
with periodic yields, deliberately incapable of exhausting the pool, kept for
demonstrating pipe/pool stress without taking the bus down.

### 4.2 Implementation and rationale

The maximum-size packet type (`NOISY_MaxPkt_t`): a telemetry header padded to the
mission max size (32 KB, `CFE_MISSION_SB_MAX_SB_MSG_SIZE`, `cfe_mission_cfg.h:53`),
so a parked buffer consumes a full max-bucket block and only ~15 are needed
(`512 KB / 32 KB`). The routine `NOISY_SbPoolLock`:

1. Create a deep sink pipe (`LOCK_PIPE_DEPTH`) that it never drains. Keep
   `LOCK_PIPE_DEPTH` at or below `CFE_PLATFORM_SB_MAX_PIPE_DEPTH`;
   `CFE_SB_CreatePipe` rejects any larger depth, so an over-large value would
   silently fail and the lock would never arm.
2. `CFE_SB_SubscribeEx` to `LOCK_MID = NOISY_APP_HK_TLM_MID` (`0x08F2`) with a
   high per-MID limit so the parked buffers are kept, not `MSGID_LIM`-dropped.
3. Transmit max-size buffers until an allocation fails; the parked buffers stay in
   the undrained pipe.
4. Leave the sink pipe in place (deleting it would return the parked buffers and
   lift the lock).

Design points worth calling out:

- `LOCK_MID` is the implant's OWN HK MID (`0x08F2`). The flood loops back on a MID
  the app legitimately owns; it never forges or impersonates a victim MID. The
  one operation the whole mechanism depends on is the subscribe to its own MID -
  without a subscriber, every `CFE_SB_TransmitMsg` frees its buffer immediately
  and the pool never drains.
- The CRITICAL `SB POOL LOCK engaged` event is emitted BEFORE the transmit loop,
  on purpose: EVS itself rides the bus, so once the pool is dry no further EVS can
  be sent. The warning must go out while a buffer is still available.
- A `CFE_ES_WriteToSysLog` line records the parked-buffer count. The ES syslog
  does not ride the SB, so this forensic line survives the blackout that silences
  EVS and HK.

The full mechanism, the on-bus vs off-bus framing, and the
`AllocateMessageBuffer` bound on the claim are in
`docs/security/sb-pool-lock-analysis.md`.

### 4.3 What was tested and found

The blackout is total: once the pool drains, all telemetry stops at once. In
COSMOS every panel freezes; in `cfs_god_view.json` all MIDs flatline
simultaneously. The CRITICAL `SB POOL LOCK engaged` event is among the last
messages the bus carries, and the `CFE_ES_WriteToSysLog` parked-buffer line
survives in the console/syslog. Recovery is a sim restart (section 8.2).

### 4.4 Limitations

`CFE_SB_AllocateMessageBuffer` (`cfe_sb_api.c:1372`) draws from the same pool with
neither a subscribe nor a transmit, so a pure allocate-leak variant would exhaust
the identical 512 KB arena without using the subscribe-park mechanism at all. This
PoC uses the subscribe-park mechanism deliberately (it is the on-bus counterpoint
to the off-bus PoCs); the allocate-leak variant is noted only to bound the
conclusion. The right control for resource exhaustion as a class is a resource
watchdog on the SB HK `MemInUse` / `PeakMemInUse` fields (`cfe_sb_task.c:311-313`).

---

## 5. PoC 3: IMU bias injection over a file dead-drop covert channel

### 5.1 Mechanism

Two cooperating implants pass a "calibration" spec through a file on the RAM disk,
never touching the Software Bus. The attacker app (`noisy_app`) writes the spec;
a backdoored copy of the victim app (`generic_imu`) polls for the file each
telemetry cycle, latches the spec, removes the file (transient evidence), and
applies a slow-drift bias to its own published gyro telemetry. The published
packet is a faithful, in-spec CCSDS message that happens to carry a wrong number.
The channel is the file, so there is no on-bus cause for a bus-level defence to
key on.

### 5.2 Implementation and rationale

Attacker side (`noisy_app`):

- `OPCODE_IMU_BIAS (0x0A)`, dormant until triggered.
- The wire contract `NOISY_ImuDrop_t` and its constants: path `/ram/.imu_cal` (a
  dotfile that reads as calibration data), magic `'IMUB'` (`0x494D5542`),
  `axis_mask 0x07` (X, Y, Z), `profile 1` (slow drift), `flags 0x01` (consume the
  file on latch), `gyro_step 0.0010`, `gyro_cap 0.10`.
- `NOISY_WriteImuDeadDrop()` packs that 24-byte struct and writes it with
  `OS_OpenCreate(... CREATE|TRUNCATE, WRITE_ONLY)`. It deliberately emits NO EVS
  event: the channel and its effect must be silent.

Victim side (`generic_imu`, the part that was added to close the loop):

- `nos3/components/generic_imu/fsw/cfs/src/generic_imu_app.h`: a byte-identical
  mirror `IMU_CovertDrop_t`, the path and magic constants, the bias state on the
  AppData global (`BiasLatched`, `BiasAxisMask`, `BiasProfile`, `BiasGyroStep`,
  `BiasGyroCap`, and the accumulator `BiasGyro`), and a prototype
  `GENERIC_IMU_PollCovertDrop`. All zero-initialised, so the app is benign until a
  valid drop latches.
- `nos3/components/generic_imu/fsw/cfs/src/generic_imu_app.c`:
  `GENERIC_IMU_PollCovertDrop()` opens `/ram/.imu_cal` read-only, returns silently
  if absent, validates magic, version, and read length, latches the spec, and (if
  `flags & 0x01`) `OS_remove`s the file. The drift is applied inside
  `GENERIC_IMU_ReportDeviceTelemetry()`, AFTER the device read fills the packet
  and BEFORE the publish: accumulate `BiasGyro += BiasGyroStep` clamped at
  `BiasGyroCap` (profile 1), then add `BiasGyro` to `AngularAcc` for each axis
  selected by `BiasAxisMask`.

The layout is mirrored out-of-band on both sides on purpose (not a shared build
artifact): the two implants agree on the contract, and the only thing crossing
between them is the file.

Rationale for the design:

- The bias is applied to the published packet only; the value the app read from
  the (simulated) device, and the 42/sim truth behind it, are never changed. That
  gap between published value and truth is the entire point and is what the
  detection keys on.
- Once `BiasLatched` is set the bias keeps accumulating every cycle regardless of
  whether the file is still present, so a single successful latch is enough.
- The clamp makes the steady-state effect a fixed offset reached asymptotically.
  Slow approach plus bounded magnitude is what keeps it under a naive
  rate/threshold alarm.

The full analysis, the live verification, and the OS-layer stealth wart are in
`docs/security/imu-covert-channel-analysis.md`.

### 5.3 What was tested and found

With IMU publishing `DEVICE_TLM` (`0x0926`), the decoded per-axis values in
`omni_logs/tlm_hk_decoded.log` walked from a steady -400.0 and clamped at exactly
`-400.0 + 0.10` = -399.90 (float32 `-399.8999938964844`) on all three axes
(`axis_mask 0x07`), then held. The only on-bus trace was the delivery line
(`NOISY_APP: piggyback opcode 0x0A`); the IMU app emitted zero EVS about the bias,
and the `/ram/.imu_cal` file was consumed after the first latch. The cap and step
are config bytes in the drop, so an attacker trades stealth against speed/magnitude
freely; -400.0 is the sim's synthetic constant, not a physical rate.

---

## 6. PoC 4: GNSS direct-memory overwrite (teleport and drift)

This is the deepest example of the off-bus class.

### 6.1 Mechanism

The implant writes directly into the GNSS app's memory, then keeps writing on a
fast loop so the value survives the GNSS app's own refresh. Because the write
never touches the Software Bus, a bus-level access control provides zero
protection.

Why it is possible (the enablers from section 1.3 made concrete):

1. Single shared address space, no inter-app memory protection. Every app is a
   pthread in one process (`os-impl-tasks.c`); a pointer in one app dereferences
   anywhere.
2. Runtime symbol resolution. On this Linux target the POSIX OSAL loader honors
   `OS_MODULE_FLAG_LOCAL_SYMBOLS` and loads each app `.so` with `RTLD_LOCAL`
   (`os-impl-posix-dl-loader.c`), so a plain link-time `extern` of a peer's global
   does not resolve. But the module is resident: `dlopen("generic_gnss.so",
   RTLD_NOLOAD)` returns a handle to the already-loaded library, and `dlsym()` on
   that handle returns the symbol's address despite `RTLD_LOCAL` (which only hides
   a symbol from global-scope resolution, not from `dlsym` on the module's own
   handle). No memory-corruption primitive is required.
3. The GNSS source of truth is a global. The GNSS app caches the simulator's
   position in `GENERIC_GNSS_AppData.LastBusLat / LastBusLon / LastBusAlt`
   (`generic_gnss_app.h:59-65`). Each HK cycle `ReportHousekeeping` calls
   `UpdatePositionAndFlags`, which recomputes the downlinked `DeviceHK.GnssLat /
   GnssLon` AND the `InDenmarkBox` geofence flag from `LastBus*`, under the same
   `HkDataMutex`, right before `CFE_SB_TransmitMsg`.

`LastBus*` is the only field worth writing. Poking `DeviceHK` directly would be
overwritten at the top of the next cycle. Spoofing `LastBus*` drives the geofence
flag for free. `LastBusAlt` is not in the GNSS HK layout, so altitude never
downlinks and the implant does not write it.

Two modes share one shadow task:

- TELEPORT (`0x0C`): write Null Island (0,0). An obviously-wrong, recognizable
  signature that also drops the spacecraft out of the Denmark geofence box. A
  trivial range/sanity check would catch it. Good for an unambiguous demo.
- DRIFT (`0x0E`): write `genuine + a slowly-growing offset` (default 0.0002 deg
  per 100 ms cycle, capped at 0.5 deg). The result still traces an orbit-shaped
  track, just displaced, so a range check does NOT catch it. This is the case
  that motivates the cross-source detection.

### 6.2 Implementation and rationale

The complete patch (the `dlopen(RTLD_NOLOAD)` + `dlsym` resolver, the shadow task,
the CMakeLists additions for `_GNU_SOURCE` / `${CMAKE_DL_LIBS}` / the generic_gnss
include roots) is in `docs/security/draco-linux-poc.md`; the portable mechanism
analysis is in `docs/security/gnss-mem-overwrite-analysis.md`. The key points:

- The implant includes the real `generic_gnss_app.h` so field offsets are exact
  across struct changes, the one place it links the victim's real header rather
  than mirroring a layout.
- A `volatile` mode/state flag drives the shadow child task; volatile because the
  flag is written from the main loop and read in the child task's loop. Without it
  a register-cached stale read would make opcode `0x00` fail to stop the spoof.
- The shadow task `NOISY_GnssShadowTask` guards on `OS_ObjectIdDefined` for the
  victim's `HkDataMutex` first (so it never takes an uninitialised mutex id if
  GNSS is not yet up), takes that mutex to serialize against the GNSS BusRx and HK
  tasks and avoid torn 64-bit reads, writes the spoof (TELEPORT) or `genuine +
  offset` (DRIFT, anti-compounding), and sleeps ~100 ms - faster than the sim
  refresh - so the HK report predominantly samples the spoofed value. Expect minor
  oscillation between spoofed and genuine samples; that residual is itself a
  detection artifact.
- `NOISY_StartGnssSpoof` spawns the shadow child task once and flips the mode; the
  dispatcher clears the mode on `0x00`.

### 6.3 What was tested and found

Under TELEPORT, the downlinked `gnss_lat/lon` collapse to (0,0) while
`gnss_truth_lat/lon` stay on the real orbit track. Under DRIFT, the divergence
`abs(gnss_lat - gnss_truth_lat)` grows smoothly rather than stepping, and the
spoofed coordinate stays a valid-looking on-orbit position the whole time.
Detection caveat learned in testing: compare on LATITUDE. The longitude bus field
lags the truth field in the logs, so a lat comparison is the clean discriminator.

### 6.4 Limitations

- TELEPORT is trivially caught by any range/plausibility filter on the downlinked
  position. It exists for the unambiguous demo, not stealth.
- DRIFT is bounded by the drift cap. It is the stealthier mode but still produces
  a steadily growing truth-vs-bus divergence, which is exactly the detection.
- The residual oscillation between spoofed and genuine samples (from racing the
  shadow loop against the sim refresh) is visible in the bus series.

If `dlopen("generic_gnss.so", RTLD_NOLOAD)` returns NULL, the module file name or
search path differs on that build. Fallbacks in order: confirm the exact `.so`
name in `fsw/build/exe/cpu1/cf/` and update `GNSS_MODULE_SO`; pass an absolute
path to `dlopen`; use `OS_SymbolLookupInModule` against the GNSS app's module id;
or, if a build links cFS statically into one binary (no per-app `.so`), the symbol
is in the main executable's table and a plain `extern` resolves at link time.

---

## 7. Running the PoCs

All `make` commands run from `nos3/`.

### 7.1 Build

The implant must be compiled (listed in `targets.cmake`) and loaded
(`cpu1_cfe_es_startup.scr` row set to `CFE_APP, noisy_app`):

```bash
cd nos3
make config && make fsw
# confirm the implant is set to load
grep noisy cfg/build/nos3_defs/cpu1_cfe_es_startup.scr   # expect: CFE_APP, noisy_app
```

### 7.2 Launch

```bash
cd nos3
make launch
```

Confirm the implant loaded:

```bash
grep "NOISY_APP: Initialized" omni_logs/cfs_evs.log
```

GUI (the COSMOS Launcher): `make cosmos-gui`. This needs X11 working in the
devcontainer; verify with `echo "$DISPLAY"` and `xeyes &` BEFORE launching, and
do not edit the devcontainer/X11 config (see the repository guard rails on GUI /
X11 forwarding). The full per-PoC GUI workflow, with the exact command and OPCODE
to send for each scenario, is section 7.4 below.

### 7.3 Headless drivers (the commands used non-interactively)

The raw-CCSDS driver sends to CI_LAB UDP 5012. Resolve the FSW endpoint
(127.0.0.1 normally reaches CI_LAB on the host; if not, use the FSW container IP
that the launch / god_view capture prints):

```bash
cd nos3
python3 poc/piggyback_noisy/drive_poc.py <FSW_IP> <DEST_IP>            # full demo ladder
python3 poc/piggyback_noisy/drive_poc.py <FSW_IP> <DEST_IP> override   # persistent EPS override (re-arms AP0)
python3 poc/piggyback_noisy/drive_poc.py <FSW_IP> <DEST_IP> spoof      # one-shot EPS_SPOOF (0x02)
python3 poc/piggyback_noisy/drive_poc.py <FSW_IP> <DEST_IP> imu        # arm the IMU dead-drop (0x0A)
python3 poc/piggyback_noisy/drive_poc.py <FSW_IP> <DEST_IP> gnss       # GNSS teleport to (0,0) (0x0C)
python3 poc/piggyback_noisy/drive_poc.py <FSW_IP> <DEST_IP> gnss_drift # GNSS slow plausible drift (0x0E)
python3 poc/piggyback_noisy/drive_poc.py <FSW_IP> <DEST_IP> flood      # DESTRUCTIVE SB pool lock (0x08)
python3 poc/piggyback_noisy/drive_poc.py <FSW_IP> <DEST_IP> off        # clear a running override / GNSS spoof (0x00)
```

The 9-byte piggyback wire format (cFS / CCSDS, big-endian primary header):
bytes 0-1 StreamID (MsgId), 2-3 Sequence (`0xC000`), 4-5 Length (total - 7),
byte 6 function code, byte 7 checksum (not validated by CI_LAB in simulation),
byte 8 the covert opcode (read by `noisy_app` as `buf[Size-1]`). Example
EPS_SPOOF datagram: `18e0c0000002000002` (trailing `02`).

### 7.4 Manual run with the COSMOS GUI (`make cosmos-gui`)

This is the interactive procedure: launch the GUI, send the commands by hand from
the COSMOS Command Sender, and watch the effects in COSMOS and Kibana. All COSMOS
command names and targets below were read from the
`gsw/cosmos/config/targets/` definitions.

Step 1: launch `make cosmos-gui` (verify X11 first with `echo "$DISPLAY"`,
`xeyes &`; do not edit the X11 config). This brings up the full stack and the
COSMOS Launcher window.

Step 2: confirm the implant loaded:
`grep "NOISY_APP: Initialized" omni_logs/cfs_evs.log`.

Step 3: enable the downlink once per session. Command Sender, target `TO`,
command `TO_ENABLE_OUTPUT`, Send. Confirm telemetry is flowing with
`tail -f omni_logs/tlm_hk_decoded.log` before sending the attack.

Step 4: baseline control (negative test). Command Sender, target `CFS`, command
`CI_DEBUG_NOOP_CC`, Send. This is a normal 8-byte NOOP: `noisy_app` stays silent
and the carrier's command counter advances. Your clean "before" for the dashboard.

Step 5 (PoC 1 only, optional): if a prior run latched AP0 passive, re-arm it.
`cmd("CFS LC_SET_AP_STATE with APNUMBER 0, NEWAPSTATE 1")`. A fresh boot has AP0
active already, so you usually skip this.

Step 6: send the PoC. The covert-opcode command is defined in
`gsw/cosmos/config/targets/CI_DEBUG/cmd_tlm/PIGGYBACK_POC.txt` as
`CI_DEBUG_PIGGYBACK_NOOP_CC`: a CI_LAB NOOP on carrier `0x18E0` with one trailing
`OPCODE` byte. Choose target `CFS`, command `CI_DEBUG_PIGGYBACK_NOOP_CC`, set the
`OPCODE` field, and Send. Per PoC:

| PoC | OPCODE to set | Script Runner line | Watch for |
|-----|---------------|---------------------|-----------|
| 1 EPS one-shot spoof | `EPS_SPOOF` (`0x02`) | `cmd("CFS CI_DEBUG_PIGGYBACK_NOOP_CC with OPCODE 2")` | `NOISY_APP: EPS HK SPOOF sent ...` |
| 1 EPS persistent override | `EPS_OVERRIDE` (`0x06`) | `cmd("CFS CI_DEBUG_PIGGYBACK_NOOP_CC with OPCODE 6")` | `EPS override ENGAGED`, then SAFE |
| 1 clear override | `DORMANT` (`0x00`) | `cmd("CFS CI_DEBUG_PIGGYBACK_NOOP_CC with OPCODE 0")` | `EPS override CLEARED` |
| 2 capped SB burst | `SB_BURST` (`0x04`) | `cmd("CFS CI_DEBUG_PIGGYBACK_NOOP_CC with OPCODE 4")` | `SB BURST sent 64 packets` (sim stays up) |
| 2 SB pool-lock flood | `SB_FLOOD` (`0x08`) | `cmd("CFS CI_DEBUG_PIGGYBACK_NOOP_CC with OPCODE 8")` | `SB POOL LOCK engaged` then total flatline (destructive) |
| 3 IMU bias dead-drop | `IMU_BIAS` (`0x0A`) | `cmd("CFS CI_DEBUG_PIGGYBACK_NOOP_CC with OPCODE 10")` | only `piggyback opcode 0x0A`; effect is off-bus (see 9.3) |
| 4 GNSS teleport | `GNSS_SPOOF` (`0x0C`) | `cmd("CFS CI_DEBUG_PIGGYBACK_NOOP_CC with OPCODE 12")` | `GNSS position SPOOF ENGAGED` |
| 4 GNSS drift | `GNSS_DRIFT` (`0x0E`) | `cmd("CFS CI_DEBUG_PIGGYBACK_NOOP_CC with OPCODE 14")` | `GNSS position DRIFT ENGAGED` |

The EPS_SPOOF one-shot is best sent in a tight loop to fire SAFE (LC needs several
consecutive failing samples faster than the ~1 Hz real EPS HK), e.g.
`20.times { cmd("CFS CI_DEBUG_PIGGYBACK_NOOP_CC with OPCODE 2"); wait(0.12) }`.
The persistent override (`0x06`) fires SAFE on its own.

Step 7: observe the effect in COSMOS (EVS / event log panel and Telemetry
Grapher), Kibana (section 9), and pcap (section 9.5). The off-bus PoCs (3 IMU,
4 GNSS) are confirmed via the truth-vs-bus divergence of section 9.3, since they
produce no on-bus effect to see in COSMOS.

Step 8: clear or recover. Send `OPCODE 0` (DORMANT) to clear a running EPS
override or GNSS spoof. After the destructive SB flood (PoC 2, `OPCODE 8`) the bus
is dead and COSMOS goes quiet; run `make save-run` then `make stop` and relaunch
(section 8.2).

The GUI changes only how the command is sent; the telemetry, Kibana, and pcap
verification are identical to the headless path.

---

## 8. Operational recovery

### 8.1 Recovering from the destructive SB flood (PoC 2)

The SB pool lock (`0x08`) is a total blackout that needs a sim restart:

```bash
cd nos3
make save-run RUN_LABEL=sb_flood   # archive the evidence FIRST (logs survive the blackout)
make stop                          # then tear down
# relaunch as in section 7.2
```

### 8.2 Downlink relay latency

Telemetry confirming a mode change (for example SAFE) can arrive with some launch
latency; allow for that when polling Elasticsearch right after sending a command.

---

## 9. Verification reference

| Thing | Value |
|-------|-------|
| Elasticsearch | `http://localhost:9200` |
| Kibana | `http://localhost:5601` |
| ELK index / data view | `nos3-telemetry-YYYY.MM.dd` (pattern `nos3-telemetry*`) |
| Command uplink (in) | UDP 5012 (CI_LAB) |
| Telemetry downlink (out) | UDP 5013 (TO_LAB) |
| Carrier MID | `0x18E0` (CI_LAB), FC 0 (NOOP) |
| Implant cmd MID / spoofed EPS MID | `0x18F2` / `0x091A` |
| pcap capture dir | `nos3/attack_logs/tcpdump/` |

Port numbers may be parameterized via `elk/.env` when multiple stacks share the
host; check `elk/.env` for the `ES_PORT` / `KIBANA_PORT` your stack uses.

### 9.1 Kibana dashboards

`elk/build_kibana_dashboards.py` builds a registry of dashboards on every
`make launch`. The per-PoC playbook in section 10 says which panel proves which
PoC. The key views: EPS battery dip + spacecraft-mode timeline (PoC 1); SB
packet-rate collapse + buffer-alloc-fail rate (PoC 2); per-axis bus `imu_gyro_*`
overlaid on `imu_truth_gyro_*` (PoC 3); `gnss_lat/lon` (bus) vs `gnss_truth_lat/lon`
and the geofence `in_denmark_box` flip (PoC 4).

Rebuild a single dashboard without a full launch:

```bash
python3 elk/build_kibana_dashboards.py --only <dashboard-id>
```

### 9.2 KQL detection signals (Discover, time range "Last 1 hour")

| What | KQL | PoC |
|------|-----|-----|
| Covert opcode read by the sniffer | `message:"piggyback opcode"` | all (delivery) |
| Endpoint length rejection (the piggyback tell-tale) | `message:"Invalid msg length" and message:"0x18E0"` | all (delivery) |
| EPS HK spoof emitted | `message:"EPS HK SPOOF sent"` | 1 |
| EPS override engaged | `message:"EPS override ENGAGED"` | 1 |
| Spoofed low battery in decoded HK | `eps_battery_mv < 14800` | 1 |
| Autonomous SAFE fired (LC AP0) | `message:"Batt volt critical: SAFE"` | 1 |
| SAFE RTS executed | `message:"RTS 004"` | 1 |
| Comms downgrade engaged | `message:"SAFE telemetry set active"` | 1 |
| Spacecraft mode change to SAFE | `spacecraft_mode_name:"SAFE"` | 1 |
| Capped SB burst | `message:"SB BURST sent"` | 2 |
| SB pool lock engaged | `message:"SB POOL LOCK engaged"` | 2 |

### 9.3 Truth-vs-bus divergence (the off-bus detection)

The IMU and GNSS PoCs produce no on-bus cause, so the detection is a cross-source
integrity check: compare the value on the trust boundary (the downlinked bus
value) against an independent ground truth the in-process implant cannot reach
(the sim's own output).

- GNSS (PoC 4): bus side `gnss_lat` / `gnss_lon` (decoded by
  `passive_listener.py` into `cfs_god_view.json`, converted to float in
  `logstash.conf`); truth side `gnss_truth_lat` / `gnss_truth_lon` (the GNSS sim
  emits `[GNSS_TRUTH] lat= lon=` into `omni_logs`, parsed by `logstash.conf`).
  Under normal operation `gnss_lat` is approximately `gnss_truth_lat`. Alert on
  `abs(gnss_lat - gnss_truth_lat) > epsilon`. TELEPORT shows a collapse to (0,0);
  DRIFT shows a smoothly growing divergence. Compare on LATITUDE (the longitude
  bus field lags the truth field in the logs).
- IMU (PoC 3): bus side `imu_gyro_x/y/z` (decoded into
  `omni_logs/tlm_hk_decoded.log`); truth side `imu_truth_gyro_x/y/z` (the IMU sim
  emits an `[IMU_TRUTH] gyro_x=.. ...` line, parsed by `logstash.conf`). Alert on
  `abs(imu_gyro_axis - imu_truth_axis) > epsilon`; pick epsilon below the
  per-sample growth so it trips within seconds.

### 9.4 Offline log greps

```bash
D=~/nos3_runs/<label>   # after make save-run; or use omni_logs/ live

# Attack-chain EVS in order (PoC 1)
grep -E "NOISY_APP|Invalid msg length|Batt volt critical|RTS Number 004" $D/omni_logs/cfs_evs.log
# Spoofed low-battery HK records (PoC 1)
grep '"battery_mv": 10000' $D/omni_logs/tlm_hk_decoded.log
# IMU gyro drift (PoC 3): watch imu_gyro_* climb to the clamp
grep -E '"imu_gyro_' $D/omni_logs/tlm_hk_decoded.log
# Per-MID message counts from the god view (0x091A = EPS HK)
grep -oE '"msg_id": "0x[0-9A-Fa-f]+"' $D/attack_logs/cfs_god_view.json | sort | uniq -c | sort -rn | head
# SB flood forensic line that survives the blackout (PoC 2)
grep "SB pool lock parked" $D/omni_logs/*.log
```

Note: `cfs_god_view.json` is header-only for some streams (for example the IMU
`0x0926`), so the IMU bias is invisible there; it shows in the decoded HK log.

### 9.5 pcap capture and filters

`make launch` starts a passive tcpdump sidecar that sniffs UDP 5012 (commands) and
5013 (telemetry) and injects nothing. Files land in `nos3/attack_logs/tcpdump/`.
The piggyback datagrams are isolated in Wireshark by `udp.length == 17` (8-byte
UDP header + 9-byte payload); the covert opcode is the last payload byte, in the
clear (the uplink is unauthenticated and unencrypted). The off-bus PoCs (IMU,
GNSS) look identical on the wire to PoC 1 at the carrier level: the distinguishing
byte is the trailing opcode (`0x0A`, `0x0C`, `0x0E`), and their EFFECT is not on
the wire at all, which is the whole point. To see those effects, use the
truth-vs-bus divergence in section 9.3.

### 9.6 Archive and replay

```bash
cd nos3
make save-run RUN_LABEL=<label>   # copies omni_logs, cfs_god_view.json, tcpdump/ to ~/nos3_runs/<label>
make load-run RUN=<label>         # restores logs + restarts ELK only (no sim) for later analysis
```

---

## 10. Per-PoC verification playbook

| PoC | Bus | TM operator-noticeable? | Primary detection |
|-----|-----|-------------------------|-------------------|
| 1 EPS spoof / override | on-bus | YES, very: battery dips to 10000 mV, autonomous SAFE, comms downgrade | `message:"EPS HK SPOOF sent"`; `eps_battery_mv < 14800`; `spacecraft_mode_name:"SAFE"` |
| 2 SB pool-lock flood | on-bus | YES, catastrophically: total bus blackout | `message:"SB POOL LOCK engaged"` then a coincident flatline of all HK |
| 3 IMU bias dead-drop | off-bus | NO on the bus value alone; needs truth-vs-bus | `imu_gyro_*` vs `imu_truth_gyro_*` divergence |
| 4 GNSS overwrite (teleport / drift) | off-bus | teleport YES (jumps to 0,0); drift NO (needs truth-vs-bus) | `gnss_lat` vs `gnss_truth_lat`; teleport: `gnss_lat >= -0.5 and gnss_lat <= 0.5` |

The contrast in rows 1-2 versus 3-4 is the thesis: a Software Bus mediation
control sees the on-bus attacks but is blind to the off-bus ones, which act
through memory and the filesystem instead.

For each PoC, build the stack, send the OPCODE from section 7.4, watch the EVS in
COSMOS / `omni_logs/cfs_evs.log`, and open the Kibana panel named in section 9.1.

- PoC 1 PASS: `eps_battery_mv` reaches 10000, `spacecraft_mode_name` goes SAFE,
  and the SAFE / comms-downgrade EVS lines are present.
- PoC 2 PASS: `SB POOL LOCK engaged` + the `holding` syslog line, then a
  simultaneous flatline across all MIDs. (Run `make save-run` first; this is
  destructive.)
- PoC 3 PASS: the bus `imu_gyro_*` diverge from `imu_truth_gyro_*` after the
  opcode while no EVS references IMU at all (silent off-bus manipulation,
  detectable only by truth-vs-bus).
- PoC 4 PASS: teleport - bus `gnss_lat` collapses to ~0 while `gnss_truth_lat`
  holds the real value; drift - `gnss_lat` diverges smoothly from
  `gnss_truth_lat` with no step. Either way invisible to a bus-only range check,
  visible only in the truth comparison.

---

## 11. Reversibility

The implant is a removable fixture, not a permanent backdoor. To prove it:

```bash
cd nos3
# 1. In cfg/nos3_defs/targets.cmake: re-comment the noisy_app applist line.
# 2. In cfg/nos3_defs/cpu1_cfe_es_startup.scr: change
#    "CFE_APP, noisy_app" -> "OFF_APP, noisy_app".
# 3. Optionally delete gsw/cosmos/config/targets/CI_DEBUG/cmd_tlm/PIGGYBACK_POC.txt
make config
grep noisy_app cfg/build/nos3_defs/cpu1_cfe_es_startup.scr   # now OFF_APP
make fsw
```

After this, `noisy_app` is absent at boot and the piggyback command is a no-op.
The backdoored `generic_imu` consumer (PoC 3) is the one change to a legitimate
app; reverting it removes the `GENERIC_IMU_PollCovertDrop` call from
`generic_imu_app.c` and the gyro bias state from `generic_imu_app.h`. No
load-bearing guard-rail entry in this repository is touched by any of this.

The SAFE chain itself (PoC 1: WP0, AP0, RTS 4, `MGR SET_MODE SAFE` + DS disable +
`TO_LAB SET_SAFE_TLM`) is normal, legitimate fault-protection that stays in the
build; nothing about it needs reverting. Only the attacker's ability to invoke it
via a forged, unauthenticated EPS reading is the issue, and that is addressed by
the mitigations below (authenticate the source, corroborate the trigger), not by
removing the SAFE response.

### Mitigations (cross-cutting)

1. Authenticate the uplink (TB-1): put CryptoLib SDLS/TC authentication plus
   anti-replay on the active CI/TO path; reject unauthenticated commands.
2. Verify checksums on ingest (`CFE_MSG_ValidateChecksum` before `TransmitBuffer`),
   and enforce exact length at ingress so over-length frames never reach the bus.
3. Constrain SB subscriptions: an app subscribing to another app's command or
   telemetry MID should be a build-time review gate.
4. Provenance on autonomous triggers (TB-2): LC/RTS decisions on critical values
   (battery, geofence) should require corroboration (multiple sources,
   rate-of-change sanity) rather than a single unauthenticated HK field.
5. Resource watchdog for exhaustion (PoC 2): alert on the SB HK `MemInUse`
   approaching `CFE_PLATFORM_SB_BUF_MEMORY_BYTES`; an onboard monitor is needed
   because the dry state is reached in under a second.
6. Memory and filesystem isolation for the off-bus class (PoC 3, 4): the real
   structural fix is not available in a single-process cFS deployment without
   inter-app memory protection. Hardening the loader-resolution exposure raises the
   bar but does not stop a determined implant; a per-app `/ram` filesystem ACL
   closes the file channel but not the memory channel. The residual control is the
   truth-vs-bus divergence detection (section 9.3).
7. Supply-chain control is the primary defense across all PoCs: do not ship the
   implant. Load-time image integrity (signed app modules, an expected-app
   manifest, boot attestation of the loaded module set) keeps a backdoored app off
   the target in the first place. App allow-listing in `targets.cmake` /
   `startup.scr` is the reviewed manifest.

---

## 12. References (source code only)

- Implant: `nos3/fsw/apps/noisy_app/fsw/src/noisy_app.c`,
  `nos3/fsw/apps/noisy_app/CMakeLists.txt`,
  `nos3/fsw/apps/noisy_app/fsw/platform_inc/noisy_app_msgids.h`.
- Backdoored IMU consumer (PoC 3): `nos3/components/generic_imu/fsw/cfs/src/`
  (`generic_imu_app.c`, `generic_imu_app.h`).
- GNSS victim globals (PoC 4): `nos3/components/generic_gnss/fsw/cfs/src/`
  (`generic_gnss_app.h`, `generic_gnss_app.c`).
- SAFE trigger chain: `nos3/cfg/nos3_defs/tables/lc_def_wdt.c` (WP0),
  `lc_def_adt.c` (AP0 -> RTS 4), `sc_rts004.c` (RTS 4: `MGR SET_MODE SAFE`, DS
  disable, `TO_LAB SET_SAFE_TLM`).
- Comms downgrade: `nos3/fsw/apps/to_lab/fsw/src/to_lab_app.c`
  (`TO_LAB_SetSafeTlm` / `TO_LAB_SetNominalTlm`),
  `nos3/cfg/nos3_defs/tables/to_lab_sub.c` (SAFE-first table).
- Driver: `nos3/poc/piggyback_noisy/drive_poc.py` (raw CCSDS).
- COSMOS command: `nos3/gsw/cosmos/config/targets/CI_DEBUG/cmd_tlm/PIGGYBACK_POC.txt`.
- Detection pipeline: `nos3/scripts/passive_listener.py` (downlink decode),
  `nos3/elk/logstash.conf` (field extraction, `gnss_truth_*`),
  `nos3/elk/build_kibana_dashboards.py` (the dashboards, section 9.1).
- Platform sizing: `nos3/cfg/nos3_defs/cpu1_platform_cfg.h`
  (`CFE_PLATFORM_SB_BUF_MEMORY_BYTES`), `cfe_mission_cfg.h`
  (`CFE_MISSION_SB_MAX_SB_MSG_SIZE`).
- CI_LAB ingest (the unauthenticated uplink): `nos3/fsw/apps/ci_lab/fsw/src/ci_lab_app.c`.

The per-topic analysis documents that this paper summarizes:
`docs/security/threat-analysis.md` (PoC 1 threat model, STRIDE/SPARTA),
`docs/security/eps-spoof-comms-downgrade.md` (PoC 1 SAFE comms downgrade),
`docs/security/noisy_app-escalation-design.md` (PoC 1 override + PoC 2 design),
`docs/security/sb-pool-lock-analysis.md` (PoC 2),
`docs/security/imu-covert-channel-analysis.md` (PoC 3),
`docs/security/gnss-mem-overwrite-analysis.md` (PoC 4 mechanism),
`docs/security/draco-linux-poc.md` (PoC 4 Linux patch),
`docs/security/gps-spoof-poc-runbook.md` (NovAtel GPS-spoof variant),
`docs/security/poc-manual-runbook.md` (COSMOS GUI walkthrough).

---

## 13. Appendix A: per-PoC file manifest

Paths are relative to the repository root.

### A.1 Shared foundation (all PoCs depend on this)

| File | Role |
|------|------|
| `nos3/fsw/apps/noisy_app/fsw/src/noisy_app.c` | The implant. All opcode scenarios, the dispatcher, the piggyback detector, the main loop. |
| `nos3/fsw/apps/noisy_app/CMakeLists.txt` | Build definition for the implant. |
| `nos3/fsw/apps/noisy_app/fsw/platform_inc/noisy_app_msgids.h` | The implant's MIDs (`NOISY_APP_CMD_MID 0x18F2`, `NOISY_APP_HK_TLM_MID 0x08F2`, `NOISY_APP_SEND_HK_MID`). |
| `nos3/cfg/nos3_defs/targets.cmake` | Applist toggle (listed = compiled). |
| `nos3/cfg/nos3_defs/cpu1_cfe_es_startup.scr` | Startup toggle (`CFE_APP, noisy_app` = loaded). |
| `nos3/gsw/cosmos/config/targets/CI_DEBUG/cmd_tlm/PIGGYBACK_POC.txt` | The COSMOS GUI command: `CI_DEBUG_PIGGYBACK_NOOP_CC` (carrier `0x18E0` + OPCODE). |
| `nos3/poc/piggyback_noisy/drive_poc.py` | Headless raw-CCSDS driver (modes incl. `gnss` / `gnss_drift`). |
| `nos3/poc/piggyback_noisy/run_poc.md` | Quick-reference run guide. |
| `nos3/elk/logstash.conf` | Detection-field extraction: the piggyback / EPS tags, the truth-vs-bus fields (`gnss_truth_*`, `imu_truth_gyro_*`). |
| `nos3/scripts/passive_listener.py` | Decodes the downlink into `cfs_god_view.json` (EPS `battery_mv`, GNSS `gnss_lat/lon`, IMU `imu_gyro_*`). |
| `nos3/elk/build_kibana_dashboards.py` | Builds the dashboard registry (section 9.1). |

### A.2 PoC 1: EPS spoof and persistent override

| File | What changed |
|------|--------------|
| `noisy_app.c` | `NOISY_EpsHkMimic_t`, `NOISY_SendEpsHk`, `NOISY_SpoofEpsHk`, the reactive-override branch in the main loop, opcodes `0x02` / `0x06`, the EPS HK subscribe. |
| `nos3/cfg/nos3_defs/tables/lc_def_wdt.c` | WP0 watches `GENERIC_EPS_HK` BatteryVoltage at WatchpointOffset 16, comparison `< 14800` (`:120-129`). |
| `nos3/cfg/nos3_defs/tables/lc_def_adt.c` | AP0 `SAFE_ON_LOW_BAT`, `RTSId = 4` (`:180-196`). |
| `nos3/cfg/nos3_defs/tables/sc_rts004.c` | RTS 4: `MGR SET_MODE SAFE`, DS recording-disable, `TO_LAB SET_SAFE_TLM`. |
| `nos3/fsw/apps/to_lab/fsw/src/to_lab_app.c`, `nos3/cfg/nos3_defs/tables/to_lab_sub.c` | `TO_LAB_SetSafeTlm` / `TO_LAB_SetNominalTlm` and the SAFE-first subscription table (the comms-downgrade behaviour). |

### A.3 PoC 2: SB pool-lock flood

| File | What changed |
|------|--------------|
| `noisy_app.c` | `NOISY_MaxPkt_t`, `NOISY_SbPoolLock`, the `LOCK_*` tuning constants, opcode `0x08`; the benign capped `NOISY_SbBurst` / opcode `0x04`. |
| `drive_poc.py` | `flood` / `burst` modes. |

### A.4 PoC 3: IMU bias dead-drop covert channel

| File | What changed |
|------|--------------|
| `noisy_app.c` | `NOISY_ImuDrop_t`, the `IMU_*` constants, `NOISY_WriteImuDeadDrop`, opcode `0x0A` (the writer). |
| `nos3/components/generic_imu/fsw/cfs/src/generic_imu_app.h` | `IMU_CovertDrop_t` mirror, path/magic constants, bias state on `GENERIC_IMU_AppData_t`, `GENERIC_IMU_PollCovertDrop` prototype. |
| `nos3/components/generic_imu/fsw/cfs/src/generic_imu_app.c` | `GENERIC_IMU_PollCovertDrop` (latch + consume the file) and the drift application inside `GENERIC_IMU_ReportDeviceTelemetry`. |

Detection plumbing: `generic_imu` sim emits the `[IMU_TRUTH] gyro_x=.. ...` line,
and `logstash.conf` parses it into `imu_truth_gyro_x/y/z` for the bus-vs-truth
detector.

### A.5 PoC 4: GNSS direct-memory overwrite

| File | What changed |
|------|--------------|
| `noisy_app.c` | `#include "generic_gnss_app.h"`, the `dlopen`/`dlsym` resolver, `NOISY_GnssShadowTask` (teleport + anti-compounding drift), `NOISY_StartGnssSpoof`, opcodes `0x0C` / `0x0E`, the clear-on-DORMANT branch. |
| `nos3/fsw/apps/noisy_app/CMakeLists.txt` | `generic_gnss` include roots, `${CMAKE_DL_LIBS}` link, `_GNU_SOURCE` define. |
| `nos3/poc/piggyback_noisy/drive_poc.py` | `gnss` / `gnss_drift` modes. |

Not modified but central:
`nos3/components/generic_gnss/fsw/cfs/src/generic_gnss_app.h` (the victim
`GENERIC_GNSS_AppData` global is resolved at runtime, never edited). Detection
uses the pre-existing `gnss_lat/lon` decode in `passive_listener.py` and
`gnss_truth_*` parse in `logstash.conf`. The complete patch is in
`docs/security/draco-linux-poc.md`.
