# 05.02 Elasticsearch Mappings

This document describes the Elasticsearch index template at
`nos3/elk/index_template.json`. The template fixes the per-field
data type, the index settings, and (implicitly) the lifecycle
behaviour of the daily-rolling index pattern
`nos3-telemetry-YYYY.MM.DD`.

## 1. Why an explicit template

Without an explicit template, Elasticsearch's dynamic mapping
guesses each field's type from the first observed document.
Two failure modes follow:

- A keyword field that arrives as a digit string ("0917") gets
  guessed as a long; a later document with a non-numeric value
  on the same field is rejected by the index with a type
  mismatch.
- A free-text field that arrives short ("OK") gets guessed as
  a keyword; a later long log message on the same field is
  truncated at 256 bytes.

The testbed's index template makes the type explicit for every
field that the dashboards aggregate against, plus a per-field
`fields.keyword` sub-mapping that lets a `text`-typed field
also participate in aggregations through its keyword variant.

## 2. The template structure

`index_template.json` declares:

- `index_patterns: ["nos3-telemetry-*"]`: the template binds to
  every daily-rolling index.
- `priority: 500`: high enough to override any default
  templates Elasticsearch auto-installs.
- `_meta.description`: a human-readable note pointing at the
  source of the template.
- `template.settings.index`:
  - `number_of_shards: 1`. Single-host testbed; a single shard
    is the natural choice and avoids the cross-shard doc-count
    drift Elasticsearch's `total_hits` reports under high
    indexing rate.
  - `number_of_replicas: 0`. Single-node cluster; no replica
    set is meaningful.
  - `refresh_interval: 5s`. The dashboard's lag from FSW
    emission is at most this interval.
- `template.mappings.dynamic: true`: fields not named in
  `properties` are still accepted, with a guessed type.
- `template.mappings.date_detection: false`: prevents
  Elasticsearch from auto-coercing string fields that happen to
  look like dates.

## 3. Per-field property table

The template's `properties` block names every load-bearing
field. Representative entries (drawn from the live template
file):

| Field | Type | Notes |
|-------|------|-------|
| `@timestamp` | date | The Logstash-set ingestion timestamp. |
| `timestamp` | date, format `epoch_millis` | The FSW-or-sim emit time, post-coercion (see [`01-logstash-filters.md`](01-logstash-filters.md) section 2). |
| `docker_ts` | date with multiple format fallbacks | Container-side timestamps from `docker logs`. |
| `sim_timestamp_unix` | double | Some sims emit an explicit Unix double; kept verbatim. |
| `subsystem` | text + `keyword` (256 bytes) | The component name (`EPS`, `MGR`, ...). |
| `msg_id` | text + `keyword` (32 bytes) | CCSDS hex-string MID. |
| `msg_id_int` | long | Integer form for numeric range queries. |
| `msg_name` | text + `keyword` (128 bytes) | Human-friendly name from the MID dictionary. |
| `msg_direction` | text + `keyword` (32 bytes) | `CMD` or `TLM`. |
| `pkt_type` | text + `keyword` | CCSDS packet type. |
| `layer` | text + `keyword` | One of `FSW_CORE`, `FSW_HERITAGE`, `COMPONENT`, `RESEARCH`, `UNKNOWN`. |
| `app` | text + `keyword` | App name. |
| `attack_app` | text + `keyword` | Attack-relevant app, set by per-app rules. |
| `apid` | long | CCSDS APID. |
| `sequence` | long | CCSDS sequence count. |
| `length` | long | Packet length. |
| `sb_seq_wrap` | boolean | Sequence-counter wraparound observed. |
| `sb_gap_size` | long | Gap size between sequence numbers. |

The full list (around 200 fields) covers per-component
extracted numeric fields (`eps_*`, `gps_*`, `rw_*`,
`truth_q0..q3`, `cpu_*`, `mgr_mode_num`, etc.) and the
attack-tag set. Inspecting the file is straightforward; this
document does not replicate it in full.

### 3.1 GS Hello-Flow / GNSS / mode fields (added 2026-05-03)

Commit `bc6369c8` added seven new field mappings to the index template. These power the GNSS GS validation AND-gate panel, the freshness tile row, and the spacecraft-mode chip context. They must be present in the index template *before* the first document with that field arrives, otherwise dynamic mapping will pick the wrong type and the panel will not render.

| Field | Type | Source | Powers |
|---|---|---|---|
| `gnss_lat` | float | god_view_capture per-tick | AND-gate, ground-track panel |
| `gnss_lon` | float | god_view_capture per-tick | AND-gate, ground-track panel |
| `in_denmark_box` | boolean | Logstash derived (lat/lon range) | AND-gate, geofence dashboards |
| `in_science_mode` | boolean | Logstash derived from MGR HK | AND-gate, mode chip |
| `last_ping_seq` | long | beacon_app `pingcounter` -> Logstash kv | AND-gate (advancing-seq check) |
| `last_ping_time` | date | god_view_capture timestamp | Freshness tile, latency line panel |
| `ping_count` | long | Logstash counter accumulator | AND-gate (count > 0 check) |

`refresh_index_pattern_fields.py` was extended in the same commit with a `REQUIRED_NEW_FIELDS` list that holds the seven names; the script polls Elasticsearch for up to 180 s for those fields to materialise before refreshing the Kibana index-pattern field cache. Without this gate the dashboard reload would race the data and pick up an empty schema.

## 4. The `text` + `keyword` pattern

Almost every textual field appears as both a `text`-typed
parent and a `keyword` sub-field with `ignore_above`. This is
the standard Elasticsearch idiom that lets:

- Free-text search work against the parent (analyzed,
  case-folded).
- Aggregations and exact match queries work against
  `<field>.keyword` (raw byte sequence).

The `ignore_above` cap (32, 64, 256, 1024 bytes by field) is
chosen per field by expected length. For example, `msg_id` caps
at 32 (a hex MID is 6 characters max in this testbed); long
EVS messages cap at 1024 bytes.

## 5. Dynamic fields

The template sets `dynamic: true`, so any field a Logstash
filter introduces but the template does not name still flows
into the index. The trade-off is that the field's type comes
from Elasticsearch's first-document guess.

The convention for the testbed: any field named in a panel
must appear in the template's `properties`. This is enforced by
the dashboard build script
(`nos3/elk/build_kibana_dashboards.py`) when it cross-checks
the data view's field cache; missing-field references error out
at build time rather than at panel-render time.

## 6. Lifecycle

The template does not set an explicit ILM policy. The testbed
relies on manual cleanup:

```
curl -X DELETE "http://localhost:9200/nos3-telemetry-*"
```

(per the project's standard wipe-and-rerun command in
[`../01-reproducibility.md`](../01-reproducibility.md)).

For a longer-running deployment, an ILM policy that closes
indices older than seven days and deletes them after thirty
would be a natural fit. The single-node testbed has not needed
this.

## 7. Verification

- `curl http://localhost:9200/_index_template/nos3-telemetry`
  returns the template.
- `curl http://localhost:9200/nos3-telemetry-*?include_type_name=true`
  shows the daily indices and their effective mappings.
- Field-type sanity check: a query that aggregates against
  `eps_power_used_w` returns numeric histograms (not the
  text-aggregation error Elasticsearch raises on a
  misclassified field).
