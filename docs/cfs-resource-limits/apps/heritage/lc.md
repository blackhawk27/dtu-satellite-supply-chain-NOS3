# lc (Limit Checker)

- **Source:** `nos3/fsw/apps/lc/`
- **Loaded by:** `cfe_es_startup.scr` line 21 (priority 53, stack 32768)
- **Pipe depth:** 12 (`DEFAULT_LC_PIPE_DEPTH` in `lc_internal_cfg.h`)
- **MIDs:** CMD `0x18A4`, HK TLM `0x08A4`
- **Tables:**
  - `nos3/cfg/nos3_defs/tables/lc_def_wdt.c` - Watchpoint Definition Table
  - `nos3/cfg/nos3_defs/tables/lc_def_adt.c` - Actionpoint Definition Table

  Both files are **currently dirty in the working tree** as of 2026-05-03 (see `git status`); pending changes track the ADCS sensor-wakeup additions and FOV-exit recovery RTS from commit `8af2fdea`.

## Role

Watches MID fields against thresholds; when a watchpoint fires, evaluates an actionpoint expression and dispatches a stored-command sequence (typically via `sc`). The `validate_lc_rearm.sh` script in `debug/` is the runtime-rearm test.

## Silent-failure modes

- A WDT row pointing at an unmapped MID is silently skipped; no event raised at table load.
- An ADT expression that always evaluates false never fires regardless of WDT trips.

## Attack-surface notes

- WDT/ADT tampering disables limit checks without affecting observable HK output. An attacker who can write the tables can effectively disarm autonomous mission triggers.
- Hooks into `sc` (stored cmd) and `mgr` (mode manager) make LC a privileged trigger surface.
