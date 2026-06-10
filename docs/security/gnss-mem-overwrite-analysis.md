# Cross-app memory overwrite of the GNSS position

## Question

Can one cFS application overwrite another application's memory, e.g. the Novatel /
generic GNSS position value? Yes, trivially, on this target. This document records the
vector, demonstrates it with a PoC, explains why a Software-Bus-level mediation control
provides zero protection against it, and identifies the detections and mitigations that
do apply.

## Why it is possible

1. Single shared address space, no inter-app memory protection. cFS on this Linux /
   NOS-Engine target runs every app as a pthread in one process
   (`nos3/fsw/osal/src/os/posix/src/os-impl-tasks.c` -> `pthread_create`). There is no
   process boundary and no per-task memory protection between apps. A pointer in one app
   dereferences anywhere in the shared address space.

2. The victim's symbol is resolvable at runtime. The POSIX OSAL loader honors
   `OS_MODULE_FLAG_LOCAL_SYMBOLS` and loads each app `.so` with `RTLD_LOCAL`
   (`os-impl-posix-dl-loader.c`), so a plain link-time `extern` of a peer app's global
   does not resolve. But the module is resident: `dlopen("generic_gnss.so", RTLD_NOLOAD)`
   returns a handle to the already-loaded library, and `dlsym()` on that handle returns
   the symbol's address despite `RTLD_LOCAL` (which only hides a symbol from global-scope
   resolution, not from `dlsym` on the module's own handle). The concrete patch is in
   `docs/security/draco-linux-poc.md`. No memory-corruption primitive is required; this
   is the loader and the loader API behaving as designed.

3. The GNSS source of truth is a global. The FSW GNSS app caches the position the
   simulator feeds it in `GENERIC_GNSS_AppData.LastBusLat / LastBusLon / LastBusAlt`
   (`nos3/components/generic_gnss/fsw/cfs/src/generic_gnss_app.h:59-61`; the global is
   declared `extern` at line 65). Each housekeeping cycle, `ReportHousekeeping` copies
   these into the downlinked packet field `HkTelemetryPkt.DeviceHK.GnssLat/GnssLon`
   (msg layout in `generic_gnss_msg.h:53-54`) and transmits HK MID `0x0952`.

## The PoC

The implant is the existing supply-chain fixture `noisy_app`
(`nos3/fsw/apps/noisy_app/fsw/src/noisy_app.c`), extended with a new covert opcode:

- `OPCODE_GNSS_SPOOF (0x0C)` and `OPCODE_GNSS_DRIFT (0x0E)`. Dormant until triggered over
  the existing piggyback channel (an over-length CI_LAB NOOP whose trailing byte is the
  opcode). On trigger the implant spawns a shadow child task that writes directly into
  `GENERIC_GNSS_AppData.LastBusLat/Lon` via the `dlopen`/`dlsym`-resolved pointer.
  `noisy_app` includes the real `generic_gnss_app.h` so field offsets are exact across
  struct changes.

`LastBus*` is the only field worth writing. The victim's `ReportHousekeeping` calls
`UpdatePositionAndFlags` (`generic_gnss_app.c:361, 344-355`) every cycle, which recomputes
the downlinked `DeviceHK.GnssLat/Lon` AND the `InDenmarkBox` geofence flag *from*
`LastBus*`, under the same `HkDataMutex`, immediately before `CFE_SB_TransmitMsg`. So
poking `DeviceHK` directly would just be overwritten at the top of the next cycle (an
earlier revision did this; it was dead weight), and spoofing `LastBus*` drives the
geofence flag for free. `LastBusAlt` is not represented in the GNSS HK layout
(`generic_gnss_msg.h:41-60` has no altitude field), so altitude never downlinks and the
implant does not bother writing it.

Two modes share one shadow task:

- TELEPORT (`0x0C`): writes Null Island (0,0). An obviously-wrong, recognizable spoof
  signature that also drops the spacecraft out of the Denmark geofence box. Good for an
  unambiguous demo, but a trivial range/sanity check on the downlinked position would
  catch it.
- DRIFT (`0x0E`): writes `genuine + a slowly-growing offset` (default 0.0002 deg per
  100 ms cycle, capped at 0.5 deg). The result still traces an orbit-shaped track, just
  displaced, so it stays plausible and a range check does NOT catch it. This is the case
  that motivates the cross-source detection below. The drift is anti-compounding: the
  task only advances its offset accumulator when the victim's `BusRxChildTask` has
  refreshed `LastBus*` with a fresh genuine sample (detected as `genuine != our last
  write`), then writes `genuine + offset` - so it tracks live truth plus a growing delta
  rather than runaway-summing its own previous writes.

Timing: the GNSS `BusRxChildTask` rewrites `LastBus*` from the sim UART line every tick,
so a single write would be clobbered. The implant therefore shadows the value on a
100 ms loop (faster than the sim refresh), taking the victim's own `HkDataMutex`
(its `osal_id_t` is reachable through the same global) to serialize cleanly and avoid
torn 64-bit reads. It guards on `OS_ObjectIdDefined(HkDataMutex)` first so it never takes
an uninitialized id if GNSS is not yet up. Expect minor oscillation between spoofed and
genuine samples; that residual is itself a detection artifact.

Trigger:
```
python3 nos3/poc/piggyback_noisy/drive_poc.py <FSW_IP> <DEST_IP> gnss        # teleport (0,0)
python3 nos3/poc/piggyback_noisy/drive_poc.py <FSW_IP> <DEST_IP> gnss_drift  # slow drift
python3 nos3/poc/piggyback_noisy/drive_poc.py <FSW_IP> <DEST_IP> off         # clear either
```

## Why message-bus mediation provides zero protection

A control that mediates Software Bus operations (per-app, per-MID allowlists on
`CFE_SB_Subscribe` and `CFE_SB_TransmitMsg`) inspects no raw memory.

The GNSS overwrite never calls either. It is a direct store to another app's globals.
So unlike a forged SB publish of GNSS HK (which a bus mediator could deny), the GNSS
spoof is invisible to any bus-level access control and produces no deny event. This is
the core finding: a message-bus ACL is necessary but not sufficient. Without memory
isolation (which a single-process, no-inter-app-MMU cFS deployment cannot give), an
in-process implant bypasses it entirely. The same is true of the legitimate Memory
Manager (MM Poke) and Memory Dwell (MD JAM) apps, which write arbitrary addresses by
design and are not meaningfully gated by a bus mediator.

## Detection that does work: ELK truth-vs-bus divergence

The pipeline already captures both sides of the comparison, so no new ELK code is
needed:

- Bus / FSW value (corrupted under attack): `passive_listener.py` taps the SB downlink
  and decodes the GNSS HK payload's `GnssLat`(offset 39) / `GnssLon`(offset 47) into
  `attack_logs/cfs_god_view.json` as fields `gnss_lat` / `gnss_lon`
  (`nos3/scripts/passive_listener.py:184-185`; logstash converts them to float at
  `nos3/elk/logstash.conf:1404`).
- Ground truth (unaffected by the FSW-side write): the GNSS sim emits
  `[GNSS_TRUTH] lat= lon= alt_m=` into `omni_logs`
  (`components/generic_gnss/sim/src/generic_gnss_hardware_model.cpp:185`), which logstash
  parses into `gnss_truth_lat` / `gnss_truth_lon` (`logstash.conf:1108-1110`).

Detection rule: under normal operation `gnss_lat ≈ gnss_truth_lat` and
`gnss_lon ≈ gnss_truth_lon` (the FSW transports the sim value unchanged). Under the
TELEPORT mode (`0x0C`), `gnss_lat/lon` collapse to (0,0) while `gnss_truth_lat/lon` stay
on the real orbit track. A Kibana panel plotting both series, or a divergence alert on
`abs(gnss_lat - gnss_truth_lat) > epsilon`, flags the overwrite that a bus ACL cannot
see. Index: `nos3-telemetry-*`.

The DRIFT mode (`0x0E`) is the reason this cross-source check matters rather than a
simple range/sanity bound on the bus value alone. A blatant (0,0) jump is caught by any
"is this position physically plausible" filter; the slow drift is not - it stays a
valid-looking on-orbit coordinate the whole time. Only the comparison against an
independent ground truth (`gnss_truth_*`, which the in-process implant cannot reach)
surfaces it, as a steadily growing `abs(gnss_lat - gnss_truth_lat)` rather than a step.
The same `epsilon` divergence alert catches both modes; pick `epsilon` below the drift's
per-sample growth so the alert trips within the first few seconds of drift.

This is a cross-source integrity check (independent ground truth vs the value on the
trust boundary), not an access-control check. It works precisely because the attack
cannot reach the sim's truth log from inside the FSW process.

## Mitigations and their limits

- A single-process cFS deployment without inter-app memory protection cannot
  structurally prevent this. There is no enforcement surface in the OS to gate an
  in-process store. This is an architecture property, not a bug to patch in FSW.
- Fixing the loader exposure (the `dlopen`/`dlsym` resolution path) raises the bar by
  removing the easy symbol resolution, but does not stop a determined implant:
  `OS_SymbolLookup` and raw pointer arithmetic via the CFE_ES app/module tables still
  reach the address. It is hardening, not isolation.
- The real defense is supply-chain control: do not ship the implant. Load-time image
  integrity (signed app modules, a manifest of expected loaded apps, attestation of the
  module set at boot) is what keeps a backdoored app off the bus in the first place.
- Runtime detection (the truth-vs-bus divergence above) is the residual control when an
  implant is already resident: it does not prevent the write but makes the effect
  observable on the ground.
- Note that MM/MD command auditing is irrelevant to this specific direct-write vector
  (no MM/MD command is issued); it is an orthogonal path that would need its own
  command-level allowlist.

## Scope

This document and the PoC demonstrate the vector and its detection. They deliberately do
not add any new FSW enforcement or mitigation, do not use the MM Poke / MD JAM vector
(the direct store is the cleaner demonstration of the no-isolation gap), and do not fix
the loader-resolution exposure (recorded as a recommendation only).
