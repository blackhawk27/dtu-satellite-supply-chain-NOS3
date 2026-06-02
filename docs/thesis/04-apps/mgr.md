# `mgr` (mission manager)

## Purpose

`mgr` is the mission manager: the small cFS app that owns the
spacecraft's operational mode and the cross-boot persistence of
that mode. It accepts mode-change commands from the ground,
publishes the current mode in its housekeeping packet, and writes
the mode to a small binary file on the data partition so that a
reboot resumes in the right state. In the DTU testbed it is the
state machine that the mission timeline and the science-overfly
tagging in Logstash key off; without it, the `mgr_mode` field on
Kibana documents has no source.

The component predates the DTU fork; it was present in the import
baseline `55ad2148` as part of the flattened submodule import. It
is documented here because the fork has made a behavioural change
to it (commit `8161e160`) and because the mode it exposes is
load-bearing for downstream observability.

## Source files

- `nos3/components/mgr/fsw/cfs/src/mgr_app.c` is the application
  body (about 630 lines). It is the only translation unit that
  changes mode, persists mode, and handles the command dispatch.
- `nos3/components/mgr/fsw/cfs/src/mgr_app.h` defines the four
  mode constants (`MGR_SAFE_MODE = 1`, `MGR_SAFE_REBOOT_MODE = 2`,
  `MGR_SCIENCE_MODE = 3`, `MGR_SCIENCE_REBOOT_MODE = 4`).
- `nos3/components/mgr/fsw/cfs/src/mgr_msg.h` defines the command
  packet structs and the command codes
  (`MGR_SET_MODE_CC = 2`, among others).
- `nos3/components/mgr/fsw/cfs/platform_inc/mgr_msgids.h` carries
  the three MIDs (`MGR_CMD_MID = 0x18F8`,
  `MGR_REQ_HK_MID = 0x18F9`, `MGR_HK_TLM_MID = 0x08F8`).
- `nos3/components/mgr/fsw/cfs/platform_inc/mgr_platform_cfg.h`
  pins the persistence path
  (`MGR_CFG_FILE_PATH = "/data/mgr.bin"`).

## Wire-level surface

`mgr` subscribes to two MIDs on the Software Bus:

- **`0x18F8` (`MGR_CMD_MID`).** Ground commands. The dispatch in
  `mgr_app.c` reads the function code field and selects a
  handler: `MGR_NOOP_CC` (1), `MGR_SET_MODE_CC` (2),
  `MGR_RESET_COUNTERS_CC` (or similar reset), and a small number
  of test commands. `MGR_SET_MODE_CC` carries a single-byte
  payload that selects the target mode; the handler validates
  the value against the `1..4` range and rejects out-of-band
  values with an EVS error.
- **`0x18F9` (`MGR_REQ_HK_MID`).** Periodic housekeeping request
  from the scheduler. Triggers the next HK packet emission.

It publishes one MID:

- **`0x08F8` (`MGR_HK_TLM_MID`).** Housekeeping telemetry. The
  payload struct is `MGR_Hk_tlm_t` and includes the boot
  counter, command counters, the current `SpacecraftMode` byte,
  and a small amount of derived state. The struct lives in
  `mgr_msg.h`.

## Telemetry it emits

The single byte that matters for the testbed is the
`SpacecraftMode` field on the HK packet. The Logstash pipeline
extracts it through a grok pattern on the EVS event text rather
than from the bus packet directly, because the EVS event is
emitted only when a mode change happens and is therefore the
clean signal. The relevant filter block is at
`nos3/elk/logstash.conf` around lines 406 through 415:

- A grok captures `mgr_mode_num`.
- A `mutate` converts it to integer.
- Two conditional mutates assign the human-readable label:
  `mgr_mode_num == 1` becomes `mgr_mode = SAFE`,
  `mgr_mode_num == 3` becomes `mgr_mode = SCIENCE`.
  The two reboot-flavoured modes are intentionally left
  unlabelled so they appear in Kibana as raw numerics; a
  reboot-in-progress is observable as the number going from
  `3` to `4` to `1` across a small time window.

A second tag, `science_overfly` (logstash.conf line 430), is
set when the spacecraft mode is `SCIENCE` and the GPS latitude
falls into the Denmark overfly window. This is the tag that
makes "what happened during the overfly" a one-line Kibana
query.

## What was changed from upstream

The mgr component was imported intact. Two DTU commits have
touched it since:

- `8161e160` ("fix(mgr): suppress duplicate SET_MODE EVS spam;
  revert reboot to SAFE mode"). Two distinct changes bundled.
  The first short-circuits the SET_MODE handler when the target
  mode equals the current mode, so a redundant command does not
  emit an EVS event; before the change, repeated ground
  commands during operator-run mode rehearsals filled the EVS
  stream with duplicates. The second changes the reboot-state
  policy: the reboot path now writes `MGR_SAFE_REBOOT_MODE`
  rather than the previous mode, so a reboot always resumes in
  SAFE and the operator has to issue an explicit `SET_MODE` to
  re-enter SCIENCE. The rationale is operational safety; an
  automatic resume into SCIENCE after a reboot risks driving
  the spacecraft into the science profile before the ground
  has confirmed health.
- `2111b907` ("Subject: Update SCH tables; debug HK/HS
  execution and startup issues"). Bundled across several apps;
  the mgr-relevant portion adjusted the scheduler tables so the
  HK request reaches `mgr` at the expected cadence.

The data-persistence path (`/data/mgr.bin`) is unchanged from
upstream. The cFS PSP creates the directory at startup; on this
fork the path is also referenced in the `make launch` Makefile
target which removes a stale `mgr.bin` so a fresh launch starts
from the default SAFE mode rather than picking up the prior
run's state.

## Load-bearing invariants

- **The four mode constants are positional.** The validation
  range check in `mgr_app.c:269` reads
  `(mode_cmd->U8 < MGR_SAFE_MODE) || (mode_cmd->U8 > MGR_SCIENCE_REBOOT_MODE)`.
  Renumbering or reordering them shifts the range silently.
- **`SpacecraftMode == 1` and `== 3` are the two Logstash
  labels.** Adding a new mode requires a matching `mutate` in
  `logstash.conf` or the new mode shows up as a bare number.
- **The `mgr.bin` persistence file is intentionally wiped at
  launch.** `make launch` deletes
  `fsw/build/exe/cpu1/data/mgr.bin` on every start; a launch
  that needs to preserve mode across restart-of-stack has to
  set `KEEP_TLM=1` (the same flag that preserves the ELK
  indices) and avoid the wipe. There is currently no Makefile
  variable for the `mgr.bin` wipe; this is a candidate
  improvement.
- **`MGR_HK_TLM_MID = 0x08F8` must be in `to_lab_sub.c`.** The
  fork's subscription list (`nos3/cfg/nos3_defs/tables/to_lab_sub.c`,
  expanded in DTU commit `8cad7cc8` "Added all msgid's to
  to_lab_sub.c, so now they all show up i Kibana") is what
  makes the mode byte visible in `cfs_god_view.json`. If the
  MID is dropped from the subscription, the mode never reaches
  Kibana through the bus path; only the EVS-based extraction
  in Logstash continues to work.

The four invariants above have not been violated in
production yet. They are recorded here as the surface area a
future edit needs to leave alone.
