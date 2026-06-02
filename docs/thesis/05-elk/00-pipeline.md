# ELK pipeline: end-to-end

This document is the long-form description of the telemetry
pipeline whose wire-level boundary is documented in
[../03-communication/06-telemetry-to-elk.md](../03-communication/06-telemetry-to-elk.md).
The pipeline turns four host-side capture files into a queryable,
visualised record of an attack run. Every component on the path
is self-contained, restartable, and unaware of the others; the
shared filesystem under `nos3/attack_logs/` and `nos3/omni_logs/`
is the entire integration surface.

Figure F4 in [../08-figures/figures.md](../08-figures/figures.md)
draws the pipeline end-to-end: capture scripts on the left, the
two log directories in the middle, Logstash and Elasticsearch on
the right, and the Kibana dashboard set as the consumer surface.

## The four input layers

The Logstash pipeline (`nos3/elk/logstash.conf`, top comment
block) groups its inputs into four layers. Three of them are
direct telemetry-capture surfaces; the fourth is a ground-side
side-channel for command uplinks. The full list:

- **Layer 1: `software_bus`.** Input file
  `nos3/attack_logs/cfs_god_view.json`. One JSON line per CCSDS
  packet that `to_lab` puts on the wire, written by
  `scripts/god_view_capture.py`. Carries `msg_id` as a hex
  string, `sequence` as an integer, `length`, `timestamp` as a
  Unix-epoch float, and the raw payload bytes. This is the
  unfiltered Software Bus record from the spacecraft's point of
  view.
- **Layer 2: `system_log`.** Input glob
  `nos3/omni_logs/*.log` (with `tlm_hk_decoded.log` explicitly
  excluded). One file per running simulator
  (`nos3-<simname>.log`), one file for the cFS Event Service
  (`cfs_evs.log`), one file for the Docker stats sampler
  (`cpu_monitor.log`). Line format is mostly
  `TIMESTAMP [LEVEL] - Component::Method:  message text` but is
  not enforced; the filter section deals with the variance.
- **Layer 3: `hk_decoded`.** Input file
  `nos3/omni_logs/tlm_hk_decoded.log`. Also written by
  `god_view_capture.py`, but only for MIDs the script has a
  decoder for; each line is a JSON object containing the
  unpacked payload fields (e.g. `eps_battery_mv`,
  `gps_lat`) alongside the same identification fields as Layer
  1. This is the layer that backs every numeric Kibana panel.
- **Layer 4: `gs_uplink`.** Input file
  `nos3/omni_logs/gnss_uplinks.log`. Written by the COSMOS
  ground-side script `gnss_gs_responder_task.rb` on every
  successful command dispatch. The Layer 1 `cfs_god_view.json`
  never carries command packets (TO_LAB is downlink-only), so
  this side-channel is the only way to count uplinks honestly
  without aliasing them off housekeeping responses.

The earlier framing in
[../01-reproducibility.md](../01-reproducibility.md) and
[../03-communication/06-telemetry-to-elk.md](../03-communication/06-telemetry-to-elk.md)
discussed four capture *files* (`cfs_god_view.json`,
`cfs_evs.log`, `nos3-*.log`, `cpu_monitor.log`); the Logstash
view of the world is four input *types*, with the three log
files (`cfs_evs.log`, `nos3-*.log`, `cpu_monitor.log`) collapsing
into the single `system_log` type. Both framings are correct;
they differ only in where you put the boundary.

## Transport: bind mounts, not network

Logstash does not poll any port on the cFS side. Its file inputs
are bind-mounted directories from the host:

- `../attack_logs:/attack_logs:ro` for Layer 1.
- `../omni_logs:/omni_logs:ro` for Layers 2, 3, and 4.

This is declared in `nos3/elk/docker-compose.yml`. The mount is
read-only so Logstash cannot corrupt the underlying files even
if its pipeline crashes mid-write. The pipeline uses
`sincedb_path => "/dev/null"` on each input so that a Logstash
restart re-reads the files from the beginning. The intentional
side effect is that `make launch`'s automatic Logstash restart
re-indexes the entire log set for the current run, which
combined with the daily-rollover index name and the launch-time
index delete is what produces deterministic per-run Kibana
views.

## Filter dispatch

The filter section is a single `filter { ... }` block (about
1300 lines) structured as a dispatch on the `type` field set by
each input. The high-level shape:

- A small unconditional preamble normalises the `timestamp`
  field on `software_bus` documents from a Unix-epoch float to
  integer milliseconds, because Elasticsearch's `epoch_*` date
  parsers reject the scientific notation that Jackson emits for
  large floats.
- One block per type (`if [type] == "software_bus" { ... }`,
  `if [type] == "system_log" { ... }`, and so on) handles the
  type-specific extraction.
- A trailing tail handles cross-cutting concerns:
  spacecraft-mode transition detection (a Ruby filter
  maintaining a per-subsystem state map), human-readable mode
  aliases (`SAFE`, `SCIENCE`, and so on), and projection of the
  mode label onto GNSS housekeeping documents so dashboard
  panels can correlate ping round-trips by mode.

The filter logic is described in detail in
[01-logstash-filters.md](01-logstash-filters.md).

## Output and indexing

A single Elasticsearch output writes to
`nos3-telemetry-%{+YYYY.MM.dd}`. The index name is daily-rolled
so that a long-running stack does not produce one huge index;
the operational consequence is documented in
[02-elasticsearch-mappings.md](02-elasticsearch-mappings.md).

The output also writes a `rubydebug`-formatted copy of every
event to stdout. This is intentionally verbose: `docker logs
nos3-logstash` is the diagnostic of record when a Logstash
filter is silently dropping events.

There is no dead-letter queue, no upstream retry, and no
buffering layer between Logstash and Elasticsearch. The
single-node Elasticsearch is fast enough that Logstash's default
in-memory queue absorbs the spikes produced by `noisy_app`'s
broadcast-storm phase. The diagnostic for "events are missing in
Kibana" is `docker logs nos3-logstash` followed by `curl` against
the relevant index; `make doctor` automates the first three
layers of that walk.

## Consumer surface

Kibana attaches to Elasticsearch on
`http://nos3-elasticsearch:9200` (or, from the host, on
`http://localhost:9200`). The frontend serves fifteen saved
objects (fourteen dashboards plus one saved search) built by
`elk/build_kibana_dashboards.py`. The dashboards are listed in
[03-kibana-dashboards.md](03-kibana-dashboards.md); the build
mechanics are in
[04-build-and-load.md](04-build-and-load.md).

The Kibana index pattern `nos3-telemetry*` is created by the
same builder script on first launch. The pattern survives across
`make stop` because the pattern's saved-object UUID is stable
across runs; what changes is the set of indices the pattern
matches. The `refresh_index_pattern_fields.py` helper is run on
every `make launch` to keep the pattern's cached field list in
sync with the documents Logstash has just produced.

## Failure modes the pipeline is designed to surface

Three failure modes are surfaced as Kibana tags rather than
left for the operator to diagnose:

- **`sb_pipe_overflow`.** Set on `system_log` events from the
  cFS `cfe_sb` app when EVS emits `SB_PIPE_OVERFLOW`. Visible in
  the FSW health dashboard.
- **`attack_armed`, `attack_loaded`, `attack_trigger_ping`.**
  Set on `system_log` events from `NOISY_APP`. Visible in the
  "Thesis: Cyber-Physical Attack Radar" dashboard.
- **`spacecraft_mode_change`.** Set on `hk_decoded` events from
  the mission manager when its `spacecraft_mode` field
  transitions. Visible in the "Spacecraft Mode Changes"
  dashboard.

Each of these is a direct mapping from a known attack mechanic
or operational event onto a Kibana-queryable tag, and each was
added to the pipeline only after the underlying mechanic became
reproducible. The list will grow as new attack shapes are added
to the testbed; the place to grow it is the trailing tail of
the filter section.
