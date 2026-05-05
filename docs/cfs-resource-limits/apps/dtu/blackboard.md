# blackboard - DTU research (NOT LOADED in mission profile)

- **Source:** `nos3/components/blackboard/fsw/`
- **Loaded by:** *not* in the active CFE_APP set for `sc-mission-config.xml`. Activated only by `sc-research-config.xml`.

## Role

Shared in-memory key-value store accessible from multiple apps. Used in research integrations to short-circuit the SB for high-rate cross-app state sharing.

## When enabled

- Pipe depth 10, standard component pattern.
- Backing memory lives in the cFE pool; size depends on table config.

## Attack-surface notes (if enabled)

- Bypasses SB. Any app that can write to the blackboard can affect any reader without going through SB pipe-overflow / EVS / HK observability paths.
- Make this an explicit allowlist in security-test scope.
