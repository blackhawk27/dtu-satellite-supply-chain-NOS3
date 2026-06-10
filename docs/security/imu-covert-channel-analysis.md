# IMU bias injection over a file dead-drop covert channel

## Question

Can a backdoored cFS application corrupt another sensor app's telemetry without
ever touching the Software Bus, so that a message-bus ACL can neither observe the
channel nor its effect? Yes. This document records the vector, the two-app split
that implements it, the exact code that was added to make it work end to end, a
live verification run, the reason a bus-level mediation control provides zero
protection, and the cross-source detection that does apply.

It is the file-channel sibling of `gnss-mem-overwrite-analysis.md`. The GNSS PoC
writes a peer app's globals directly (an in-process store); this one passes a
spec between two cooperating implants through a file on the RAM disk and lets the
victim app bias its own telemetry. Both share the same end property: the effect
is real on the downlinked telemetry, but there is no on-bus cause for a bus-level
defense to key on.

## Why it is possible

1. A writable shared filesystem with no per-app access control. cFS mounts a
   volatile RAM disk at `/ram`. Any app can `OS_OpenCreate`, `OS_read`, `OS_write`,
   and `OS_remove` any path on it. OSAL imposes no per-app file ownership or ACL, so
   a file written by one app is readable and removable by any other. The file is the
   channel.

2. The channel is off the Software Bus entirely. A bus mediation control gates SB
   operations (`CFE_SB_Subscribe`, `CFE_SB_TransmitMsg`). A file write/read is
   neither, so the dead-drop produces no subscribe and no publish for it to allow or
   deny. It inspects no filesystem activity.

3. The victim owns its telemetry and can be made to lie about it. The
   `generic_imu` FSW app builds its own `GENERIC_IMU_DEVICE_TLM` packet each cycle
   from the value it read off the (simulated) device. A backdoored copy of that
   app can add a bias to the values it just collected, before it publishes them.
   The published packet is then a faithful, in-spec CCSDS telemetry message that
   happens to carry a wrong number. Nothing downstream can tell it was altered.

The split matters: the attacker app (`noisy_app`) never touches the IMU, and the
IMU app never talks to the attacker over the bus. They agree out-of-band on a
file layout, and the file is the only thing that crosses between them.

## The PoC

### Attacker side (writer): noisy_app

The implant is the existing supply-chain fixture `noisy_app`
(`nos3/fsw/apps/noisy_app/fsw/src/noisy_app.c`). The covert opcode and the
dead-drop writer are present:

- `OPCODE_IMU_BIAS (0x0A)` (noisy_app.c:98). Dormant until triggered over the
  piggyback channel.
- The wire contract `NOISY_ImuDrop_t` and its constants (noisy_app.c:125-155):
  path `/ram/.imu_cal` (a dotfile that reads as calibration data), magic `'IMUB'`
  (0x494D5542), `axis_mask 0x07` (X|Y|Z), `profile 1` (slow drift), `flags 0x01`
  (consume the file on latch), `gyro_step 0.0010`, `gyro_cap 0.10`,
  `accel_step 0.0`, `accel_cap 0.0`.
- `NOISY_WriteImuDeadDrop()` (noisy_app.c:357-376) packs that struct and writes it
  to `/ram/.imu_cal` with `OS_OpenCreate(... CREATE|TRUNCATE, WRITE_ONLY)`.
  Dispatched from the opcode switch at noisy_app.c:538-541. It deliberately emits
  NO EVS event: the channel and its effect must be silent.

The 24-byte wire layout (`__attribute__((packed))`, little-endian):

| offset | field        | type   | value written |
|--------|--------------|--------|---------------|
| 0      | magic        | uint32 | 0x494D5542 'IMUB' |
| 4      | version      | uint8  | 1 |
| 5      | axis_mask    | uint8  | 0x07 (X|Y|Z) |
| 6      | profile      | uint8  | 1 (slow drift) |
| 7      | flags        | uint8  | 0x01 (consume on latch) |
| 8      | gyro_step    | float  | 0.0010 |
| 12     | accel_step   | float  | 0.0 |
| 16     | gyro_cap     | float  | 0.10 |
| 20     | accel_cap    | float  | 0.0 |

### Victim side (consumer): generic_imu

The backdoor consumer is in the `generic_imu` FSW app, closing the loop so
`OPCODE_IMU_BIAS` has an effect rather than writing a file nothing reads. Two
files carry it:

`nos3/components/generic_imu/fsw/cfs/src/generic_imu_app.h`
- `IMU_CovertDrop_t` (lines 35-57): a byte-identical mirror of `NOISY_ImuDrop_t`,
  plus `GENERIC_IMU_DROP_PATH "/ram/.imu_cal"` and `GENERIC_IMU_DROP_MAGIC`
  (0x494D5542). Mirrored out-of-band on purpose (not a shared build artifact), the
  same contract style the noisy_app comment names. The layout must stay in sync
  with `NOISY_ImuDrop_t`.
- Bias state on `GENERIC_IMU_AppData_t` (lines 92-101): `BiasLatched`,
  `BiasAxisMask`, `BiasProfile`, `BiasGyroStep`, `BiasGyroCap`, and the running
  accumulator `BiasGyro`. Zero-initialized with the rest of the global, so the app
  is benign until a valid dead-drop latches.
- Prototype `GENERIC_IMU_PollCovertDrop(void)` (line 125).

`nos3/components/generic_imu/fsw/cfs/src/generic_imu_app.c`
- `GENERIC_IMU_PollCovertDrop()` (lines 392-424): opens `/ram/.imu_cal` read-only;
  returns silently if absent (the common case, leaving any prior latch intact);
  validates `magic == 'IMUB'`, `version == 1`, and the read length; on a valid
  read latches the spec into the bias state and sets `BiasLatched = 1`; if
  `flags & 0x01`, `OS_remove`s the file so the evidence is transient. Emits no EVS.
- Drift application in `GENERIC_IMU_ReportDeviceTelemetry()` (lines 442-475),
  placed inside the `status == OS_SUCCESS` block, AFTER `GENERIC_IMU_RequestData`
  fills `DevicePkt.Generic_imu` (line 436) and BEFORE the publish
  `CFE_SB_TransmitMsg(... DevicePkt ...)` (line 479):
  1. `GENERIC_IMU_PollCovertDrop()` once per device-telemetry cycle.
  2. If latched: for `profile 1` accumulate `BiasGyro += BiasGyroStep`, clamped at
     `BiasGyroCap`; for `profile 0` set `BiasGyro = BiasGyroStep` (constant offset).
  3. Add `BiasGyro` to `X/Y/Z_Data.AngularAcc` for each axis selected by
     `BiasAxisMask` (X=bit0, Y=bit1, Z=bit2). `LinearAcc` is left untouched
     (`accel_step 0`).

The bias is applied to the published packet only; the value the app read from the
(simulated) device, and the 42/sim truth behind it, are never changed. That gap
between the published value and the truth is the entire point, and is what the
detection below keys on.

Two design notes:
- The poll lives inside the device-telemetry path, so the dead-drop is only read
  when the app is actually publishing `DEVICE_TLM`. Once `BiasLatched` is set the
  bias keeps accumulating every cycle regardless of whether the file is still
  present, so a single successful latch is enough; a later re-drop simply
  re-latches the spec without resetting the accumulator.
- The clamp makes the steady-state effect a fixed offset (`gyro_cap`), reached
  asymptotically. Slow approach + bounded magnitude is what keeps it under a naive
  rate/threshold alarm.

### Build requirement

The consumer is compiled into the `generic_imu` app. Rebuild with:

```
cd nos3 && make fsw
```

`make launch` reloads the app from `fsw/build/exe/cpu1/cf/` each boot.

### Trigger

The opcode is delivered to `noisy_app` over the existing piggyback channel (an
over-length CI_LAB NOOP whose trailing byte is the opcode `0x0A`), using the same
tool as the EPS/GNSS scenarios:

```
python3 nos3/poc/piggyback_noisy/drive_poc.py <FSW_IP> <DEST_IP> imu
```

Once `noisy_app` processes the opcode it logs one generic line,
`NOISY_APP: piggyback opcode 0x0A on MID 0x...`, and writes the file. There is no
further EVS about the IMU effect from either app.

## Live verification

Run conditions: IMU enabled and publishing `DEVICE_TLM` (`0x0926`). `DEVICE_TLM`
is only emitted on `GENERIC_IMU_REQ_HK_MID 0x1926` with FC=1, which
`passive_listener.py` does not drive, so a 2 s poller sent that command to CI_LAB
(FSW container IP):5012. The decoded per-axis values land in
`omni_logs/tlm_hk_decoded.log` as `imu_gyro_x/y/z` and `imu_acc_x/y/z`
(`passive_listener.py` IMU decoder, offsets 16/20/24/28/32/36). The header-only
`attack_logs/cfs_god_view.json` shows the `0x0926` packets but no payload, so the
effect is invisible there.

Result:

| stage | imu_gyro_x/y/z | imu_acc_x/y/z |
|-------|----------------|----------------|
| baseline (steady) | -400.0 | -10.0 |
| after opcode 0x0A | climbs +0.0010 per device-tlm publish | -10.0 (unchanged) |
| steady state (clamped) | -399.8999938964844 | -10.0 |

The bus value walked from -400.0 and clamped at exactly `-400.0 + 0.10` =
-399.90 (float32 `-399.8999938964844`) on all three axes (`axis_mask 0x07`), then
held. `LinearAcc` never moved (`accel_step 0`). The only on-bus trace was the
delivery line (`NOISY_APP: piggyback opcode 0x0A on MID 0x18E0`); the IMU app
emitted zero EVS about the bias. The `/ram/.imu_cal` file was consumed (removed
after the first latch), confirmed by the consumer's subsequent open attempts
failing.

On magnitude: 0.10 against a -400.0 reading is 0.025 percent, deliberately small.
The damage model for a gyro bias is accumulation in the attitude estimator, not
instantaneous magnitude; a small persistent error integrates into a growing
pointing error. The cap and step are just config bytes in the dead-drop, so an
attacker trades stealth against speed/magnitude freely. -400.0 is the sim's
synthetic constant, not a physical rate, so treat the numbers as a demonstration.

## Why message-bus mediation provides zero protection

A bus mediation control gates `CFE_SB_Subscribe` and `CFE_SB_TransmitMsg` and
inspects no raw memory and no filesystem. The dead-drop is a file write on the
attacker side and a file read on the victim side; the bias is a local arithmetic
add inside the victim before its own (fully allowed) `CFE_SB_TransmitMsg`. None of
that is a gated operation, so a bus ACL emits no deny for the channel or its
effect. The only on-bus element is the delivery of the opcode into `noisy_app`,
and even that is an ordinary command. This is the same lesson as the GNSS
overwrite: a message-bus ACL is necessary but not sufficient; without filesystem
ACLs or memory isolation (which a single-process cFS deployment without inter-app
memory protection does not provide) an in-process implant routes around it.

## Detection that does work: truth-vs-bus divergence

The bias lives only in the published telemetry; the value the app read from the
device and the 42/sim truth behind it are unchanged. So the detection is the same
cross-source integrity check used for GNSS, applied to the gyro axes:

- Bus / FSW value (corrupted under attack): `imu_gyro_x/y/z` decoded from the
  `GENERIC_IMU_DEVICE_TLM` (`0x0926`) payload by `passive_listener.py` into
  `omni_logs/tlm_hk_decoded.log`.
- Ground truth (unaffected by the FSW-side add): the IMU sim's own angular-rate
  output. Under normal operation the FSW transports the device value unchanged, so
  bus approximately equals truth.

Detection rule: alert on `abs(imu_gyro_axis - imu_truth_axis) > epsilon`. The slow
drift makes the divergence grow smoothly to the `gyro_cap` offset rather than
stepping, so pick `epsilon` below the per-sample growth to trip within seconds. A
plain range/sanity bound on the bus value alone will NOT catch it, because every
sample stays a plausible in-range gyro reading; only the comparison against an
independent truth the in-process implant cannot reach surfaces it. This is a
cross-source integrity check, not access control, and it works precisely because
the dead-drop cannot reach the sim's truth from inside the FSW.

This detection plumbing mirrors `gnss_truth_*`. The IMU sim emits a dedicated INFO
line `[IMU_TRUTH] gyro_x=.. gyro_y=.. gyro_z=.. acc_x=.. acc_y=.. acc_z=..`
(`generic_imu_hardware_model.cpp`, `create_generic_imu_data` X case), and
`logstash.conf` parses it into `imu_truth_gyro_x/y/z` and `imu_truth_acc_x/y/z` on
a `subsystem: IMU_TRUTH` document. The bus side (`imu_gyro_*`) is captured from the
`GENERIC_IMU_DEVICE_TLM` decode in the `hk_decoded` layer; the two live on distinct
field names so the `abs(imu_gyro_axis - imu_truth_gyro_axis)` comparison is
possible.

## Stealth wart (known, not yet fixed)

`GENERIC_IMU_PollCovertDrop()` opens `/ram/.imu_cal` every device-telemetry cycle.
After the file is consumed, every subsequent open fails and OSAL prints
`OS_FileOpen_Impl():...:open(/ram/.imu_cal): No such file or directory` to the FSW
console (hundreds of lines in a short run). This is not on the Software Bus, not an
EVS event, and not in `cfs_god_view.json` or the ELK system_log parsing, so it
does not undermine the off-bus claim, but it is visible in the FSW container
console and is a tell for an analyst with console access. A stealthier consumer
would poll on a slow cadence (a covert channel can afford to be slow) or stop
polling once `BiasLatched` is set (the bias persists regardless), trading re-arm
responsiveness for silence. Recorded as a refinement, not fixed here.

## Mitigations and their limits

- A single-process cFS deployment without inter-app memory protection plus an
  unguarded shared `/ram` cannot structurally prevent an in-process app from
  reading/writing another app's drop file or biasing its own telemetry. This is an
  architecture property, not an FSW bug.
- A filesystem ACL (per-app path ownership) on the volatile disk would close the
  file channel specifically, but a resident implant has other off-bus channels
  (the GNSS-style direct memory write, MM/MD), so this is one hole in a class.
- The real defense is supply-chain control: do not ship either implant. Load-time
  image integrity (signed app modules, a manifest of expected loaded apps, boot
  attestation of the module set) keeps both the writer and the backdoored consumer
  off the target in the first place.
- Runtime detection (the truth-vs-bus divergence above) is the residual control
  once an implant is resident: it does not prevent the bias but makes the effect
  observable on the ground.

## Scope

This document and the implemented consumer demonstrate the vector, its effect on
real downlinked telemetry, and its detection. The change adds a backdoor to a copy
of `generic_imu` for the demonstration; it adds no new FSW enforcement and does
not fix the open `/ram` ACL gap (recorded as a recommendation only).
