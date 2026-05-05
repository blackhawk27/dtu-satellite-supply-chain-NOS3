# 05.00 ELK Pipeline End-to-End

This sub-directory walks the testbed's ELK telemetry pipeline
in detail. The companion at
[`../03-communication/06-telemetry-to-elk.md`](../03-communication/06-telemetry-to-elk.md)
covers the same surface as one layer of the communication stack;
this directory drills into it for the operations-layer chapter
of the thesis.

The figure that pins this directory is F7 in
[`../08-figures/figures.md`](../08-figures/figures.md).

## 1. The four stages

A document moves through four stages between the FSW and a
Kibana panel:

1. **Capture**: a host-side script reads from the FSW (Software
   Bus, EVS) or from the simulator container stdout and writes
   the line to a file under `nos3/attack_logs/` or
   `nos3/omni_logs/`.
2. **Ingest**: Logstash file-input tails each file (Layer 1, 2,
   or 3 in `logstash.conf`'s vocabulary) and applies a sequence
   of filters.
3. **Index**: the filtered document goes to Elasticsearch's
   daily-rolling index `nos3-telemetry-YYYY.MM.DD` per the
   index template at `nos3/elk/index_template.json`.
4. **Render**: Kibana's saved-object index pattern resolves
   `nos3-telemetry-*` and the dashboard panels query that
   pattern.

Each stage is a separate document in this directory:

| # | Document | Stage | Source files |
|---|----------|-------|---------------|
| 1 | This document | Pipeline overview | - |
| 2 | [`01-logstash-filters.md`](01-logstash-filters.md) | Ingest | `nos3/elk/logstash.conf` |
| 3 | [`02-elasticsearch-mappings.md`](02-elasticsearch-mappings.md) | Index | `nos3/elk/index_template.json` |
| 4 | [`03-kibana-dashboards.md`](03-kibana-dashboards.md) | Render | `nos3/elk/dashboards/nos3-eo1-dashboards.ndjson` |
| 5 | [`04-build-and-load.md`](04-build-and-load.md) | Build / load tooling | `build_kibana_dashboards.py`, `load_dashboards.py`, `refresh_index_pattern_fields.py` |

## 2. The three input layers

`nos3/elk/logstash.conf` declares three file inputs, each
tagged for a separate filter pipeline:

- **Layer 1 - software_bus**:
  `nos3/attack_logs/cfs_god_view.json`. JSON-per-line. One
  document per CCSDS message observed on the cFS Software Bus;
  fields include `msg_id`, `sequence`, `timestamp`, `length`.
  Tagged `software_bus`. Written by
  `nos3/scripts/god_view_capture.py`.
- **Layer 2 - system_log**: `nos3/omni_logs/*.log`. Free-text
  per-component simulator logs plus the host-side
  `cpu_monitor.log` and `cfs_evs.log`. Tagged `system_log`.
  The line format is
  `TIMESTAMP [LEVEL] - Component::Method:  message text`,
  matched by per-source grok patterns.
- **Layer 3 - hk_decoded**:
  `nos3/omni_logs/tlm_hk_decoded.log`. JSON-per-line. Decoded
  HK payload telemetry with per-app fields. Also written by
  `god_view_capture.py`. Tagged `hk_decoded`. The Layer 2
  input excludes this filename so the file is parsed only
  once.

The "layer" naming is confined to Logstash; the rest of the
testbed uses the same word with different meaning (the
communication-layer documents under
[`../03-communication/`](../03-communication/) are unrelated).
A future sweep could rename to remove the overload, but the
word matches the in-source comments.

## 3. Pipeline steady-state

Once the testbed is running:

- Layer 1 receives roughly 100 Hz of SB packets.
- Layer 2 receives bursty per-tick simulator log lines plus
  EVS events at lower rate.
- Layer 3 receives one decoded-HK document per HK packet, so
  roughly the per-component HK rate (~1 Hz per app times the
  active component count).

Logstash's per-pipeline thread pool absorbs all three on a
modest host. The Elasticsearch index has
`refresh_interval: 5s` (per the index template) so the
dashboard updates lag the FSW by roughly that interval.

## 4. Cross-cutting filter behaviour

Three filter patterns appear repeatedly across the per-source
blocks; they live early in
[`01-logstash-filters.md`](01-logstash-filters.md):

- **Timestamp coercion**: an early `ruby` block at line 67 of
  `logstash.conf` converts any `timestamp` field from a Unix
  epoch float to integer milliseconds, because Elasticsearch's
  `epoch_millis` parser rejects scientific notation.
- **MID-to-name dictionary**: a `translate` filter at line 86
  uses a generated dictionary at
  `/etc/logstash/mid/mid_to_name.yml` (regenerated at
  `make config` time by
  `nos3/scripts/mid/gen_mid_registry.py`) to attach a
  human-friendly `msg_name` to every Layer 1 document.
- **Layer classification**: a sibling `translate` at line 98
  attaches a `layer` keyword (one of `FSW_CORE`,
  `FSW_HERITAGE`, `COMPONENT`, `RESEARCH`, `UNKNOWN`) using
  `mid_to_layer.yml`.

The `layer` field is the key the dashboards use to slice the
attack-classification panels; section 4 of
[`03-kibana-dashboards.md`](03-kibana-dashboards.md) walks the
panels.

## 5. Data view and dashboards

A single Kibana data view (saved-object ID
`5b3163a0-3ea7-11f1-adf4-55f5fc5a104a`) resolves the
`nos3-telemetry-*` index pattern. Every dashboard panel
references this ID. The build script at
`nos3/elk/build_kibana_dashboards.py` emits panels with this
hardcoded ID; the load script at
`nos3/elk/load_dashboards.py` re-imports the saved-objects
NDJSON; the refresh script at
`refresh_index_pattern_fields.py` re-populates the data view's
cached field list.

Two dashboards ship by default:

- `nos3-std-telemetry-overview`: per-component HK panels.
- `nos3-std-mission-validation`: cross-component panels for
  the supply-chain attack visibility.

## 6. Why this pipeline matters for the thesis

Three properties make the ELK layer the testbed's single
operations-layer surface:

- **Single source of truth for fields**: the Logstash filter
  is the only place where free-text simulator logs become
  structured Kibana fields. An attacker who edits the filter
  edits the dashboard.
- **Single source of truth for dashboards**: the NDJSON in
  `nos3/elk/dashboards/` is checked into the repo and is the
  artifact a fresh clone would re-import. Changes are
  reviewable in the diff.
- **Single source of truth for capture**: the Python and shell
  capture scripts under `nos3/scripts/` are the only writers
  to the log files Logstash reads. An attacker who patches the
  capture rewrites the dashboard while the FSW operates
  correctly.

Each of those is an operations-layer attack position in the
trust-boundary overlay (figure F8).
