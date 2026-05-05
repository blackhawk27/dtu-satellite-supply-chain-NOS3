# Thesis Testbed Documentation

This directory is the technical record of the DTU NOS3 satellite
simulation testbed used as the experimental platform for a bachelor
thesis on supply-chain attacks in satellite systems. The goal of
the document set is twofold: let a third party rebuild the testbed
exactly, and explain every divergence from upstream NOS3 in enough
detail to defend the thesis.

## Reading order

For the first pass, read in this order:

1. [`00-overview.md`](00-overview.md) - thesis framing, the supply-chain attack model, what NOS3 is, and what DTU added on top.
2. [`01-reproducibility.md`](01-reproducibility.md) - phase-by-phase clone-to-launch guide, mapped to the Makefile targets.
3. [`02-architecture.md`](02-architecture.md) - container model, process layout, trust boundaries, app inventory.
4. [`03-communication/`](03-communication/) - one document per communication layer, each backed by a figure in [`08-figures/figures.md`](08-figures/figures.md).
5. [`04-apps/`](04-apps/) - per-app deep-dives for every DTU-altered cFS app (EPS, ADCS cluster, communications, custom apps).
6. [`05-elk/`](05-elk/) - the ELK telemetry pipeline, end-to-end.
7. [`07-customizations-vs-upstream.md`](07-customizations-vs-upstream.md) - the authoritative divergence list, generated against the import baseline.
8. [`09-glossary.md`](09-glossary.md) - acronyms, message identifiers, key terms.

The mission-doc cross-cut sits in [`06-mission-doc-update.md`](06-mission-doc-update.md); it lists the surgical inserts applied to `docs/missions/NOS3-DTU-EO1-mission-implementation.md` once the rest of the set is in place.
