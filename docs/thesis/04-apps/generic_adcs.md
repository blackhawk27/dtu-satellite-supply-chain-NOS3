# `generic_adcs`

## Purpose

`generic_adcs` is the attitude determination and control
subsystem. It consumes the attitude-sensing chain (IMU,
magnetometer, coarse and fine sun sensors) and the reaction-
wheel momentum, runs the attitude determination algorithm,
applies the control law, and emits torque commands to the
reaction wheels and the magnetorquers. It is the heaviest cFS
app in the testbed in terms of telemetry surface and is the most
heavily research-flagged.

The component predates the DTU fork; it was present at the
import baseline and has not been modified at the source level
since (verified by `git log -- nos3/components/generic_adcs/`,
which shows only the import commit). It is documented here
because the supply-chain research notes flag it as an attack
surface, not because the code has changed. This is the single
"per-app deep dive of an unmodified component" in the
collection; readers can take its presence here as evidence that
the threat-model framing is broader than the diff against the
import baseline.

## Source files

- `nos3/components/generic_adcs/fsw/shared/generic_adcs_adac.c`
  is the attitude-determination-and-attitude-control body. The
  PD-method control law, the eclipse-mode logic, and the
  hardware polling loops live here.
- `nos3/components/generic_adcs/fsw/cfs/src/generic_adcs_app.c`
  and its sibling headers are the cFS-side scheduler entry
  point and command dispatch.
- `nos3/components/generic_adcs/fsw/cfs/platform_inc/generic_adcs_msgids.h`
  carries the MID range.
- `nos3/components/generic_adcs/fsw/fprime/` carries the F-prime
  variant; this fork uses the cFS path exclusively.

## Wire-level surface

`generic_adcs` occupies a wider MID range than the other
components because it emits several distinct telemetry types per
control cycle (declared in `generic_adcs_msgids.h`):

- **`0x1940` (`GENERIC_ADCS_CMD_MID`).** Ground commands.
- **`0x1941` (`GENERIC_ADCS_REQ_HK_MID`).** Periodic HK request.
- **`0x1942` (`GENERIC_ADCS_ADAC_UPDATE_MID`).** The
  attitude-update tick that runs the control law. Subscribed to
  by the app itself on a fast cadence.
- **`0x0940` (`GENERIC_ADCS_HK_TLM_MID`).** Housekeeping.
- **`0x0941` (`GENERIC_ADCS_DI_MID`).** Data-input telemetry:
  the raw sensor reads the controller saw on this tick.
- **`0x0942` (`GENERIC_ADCS_AD_MID`).** Attitude-determination
  output: the estimated body-frame state.
- **`0x0943` (`GENERIC_ADCS_GNC_MID`).** Guidance / navigation /
  control intermediate state.
- **`0x0944` (`GENERIC_ADCS_AC_MID`).** Attitude-control output:
  the commanded torques.
- **`0x0945` (`GENERIC_ADCS_DO_MID`).** Data-output telemetry:
  the actuator commands actually written.

The five telemetry MIDs in the `0x094x` range are emitted on
every control cycle. This is by far the highest publish rate of
any single component on the bus and is therefore the most
exposed to the Software Bus pipe-overflow hazard described in
[../03-communication/02-fsw-software-bus.md](../03-communication/02-fsw-software-bus.md).

## Telemetry it emits

The Logstash pipeline does not currently extract numeric fields
from any of the ADCS telemetry MIDs into top-level Kibana
fields. The packets reach Elasticsearch as `software_bus`
documents via `cfs_god_view.json`, but their payload remains a
hex blob until a saved search decodes it through a runtime
field. The `mid_subsystem` translation does set `subsystem =
ADCS` on each ADCS MID through the MID-registry YAMLs under
`cfg/build/elk/`, so a per-subsystem query in Kibana works.

The downstream reaction-wheel telemetry (`rw_momentum`) is
extracted from the reaction-wheel component's own logs, not
from ADCS. Likewise the magnetometer and IMU emit their own
numeric fields. ADCS's contribution to Kibana is therefore
mostly *event* telemetry (control-mode changes, eclipse
entry / exit, saturation flags) routed through EVS rather than
*scalar* telemetry routed through MIDs.

## What was changed from upstream

Nothing at the source level. The component is byte-identical to
the import baseline.

Two things in this fork's environment shape how the component
behaves at runtime and are recorded here so the framing is
honest:

- **Build flags.** This fork builds cFS at `-O0` (the cFE
  default for `BUILDTYPE = debug`, which is what the Makefile
  sets unless overridden). Switching to `-O2` is a build-flag
  change, not a code change, but it activates the LICM hazard
  described next.
- **The LICM hazard.** Under `-O2`, GCC's loop-invariant code
  motion optimisation can hoist a register-pointer dereference
  out of an ADCS hardware-polling loop. The polling loop in
  `generic_adcs_adac.c` reads a wheel status register; under
  `-O0` the read is reissued on every iteration; under `-O2`
  the read is hoisted before the loop and the loop body
  observes a frozen value. Two cascading consequences:
  - The control task never exits the polling loop because the
    pre-command status never changes. The task starves
    lower-priority HK tasks and telemetry stops.
  - Alternatively, the loop exits on a pre-command state and
    the controller issues a stream of momentum-correction
    commands without waiting for feedback. The wheels saturate
    and the spacecraft tumbles.

  Neither path is reachable at `-O0`. The countermeasure is the
  upstream-recognised one: declare the register pointer
  `volatile`. This fork does not currently apply the
  countermeasure because the build runs at `-O0`. A change to
  `BUILDTYPE` therefore implicitly arms the hazard, which is
  why this document records the build flag as part of the
  component's invariant set.

## Load-bearing invariants

- **`BUILDTYPE = debug` (i.e. `-O0`) is the current safe
  setting.** The Makefile defaults to this; the cFS build
  scripts under `cfg/build/fsw_build.sh` honour it. Switching
  to `release` requires the `volatile` qualifier fix on the
  hardware-pointer reads in `generic_adcs_adac.c` or the
  spacecraft hangs in the polling loop.
- **The ADCS MID range `0x094x` and `0x194x` is dense.** Six
  command-or-update MIDs and six telemetry MIDs are allocated.
  Any new fork-added component must avoid this range.
- **The MID-registry YAMLs must keep mapping `0x094x` and
  `0x194x` to `subsystem = ADCS`.** The `gen_mid_registry.py`
  generator targets the Draco-era macros and silently fails on
  this cFE; the Makefile `start-elk` target falls back to
  copying the seed files under `nos3/elk/seed_mid/` into
  `cfg/build/elk/` if the generator fails. If the seed is
  regenerated, the ADCS assignments must be preserved.
- **No `volatile` qualifier currently exists on the
  hardware-pointer reads.** This is recorded as an invariant
  because adding it is a candidate future change; the LICM
  hazard above is the diagnostic record for when that change
  becomes load-bearing.

## Remaining apps

All five apps from the Stage 3 list are now written:

- [noisy_app.md](noisy_app.md)
- [mgr.md](mgr.md)
- [blackboard.md](blackboard.md)
- [generic_tt_c.md](generic_tt_c.md)
- [generic_gnss.md](generic_gnss.md)
- [generic_adcs.md](generic_adcs.md)

The cFS housekeeping apps (`mm`, `md`, `fm`, `cs`, `lc`, `sc`)
and the imported-intact components (`syn`, `onair`) remain
unwritten. Decide at the start of Stage 4 whether to fold any
of them in or leave the directory at the current six entries.
