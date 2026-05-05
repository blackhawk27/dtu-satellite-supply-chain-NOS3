# 04. generic_gnss - GNSS Receiver (DTU new component)

This document describes the `generic_gnss` component: the GNSS
receiver cFS app and its simulator. Like `generic_tt_c`, this
component is a DTU addition with no upstream NOS3 baseline.

## 1. Real-world hardware

A GNSS receiver provides the spacecraft's position and time of
day. On a low-earth-orbit small satellite, the receiver is
typically a single-frequency unit returning ECEF position, ECEF
velocity, geodetic lat/lon/alt, and a precise timestamp.

## 2. App overview

The flight-software half lives at
`nos3/components/generic_gnss/fsw/cfs/`. The simulator half lives
at `nos3/components/generic_gnss/sim/`:

- `src/generic_gnss_data_point.cpp`: per-tick parser. Same
  bracket-stripping pattern as TT&C.
- `src/generic_gnss_hardware_model.cpp`: bus simulator.
- `src/generic_gnss_42_data_provider.cpp`: 42 IPC consumer on
  port 4287; tight prefix `SC[0].GPS[0].PosW` (count 1).
- `src/generic_gnss_data_provider.cpp`: alternate provider.

MIDs (component-base, `0x1900 / 0x0900`), per `nos3/components/generic_gnss/fsw/platform_inc/*.h`:

- Topic IDs: `0x52` (CMD/HK), `0x53` (REQ_HK / Device TLM), `0x54` (Sat-Hello TLM).
- Command: `0x1952`.
- HK telemetry: `0x0952`.
- Device TLM: `0x0953`.
- Sat-Hello TLM: `0x0954`.

Bus: UART.

The component was added in the series leading up to the thesis baseline: commits `a385a187` (sources), `d8e107f0` (FSW wiring), `0f0b65ac` (sim registration), and `4e2022fb` (TT&C/GNSS sim integration plus status codes plus optimised dashboards). Post-thesis additions on 2026-05-03 (commits `c6c79140`, `0dcccd58`, `bc6369c8`, `8a45426f`, `7afa0107`) added the GS Hello-Flow loop, the new ELK fields, and the AND-gate validation panel.

## 3. Headline function: bracket-stripping parser

`Generic_gnssDataPoint::do_parsing`. Same pattern as TT&C:

```
if (sP.empty()) {
    sim_logger->debug(
        "Generic_gnssDataPoint::do_parsing: empty SC[%d].GPS[0].PosW "
        "frame, deferring", _sc);
    return;
}
for (size_t i = 0; i < sP.size(); ++i)
    if (sP[i] == ',' || sP[i] == '[' || sP[i] == ']')
        sP[i] = ' ';
std::stringstream ss(sP);
ss >> _ecefX >> _ecefY >> _ecefZ;
```

The same load-bearing reasoning applies: 42's vector encoding is
`[x y z]`; without the strip, the first value parse fails and
the data point stays invalid. Both the empty-string guard and
the bracket strip are covered by the load-bearing-edits register
in the project root notes file.

The receiver also derives geodetic coordinates
(`gps_lat`, `gps_lon`, `gps_alt`) from the ECEF triple, plus a
GPS-style absolute time stamp from the 42 system clock. Those
derivations live downstream in the hardware model rather than in
`do_parsing`.

## 4. ELK extraction

Logstash extracts the following GNSS-related fields. The lower group was added on 2026-05-03 (commit `0dcccd58`, with the index-template + field-cache refresh wiring in commit `bc6369c8`):

- `gps_lat`, `gps_lon`, `gps_alt`: geodetic position.
- `gps_ecef_x/y/z`: ECEF position.
- `gps_abs_time`: absolute time tag.
- `gnss_lat`, `gnss_lon`: redundant per-GNSS lat/lon for the AND-gate panel.
- `in_denmark_box`: boolean, true iff `gnss_lat in [55, 58]` AND `gnss_lon in [8, 16]`.
- `in_science_mode`: boolean, mirrored from MGR HK (`spacecraft_mode == SCIENCE`).
- `last_ping_seq`, `last_ping_time`, `ping_count`: GS Hello-Flow loop signals; populated by the responder + beacon_app round-trip.

Kibana panels:

- "GPS Ground Track" on `nos3-std-telemetry-overview`: a map panel using `gps_lat` / `gps_lon`. Used to verify Denmark overflies under the EO1 orbit.
- "GPS Position vs Truth42" on `nos3-std-mission-validation`.
- "GNSS GS Validation AND-gate" on `nos3-std-mission-validation` (added 2026-05-03): turns green only when `last_ping_seq` is advancing AND `ping_count > 0` AND `in_denmark_box == true`.
- Freshness Tile row on the same dashboard: a per-component last-seen heatmap with a spacecraft-mode chip context, also added 2026-05-03 (commit `7afa0107`).

## 4a. GS Hello-Flow / Responder Loop (2026-05-03)

The closed-loop liveness check landed in this same commit window. The responder is a Cosmos-side Ruby task at `nos3/gsw/cosmos/COMPONENTS/GENERIC_GNSS/lib/gnss_gs_responder_task.rb`. Each `POLL_INTERVAL_S = 5.0` cycle it polls GNSS HK, dispatches `BEACON_PING_CC` to the spacecraft, captures `send_time = Time.now` immediately after the dispatch, and waits until `send_time + ECHO_TIMEOUT_S` for the ack to arrive over the mirror socket.

```ruby
ECHO_TIMEOUT_S = 5.0
# ...
send_time = Time.now
deadline = send_time + ECHO_TIMEOUT_S
# ...
elapsed_ms = ((Time.now - send_time) * 1000).round
log "ping seq=#{seq} elapsed_ms=#{elapsed_ms} status=ok"
```

Why 5.0 s: the empirical p90 echo latency from in-run measurements is 2.93 s; pinning the deadline at 5.0 s sits above p90 and inside the poll cadence. Earlier values (2.0 s, 3.0 s) had timeouts on every cycle. See `debug/journal.md` (2026-05-03 ECHO_TIMEOUT_S Tuning entry) for the reasoning.

Each successful pass advances `ping_count` and `last_ping_seq` in the index. The mirror-socket plumbing in `nos3/scripts/god_view_capture.py` (env var `NOS3_TLM_MIRROR_DEST`) and the `CmdTlmServerHeadless` wrapper at `nos3/gsw/cosmos/tools/CmdTlmServerHeadless` are required for the loop to close; see [03-communication/05-fsw-to-gsw.md](../03-communication/05-fsw-to-gsw.md) for the COSMOS netns story.

## 5. Attack-surface relevance

- **Component supply**: a tampered GNSS simulator can
  fabricate a position; ADCS inertial-mode pointing solutions
  derive from this position.
- **Operations layer**: dropping the GPS filter silences the
  ground-track panel; an attacker who wants to hide an orbit
  shift exploits this.

## 6. DTU divergence vs upstream

- Component is a DTU new addition; see section 2 for the
  commit chain.
- The bracket-stripping parser is the load-bearing fix.
- The tight 42 IPC prefix on port 4287 is the load-bearing
  invariant.

## 7. Verification

- GNSS HK published at 1 Hz on the SB capture.
- The GNSS GS validation AND-gate panel is the canonical check (replacing the older "GPS Ground Track populated" check). After ~10 min, expect `last_ping_seq` to be advancing, `ping_count > 0`, and `in_denmark_box == true` during Denmark passes.
- `gps_ecef_x/y/z` values match `truth_pos_x/y/z` from the Truth42 filter within the modelled receiver noise.
- 42-side log shows successful IPC writes on port 4287 with no `EAGAIN` exits.
- Freshness tile for `generic_gnss` is green (last seen within the 30 s window).
