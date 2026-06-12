# Elasticsearch index and mappings

Elasticsearch is the storage layer of the pipeline. This
document describes the index shape, the field mappings the
pipeline relies on, and the operational properties of the
daily-rolled index pattern.

The cluster is a single-node Elasticsearch 7.17.10 running in
`nos3-legacy-elasticsearch` on the `nos3-legacy-core` Docker network, with
`xpack.security.enabled=false` and a 512 MB JVM heap. These are
the defaults from `nos3/elk/docker-compose.yml`; nothing in the
pipeline assumes a more capable cluster. The data lives on the
named volume `es_data_vol`, which is preserved across
`docker compose down` but is deleted by `make stop` for the
`nos3-telemetry-*` indices specifically.

## Index naming and rollover

The Logstash output writes to
`nos3-telemetry-%{+YYYY.MM.dd}`. Each document is routed to the
index named for its UTC date. The operational consequences are:

- A long-running stack produces one index per day. There is no
  ILM policy and no rollover by size; the pipeline assumes that
  a single day of test telemetry fits comfortably in a
  single-shard, single-replica index. On the reference
  devcontainer with `noisy_app` armed, one day of telemetry
  weighs in at a few hundred megabytes.
- `make launch` deletes every `nos3-telemetry-*` index at the
  start of every run, so a launch starts against an empty
  index. The `save-run` target sets `KEEP_TLM=1` on the
  matching `make stop` so an archived run survives across
  `make launch`.
- Kibana indexes against the index pattern `nos3-telemetry*`
  (with a trailing wildcard), which catches every daily index.
  The pattern is created by `elk/build_kibana_dashboards.py`
  on first launch and is treated as a stable saved object
  thereafter.

## Index template

The mapping is governed by `nos3/elk/index_template.json`,
which matches every index whose name begins with
`nos3-telemetry-`. The template is not currently applied
automatically; the comment at the top of the JSON records the
manual application command
(`curl -X PUT http://localhost:9203/_index_template/nos3-telemetry
-H 'Content-Type: application/json' -d @nos3/elk/index_template.json`).
This is a known operational gap; applying the template at first
launch is a candidate addition to the Makefile.

The template's settings are deliberately small:

- `number_of_shards: 1` and `number_of_replicas: 0`. Single-node
  ES has no replicas anyway; the explicit `0` avoids the
  cluster-health yellow state that Elasticsearch would
  otherwise enter while waiting for replica peers.
- `refresh_interval: 5s`. The default of `1s` is fine, but the
  pipeline runs against `god_view_capture.py` at a high
  document rate during `noisy_app`'s storm phase, and a 5s
  refresh halves the per-document indexing cost.
- `dynamic: true` and `date_detection: false`. Dynamic
  mapping is on so that adding a new numeric field in a
  Logstash filter does not require a mapping migration;
  date detection is off because Elasticsearch's date-format
  heuristics misinterpret most of the upstream string
  timestamps.

## Field mappings the pipeline depends on

The template declares explicit mappings for every field a
Kibana dashboard reads. The full list is in the JSON; the
load-bearing entries are summarised below.

### Identification and dispatch fields

- `type`, `subsystem`, `layer`, `app`, `attack_app` are all
  `text` with a `keyword` sub-field
  (`ignore_above: 64` to `256`). Kibana terms aggregations
  read the keyword sub-field.
- `msg_id` is the hex string form (`"0x094A"`); `msg_id_int`
  is the decimal `long`. Both forms exist because some
  dashboards filter on the hex string and some compute on the
  integer.
- `msg_name`, `pkt_type`, `msg_direction` are keyword-typed
  text. `msg_name` carries the resolved MID name from the
  registry translation; `pkt_type` is `CMD` or `TLM`;
  `msg_direction` is `UP` (ground to spacecraft) or `DOWN`.

### Time fields

- `@timestamp` is the document's Logstash-side timestamp
  (`date`, default ISO format).
- `timestamp` is the spacecraft-side timestamp in
  `epoch_millis` form, normalised by the Layer 1 preamble.
- `sim_timestamp_unix` is a `double` (Unix seconds); the
  Layer 3 `hk_decoded` documents use this so that
  dashboards can keep the precision of the spacecraft clock.
- `sim_timestamp_iso` is the same value converted to ISO; this
  is the field date-histogram panels prefer.

The two-timestamp model exists because the spacecraft clock
(deterministic, locked to the NOS Engine ticker) and the host
clock (subject to drift) diverge in interesting ways during
attack runs. Most analyses use `timestamp`; the
`spacecraft_mode_change` panels in particular need the
spacecraft clock so that mode transitions align with cFS event
timestamps rather than with the Logstash receive time.

### EVS fields

- `evs_app_name`, `evs_event_id`, `evs_event_type_num`,
  `evs_severity`, `evs_message`, `evs_raw`. The severity is
  the keyword Kibana groups on; the numeric event id is the
  filter the attack-radar dashboard uses to find specific
  events like the `EPS HK SPOOF sent on 0x091A` event.

### Mode and mission

- `mgr_mode`, `mgr_mode_num`, `spacecraft_mode`,
  `spacecraft_mode_name`, `mode_change_label`,
  `mission_event`. The two `*_mode` fields come from the
  EVS-text extraction (Layer 2); the two `spacecraft_*`
  fields come from the housekeeping-payload extraction
  (Layer 3) and the cross-cutting Ruby filter. Both are
  preserved because different dashboards key off different
  fields, depending on whether they care about the
  Logstash-observed view or the bus-observed view.

### Limit checker and mission event

- `lc_state`, `lc_ap_num`, `lc_ap_to`, `sci_pass_count`.
  `lc_state` is the limit-checker state at the moment of the
  event; `lc_ap_num` is the action-point number; `lc_ap_to`
  is the new state name (`PASS`, `FAIL`, `STALE`); the tag
  `lc_actionpoint_fired` is added when the value is `FAIL`.

### GPS fields

- `gps_valid` (`boolean`), `gps_lat`, `gps_lon`, `gps_alt`,
  `gps_ecef_x`, `gps_ecef_y`, `gps_ecef_z` (`double`),
  `gps_abs_time`, `gps_leap_seconds` (mapping has the latter
  as text because the field is a string in the upstream
  device telemetry), and `gps_location` (`geo_point`). The
  `geo_point` is composed from `gps_lat` and `gps_lon` at
  Logstash time so the "Mission: Denmark" dashboard can
  render an orbital track without per-query computation.

### Bus health

- `sequence` (`long`), `length` (`long`), `apid` (`long`),
  `sb_seq_wrap` (`boolean`), `sb_gap_size` (`long`). The
  sequence-gap fields are produced by a small accumulator in
  the Layer 1 filter (not described here; see the source for
  detail) and are the canonical place to look for forged
  publishers.

The full mapping has roughly forty explicit fields and
unbounded room for dynamically-mapped additions. The fields
not listed above are mostly per-component numerics
(`eps_*`, `rw_*`, `TOTAL_CPU_PCT`, and so on) whose mappings
are straightforward `double` and `long` types.

## Failure modes and the field-cache problem

Kibana caches the list of fields for each index pattern as part
of the saved-object document. When Logstash starts producing a
new field (e.g. a new tag added by a filter), the new field
exists in Elasticsearch documents but not in the cached field
list. Visualisations that try to filter on the new field
render "No data" until the cache is refreshed.

The fix is `elk/refresh_index_pattern_fields.py`, which calls
the Kibana saved-objects API to rewrite the cached field list.
It is run automatically on every `make launch` and can be
re-run any time with `make refresh-kibana-fields`. The script
itself is small (about 80 lines) and is the second of the two
operational helpers that exist purely to work around Kibana
7.17's cache semantics; the first is `make doctor`.

## Operational verification

The end-to-end verification queries from
[../01-reproducibility.md](../01-reproducibility.md) Phase 6 hit
this layer directly:

- `curl http://localhost:9203/_cat/indices/nos3-telemetry-*?v`
  must show at least one index in yellow or green health.
- `_count?q=type:software_bus` must be greater than zero
  within roughly a minute of launch.
- `_count?q=type:system_log` likewise.

Two further queries are useful during attack-run analysis:

- `_count?q=tags:attack_armed` is non-zero if `noisy_app`
  has armed during the run.
- `_count?q=tags:sb_pipe_overflow` is non-zero if the storm
  phase has reached at least one subscriber.

A non-zero count on either tag with a zero count on the other
is a sign that the attack window is partially captured; the
fix is to extend the run and let the storm propagate.
