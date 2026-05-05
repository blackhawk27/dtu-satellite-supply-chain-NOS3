# sample - reference component

- **Source:** `nos3/components/sample/fsw/`
- **Loaded by:** `cfe_es_startup.scr` line 43 (priority 71, stack 32768)
- **Pipe depth:** 10
- **MIDs:** CMD `0x18FA`, HK TLM `0x08FA`, send-HK `0x18FB`

## Role

Reference / template app shipped with NOS3. Loaded in the mission profile as a known-good app slot.

## Silent-failure modes

- None of operational consequence; sample acts as a no-op heartbeat.

## Attack-surface notes

- Useful as a control in security testing: any anomalous behaviour in `sample` HK is almost certainly external (SB stress, ES timing, EVS muting) rather than the app itself.
