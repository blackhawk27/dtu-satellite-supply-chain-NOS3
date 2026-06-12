# Figures

The six figures referenced by the other documents in this set
are described below. The descriptions are detailed enough to
produce the figures independently; the images themselves are
not embedded here. Each figure has a number, a title, a
caption, and a textual description of layout and content.

Figures are referenced in the prose by number only
(`Figure F1`, `Figure F2`, etc.). The mapping below records
which documents currently reference each figure; the back-
references are stable to subsequent edits.

## F1: System overview

**Title.** NOS3 deployment topology: containers, networks, and
trust boundaries.

**Caption.** Container-level view of a running NOS3 stack on
this fork. Two Docker bridge networks (the shared
`nos3-legacy-core`, which hosts the shared services and the ELK
stack together, and the per-spacecraft `nos3-sc01`) carry every
process; trust boundaries are annotated as dashed lines.

**Referenced by.**
[00-overview.md](../00-overview.md),
[02-architecture.md](../02-architecture.md),
[03-communication/00-overview.md](../03-communication/00-overview.md).

**Layout.** The figure is wider than tall, roughly 4:3. A
single light-grey rectangle frames the host machine, occupying
the entire canvas. Inside the host rectangle two rounded
rectangles represent the Docker networks, side by side:

- Left rectangle, labelled `nos3-sc01`: contains the cFS
  process container at the top, then a vertical stack of
  hardware-simulator containers (`generic_eps`,
  `generic_gnss`, `generic_tt_c`, `generic_adcs`,
  `generic_imu`, `generic_mag`, `generic_css`, `generic_fss`,
  `generic_radio`, `generic_reaction_wheel`,
  `generic_torquer`, `blackboard`, `mgr`), and the 42
  dynamics container at the bottom.
- Right rectangle, labelled `nos3-legacy-core` (the shared core
  network): contains the NOS Engine time-driver container at the
  top, the COSMOS ground-software cluster (one rectangle grouping
  `cosmos-openc3-*-1` containers), the CryptoLib standalone
  container, and the three ELK containers
  (`nos3-legacy-elasticsearch`, `nos3-legacy-logstash`,
  `nos3-legacy-kibana`) all inside this same rectangle.

The COSMOS operator container and the four NOS Engine helpers
(`nos-time-driver`, `nos-terminal`, `nos-udp-terminal`,
`nos-sim-bridge`) straddle the two rectangles (small notches
indicating dual-home). The cFS container itself sits only on
`nos3-sc01`; the FSW-to-GSW wire crosses the `nos3-sc01` bridge
because COSMOS is attached there on its second hop.

**Arrows.** Solid arrows for runtime traffic:

- cFS to every simulator (NOS Engine carrier, single arrow
  labelled `I2C/SPI/CAN/UART via NOS Engine`).
- Each simulator to 42 (per-simulator TCP arrow, all bundled
  into one labelled arrow `42xx TCP IPC`).
- COSMOS to cFS on two parallel arrows: one labelled
  `CCSDS UDP DEBUG, plaintext` (port 5012/5013 direct to cFS)
  and one labelled `CCSDS UDP RADIO, CryptoLib-wrapped`
  (port 6010/6011 to CryptoLib, then 8010/8011 to cFS).
- Four host-side capture scripts (drawn outside the Docker
  rectangles, on the bottom edge of the host frame) feeding
  the two log directories (`attack_logs/`, `omni_logs/`),
  which feed `nos3-legacy-logstash` via a bind-mount arrow.
- The operator's browser (drawn outside the host frame)
  reaching `nos3-legacy-kibana` and the COSMOS UI on
  `localhost:5604` and `localhost:2900`.

**Trust boundaries.** Dashed lines, annotated:

- Around the host frame: "Host to Docker boundary (trusted)".
- Around the COSMOS-to-CryptoLib-to-cFS span: "GSW boundary,
  RADIO path only (CryptoLib-mediated, authenticated)".
- Around the COSMOS-to-cFS direct UDP arrow: "GSW boundary,
  DEBUG path (plaintext UDP 5012/5013, no CryptoLib)".
- Around the cFS container only: "Software Bus boundary (no
  internal authentication; supply-chain threat lives here)".
- Around the four capture scripts and the ELK stack: "ELK
  boundary (one-way, forensic)".

## F2: Six wire boundaries

**Title.** The six wire-level communication boundaries
overlaid on the container topology.

**Caption.** Numbered overlay of Figure F1 showing every
wire-level boundary a message crosses in a running stack.
Each number corresponds to one file in
[03-communication/](../03-communication/).

**Referenced by.**
[03-communication/00-overview.md](../03-communication/00-overview.md).

**Layout.** Re-use the Figure F1 frame at smaller scale,
desaturated. Six numbered circular badges (1 through 6)
overlay specific arrows:

1. On the operator's browser arrow into the host frame.
2. Inside the cFS container, on a small arrow loop labelled
   "Software Bus" (drawn as a self-edge on the cFS
   rectangle).
3. On the cFS-to-simulator arrow.
4. On the simulator-to-42 arrow bundle.
5. On the cFS-to-COSMOS-via-CryptoLib arrow.
6. On the capture-scripts-to-Logstash bind-mount arrow.

**Annotations.** A right-hand sidebar lists each numbered
boundary with one-line summaries:

- "1. Host to container. Loopback ports 5601, 9200, 2900."
- "2. Software Bus. In-process pub-sub. No authentication."
- "3. cFS to sim. HWLIB through NOS Engine. I2C/SPI/CAN/UART."
- "4. Sim to 42. TCP 42xx. Non-blocking write."
- "5. FSW to GSW. CCSDS UDP. CryptoLib SDLP wrap."
- "6. Telemetry to ELK. File bind mount. One-way."

## F3: Software Bus pub-sub topology

**Title.** cFS Software Bus pub-sub topology and the absence
of provenance.

**Caption.** The cFE Software Bus routes packets by MID only;
no field in the routing table records who published a given
message. The diagram makes the trust assumption visible.

**Referenced by.**
[03-communication/02-fsw-software-bus.md](../03-communication/02-fsw-software-bus.md).

**Layout.** Three vertical columns of equal width. Slightly
taller than wide.

- Left column: publishers. Eight to ten small rectangles
  labelled with cFS app names, including one outlined in
  red labelled `noisy_app` and one outlined in blue
  labelled `generic_eps` (the legitimate EPS publisher).
- Middle column: a single tall rectangle representing the
  Software Bus routing table. Its content is depicted as
  rows of `MID -> [PipeId, PipeId, ...]`. The
  `0x091A` row is highlighted: both the red `noisy_app`
  arrow and the blue `generic_eps` arrow terminate at this
  row, with no visible distinction between them.
- Right column: subscribers. Includes `to_lab`,
  `housekeeping aggregator`, and others. Arrows fan out
  from the highlighted `0x091A` row into the subscribers.

**Annotations.** Two text callouts:

- Below the routing table: "The bus stores MID, pipe, and
  packet bytes. It does not store the publishing task ID."
- Below the right column: "Every subscriber receives an
  identical packet regardless of which publisher emitted
  it."

A footnote at the bottom: "The `attack_app = NOISY_APP` tag
in Kibana is reconstructed from EVS events, not from the bus
itself."

## F4: ELK pipeline data flow

**Title.** End-to-end ELK pipeline: capture, transform,
index, visualise.

**Caption.** The four host-side capture surfaces feed Logstash
through a read-only bind mount. Logstash dispatches by `type`,
applies per-input filters, and writes to a daily-rolled
Elasticsearch index. Kibana hosts twenty saved objects
(nineteen dashboards and one saved search) on top.

**Referenced by.**
[05-elk/00-pipeline.md](../05-elk/00-pipeline.md).

**Layout.** Horizontal flow, left to right, in four columns.

- Column 1: capture sources. Five labelled boxes:
  `god_view_capture.py`, `cfs_evs_capture.sh`,
  `sim_logs_capture.sh`, `cpu_monitor.sh`, and (smaller,
  drawn separately) `gnss_gs_responder_task.rb`.
- Column 2: host-side files. Six file icons labelled:
  `attack_logs/cfs_god_view.json`, `omni_logs/cfs_evs.log`,
  `omni_logs/nos3-*.log`, `omni_logs/cpu_monitor.log`,
  `omni_logs/tlm_hk_decoded.log`,
  `omni_logs/gnss_uplinks.log`. Arrows from the capture
  scripts in column 1 to the matching files.
- Column 3: Logstash. A single rectangle with four input
  bands labelled `software_bus`, `system_log`, `hk_decoded`,
  `gs_uplink`. Below the inputs, a stack of filter rules:
  `MID translate`, `attack tag`, `EVS severity`,
  `numeric extract`, `mode-change Ruby`. The Logstash
  output emerges from the right.
- Column 4: Elasticsearch and Kibana. ES drawn as a database
  cylinder labelled `nos3-telemetry-YYYY.MM.dd`; Kibana drawn
  as a window labelled `20 saved objects` with thumbnails of
  three dashboards visible.

**Cross-cutting arrows.**

- A dashed arrow from `cfg/build/elk/mid_to_*.yml` (drawn
  as a small box above Logstash) into the `MID translate`
  filter row.
- A dashed arrow from `make doctor` (drawn as a small
  diagnostic icon at the bottom) reaching every column,
  indicating the seven layers it walks.
- A return arrow from Kibana back to the operator (off-
  diagram on the right) labelled `localhost:5604`.

## F5: Denmark orbit track in Kibana

**Title.** Spacecraft track over Denmark, rendered against
the `gps_location` `geo_point` field.

**Caption.** The "NOS3 Mission: Denmark" Kibana dashboard
renders the spacecraft's geodetic track over a map tile. The
science-overfly window is highlighted; the spacecraft mode is
overlaid as a coloured trail.

**Referenced by.**
[04-apps/generic_gnss.md](../04-apps/generic_gnss.md).

**Layout.** A single landscape-orientation panel modelled on
the Kibana Maps view.

- Background: a low-detail political map of north-western
  Europe, centred on Denmark, with country borders visible.
- Track: a polyline drawn from a sequence of `gps_location`
  points. The track runs roughly north-south as the
  spacecraft passes over Denmark on the 60-degree-inclination
  orbit. Colour the track by spacecraft mode: green for
  `SCIENCE`, blue for `SAFE`, grey for any other state.
- Overfly window: a translucent rectangle covering the
  latitude / longitude bounds the LC table fires on. The
  rectangle is annotated with the watchpoint expression that
  produces the `science_overfly` tag.
- Ground stations: two pin icons, one at Svalbard (top of
  map, above the visible frame), one at DTU (Kongens
  Lyngby, near Copenhagen). Ground-station arcs show
  approximate field-of-view circles.

**Annotations.** A legend in the bottom-left lists the colour
encoding; a small text block in the top-right reports the run
metadata (run label, mission start time, document count) as
the dashboard normally renders it.

## F6: noisy_app attack timeline

**Title.** `noisy_app` attack sequence, from covert-opcode
trigger through payload, with observable Kibana tags.

**Caption.** The lifecycle of a `noisy_app` attack run. Time
runs left to right; each tag in the bottom strip is the
Logstash tag that becomes visible in Kibana at that moment.

**Referenced by.**
[04-apps/noisy_app.md](../04-apps/noisy_app.md).

**Layout.** A horizontal swim-lane diagram with four lanes
and one time-axis tag strip at the bottom.

- Lane 1 (top): `noisy_app` internal state. Two states
  drawn as rounded boxes connected left to right:
  `SNIFFING` and `ACTING`. While `SNIFFING`, the app
  watches the CI_LAB carrier MID `0x18E0` for a covert
  opcode byte appended after a header-only NOOP. The
  transition to `ACTING` happens the instant a covert
  opcode arrives. Annotate the transition with the
  source-file line numbers from `noisy_app.c`.
- Lane 2: Software Bus traffic. A timeline showing the
  CI_LAB carrier (MID `0x18E0`) with a single covert
  opcode byte riding a header-only NOOP (one short
  vertical bar, labelled e.g. `opcode 0x02`). There is
  no three-ping arming and no carpet-bomb storm; the
  payload that follows depends on the opcode (a forged
  EPS HK packet on `0x091A`, a burst at the seven spam
  targets, or a pool-lock DoS).
- Lane 3: cFS resource state. A line graph of CPU
  utilisation and a parallel line for buffer-pool usage.
  For the EPS-spoof opcode (`0x02`) both stay flat (the
  spoof is a single forged packet, no resource hit). For
  the SB-burst (`0x04`) and SB pool-lock DoS (`0x08`)
  opcodes, buffer-pool usage rises toward saturation.
- Lane 4: EVS event stream. Discrete event markers
  corresponding to each `CFE_EVS_SendEvent` call in
  `noisy_app.c`: the startup event ("Initialized. CMD MID
  0x.., sniffing carrier 0x.."), the per-opcode event
  ("piggyback opcode 0x.. on MID 0x.."), and the payload
  event for the selected action (e.g. "EPS HK SPOOF sent
  on 0x091A (BatteryVoltage=10000 mV)", "SB POOL LOCK
  engaged", or "PIPE FLOOD - .. junk cmds delivered").
  Plus external events on the same lane: `SB_PIPE_OVERFLOW`
  from `cfe_sb` and `HS_APP_FAILURE` from `hs` during the
  DoS opcodes.
- Tag strip (bottom): horizontal bar with the live Kibana
  tags from `nos3/elk/logstash.conf` colour-coded along
  the same time axis: `piggyback_opcode` at the opcode
  decode, then the payload tag (`eps_spoof`,
  `sb_pool_lock`, or `noisy_app_spam_target`), with
  `sb_pipe_overflow` and `hs_app_failure` overlapping
  during the DoS opcodes.

**Annotations.** A small inset panel labelled "EPS spoof
variant" depicts the EPS-spoof opcode (`0x02`) path: a single
forged `BatteryVoltage = 10000 mV` (low-battery spoof) packet
on the EPS HK MID `0x091A`, with a callout indicating that the
spoof leaves no trace in lane 3 (no CPU spike) but is
detectable in the `fsw-vs-sim cross-reference` dashboard as a
divergence between the FSW view and the simulator-side log.
