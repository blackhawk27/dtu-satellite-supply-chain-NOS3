# 05.01 Logstash Filter Walkthrough

This document walks `nos3/elk/logstash.conf` block-by-block. The
file is roughly 1316 lines long; the structure is:

- `input { ... }`: file inputs, three of them (Layer 1, 2, 3).
- `filter { ... }`: per-layer filter pipelines, with a
  shared timestamp-coercion block at the top.
- `output { ... }`: a single Elasticsearch output and a
  stdout `rubydebug` codec for live debugging.

This document is organised by the same structure.

## 1. Inputs (lines 24-58)

Three `file` inputs, all with `start_position => "beginning"`
and `sincedb_path => "/dev/null"`. The deliberate choice is to
re-ingest from the start of every run rather than persist a
since-db; this matches the testbed's convention of starting from
clean per launch.

- Layer 1: `/attack_logs/cfs_god_view.json`, `codec => "json"`,
  `type => "software_bus"`. Each line is a JSON object decoded
  by Logstash's JSON codec.
- Layer 2: `/omni_logs/*.log`, with an explicit `exclude` for
  `tlm_hk_decoded.log` (so the Layer 3 input gets it
  exclusively), `type => "system_log"`. Free-text lines.
- Layer 3: `/omni_logs/tlm_hk_decoded.log`, `codec => "json"`,
  `type => "hk_decoded"`. Decoded HK payloads.

All three paths are inside the Logstash container and are
bind-mounted from the host repo by `nos3/elk/docker-compose.yml`.

## 2. Universal preamble (lines 60-77)

The first block of `filter` runs on every document regardless
of type. A single `ruby` filter:

```
if [timestamp] {
  ruby {
    code => 'ts = event.get("timestamp");
             event.set("timestamp", (ts.to_f * 1000).to_i)
                if ts.is_a?(Numeric)'
  }
}
```

The reason is in the comment block at lines 67-72:
`cfs_god_view.json` (Layer 1) and `tlm_hk_decoded.log`
(Layer 3) emit the field as a Unix epoch float like
`1777207816.124`. Elasticsearch's `epoch_millis` date parser
rejects scientific notation (which Jackson, the Ruby JSON
encoder, can emit for large floats). Coercing to an integer in
millisecond units sidesteps the issue.

## 3. Layer 1 - software_bus (lines 78-380)

The bulk of the per-document enrichment for SB messages.

### 3.1 MID -> name translate (lines 86-95)

```
translate {
  field => "msg_id"
  destination => "msg_name"
  dictionary_path => "/etc/logstash/mid/mid_to_name.yml"
  refresh_interval => 60
  fallback => "UNKNOWN_MSG"
}
```

The dictionary is generated at `make config` time by
`nos3/scripts/mid/gen_mid_registry.py` from the FSW
`*_topicids.h` and `*_msgid_values.h` headers. The
`refresh_interval => 60` lets a fresh re-run of `make config`
re-shape the lookup table without restarting Logstash.

### 3.2 Layer translate (lines 98-107)

A second `translate` populates a `layer` keyword from
`mid_to_layer.yml`, also generated at `make config` time. The
layer values, per the file header comment block, are:

- `FSW_CORE`: cFE OS services (ES, EVS, SB, TBL, TIME).
- `FSW_HERITAGE`: standard cFS apps (TO, CI, DS, FM, CF, LC,
  SC, SCH).
- `COMPONENT`: per-hardware-component apps (EPS, GPS, RW,
  Radio, ADCS, ...).
- `RESEARCH`: DTU custom apps (`BEACON_APP`).
- `UNKNOWN`: unrecognised MID.

### 3.3 Per-app subsystem extraction (lines 108-180)

A series of `if` blocks, keyed on the human-friendly `msg_name`,
attach `subsystem` and additional tags. Two examples:

- The `noisy_app_spam_target` tag fires when MGRR a packet from
  the `0x091A` MID arrives that is not the legitimate EPS
  publication (line 132). This is the in-band detection
  signal for the noisy_app attack.
- The `beacon_cmd` tag fires when the `BEACON_APP_CMD_MID` is
  observed (line 137).

### 3.4 EVS-line decoding (lines 184-290)

`software_bus` documents whose payload is an EVS event get
parsed by a sequence of `grok` and `mutate` filters:

- The first `grok` at line 184 extracts the EVS app name,
  event ID, event-type number, and message text.
- A `mutate` chain at line 230 converts the event-type number
  to integer and assigns a human-readable `evs_severity` field
  (`DEBUG`, `INFORMATION`, `ERROR`, `CRITICAL`).
- Per-app severity exceptions (lines 250-289) override the
  default. For example, LC's "AP failed while passive" event 60
  is emitted at INFORMATION level by the source but gets
  promoted to ERROR by a default mapping; the override here
  reverses that. This is the noise-sweep work documented at
  `docs/noise-filters.md`.

### 3.5 Attack-tagging (lines 295-320)

A small block of attack-relevant tags:

- `attack_app => "NOISY_APP"`, `attack_armed`,
  `attack_trigger_ping`, `attack_loaded`: the noisy_app
  scenario steps.
- `beacon_pong`: a beacon_app pong telemetry packet observed.
- `sb_pipe_overflow`: a backpressure indicator on the SB.

### 3.6 Per-component grok groups (lines 320-380)

Per-app specific extraction:

- MGR mode-number to mode-name (line 373); `mgr_mode_num`
  becomes `mgr_mode` of `SAFE` or `SCIENCE`.
- LC actionpoint tagging (lines 387-392): if `lc_ap_to ==
  "FAIL"`, add the `lc_actionpoint_fired` tag.
- `science_overfly` tag and `mission_event` field.

The `attack_app` field is the surface the
`nos3-std-mission-validation` dashboard uses for its
attack-overlay panel.

## 4. Layer 2 - system_log (lines 380-1100)

Per-source grok pipelines. Each simulator log file gets its own
`if [path] =~ "..."` block. Representative examples:

### 4.1 EPS (per its log filename)

```
grok {
  match => { "sim_message" => "EPS_TLM solar=%{NUMBER:eps_solar_power_w}
              used=%{NUMBER:eps_power_used_w}
              balance=%{NUMBER:eps_power_balance_w}
              wh=%{NUMBER:eps_battery_wh}
              mv=%{NUMBER:eps_battery_mv}
              soc=%{NUMBER:eps_battery_soc_pct}
              sun=%{NUMBER:eps_solar_array_enabled}" }
}
mutate {
  convert => {
    "eps_solar_power_w"        => "float"
    "eps_power_used_w"         => "float"
    ...
  }
}
```

This is the consumer of the composite EVS line emitted by the
EPS sim's `update_battery_values`
([`../04-apps/eps.md`](../04-apps/eps.md) section 5.7). The
single-line emission means each Logstash document carries every
EPS field; joint aggregations work without `bool: must` joins.

### 4.2 Per-app key=value patterns

`beacon_app` emits its counters as `key=value` pairs (see
[`../04-apps/beacon_app.md`](../04-apps/beacon_app.md) section
4); Logstash's `kv` filter is enabled for those lines. No
custom grok pattern is needed.

### 4.3 Truth42 / torquer / sample (commit `6c67213e`)

The three filter sections added in commit `6c67213e`:

- `nos3-truth42`: extracts `SC[0].PosN` (`truth_pos_x/y/z`),
  `SC[0].VelN` (`truth_vel_x/y/z`), and the attitude
  quaternion (`truth_q0..q3`) from a 42-side dump log.
- Torquer: extracts torquer current and dipole.
- Sample: regression-baseline canary fields.

### 4.4 GPS and other component groks

Per-component groks for GPS lat/lon/alt, ECEF, abs_time
(line ~700 onwards); CPU monitor (`TOTAL_CPU_PCT`,
`CPU_PCT`, `NUM_PIDS`); reaction-wheel momentum
(`rw_momentum`).

## 5. Drop rules (near end of filter)

The legitimate noise-drop rules:

- `CFE_SB no-subscribers` messages (commit `1d296fe9`): the SB
  router emits these for every TLM MID on a single-FSW peer
  setup; harmless but high-volume.
- LC startup-transient float-NaN watchpoint events
  (commit `dde4881a`): the LC app rapidly fires watchpoints
  during the first 1-2 seconds before all sensors have
  populated.
- GeoIP downloader cold-start exceptions (commit `e756a1fd`):
  the Elasticsearch GeoIP downloader is disabled in the
  testbed; the residual exception is dropped.

The catalogue at `docs/noise-filters.md` is the canonical list.
Divergence from that catalogue is the operations-layer attack
detection.

## 6. Layer 3 - hk_decoded (the MGR mode tail block)

The very end of `filter { ... }` (the tail visible in the
diagnostic read) handles the `hk_decoded` documents. The MGR
mode-change tagger uses a per-process Ruby-state map:

```
@prev_mode_map ||= {}
sub = event.get("subsystem") || "MGR"
new_mode = event.get("spacecraft_mode")
prev = @prev_mode_map[sub]
if prev && prev != new_mode
  event.tag("spacecraft_mode_change")
  event.set("spacecraft_mode_prev", prev)
  event.set("spacecraft_mode_new",  new_mode)
  event.set("mode_change_label",
            "#{prev} -> #{new_mode}")
end
@prev_mode_map[sub] = new_mode
```

Per-process state is acceptable because the testbed runs a
single Logstash instance with a single pipeline. The mode-name
aliases (`SAFE`, `STANDBY`, `SCIENCE`, `UNKNOWN`) are added by
the static `if [spacecraft_mode] == N` chain immediately
after.

## 7. Output (final block)

```
output {
  elasticsearch {
    hosts => ["elasticsearch:9200"]
    index => "nos3-telemetry-%{+YYYY.MM.dd}"
  }
  stdout { codec => rubydebug }
}
```

Two outputs:

- Elasticsearch on the `nos3-core` Docker network. The index
  name is templated from the document timestamp; daily roll.
- Stdout in `rubydebug` codec. This is the surface a developer
  uses to confirm a fresh filter change parses; it duplicates
  the Logstash container stdout.

## 8. Cross-references

- Per-component filter blocks: each `04-apps/<name>.md` ELK
  section.
- Index template: [`02-elasticsearch-mappings.md`](02-elasticsearch-mappings.md).
- Dashboard panel queries: [`03-kibana-dashboards.md`](03-kibana-dashboards.md).
- Drop-rule catalogue: `docs/noise-filters.md`.
