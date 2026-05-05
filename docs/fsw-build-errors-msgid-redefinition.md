# FSW Build Errors: MsgID Macro Redefinition and Missing Component Headers

> **Scope note (added 2026-04-25):** This document is only relevant when the arducam component is enabled. The current active spacecraft profile (`nos3/cfg/spacecraft/sc-mission-config.xml`) has `<cam><enable>false</enable></cam>`, so the failures described here do **not** occur in the default EO1 build. Keep this doc as reference for anyone re-enabling arducam (or porting another component that lacks `_msgid_values.h`). For the canonical mission description see [`missions/NOS3-DTU-EO1-mission-implementation.md`](missions/NOS3-DTU-EO1-mission-implementation.md).

## Summary

`make fsw` fails with two categories of fatal errors when the arducam component is included in the build:

1. `CFE_PLATFORM_CMD_TOPICID_TO_MIDV` / `CFE_PLATFORM_TLM_TOPICID_TO_MIDV` redefined
2. `generic_adcs_msgids.h: No such file or directory` (and similar component msgid headers)

Both are caused by gaps in how NOS3's mission cmake wires up the cFE Draco MsgID system.

---

## Error 1: Macro Redefinition

### Symptom

```
error: "CFE_PLATFORM_CMD_TOPICID_TO_MIDV" redefined
  cfg/build/nos3_defs/cfe_msgid_api.h:22
note: this is the location of the previous definition
  fsw/cfe/modules/core_api/config/default_cfe_core_api_msgid_mapping.h:49
```

Appears in every translation unit that includes a component `*_msgid_values.h` header.

### Root Cause

The include chain from any component msgid header is:

```
cam_msgid_values.h
  → cfe_msgid_api.h                          (cfg/nos3_defs/)
      → cfe_core_api_base_msgids.h
          → cfe_core_api_msgid_mapping.h     (fsw/build/inc/ - generated wrapper)
              → default_cfe_core_api_msgid_mapping.h
                  #define CFE_PLATFORM_CMD_TOPICID_TO_MIDV(t) ...   ← FIRST definition
                  #define CFE_PLATFORM_TLM_TOPICID_TO_MIDV(t) ...
      ← returns to cfe_msgid_api.h
  #define CFE_PLATFORM_CMD_TOPICID_TO_MIDV(t) (0x1800 | (t))       ← REDEFINITION
  #define CFE_PLATFORM_TLM_TOPICID_TO_MIDV(t) (0x0800 | (t))
```

`cfe_msgid_api.h` includes `cfe_core_api_base_msgids.h` first (line 15), which already defines both macros. Then lines 22-23 define them again. The values are numerically identical - `CFE_PLATFORM_BASE_MIDVAL(CMD)` resolves to `0x1800` and `CFE_PLATFORM_BASE_MIDVAL(TLM)` resolves to `0x0800` - but the C preprocessor still flags the redefinition. Because `arch_build_custom.cmake` sets `-Werror`, the warning becomes a fatal error.

### Fix

Add `#ifndef` guards in `cfg/nos3_defs/cfe_msgid_api.h`:

```c
#ifndef CFE_PLATFORM_CMD_TOPICID_TO_MIDV
#define CFE_PLATFORM_CMD_TOPICID_TO_MIDV(topic)  (0x1800 | (topic))
#endif
#ifndef CFE_PLATFORM_TLM_TOPICID_TO_MIDV
#define CFE_PLATFORM_TLM_TOPICID_TO_MIDV(topic)  (0x0800 | (topic))
#endif
```

The guard is safe because the values are equivalent - whichever definition wins, all MID computations produce the same result.

---

## Error 2: Missing Component Msgid Headers

### Symptom

```
cfg/build/nos3_defs/tables/hk_cpy_tbl.c:36:10: fatal error: generic_adcs_msgids.h: No such file or directory
```

Also occurs for other component msgid headers included from table source files.

### Root Cause

`cfg/nos3_defs/targets.cmake` builds `APPLICATION_PLATFORM_INC_LIST` - a CMake list meant to expose every component's `platform_inc/` globally:

```cmake
FOREACH(X ${MISSION_GLOBAL_APPLIST})
    LIST(APPEND APPLICATION_PLATFORM_INC_LIST ${${X}_MISSION_DIR}/platform_inc)
    LIST(APPEND APPLICATION_PLATFORM_INC_LIST ${${X}_MISSION_DIR}/fsw/cfs/platform_inc)
    # ... many subdirectory variants
ENDFOREACH(X)
```

However, `APPLICATION_PLATFORM_INC_LIST` was **never passed to `include_directories()`** anywhere in the cmake files. It was dead code. The only global include path added in `arch_build_custom.cmake` was:

```cmake
include_directories(${MISSION_DEFS})   # only cfg/build/nos3_defs/
```

Table compilation targets (e.g., the `tblobj_*` targets in `apps/hk/`) get their include paths from cmake's global include directories at the time those targets are defined. Because component platform_inc paths were never added globally, headers like `generic_adcs_msgids.h` were not findable despite the file existing at:

```
components/generic_adcs/fsw/cfs/platform_inc/generic_adcs_msgids.h
```

### Fix

Add one line to `cfg/nos3_defs/arch_build_custom.cmake` after the existing `include_directories(${MISSION_DEFS})`:

```cmake
include_directories(${APPLICATION_PLATFORM_INC_LIST})
```

This wires up the previously dead list and makes all component `platform_inc/` directories globally available to every FSW compilation target including table objects.

---

## How to Apply and Verify

```bash
cd nos3
make config    # re-copies cfg/nos3_defs/ → cfg/build/, re-runs cmake with the fix
make fsw       # should now compile cleanly
```

Check for absence of:
- `error: "CFE_PLATFORM_CMD_TOPICID_TO_MIDV" redefined` in build output
- `fatal error: generic_adcs_msgids.h: No such file or directory` in build output

---

## Prevention: Adding New Components

When adding a new hardware component to `MISSION_GLOBAL_APPLIST` in `targets.cmake`:

1. The component's `platform_inc/` will automatically be on the include path (via `APPLICATION_PLATFORM_INC_LIST` now wired up in `arch_build_custom.cmake`).
2. If the component's `*_msgid_values.h` includes `cfe_msgid_api.h`, no action is needed - the `#ifndef` guards prevent redefinition errors.
3. Run `make config` after changing `targets.cmake` or any `cfg/nos3_defs/` cmake file before rebuilding.
4. If a new component defines its own topic-ID-to-MsgID macros with the same names as cFE core macros, always guard them with `#ifndef` / `#endif`.

---

## Error 3: LC Table Compilation Failures

### Symptom

```
cfg/build/nos3_defs/tables/lc_def_adt.c:189:27: error: initializer-string for array of 'char' is too long [-Werror]
  189 |      .EventText         = {"Battery voltage critical - entering SAFE mode"},
cfg/build/nos3_defs/tables/lc_def_adt.c:217:27: error: initializer-string for array of 'char' is too long [-Werror]
  217 |      .EventText         = {"Reaction wheel offline - starting desaturation sequence"},

cfg/build/nos3_defs/tables/lc_def_wdt.c:127:26: error: 'LC_MultiType_t' has no member named 'Unsigned16in32'
  127 |         .ComparisonValue.Unsigned16in32.Unsigned16 = 14800,
cfg/build/nos3_defs/tables/lc_def_wdt.c:181:26: error: 'LC_MultiType_t' has no member named 'Unsigned8in32'
  181 |         .ComparisonValue.Unsigned8in32.Unsigned8 = 0,
```

### Root Cause A - EventText too long

`LC_MAX_ACTION_TEXT` is defined as 32 in `fsw/apps/lc/fsw/inc/lc_interface_cfg.h`. The `EventText` field in `LC_ADTEntry_t` is a `char[32]`, which can hold at most 31 characters plus a null terminator. Three strings in `cfg/nos3_defs/tables/lc_def_adt.c` exceeded this limit:

| Entry | Original length |
|-------|----------------|
| `"Battery voltage critical - entering SAFE mode"` | 46 chars |
| `"CPU utilization high - check FSW"` | 32 chars |
| `"Reaction wheel offline - starting desaturation sequence"` | 56 chars |

### Root Cause B - Wrong union member names

`LC_MultiType_t` (defined in `fsw/apps/lc/config/default_lc_tblstruct.h`) is a flat union with direct members:

```c
typedef union {
    uint32 Unsigned32;
    uint16 Unsigned16;
    uint8  Unsigned8;
    // ...
} LC_MultiType_t;
```

The WDT table was written against an older LC version that wrapped 16-bit and 8-bit values in intermediate structs (`Unsigned16in32`, `Unsigned8in32`). Those wrappers no longer exist.

### Fix

In `cfg/nos3_defs/tables/lc_def_adt.c`, shorten the three EventText strings to ≤31 chars:

```c
/* was: "Battery voltage critical - entering SAFE mode" */
.EventText = {"Batt volt critical: SAFE"},

/* was: "CPU utilization high - check FSW" */
.EventText = {"CPU utilization high"},

/* was: "Reaction wheel offline - starting desaturation sequence" */
.EventText = {"Reaction wheel offline"},
```

In `cfg/nos3_defs/tables/lc_def_wdt.c`, drop the intermediate wrapper name:

```c
/* was: .ComparisonValue.Unsigned16in32.Unsigned16 = N */
.ComparisonValue.Unsigned16 = N,

/* was: .ComparisonValue.Unsigned8in32.Unsigned8 = N */
.ComparisonValue.Unsigned8 = N,
```

Four watchpoint entries were affected (WP #0, #1, #4, #5).

### Verification

```bash
cd nos3
make config   # re-copies cfg/nos3_defs/ → cfg/build/ and re-runs cmake
make fsw      # LC table objects should now compile cleanly
```

Confirm absence of `initializer-string for array of 'char' is too long` and `has no member named 'Unsigned16in32'` / `Unsigned8in32` in the build output.

---

## Error 4: SC RTS Table - Missing `.Payload` Member and Malformed Union Initializer

### Symptom

```
cfg/build/nos3_defs/tables/sc_rts001.c:58:15: error: 'MGR_U8_cmd_t' has no member named 'Payload'
   58 |     .rts.cmd1.Payload.U8 = MGR_SAFE_MODE,
cfg/build/nos3_defs/tables/sc_rts001.c:54:30: error: missing braces around initializer [-Werror=missing-braces]
   54 | SC_RtsTable001_t SC_Rts001 = {

cfg/build/nos3_defs/tables/sc_rts002.c:58:15: error: 'MGR_U8_cmd_t' has no member named 'Payload'
   58 |     .rts.cmd1.Payload.U8 = MGR_SCIENCE_MODE,
cfg/build/nos3_defs/tables/sc_rts002.c:54:30: error: missing braces around initializer [-Werror=missing-braces]
   54 | SC_RtsTable002_t SC_Rts002 = {
```

### Root Cause A - Wrong struct member path

`MGR_U8_cmd_t` (defined in `components/mgr/fsw/cfs/src/mgr_msg.h`) is a flat struct:

```c
typedef struct
{
    CFE_MSG_CommandHeader_t CmdHeader;
    uint8                   U8;
} MGR_U8_cmd_t;
```

There is no `Payload` wrapper. The tables were written against a hypothetical layout where `U8` was nested under a `Payload` sub-struct, which does not exist in this codebase.

### Root Cause B - Malformed union designated initializer

`SC_RtsTable001_t` is a union with one struct member (`rts`). When initializing with designated initializers, GCC requires the inner struct to be wrapped in its own brace pair: `.rts = { ... }`. The original code used flat cross-level designators (`.rts.hdr1.WakeupCount = 0` at the union level while also using a block initializer `.rts.cmd1 = {...}`), which triggers `-Wmissing-braces` and becomes a fatal error under `-Werror`.

### Fix

In `cfg/nos3_defs/tables/sc_rts001.c` and `sc_rts002.c`, replace the flat mixed-style initializer with the nested form:

```c
/* sc_rts001.c */
SC_RtsTable001_t SC_Rts001 = {
    .rts = {
        .hdr1.WakeupCount = 0,
        .cmd1.CmdHeader   = CFE_MSG_CMD_HDR_INIT(MGR_CMD_MID, SC_MEMBER_SIZE(cmd1), MGR_SET_MODE_CC, 0x00),
        .cmd1.U8          = MGR_SAFE_MODE,
    }
};

/* sc_rts002.c - identical structure, different mode constant */
SC_RtsTable002_t SC_Rts002 = {
    .rts = {
        .hdr1.WakeupCount = 0,
        .cmd1.CmdHeader   = CFE_MSG_CMD_HDR_INIT(MGR_CMD_MID, SC_MEMBER_SIZE(cmd1), MGR_SET_MODE_CC, 0x00),
        .cmd1.U8          = MGR_SCIENCE_MODE,
    }
};
```

This pattern matches the established convention used in `sc_rts025.c` and `sc_rts026.c`.

### Verification

```bash
cd nos3
make config   # re-copies cfg/nos3_defs/ → cfg/build/ and re-runs cmake
make fsw      # SC RTS table objects should compile cleanly
```

Confirm absence of `has no member named 'Payload'` and `missing braces around initializer` in build output.
