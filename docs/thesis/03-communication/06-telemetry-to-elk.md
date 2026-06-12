# Boundary 6: telemetry to Elasticsearch

This is the only boundary in the system that is entirely a DTU
addition. The upstream NOS3 emits stdout logs and some ad-hoc
files; nothing aggregates them and nothing turns them into a
queryable record. This fork adds four capture surfaces on the cFS
side, a Logstash pipeline that tails them, an Elasticsearch index
that stores the resulting documents, and a Kibana frontend that
serves dashboards over them. The full pipeline is documented in
detail in [../05-elk/](../05-elk/); the present document covers
the wire shape between the spacecraft side and the ELK side.

## The four capture surfaces

Four host-side files are the producers at this boundary. Each
sits inside the simulation Docker mount and is bind-mounted
read-only into the Logstash container.

- **`nos3/attack_logs/cfs_god_view.json`.** One JSON line per
  CCSDS packet that `to_lab` puts on the wire. Produced by
  `scripts/god_view_capture.py` (a host-side Python process,
  not a container), which binds to the same UDP socket that
  CryptoLib's ground side does and decodes the CCSDS header,
  MID, sequence count, function code, and the raw payload bytes
  into a single JSON object. Fields per document include
  `msg_id`, `msg_name`, `sequence`, `function_code`,
  `timestamp`, and the payload bytes encoded as a hex string.
  See [02-fsw-software-bus.md](02-fsw-software-bus.md) for what
  this file does and does not see.

- **`nos3/omni_logs/cfs_evs.log`.** The cFS Event Service stream.
  Produced by `scripts/cfs_evs_capture.sh`, which tails the EVS
  output socket on the cFS container. Each line is a human-
  readable event record: timestamp, app name, event id, event
  type (DEBUG, INFORMATION, ERROR, CRITICAL), event text.

- **`nos3/omni_logs/nos3-<sim>.log`.** One file per running
  hardware simulator. Produced by `scripts/sim_logs_capture.sh`,
  which spawns one `docker logs --follow` tail per running
  simulator container and writes each tail to its own file. The
  format is whatever the underlying simulator chose, which on
  this fork is mostly the
  `[timestamp] [LEVEL] [component] message` shape but is not
  enforced.

- **`nos3/omni_logs/cpu_monitor.log`.** Resource-usage samples
  against the cFS container. Produced by
  `scripts/cpu_monitor.sh`, which periodically polls
  `docker stats` and writes a structured line per sample with
  `TOTAL_CPU_PCT`, `CPU_PCT`, and `NUM_PIDS`.

The four producers are independent. Killing any one of them does
not affect the simulation; Logstash on the receiving side simply
sees the corresponding input go silent.

## The transport

Logstash tails six files via four `file` inputs declared at the
top of `nos3/elk/logstash.conf`. Each input sets a `type` field
that the filter section dispatches on:

- `type: software_bus` for `attack_logs/cfs_god_view.json`.
- `type: system_log` for the `omni_logs/*.log` glob (with
  `tlm_hk_decoded.log` explicitly excluded). This covers
  `cfs_evs.log`, every per-simulator `nos3-*.log`, and
  `cpu_monitor.log`.
- `type: hk_decoded` for `omni_logs/tlm_hk_decoded.log`, the
  JSON-per-line decoded housekeeping payloads also written by
  `god_view_capture.py` for MIDs the script has a decoder for.
- `type: gs_uplink` for `omni_logs/gnss_uplinks.log`, a
  side-channel produced by the COSMOS-side
  `gnss_gs_responder_task.rb` on every dispatched command
  (the comment in the input block explains why the side
  channel is necessary: the god-view JSON does not carry
  command packets, only telemetry).

The transport between the cFS container, the four capture
processes, and the Logstash container is not a wire; it is a
shared filesystem bind mount. The capture processes write to
`nos3/attack_logs/` and `nos3/omni_logs/` on the host; Logstash
sees the same files at `/attack_logs/` and `/omni_logs/` because
the compose file in `nos3/elk/docker-compose.yml` bind-mounts
both directories read-only into the container. This is why the
pipeline survives Logstash restarts: the file inputs use
sincedb to remember their position, so a restart picks up where
they left off.

## What Logstash does between the file and the index

The filter section of `logstash.conf` does three classes of
transformation. The full mapping is in
[../05-elk/01-logstash-filters.md](../05-elk/01-logstash-filters.md);
the high-level shape:

- **Software-bus filters** apply the MID-registry translation
  (turning `msg_id` into the human-readable `msg_name`,
  `mid_layer`, and `mid_subsystem` fields by lookup in the
  three YAML dictionaries under `cfg/build/elk/`), and tag
  specific MID and function-code combinations with attacker-
  relevant tags (`noisy_app_spam_target`, `beacon_cmd`,
  `gnss_ping_uplink`, and others).
- **System-log filters** apply a base `grok` to extract a
  timestamp, log level, and component, then a series of
  `grok` and `mutate` rules per `subsystem` and `app` value
  to extract numeric fields. The EPS, GNSS, reaction-wheel,
  and CPU-monitor numeric fields listed in
  [../00-overview.md](../00-overview.md) are produced here.
- **Cross-cutting tags** (`attack_armed`, `attack_loaded`,
  `attack_trigger_ping`, `sb_pipe_overflow`,
  `science_overfly`) come from filter blocks lower in the file
  and stitch together cFS event semantics with `noisy_app`
  attack semantics so a Kibana saved search can find a complete
  attack window in one query.

## The output and index shape

The pipeline output is a single Elasticsearch output that writes
to `nos3-telemetry-%{+YYYY.MM.dd}`. Daily rollover means a long-
running stack produces one index per UTC day; `make launch`
deletes all `nos3-telemetry-*` indices at the start of each run
so a launched run does not contaminate yesterday's archived data.
The Saved Object that backs the Kibana index pattern
(`nos3-telemetry*`) is created by the dashboard builder on first
launch and survives across `make stop`.

The index has no explicit mapping template; Elasticsearch infers
the field types from the documents Logstash sends. This is
intentional for a research testbed (it lets us add new tags
without writing a mapping migration) and would need to change for
a production deployment. The implications for field-cache
freshness are documented in
[../05-elk/01-logstash-filters.md](../05-elk/01-logstash-filters.md)
and are why `refresh_index_pattern_fields.py` is part of
`make launch`.

## Authentication and threat model

The bind-mounted file inputs are read-only on the Logstash side.
The four capture processes run host-side; the Logstash container
has no path back to them. The Elasticsearch container has no
security (`xpack.security.enabled=false`) but is only reachable
from the host loopback and from inside the `nos3-legacy-core` Docker
network. The whole pipeline is therefore one-way and forensically
useful: a compromised cFS app cannot corrupt the `cfs_god_view.json`
file (it is written by the host-side `god_view_capture.py`, not
by anything inside cFS) and cannot tamper with Elasticsearch
documents that have already been indexed.

What a compromised cFS app *can* do is starve the pipeline. If
`noisy_app` floods the Software Bus with a high-rate MID,
`god_view_capture.py` writes one JSON line per packet at the
same rate; the file grows quickly and `sim_logs_capture.sh` may
fall behind. The Logstash pipeline absorbs the spike but the
attack window may run for tens of seconds at hundreds of
megabytes per second through the JSON file. The `sb_pipe_overflow`
tag and the `attack_armed` and `attack_loaded` tags are designed
to surface exactly this shape so a saved search can find the
attack window even if the spike is short.

## Deviation from upstream

Every line of this boundary is a DTU addition. The four capture
scripts, the Logstash pipeline, the Elasticsearch / Kibana
compose, the MID-registry YAMLs and the seed under
`nos3/elk/seed_mid/`, the Python dashboard builder, the
field-cache refresh helper, the `make doctor` diagnostic, and
the `save-run` / `load-run` archive flow are all new relative to
the import baseline (`55ad2148`). They are enumerated as the
"telemetry" category in
[../07-customizations-vs-upstream.md](../07-customizations-vs-upstream.md).
