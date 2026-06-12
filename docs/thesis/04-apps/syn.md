# `syn` (SYNOPSIS science-downlink prioritization)

## Purpose

`syn` is the cFS application wrapper for JPL's SYNOPSIS library, an
onboard engine that prioritizes which science data products to
downlink first. It is NOT a synthetic-data generator (the name is
sometimes misread that way). SYNOPSIS ("Science Yield improvement
via Onboard Prioritization and Summary of Information System") is a
JPL/Caltech project; the vendored copy under
`nos3/components/syn/synopsis/` is what builds here.

The component ships as a worked example. Per
`nos3/components/syn/README.md`, the demo "does not currently
utilize the radio for any type of downlink procedures": it ranks
data products and flips their downlink flags in an onboard database,
but nothing actually leaves the spacecraft. Wiring the prioritized
output to a real downlink path is left to the integrator.

## Source files

- `nos3/components/syn/fsw/cfs/src/syn_app.c` is the application body:
  command dispatch, housekeeping, and the SYNOPSIS library lifecycle
  (memory sizing, init, ingest, prioritize, deinit).
- `nos3/components/syn/fsw/cfs/platform_inc/syn_msgids.h` carries the
  MIDs: `SYN_CMD_MID = 0x18FC` and `SYN_HK_TLM_MID = 0x08FC` (plus a
  housekeeping-request MID and an unused device-telemetry MID).
- `nos3/components/syn/fsw/cfs/src/syn_msg.h` defines the command
  codes (see below) and the housekeeping packet struct.
- `nos3/components/syn/synopsis/` is the upstream JPL C++ library
  (CMake-built), with Python bindings and a SQLite product database.
  The `itc_app_*` bridge symbols
  (`synopsis/include/itc_synopsis_bridge.h`) come from a closed-source
  ITC binary; do not chase them in source.

## Wire-level surface

`syn` subscribes to `SYN_CMD_MID` (`0x18FC`) and publishes
housekeeping on `SYN_HK_TLM_MID` (`0x08FC`). The command set
(`syn_msg.h`) is:

| Command | FC | Effect |
|---------|----|--------|
| `SYN_NOOP_CC` | 0 | No-op |
| `SYN_RESET_COUNTERS_CC` | 1 | Reset command / error counters |
| `SYN_RESET_CC` | 5 | Wipe the ASDP database and restart the library |
| `SYN_PRIO_CC` | 6 | Run prioritization |
| `SYN_GET_PDATA_CC` | 7 | Pop the top product and mark it downlinked |
| `SYN_DISP_PDATA_CC` | 8 | Print the priority list to the FSW console |
| `SYN_CONFIG_DL_CC` | 9 | Set the downlink size constraint |
| `SYN_CONFIG_ALPHA_CC` | 10 | Set the diversity-weight alpha |
| `SYN_ADD_DATA_CC` | 11 | Ingest one of the canned data products |

The housekeeping packet carries only `CommandCount` and
`CommandErrorCount`. No heap usage, database depth, or
prioritization state is ever telemetered.

## Library lifecycle

`syn_app.c` drives the SYNOPSIS `Application` through:

1. Build the application with logger, clock, planner, and ASDP-DB
   modules.
2. `memory_requirement()` reports the buffer size needed.
3. `init(bytes, ptr)` boots the library on a caller-provided buffer.
4. `accept_dp(msg)` ingests a Data Product.
5. Ground updates metadata (science utility, priority bin, downlink
   state).
6. `prioritize()` returns an ordered list (default planner is
   MaxMarginalRelevance, which is diversity-aware).
7. After downlink, `update_downlink_state` excludes a product from
   future passes.
8. `deinit()` releases the buffer.

## Demo data

The demo ingests 8 canned OWLS (Ocean Worlds Life Surveyor) data
products (real JPL test outputs, not synthesized). `SYN_GET_PDATA_CC`
only flips the database downlink flag; no packet leaves the
spacecraft.

## Observability gap and the SYN_RESET heap leak

Two related weaknesses make this app a useful research target; both
are documented in detail in
[docs/security/syn-resource-exhaustion-analysis.md](../../security/syn-resource-exhaustion-analysis.md).

- The housekeeping packet exposes only the two counters, so ground
  cannot observe heap depletion or database state approaching a
  limit.
- `SYN_RESET_CC` (the database-wipe / library-restart command) leaks
  the SYNOPSIS working buffer on every invocation: the handler in
  `syn_app.c` calls `itc_app_deinit(memory)` and then
  `memory = malloc(mem_req_bytes)` without freeing the prior
  allocation, so the old buffer becomes unreachable. The initial
  allocation also trusts the closed-source
  `itc_app_get_memory_requiremennt()` return value with no NULL
  check. With no heap telemetry, repeated RESET commands deplete the
  heap silently until `malloc` returns NULL and the library crashes
  with only a generic `CFE_ES_CORE_APP_EXIT_EID`.
