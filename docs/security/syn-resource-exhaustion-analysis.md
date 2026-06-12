# SYN heap exhaustion via SYN_RESET_CC

## Summary

The `syn` app (the cFS wrapper for JPL's SYNOPSIS library, see
[docs/thesis/04-apps/syn.md](../thesis/04-apps/syn.md)) leaks its
SYNOPSIS working buffer on every `SYN_RESET_CC` command. Because the
housekeeping packet exposes no heap or database state, the depletion
is invisible to ground until the next allocation fails and the app
exits with a generic event. A ground operator (or a compromised
ground link) that can send `SYN_RESET_CC` repeatedly can drive the
FSW process toward heap exhaustion without tripping any telemetry
threshold.

## Affected code

All references are in `nos3/components/syn/fsw/cfs/src/syn_app.c`.

- Initial allocation: `mem_req_bytes = itc_app_get_memory_requiremennt()`
  (line ~182, note the double-`n` typo in the closed-source bridge
  symbol) followed by `memory = malloc(mem_req_bytes)` (line ~189)
  with no check that the size is non-zero and no NULL check on the
  returned pointer.
- The leak: the `SYN_RESET_CC` handler (around lines 355-389) does

  ```c
  itc_app_deinit(memory);          /* tear down the library state   */
  memory = malloc(mem_req_bytes);  /* new buffer; old pointer lost  */
  ```

  `itc_app_deinit` is the library teardown, not a `free` of the
  caller-owned buffer. The subsequent `malloc` overwrites the static
  `memory` pointer without `free(memory)`, so the previous allocation
  becomes unreachable. Each `SYN_RESET_CC` therefore leaks
  `mem_req_bytes`.

## Why it is hard to see

The housekeeping packet (`SYN_Hk_tlm_t` in `syn_msg.h`) carries only
`CommandCount` and `CommandErrorCount`. There is no field for heap
usage, buffer count, or ASDP database depth, so the leak produces no
observable telemetry trend. The failure only surfaces when a later
allocation returns NULL and the SYNOPSIS bridge dereferences it,
which the cFE reports as a generic `CFE_ES_CORE_APP_EXIT_EID` with no
causal context.

## Remediation

- Free the working buffer before reallocating in `SYN_RESET_CC`:
  `free(memory); memory = malloc(mem_req_bytes);` (or reuse the
  existing buffer if the size is unchanged).
- Validate the requirement: reject `mem_req_bytes == 0` and NULL
  `malloc` returns before calling `itc_app_init`.
- Add heap / buffer-count fields to the housekeeping packet so the
  depletion is observable from the ground before it becomes fatal.
