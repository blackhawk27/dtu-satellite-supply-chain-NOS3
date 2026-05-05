# 03.06 Telemetry to ELK (File-Tail Ingest)

This document describes the layer that connects the testbed's
log sources to the Kibana dashboards used to inspect the
mission. It pins to figure F7 in
[`../08-figures/figures.md`](../08-figures/figures.md). The
deeper end of the same layer (per-filter walk, per-panel
breakdown) lives in [`../05-elk/`](../05-elk/); this document
covers the layer's contract, sources, and failure modes.

## 1. The two source families

ELK ingests two flavours of file. Logstash distinguishes them
by an input tag and runs separate filter blocks per tag.

- **Software-Bus capture**:
  `nos3/attack_logs/cfs_god_view.json`. One newline-delimited
  JSON document per packet observed on the Software Bus.
  Tagged `software_bus`. Carries the MID, sequence, packet
  body fields, and a host timestamp.
- **System logs**: `nos3/omni_logs/*.log`. One file per
  capture source. Tagged `system_log`. Free-text format
  matched by per-source grok patterns.

The two families differ on volume and shape: the SB capture is
high-rate and structured; the system logs are bursty and
free-text. Logstash's filter pipeline at
`nos3/elk/logstash.conf` keeps the two paths separate up to
the index-output step.

## 2. The Software-Bus capture

`nos3/scripts/god_view_capture.py` is a Python process started
by `make launch`. It subscribes to the FSW telemetry stream
through TO_LAB's downlink port (the same port COSMOS uses) and
writes every observed packet to
`attack_logs/cfs_god_view.json` as a JSON document. The fields
are:

- `msg_id`: the 16-bit MID, decoded as an integer.
- `msg_name`: a human-friendly name resolved from the MID
  through a lookup table.
- `sequence`: CCSDS sequence count.
- per-app fields decoded from the packet body where the
  capture script has a decoder. The decoders for the EO1
  mission cover the EPS HK, GNSS HK, ADCS HK, MGR HK, and a
  few of the heritage cFS service apps.

The reason the capture lives in `attack_logs/` (and not
`omni_logs/`) is historical: this stream is the one the
adversary cares most about, because it shows the mission's
ground-truth telemetry as the operator sees it.

## 3. The system logs

Three capture scripts produce the per-source `omni_logs/*.log`
files:

- `nos3/scripts/sim_logs_capture.sh`: tails the simulator
  containers' stdout/stderr (one file per simulator). The
  simulators emit human-readable log lines describing bus
  reads, hardware-model state changes, and 42 IPC parse
  events.
- `nos3/scripts/cfs_evs_capture.sh`: subscribes to the cFE
  Event Services downlink and writes EVS event records to
  `omni_logs/cfs_evs.log`. EVS records are the canonical
  in-process diagnostic output from the cFS apps.
- `nos3/scripts/cpu_monitor.sh`: a host-side sampler that
  records CPU usage per process on a 1 Hz tick, written to
  `omni_logs/cpu_monitor.log`.

Each file is bind-mounted into the Logstash container at
`/var/log/nos3/omni/` so the file-input plugin can tail it.
The bind mounts are declared in
`nos3/elk/docker-compose.yml`.

## 4. Logstash filter pipeline

`nos3/elk/logstash.conf` is the single source of truth for
which fields end up in Elasticsearch. The pipeline has the
shape:

1. **Inputs**: one `file` input per source, with a
   `start_position => "beginning"` and `sincedb` disabled (the
   testbed prefers re-ingestion on restart over missing data).
2. **Filters**: a long sequence of `if` blocks keyed on the
   input tag and content patterns. Each block does one of:
   - parse a JSON line (for the SB capture);
   - apply a grok pattern (for the per-source system logs);
   - extract a numeric field with `mutate` and
     `convert => "float"` for graph-friendliness;
   - drop noise lines that would otherwise fill the index
     (CFE_SB no-subscribers messages, LC startup-transient
     watchpoints, GeoIP downloader complaints).
3. **Output**: every accepted document goes to Elasticsearch
   with a daily-rolling index name pattern
   `nos3-telemetry-YYYY.MM.DD`.

Numeric fields the testbed has explicit extractors for, drawn
from the per-component blocks:

- EPS: `eps_solar_power_w`, `eps_power_used_w`, `eps_battery_wh`, `eps_battery_mv`, `eps_solar_array_enabled`.
- Reaction wheel: `rw_momentum`.
- GPS / GNSS: `gps_lat`, `gps_lon`, `gps_alt`, `gps_ecef_x/y/z`, `gps_abs_time`.
- TT&C: status code, link state, downlink rate.
- CPU monitor: `TOTAL_CPU_PCT`, `CPU_PCT`, `NUM_PIDS`, `CMDCOUNTER`, `ERRCOUNTER`.

Added on 2026-05-03 (commits `0dcccd58`, `bc6369c8`, `7afa0107`):

- `gnss_lat`, `gnss_lon`: redundant per-GNSS lat/lon for the GS validation panels.
- `in_denmark_box`: boolean, derived from `gnss_lat in [55, 58]` AND `gnss_lon in [8, 16]`.
- `in_science_mode`: boolean, mirrored from MGR HK (`spacecraft_mode == SCIENCE`).
- `last_ping_seq`, `last_ping_time`, `ping_count`: GS Hello-Flow ack signals.

The same commit window also added (`f45ad877`) a NaN-scrub in `god_view_capture.py` (`math.isnan(x)` check) so early-tick 42 packets carrying NaN do not crash the Logstash JSON parser and drop the whole document.

The deep-dive at
[`../05-elk/01-logstash-filters.md`](../05-elk/01-logstash-filters.md)
walks every filter block.

## 5. Index template and lifecycle

Two configuration files in `nos3/elk/` shape the index:

- The index template at `nos3/elk/index_template.json`
  declares per-field types (long for sequence counts, float
  for telemetry values, keyword for app names). Without the
  template, Elasticsearch's dynamic mapping defaults often
  pick the wrong type (text instead of keyword) and break
  aggregation queries.
- The dashboard build script at
  `nos3/elk/build_kibana_dashboards.py` regenerates the
  `dashboards/nos3-eo1-dashboards.ndjson` artefact and uploads
  it to Kibana through the saved-objects API.

Indices roll daily by date stamp. The Kibana index pattern
`nos3-telemetry-*` is bound to the saved-object ID
`5b3163a0-3ea7-11f1-adf4-55f5fc5a104a`; that ID is what every
dashboard panel and visualisation references internally. The
index pattern survives index roll-over.

## 6. Two dashboards

The thesis surfaces two Kibana dashboards. Every claim in the
component deep-dives traces back to a panel on one of them.

- `nos3-std-telemetry-overview`: per-component HK panels (EPS
  power, GNSS lat/lon, ADCS rates, MGR mission state, CPU
  monitor). The base panel set for everyday operations.
- `nos3-std-mission-validation`: cross-component panels that
  surface the supply-chain attack signals (HK rate vs
  expected, EPS battery SOC integration vs noisy_app spoofed
  HK, log-line count vs expected baseline, EVS event-rate
  anomaly).

The deep-dive at
[`../05-elk/03-kibana-dashboards.md`](../05-elk/03-kibana-dashboards.md)
breaks each panel down by data source, query, and the attack
signal it surfaces.

## 7. Failure modes

The most common ELK-layer failures, in declining order of
frequency:

- **Stale index pattern field list**: a new field is added
  to a Logstash filter, but the Kibana index pattern's
  cached field list does not include it; new panels cannot
  visualise the field. The fix is
  `nos3/elk/refresh_index_pattern_fields.py`, which calls the
  Kibana API to re-scan the index. The 2026-05-03 update
  added a `REQUIRED_NEW_FIELDS` gate plus a 180 s
  `wait_for_new_fields()` polling loop so the refresh blocks
  until the seven new GS-Hello-Flow / GNSS / mode fields are
  present in the index, then refreshes; if the deadline is
  hit the script logs a warning but does not fail the launch.
- **Logstash filter dropping legitimate telemetry**: the noise
  drop rules at the bottom of `logstash.conf` are
  pattern-based; an EVS message that incidentally matches one
  of them is silently dropped. The catalogue of legitimate
  drop rules sits at `docs/noise-filters.md`. Divergence from
  that catalogue is the diagnostic for an attack against this
  layer.
- **Kibana hangs on a saved object loop**: an orphaned
  visualisation referencing a deleted index pattern causes
  Kibana to hang on dashboard load. Commit `0fda60f5` adds
  an orphan-purge step to the dashboard build script.
- **Elasticsearch GeoIP downloader exception**: a startup-
  time noise pattern; commit `e756a1fd` disables the GeoIP
  downloader to suppress the cold-start exception that would
  otherwise show up in the operator's first log review.

## 8. Why this layer matters for the thesis

ELK is the surface where the testbed surfaces attack evidence
to the operator. Two attack-surface positions sit on it:

- **Logstash filter tampering**: an attacker who can edit
  `logstash.conf` can drop attack telemetry between the FSW
  and the dashboard. The filter set is itself a supply-chain
  artefact.
- **Capture-script tampering**: an attacker who can edit
  `god_view_capture.py` or the per-source capture shell
  scripts can drop or rewrite events at the source. The
  scripts run on the host outside the FSW container.

Both positions are zone "Operations layer" in figure F8.

## 9. Cross-references

- Logstash configuration:
  `nos3/elk/logstash.conf` (1316 lines as of the slice 1
  baseline; the deep-dive at
  [`../05-elk/01-logstash-filters.md`](../05-elk/01-logstash-filters.md)
  walks each block).
- Capture scripts: `nos3/scripts/god_view_capture.py`,
  `sim_logs_capture.sh`, `cfs_evs_capture.sh`,
  `cpu_monitor.sh`.
- Index template: `nos3/elk/index_template.json`.
- Dashboard build: `nos3/elk/build_kibana_dashboards.py`.
- Dashboard NDJSON:
  `nos3/elk/dashboards/nos3-eo1-dashboards.ndjson`.
- Drop-rule catalogue: `docs/noise-filters.md`.
- Trust-boundary overlay: figure F8.
