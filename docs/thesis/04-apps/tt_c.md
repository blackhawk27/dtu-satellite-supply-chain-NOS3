# 04. generic_tt_c - TT&C Radio (DTU new component)

This document describes the `generic_tt_c` component, the TT&C
(Tracking, Telemetry & Command) radio cFS app and its
simulator. The component is a DTU addition; there is no upstream
NOS3 baseline.

## 1. Real-world hardware

A TT&C radio handles the spacecraft's primary communication
link with ground. It carries:

- Uplink commands from ground stations to the FSW.
- Downlink telemetry from the FSW to ground.
- Beacon transmissions for tracking and orbit determination.

In the EO1 mission, the TT&C radio is modelled as the
ground-link bearer; the actual cryptographic layer is handled
by CryptoLib in the GSW path
([`../03-communication/05-fsw-to-gsw.md`](../03-communication/05-fsw-to-gsw.md)).

## 2. App overview

The flight-software half lives at
`nos3/components/generic_tt_c/fsw/cfs/`. The simulator half lives
at `nos3/components/generic_tt_c/sim/`:

- `src/generic_tt_c_data_point.cpp`: the per-tick parser
  consuming 42's IPC; this is where the bracket-stripping and
  empty-string guard live.
- `src/generic_tt_c_hardware_model.cpp`: bus simulator.
- `src/generic_tt_c_42_data_provider.cpp`: 42 IPC consumer on
  port 4286; the prefix list is tight (`SC[0].GPS[0].PosW`,
  `SC[0].GPS[0].VelW`, count 2) per the load-bearing-edits
  register.
- `src/generic_tt_c_data_provider.cpp`: alternate provider.

MIDs (component-base, `0x1900 / 0x0900`), per `nos3/components/generic_tt_c/fsw/platform_inc/*.h`:

- Topic IDs: `0x50` (CMD/HK), `0x51` (REQ_HK / Device TLM).
- Command: `0x1950`.
- HK telemetry: `0x0950`.
- Device TLM: `0x0951`.

Bus: UART.

The component was added in commits `a385a187` (component sources), `d8e107f0` (cFS startup wiring), `0f0b65ac` (sim registration with 42 providers), and `6c6b2029` (TT&C downlink dashboard, mappings, parsers). Post-thesis (2026-05-03) commit `c6c79140` consolidated the empty-string guard and refreshed the COSMOS target wiring.

## 3. Headline function: bracket-stripping parser

`Generic_tt_cDataPoint::do_parsing` at
`generic_tt_c_data_point.cpp:18`. Two DTU-specific guards combine
in this function.

### 3.1 Empty-string guard (line 30)

```
if (sP.empty() || sV.empty()) {
    sim_logger->debug(
        "Generic_tt_cDataPoint::do_parsing: empty SC[%d].GPS[0].Pos/VelW "
        "frame, deferring", _sc);
    return;
}
```

Same family pattern as the ADCS-cluster sims (commit
`962b61e7`).

### 3.2 Bracket and comma stripping (line 35)

```
for (size_t i = 0; i < sP.size(); ++i)
    if (sP[i] == ',' || sP[i] == '[' || sP[i] == ']')
        sP[i] = ' ';
```

This is the load-bearing strip. 42 emits vector values as
space-separated triples wrapped in square brackets, for example
`[1.234e6 -5.678e5 9.012e4]`. The downstream
`std::stringstream` (`>>` chain at line 38) cannot parse a
leading `[`. Without the strip:

- The first value parse fails with `std::ios::failbit`.
- The catch-handler at line 47 logs at error level.
- The `DataPoint::valid` flag stays false forever; HK packet
  emits as `INVALID`.

The cleaner fix would be to switch to
`Sim42DataPoint::parse_double_vector` (the upstream-canonical
helper that handles the bracket case natively). The DTU
component keeps the `do_parsing` pattern because the rest of
the data-point class already uses `stringstream` semantics; the
strip is the smaller change. The reasoning is documented in the
project root notes file's load-bearing-edits register.

## 4. ELK extraction

The TT&C-specific Logstash filter section was added in commit
`6c6b2029`. It extracts:

- `ttc_status_code`: the radio-link status byte.
- `ttc_link_state`: keyword (UP / DOWN / DEGRADED).
- `ttc_downlink_rate_bps`: the modelled rate.

Plus the TT&C downlink dashboard added in the same commit:

- "TT&C Downlink Status" on `nos3-std-telemetry-overview`.

## 5. Attack-surface relevance

- **Component supply**: a tampered TT&C simulator can publish
  link-up status while actually dropping downlink frames. The
  cross-check is the COSMOS-side packet rate.
- **Operations layer**: dropping the TT&C filter from
  Logstash silences the dashboard while the FSW still sees
  the telemetry.

## 6. DTU divergence vs upstream

- The component is a DTU new addition; there is no upstream
  baseline. Notable choices:
  - The bracket-stripping parser path; see section 3.2.
  - The tight 42 IPC prefix on port 4286 (see
    [`../03-communication/04-sim-to-42-dynamics.md`](../03-communication/04-sim-to-42-dynamics.md)).
  - The downlink dashboard added in commit `6c6b2029`.

## 7. Verification

- TT&C HK published at 1 Hz on the SB capture.
- 42-side log shows successful IPC writes on port 4286 with no `EAGAIN` exits (the tight-prefix invariant is enforced).
- `nos3-tt_c.log` post-launch: no `Parsing exception` errors attributable to TT&C.
- "TT&C Downlink Status" panel populates within a few seconds of sim start.
- Freshness tile for `generic_tt_c` is green (last seen within the 30 s window) on `nos3-std-mission-validation`.
