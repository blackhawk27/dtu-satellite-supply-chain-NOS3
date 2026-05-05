# 04. onair - Autonomous Operations Framework

This document describes the OnAIR (On-board Artificial
Intelligence Research) framework as it sits in the EO1 mission.
OnAIR is an upstream-NASA framework for autonomous fault
detection on cFS telemetry; the DTU change for this app is its
enablement in the default mission spacecraft config (commit
`5afaa4fa`).

## 1. Real-world role

OnAIR runs autonomous fault-detection algorithms over the
telemetry stream. The framework is a separate container that
receives cFS telemetry through SBN (Software Bus Networking)
and applies model-based or learned anomaly detectors. In a
flight context, OnAIR acts as a continuous on-board health
monitor that can produce alerts even when the ground link is
unavailable.

## 2. App overview

The component lives at `nos3/components/onair/`:

- `cfs_sample.ini`: OnAIR-side ingest configuration.
- `cfs_sample_tlm.json`: telemetry-field mapping.
- `message_headers.py`: CCSDS framing helpers.
- `fsw/`: any FSW-side hooks (the OnAIR cFS bridge if loaded).

OnAIR consumes telemetry through the cFS SBN service, not
through the SB directly. SBN is the cross-process layer that
forwards SB packets between the FSW container and external
services.

## 3. Headline change: enablement

Commit `5afaa4fa` sets `<onair><enable>true</enable>` in
`nos3/cfg/spacecraft/sc-mission-config.xml`. With this change,
the OnAIR container is launched alongside the rest of the
testbed when the EO1 spacecraft profile is selected.

Before this change the OnAIR container did not start under the
default EO1 profile, even though the source tree carried it.
The enablement flips one configuration bit and pulls the OnAIR
container into the active set.

## 4. ELK extraction

OnAIR's emitted alerts and per-detector outputs land in
`omni_logs/onair*.log` (when enabled). The Logstash filter for
OnAIR-tagged events is part of the same noise-sweep pass as the
TT&C and GNSS additions; the field set is OnAIR-specific
detector outputs.

## 5. Attack-surface relevance

- **Operations-layer cross-check**: OnAIR is the on-board
  defence against forged telemetry that the ground-side
  Logstash filters might also be tampered with. Two
  independent anomaly paths increase the cost of a successful
  end-to-end attack.
- **OnAIR model tampering**: an attacker who replaces the
  OnAIR detectors with permissive ones suppresses the
  on-board alerts; the detection signal is the absence of
  expected detector triggers during attack scenarios.

## 6. DTU divergence vs upstream

- Commit `5afaa4fa`: enable OnAIR in the default mission
  spacecraft config. The component itself was already
  upstream; this commit is configuration-only.

## 7. Verification

- After `make launch`, the `onair` container appears in
  `docker ps`.
- `omni_logs/onair*.log` exists and contains
  detector-initialisation lines.
- The OnAIR detector outputs reach Kibana through the
  Logstash filter.
