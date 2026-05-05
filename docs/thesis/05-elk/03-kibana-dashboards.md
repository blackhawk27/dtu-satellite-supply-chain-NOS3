# 05.03 Kibana Dashboards

This document describes the two Kibana dashboards that ship
with the testbed. The source artefact is
`nos3/elk/dashboards/nos3-eo1-dashboards.ndjson`; the build
script that produces it is
`nos3/elk/build_kibana_dashboards.py`. This document is
organised by panel; each panel entry names the data view, the
query, the visualisation type, and the attack signal it
surfaces.

## 1. The two dashboards

The testbed ships two "Standard" dashboards:

- **`nos3-std-telemetry-overview`**: per-component HK panels.
  The base panel set the operator reads to confirm telemetry
  is flowing.
- **`nos3-std-mission-validation`**: cross-component panels
  that surface supply-chain attack signals: HK rate vs
  expected, EPS battery SOC vs noisy_app spoofed HK,
  log-line count vs baseline, EVS event-rate anomaly.

Both dashboards reference data view
`5b3163a0-3ea7-11f1-adf4-55f5fc5a104a` (the canonical
`nos3-telemetry-*` data view).

## 2. nos3-std-telemetry-overview

Built top-to-bottom by section. The build script's
markdown-section headers split the dashboard into rows:

### 2.1 Health row

- **Panel: cFE Process Up**. Lens line chart against
  `event.dataset:"system_log" AND subsystem.keyword:"FSW"`.
  Fires `0` when the FSW process is silent for >5 s.
- **Panel: HK rate per component**. Bar chart of HK packet
  count per minute, split by `subsystem.keyword`. Used to
  confirm every component is publishing.

### 2.2 Power row

- **Panel: EPS Power Balance**. Lens line of
  `eps_solar_power_w` and `eps_power_used_w` over time. The
  difference visualises charging and discharging arcs.
- **Panel: Battery SOC vs Eclipse**. Lens line of
  `eps_battery_soc_pct` overlaid with `eps_solar_array_enabled`
  (the in-sun proxy). Eclipse drives SOC down; sun arc drives
  it up; the hysteresis band is visible as a 90-100 % bracket.

### 2.3 Attitude row

- **Panel: ADCS Mode Timeline**. Discrete time-series of
  `mgr_mode` (and the mode-change tag from
  [`01-logstash-filters.md`](01-logstash-filters.md) section
  6) plus the ADCS-app-internal mode field.
- **Panel: Reaction Wheel Momentum**. Lens line, split by
  wheel index, of `rw_momentum`.
- **Panel: Magnetic Field Vector**. Lens line of `mag_field_x/y/z`.
- **Panel: IMU Angular Rates**. Lens line of `imu_rate_x/y/z`.
- **Panel: CSS Sun Vector**. Lens line of `css_svn_x/y/z`.
- **Panel: GPS Ground Track**. Map panel using `gps_lat`,
  `gps_lon`. Shows the Denmark overflies under the EO1 orbit
  profile.

### 2.4 Comms row

- **Panel: TT&C Downlink Status** (commit `6c6b2029`). Bar of
  `ttc_link_state` ratios per minute.
- **Panel: Beacon Ping/Pong**. Line of `pingcounter`. A
  monotonic series confirms the ground-link round trip.

### 2.5 Events row

- **Panel: Recent EVS Events**. Lens table of the last 20
  EVS lines, severity-coloured. The default search filter is
  `evs_severity:WARNING OR evs_severity:ERROR OR evs_severity:CRITICAL`.

## 3. nos3-std-mission-validation

The mission-validation dashboard adds attack-overlay panels.

### 3.1 Attack overlay row

- **Panel: Attack Tags Active**. Tag-count bar of the
  `attack_armed`, `attack_loaded`, `attack_trigger_ping`,
  `noisy_app_spam_target` tags. Each tag corresponds to a
  Phase-N step in the supply-chain research.
- **Panel: EPS HK rate vs spoofed HK rate**. Line chart split
  by `attack_app.keyword`. The legitimate `EPS` line is the
  reference; a `NOISY_APP` line above zero is the in-band
  detection signal for the noisy_app attack.

### 3.2 Mission-mode row

- **Panel: Spacecraft Mode Timeline**. Discrete time-series
  of `spacecraft_mode_name` (from the MGR mode-change tagger
  in Logstash, see
  [`01-logstash-filters.md`](01-logstash-filters.md) section
  6). Mode flips during attack scenarios are the surface this
  panel exposes.
- **Panel: Boot Counter Trend**. Lens line of
  `mgr_boot_counter`. A rising trend within a single launch
  is anomalous and signals the persistence-file attack.

### 3.3 Resource-exhaustion row

- **Panel: FSW Memory Footprint**. Lens line of `MEM_USED`
  (from CPU monitor). Used to visualise the SYN heap-leak
  attack from
  [`../04-apps/syn.md`](../04-apps/syn.md).
- **Panel: CPU Load**. Lens line of `TOTAL_CPU_PCT` and
  `CPU_PCT` per container.

### 3.4 Telemetry-truth cross-check

- **Panel: Truth Attitude vs ADCS Estimate** (commit
  `6c67213e`). Two superimposed quaternion time-series:
  Truth42 (`truth_q0..q3`) and the ADCS HK quaternion
  estimate. Divergence between them flags either a
  measurement-spoofing attack on a sensor sim or a tampered
  ADCS estimator.
- **Panel: GPS Position vs Truth**. Two lat/lon traces; the
  GPS receiver and the Truth42 ECEF position-derived geodetic.

### 3.5 Health-watcher row

- **Panel: SB Pipe Overflows**. Time-series of the
  `sb_pipe_overflow` tag count. Used to detect attacker-
  induced flood scenarios.
- **Panel: HS App Failures**. The `hs_app_failure` tag.
- **Panel: LC Actionpoints Fired**. The
  `lc_actionpoint_fired` tag. Indicates limit-checker
  triggers during attack scenarios.

### 3.6 Freshness Tiles row (added 2026-05-03, commit `7afa0107`)

- **Panel: Component Freshness Tiles**. Per-component last-seen heatmap (one tile per loaded component). A tile flips red if its component HK has not arrived within a 30 s window. Cross-references the `subsystem.keyword` mapping; the build script emits one tile per name in the loaded-app set from `cfe_es_startup.scr`.
- **Panel: Spacecraft Mode Chip**. A discrete chip context that shows the active mode at the same moment as the freshness heatmap. Backed by `spacecraft_mode_name` and `in_science_mode`. Frames every other panel on this dashboard with the operational mode at the time the operator is reading it.

### 3.7 GNSS GS Validation AND-gate row (added 2026-05-03, commit `bc6369c8`)

- **Panel: GNSS GS Validation AND-gate**. Aggregated validation panel that turns green only when all three conditions are simultaneously true: `last_ping_seq` is advancing within the panel window, `ping_count > 0`, and `in_denmark_box == true`. The query reduces to a per-bucket boolean reduction over the three fields; the panel renders as a Lens metric tile coloured by the reduction result.
- **Panel: GNSS GS Round-trip Latency**. Lens line of `last_ping_time - timestamp` in milliseconds. The 90th percentile sits at ~2.93 s; the `ECHO_TIMEOUT_S = 5.0` line marks the responder's deadline. Used to confirm the timeout has the right margin for current LEO jitter; if p90 climbs above 4 s, the responder will start timing out and `ping_count` advances slow.

The build script's `build_kibana_dashboards.py` emits these panels alongside the older rows; commit `0dcccd58` rebuilt the NDJSON artefact to include them. Because the panels reference the seven new index-template fields, the Kibana field-cache refresh in `refresh_index_pattern_fields.py` must complete before they render; see [`02-elasticsearch-mappings.md`](02-elasticsearch-mappings.md) section 3.1.

### 3.8 Manual GUI fallback

The post-thesis `cosmos-gui` make target (commit `c2a8c8db`) launches the X-dependent CmdTlmServer for manual debugging when the headless wrapper is not enough. It is a fallback only; the canonical operator path is COSMOS via `CmdTlmServerHeadless`, validated by the AND-gate panel above. See [`../03-communication/05-fsw-to-gsw.md`](../03-communication/05-fsw-to-gsw.md) section 1a.

## 4. Panel construction conventions

Three patterns recur in the build script's panel emission:

- **Lens vs Vega**: the testbed uses Lens for the simple
  time-series and bar charts because Lens panels survive a
  Kibana version bump cleanly. Vega is reserved for one-off
  custom views.
- **`event.dataset` filter**: every panel narrows by an
  `event.dataset` keyword (`software_bus`, `system_log`,
  `hk_decoded`) so a panel built against one source does not
  accidentally pick up documents from another.
- **`subsystem.keyword` split**: per-component panels split
  by `subsystem.keyword`, which lets the same panel type be
  reused across all components without per-component code in
  the build script.

## 5. Dashboard layout

The build script places panels by row index. Spacing is fixed
per dashboard: 8 columns on the overview, 12 on the
validation. The Markdown section panels (lines 11-14 of the
build script's docstring) are emitted as Kibana-native
markdown saved objects with the row title.

## 6. Cross-references

- Build, load, refresh tooling:
  [`04-build-and-load.md`](04-build-and-load.md).
- Per-app panel mappings: each
  [`../04-apps/<name>.md`](../04-apps/) document's ELK
  section.
- Field availability: [`02-elasticsearch-mappings.md`](02-elasticsearch-mappings.md).
- Attack-tag definitions:
  [`01-logstash-filters.md`](01-logstash-filters.md) section
  3.5.
