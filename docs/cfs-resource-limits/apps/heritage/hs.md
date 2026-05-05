# hs (Health & Safety)

- **Source:** `nos3/fsw/apps/hs/`
- **Loaded by:** `cfe_es_startup.scr` line 13 (priority 85, stack 16384). **Active in this build.**
- **Pipe depth:** 12 cmd / 32 evt / 1 wakeup (`hs_internal_cfg.h` `DEFAULT_HS_*_PIPE_DEPTH`)
- **MIDs:** CMD `0x18AE`, HK TLM `0x08AD`, wakeup driven by SCH
- **Tables:**
  - `hs_def_amt.c` - Application Monitor Table (which apps are watched)
  - `hs_def_emt.c` - Event Monitor Table (which event IDs trigger response)
  - `hs_def_xct.c` - Exec Counter Table
  - `hs_def_msgacts.c` - Action lookup
- **Crypto / link:** none

## Role

cFS Health & Safety watchdog. Polls the per-app execution counters at SCH wakeup; when an app's counter has not advanced within `MAX_RESTART_ACTIONS` cycles, fires the configured response (event, MID send, app restart, processor reset).

The 2026-05-03 doc rewrite explicitly notes that this build *does* load HS; earlier README versions claimed it was absent (that claim referred to an upstream NOS3 baseline before this repo's mission profile added it).

## Silent-failure modes

- A blank AMT row silently means "not watched"; an attacker who can edit the AMT can disable monitoring of any specific app without changing observable HS HK output.
- HS itself runs at priority 85, *below* HK (90) but above ci_lab/to_lab. If HS itself stops draining its wakeup pipe, no other app catches it.

## Attack-surface notes

- AMT/EMT tampering gives a quiet "kill switch" for the watchdog; pairs with the [executive-services.md](../../layers/executive-services.md) silent-restart behaviour.
- HS does not enforce CPU utilisation directly; it watches *execution counters*, which can be advanced by stub loops without doing real work.
- See also `nos3/cfg/nos3_defs/tables/lc_def_adt.c` and `lc_def_wdt.c` (LC tables) which are currently dirty in the working tree as of 2026-05-03.
