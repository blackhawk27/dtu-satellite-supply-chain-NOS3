# Kibana dashboards

Kibana 7.17.10 in container `nos3-kibana` is the consumer
surface of the pipeline. It serves a curated set of fifteen
saved objects (fourteen dashboards and one saved search) that
the operator and the researcher open against a live or archived
run. This document describes what each saved object is for, what
it queries, and how the dashboard set as a whole maps onto the
research question.

The saved objects are programmatically built and re-imported on
every `make launch` by `nos3/elk/build_kibana_dashboards.py`.
The builder owns its catalogue; saved objects in Kibana that are
not in the builder's `REGISTRY` are pruned on rebuild. This
means hand-editing a dashboard in the Kibana UI does not
survive a `make launch`; the way to change a dashboard is to
change the Python builder. The mechanics are documented in
[04-build-and-load.md](04-build-and-load.md).

## Reading order

The dashboards fall into four groups by purpose: standard
operations, mission-specific operations, FSW and bus health, and
threat-model analytics. The reading order suggested below moves
from the most general to the most specialised.

### Standard operations

- **`nos3-std-telemetry-overview`** ("NOS3 Standard Telemetry
  Overview"). The default landing dashboard. Document counts by
  type, the breakdown of `system_log` documents by subsystem,
  the breakdown of `software_bus` documents by `msg_name`, and
  a date histogram of total events. The dashboard answers "is
  telemetry flowing at all" and "what is dominating the
  document mix"; a flat date histogram on this view is the
  fastest way to see that the sim has stalled.
- **`nos3-std-mission-validation`** ("NOS3 Standard Mission
  Validation"). The cross-component sanity check. Spacecraft
  mode over time, EPS battery state of charge, reaction-wheel
  momentum, GPS fix availability. A run that completes a full
  orbit cleanly looks predictable on this dashboard; a run with
  a hardware-simulation problem stands out as a stuck field.

### Mission-specific operations

- **`nos3-mission-denmark`** ("NOS3 Mission: Denmark"). Maps
  the spacecraft track over Denmark using the `gps_location`
  `geo_point`. Overlays mode and science-overfly tagging so
  the operator sees exactly when the spacecraft entered the
  `SCIENCE` profile relative to the overfly window. This is
  the dashboard the mission documentation under
  [../../missions/](../../missions/) (canonical mission
  writeup) treats as the operator's primary view.
- **`nos3-eo1-power-budget`** ("NOS3 EO1: Power Budget").
  Solar-array generation, bus power consumption, battery state
  of charge, and the integrated energy balance over the run.
  This is the dashboard that tells the operator whether the
  spacecraft is energy-positive across the Denmark overfly.
- **`nos3-eo1-adcs-health`** ("NOS3 EO1: ADCS Health").
  Per-wheel momentum, the controller's commanded torques, the
  attitude-determination error norm, and the eclipse-mode
  flag. The dashboard's primary value is during attitude
  excursion debugging.

### FSW and bus health

- **`nos3-eo1-fsw-health`** ("NOS3 EO1: FSW Health"). Per-app
  CPU usage, EVS event rate by severity, command and error
  counters by app, and the tag pile for
  `sb_pipe_overflow`, `hs_app_failure`, and
  `lc_actionpoint_fired`. This is the dashboard the operator
  watches during an attack run; the `noisy_app` storm shows up
  here first as a CPU spike on `cFS` and a vertical line of
  red tags.
- **`nos3-eo1-ttc-downlink-validation`** ("NOS3 EO1: TT&C
  Downlink Validation"). MID-by-MID sequence-count traces,
  bus-throughput sparkline, and a per-subsystem document-count
  heatmap. The dashboard surfaces forged-publisher sequence
  gaps and downlink starvation.
- **`nos3-eo1-gnss-gs-validation`** ("NOS3 EO1: GNSS-to-GS
  Validation"). Joins `gs_uplink` documents from Layer 4 with
  the matching ping-pong responses on Layer 1. A column on the
  round-trip log shows the spacecraft mode at the moment of
  the ping (this is the projection described in
  [01-logstash-filters.md](01-logstash-filters.md)). Missing
  round-trips reveal a broken ground-to-FSW chain.
- **`nos3-hk-telemetry-flow`** ("HK Telemetry Flow"). The
  cFS housekeeping app's view: counts of HK request and HK
  response by source, with a heatmap of the missing
  responses. Helps diagnose subscriber misconfiguration in
  the spacecraft profile.
- **`nos3-hs-cpu-monitor`** ("HS CPU Monitor"). Numerics from
  `cpu_monitor.log` (Docker `stats` output): `TOTAL_CPU_PCT`,
  `NUM_PIDS`, per-PID CPU. The denial-of-service signature of
  `noisy_app`'s CPU burn loop is visible here as a saturated
  total with the cFS container's PID count dropping (because
  the busy loop starves other apps).
- **`nos3-obc-perf-monitor`** ("OBC Performance Monitor (A:
  CPU Budget / B: SCH Health / C: TIME)"). Three-pane onboard-
  computer performance view: CPU budget headroom, the SCH
  app's schedule-cycle latency, and the TIME app's time-step
  jitter. Useful when the system-log latency starts diverging
  from the spacecraft-clock latency in interesting ways.

### Mode and ground truth

- **`nos3-spacecraft-mode-changes`** ("Spacecraft Mode
  Changes"). A timeline of mode transitions, with the from-mode
  and to-mode columns supplied by the
  `spacecraft_mode_change` tag and the
  `mode_change_label` field. Useful for verifying the
  fork's "reboot resumes in SAFE" policy and for tracing
  mode-related EVS events.
- **`nos3-sensor-ground-truth`** ("Sensor Ground Truth
  Overview"). Cross-component sensor view: IMU rates,
  magnetometer field, CSS validity, FSS angles, reaction-wheel
  momentum. The dashboard is the place to confirm that the 42
  truth stream is reaching every consumer.

### Threat-model analytics

- **`nos3-thesis-attack-radar`** ("Thesis: Cyber-Physical
  Attack Radar"). The flagship research dashboard. Tag count
  over time for `attack_loaded`, `attack_armed`,
  `attack_trigger_ping`, `sb_pipe_overflow`,
  `noisy_app_spam_target`, `hs_app_failure`. A timeline panel
  shows the moment-by-moment progression of an attack run
  (attack loaded at boot, three pings, threshold, storm,
  resulting overflow events). A second panel correlates the
  CPU saturation against the attack tags. This is the
  dashboard the thesis discussion refers to when it talks
  about "what does a `noisy_app` run look like in Kibana".
- **`nos3-fsw-vs-sim-xref`** ("FSW vs Sim Cross-Reference").
  Side-by-side panels of FSW-published telemetry against
  simulator-side log lines for the same component. The
  dashboard is built so that a spoofed publisher (e.g.
  `noisy_app` forging EPS HK) appears as a divergence between
  the two columns: the FSW view of EPS battery voltage
  jumps to `0xDEAD` while the EPS sim's own log keeps
  reporting the truth value. This is the dashboard that makes
  the EPS-spoof attack visible without payload decoding.

### Saved search

- **`nos3-evs-error-tail`** ("NOS3 cFE EVS - ERROR / CRITICAL
  tail"). The one saved search in the catalogue. Returns
  every `system_log` document with `evs_severity` in
  `{ERROR, CRITICAL}`, sorted by `@timestamp` descending. It
  exists as a saved search rather than a dashboard so it can
  be embedded as a panel in any other dashboard without
  duplicating the query.

## What is not in the catalogue

Four things are intentionally absent and would have to be
added separately:

- **A per-attacker cumulative impact panel.** The radar
  dashboard shows the attack timeline; it does not aggregate
  "how many EPS reads were spoofed in this run" or "how many
  unique MIDs received forged packets". The aggregation
  exists in the Logstash output but no dashboard sums it.
- **A geo-fenced alerting dashboard.** The Denmark dashboard
  shows the track; there is no panel that fires when the
  spacecraft enters the science zone in `SAFE` mode (a known
  attack shape against the mission manager's mode logic).
- **CryptoLib audit panels.** The ground-to-FSW boundary has
  Kibana-visible documents only on the `gs_uplink` side;
  CryptoLib does not emit structured logs into the omni-logs
  stream. A CryptoLib audit dashboard would require a new
  capture surface, not just a new dashboard.
- **F-prime variant dashboards.** Every dashboard in the
  current catalogue assumes cFS. A switch to the
  `sc-fprime-config.xml` profile produces a different EVS
  shape that none of these dashboards parse.

These are recorded here as research-extension candidates;
none of them block the present testbed from being useful.
