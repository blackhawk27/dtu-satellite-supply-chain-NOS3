# onair - DTU research (NOT LOADED in mission profile)

- **Source:** `nos3/components/onair/fsw/`
- **Loaded by:** *not* in the active CFE_APP set for `sc-mission-config.xml`. Activated only by `sc-research-config.xml`.

## Role

NASA OnAIR (autonomous reasoning) integration. Consumes telemetry, runs classifiers, fires actions.

## When enabled

- Pipe depth 10, standard component pattern.
- Needs subscription to multiple HK MIDs; this can press the SB destination-per-MID limit (16 destinations).

## Attack-surface notes (if enabled)

- Autonomous-trigger surface. A poisoned OnAIR model can fire arbitrary actions without ground intervention.
- Model artefact integrity should be in CS scope.
