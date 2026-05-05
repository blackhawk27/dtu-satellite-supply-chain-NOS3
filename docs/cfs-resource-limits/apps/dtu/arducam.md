# arducam - DTU research (NOT LOADED in mission profile)

- **Source:** `nos3/components/arducam/fsw/`
- **Loaded by:** *not* in the active CFE_APP set for `sc-mission-config.xml`. Activated only by `sc-research-config.xml`.

## Role

CAN-bus camera component. Used in research mission profile for science-image acquisition demonstrations.

## When enabled

- Pipe depth 10, standard component pattern.
- Operates over CAN sim; bandwidth budget is sized for thumbnail-rate uplink.
- Image buffer ends up in DS or in CFDP; both downlink paths apply.

## Attack-surface notes (if enabled)

- Image content is unauthenticated. With ground-side trust placed on the image, an attacker who controls the CAN sim can deliver arbitrary frames.
