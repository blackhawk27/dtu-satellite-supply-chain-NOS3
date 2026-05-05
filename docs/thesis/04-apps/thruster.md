# 04. generic_thruster - Thruster (disabled in EO1)

This document describes the `generic_thruster` component as it
sits in the EO1 mission profile: present in the source tree but
disabled at the spacecraft-config level. The DTU-relevant
change for this app is the IPC `OFF` setting on port 4280
(commit `d16c89e3`).

## 1. Real-world hardware

A thruster provides linear delta-v for orbit raising, station
keeping, and de-orbit. On a small satellite, the most common
options are cold-gas, electrothermal, or low-power electric
propulsion. The EO1 mission does not exercise any of these; the
component is in the source tree as a placeholder for a future
mission profile.

## 2. App overview

The flight-software half lives at
`nos3/components/generic_thruster/fsw/cfs/`. The simulator half
lives at `nos3/components/generic_thruster/sim/`.

In the EO1 profile, the spacecraft config at
`nos3/cfg/spacecraft/sc-mission-config.xml` carries
`<thruster><enable>false</enable>`, so the thruster simulator
exits early during initialisation
(`Simulator 'generic-thruster-sim' is not active`).

MIDs (where applicable when enabled):

- Topic ID, command, telemetry MIDs are reserved but not
  exercised under the EO1 profile.

## 3. Headline change: IPC port 4280 OFF

The load-bearing edit is in `nos3/cfg/InOut/Inp_IPC.txt`. The
thruster IPC entry on port 4280 must stay `OFF` because:

1. The spacecraft config disables the thruster, so the
   simulator container exits early.
2. With the entry as `RX` (its upstream value), 42 opens a
   listening socket on port 4280 and waits for a client. None
   ever connects.
3. 42 hangs indefinitely in `inet_csk_accept`. The whole sim
   stalls at startup.

Commit `d16c89e3` aligns the source `Inp_IPC.txt` with the
hand-patched build copy, ensuring that `make config` does not
regenerate the broken state. The same class of fix applies to
the star-tracker entry on port 4282; both are documented in the
project root notes file's load-bearing-edits register.

## 4. ELK extraction

Not applicable in the EO1 profile; the thruster simulator does
not run, no thruster log file is generated, no Kibana panel is
populated.

## 5. Attack-surface relevance

- **Configuration tampering**: an attacker who flips
  `<thruster><enable>` to `true` without enabling the
  underlying thruster simulator binary can stall 42 and crash
  the sim. The detection signal is the absence of any 42 log
  output past the `inet_csk_accept` line.
- **Future-mission enablement**: if a future profile enables
  the thruster, the same supply-chain attack surface as the
  other components (sim binary, 42 IPC parsing) applies.

## 6. DTU divergence vs upstream

- Commit `d16c89e3`: align source `Inp_IPC.txt` port 4280
  entry to `OFF`. The build copy was already patched; this is
  the source-side correction.

## 7. Verification

- `nos3/cfg/InOut/Inp_IPC.txt` line for port 4280 reads `OFF`.
- After `make config && make launch`, the 42 container's log
  shows successful socket initialisation and the sim proceeds
  past the IPC bind step. No `inet_csk_accept` hang.
- `make stop && make launch` is reproducible; the thruster
  simulator container is not started.
