# Reading order

These documents describe the DTU satellite supply chain testbed
(this repository, a fork of NASA's NOS3) in enough detail to clone,
rebuild, and reason about the system without running it. Each file is
self-contained narrative prose; cross-references are by relative link.

Read the files in this order. Later files assume the framing from
earlier ones; they do not repeat it.

1. [00-overview.md](00-overview.md)
   What NOS3 is, what this fork adds, and the research question the
   testbed exists to support.

2. [01-reproducibility.md](01-reproducibility.md)
   Clone-to-launch as a sequence of phases. Each phase has a single
   command and a verification step the reader can run.

3. [02-architecture.md](02-architecture.md)
   Container and process layout, trust boundaries, and an inventory
   of every flight-software application and simulator that runs at
   runtime.

4. [03-communication/](03-communication/)
   One file per wire-level boundary in the system. Start with
   `00-overview.md` in that directory.

5. [04-apps/](04-apps/)
   Per-application deep dives for the cFS apps and component
   simulators that were added or modified relative to the import
   baseline.

6. [05-elk/](05-elk/)
   The DTU telemetry pipeline: Logstash filters, Elasticsearch index
   mappings, Kibana dashboards, and how a fresh clone bootstraps
   them.

7. [06-mission-doc-update.md](06-mission-doc-update.md)
   Surgical inserts proposed against the upstream NOS3 mission
   documentation so it reflects what this fork changed.

8. [07-customizations-vs-upstream.md](07-customizations-vs-upstream.md)
   The authoritative divergence list against the import-baseline
   commit, grouped by category rather than by file.

9. [08-figures/figures.md](08-figures/figures.md)
   Numbered descriptions of every figure referenced by the other
   documents. Images are not embedded; this file describes them well
   enough that they can be produced separately.

10. [09-glossary.md](09-glossary.md)
    Acronyms, message identifiers, wire formats, and key terms used
    across the other documents.

The `_audit/` directory is reserved for review notes produced when
this set of documents is checked against the codebase. It is empty in
the initial commit.
