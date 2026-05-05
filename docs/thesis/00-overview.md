# 00. Overview: Thesis, Testbed, and Attack Model

## 1. Thesis context

Satellite systems are built from a long chain of contributors:
flight-software framework maintainers, hardware-bus simulators,
ground-software vendors, dependency authors, dynamics-simulator
authors, and the integrating mission team. Each link in that chain
has commit access to code that ends up running close to mission
hardware. This thesis examines what an attacker positioned
somewhere along that supply chain can do, what their intervention
looks like in telemetry, and how realistic the resulting attack
chains are when run against a faithful simulation of a flying
spacecraft.

The platform that supports this study is the testbed described in
this document set: a DTU fork of NASA's NOS3 (NASA Operational
Simulator for Space Systems) extended with a custom telemetry
ingestion pipeline, a custom mission profile (NOS3-DTU-EO1, an
Earth-observation CubeSat in low Earth orbit targeting Denmark
overfly), three custom flight-software apps used as attack
instrumentation (`mgr`, `beacon_app`, `noisy_app`), and a set of
load-bearing fixes to existing NOS3 components that were necessary
to keep the integrated stack running. Every artefact described
here is reproducible from a single `git clone` followed by a
sequence of Makefile targets; the reproducibility appendix in
[`01-reproducibility.md`](01-reproducibility.md) walks through it
phase by phase.

## 2. What NOS3 is

NOS3 is NASA's open-source simulator for entire spacecraft
software stacks. A NOS3 simulation runs the actual flight
software (cFS, the core Flight System used on real GSFC missions)
inside a Docker container, talking to hardware-bus simulators
(I2C, SPI, CAN, UART) through the NOS Engine layer, with
spacecraft dynamics propagated by NASA's open-source 42 simulator
in a separate container. Ground software (COSMOS, OpenC3, or
Yamcs depending on configuration) sits on a third container set
and exchanges CCSDS frames with the spacecraft over a simulated
RF link. The result is a stack where the flight binary is
identical to what would fly, the bus traffic looks like real
hardware traffic, and orbit propagation respects realistic
two-line-element dynamics.

For the purposes of supply-chain attack research, the value of
NOS3 is that the simulation envelope spans the full attack
surface: an attacker who compromises a hardware-bus simulator,
a flight-software dependency, a ground-software command path,
or a build-time configuration generator can have their
intervention show up in telemetry in the same form it would on
orbit.

## 3. What DTU added

The fork's divergence from upstream NOS3 falls into five
categories, each of which has its own deep-dive in this
document set:

1. **Telemetry ingestion pipeline (ELK).** Logstash tails the
   Software-Bus god-view JSON and the per-component simulator
   logs, parses them into structured fields, and writes
   time-stamped documents to Elasticsearch. Kibana renders two
   curated dashboards: a standard-telemetry overview ("what is
   flowing, from where") and a mission-validation dashboard
   ("is the mission running"). Full walkthrough in
   [`05-elk/`](05-elk/).

2. **Custom flight-software apps.**
   - `mgr` (Mission Manager): tracks the spacecraft mode (SAFE
     or SCIENCE), enforces mode-based policies, and persists its
     state across reboots. Uses the cFS Draco computed-MID API.
   - `beacon_app`: a small, ground-commandable ping/pong
     telemetry app used as a baseline for telemetry-injection
     attack experiments.
   - `noisy_app`: an EPS-housekeeping spoofing payload used to
     simulate a tampered hardware component reporting falsified
     state.
   These are documented in [`04-apps/mgr.md`](04-apps/mgr.md),
   [`04-apps/beacon_app.md`](04-apps/beacon_app.md), and
   [`07-customizations-vs-upstream.md`](07-customizations-vs-upstream.md).

3. **Newly-added cFS apps and component simulators.**
   `generic_tt_c` (telemetry / telecommand) and `generic_gnss`
   (GNSS receiver) did not exist upstream; both ship as cFS apps
   with simulator counterparts and are wired into the cFE startup
   script, the 42 IPC port map, and the Logstash filter set.
   Documented in
   [`04-apps/tt_c.md`](04-apps/tt_c.md) and
   [`04-apps/gnss.md`](04-apps/gnss.md).

4. **Load-bearing fixes to existing apps.** A first-tick
   `std::stof` empty-string crash affected the entire ADCS
   sensor cluster (`generic_adcs`, `generic_reaction_wheel`,
   `generic_imu`, `generic_mag`, `generic_css`); the EPS load
   model needed extending; the 42 IPC port map needed two
   safety invariants (`OFF` on the unused star-tracker and
   thruster ports, tight prefixes on the TT&C and GNSS ports
   to avoid 42's TCP-send-buffer backpressure). Each of these
   has a per-app deep-dive in [`04-apps/`](04-apps/).

5. **Mission profile and ground stations.** The active orbit
   in `nos3/cfg/InOut/Orb_LEO.txt` is tuned for Denmark overfly
   (60 deg inclination, 346 deg RAAN, 61.6 deg true anomaly).
   Two ground stations were added to 42: Svalbard (SvalSat)
   and DTU Lyngby (commit `94f840af`). A research spacecraft
   profile exists alongside the standard mission profile.

## 4. Attack model

The thesis assumes an attacker who has obtained write access
somewhere in the supply chain ahead of mission integration but
does not control the flying spacecraft directly. The attacker
can modify:

- Source files in a hardware-bus simulator before integration.
- A vendored library used by the flight software (the C
  toolchain, OSAL, libcan, googletest fixtures, fprime
  framework files, etc.).
- A build-time configuration generator (Python or shell) that
  sits between the human-edited mission XML and the code that
  actually ships.
- A ground-software command path, including parameter tables
  and stored command sequences.

The attacker cannot:

- Inject network traffic into a flying mission's encrypted
  command uplink in real time. (CryptoLib in the GSW path
  models the encryption boundary; physical-layer compromise
  is out of scope.)
- Modify code that runs after integration is frozen and the
  binary is signed.

The attacker's payload runs inside the same simulator as the
defender's instrumentation (Logstash, Kibana, the
`mgr` / `beacon_app` traffic). This makes the testbed
double-purpose: every attack scenario produces a telemetry
trace, and every telemetry trace can be turned into a Kibana
panel that detects, isolates, and characterises the
intervention. The four research phases captured in
`debug/audits/` and the audit reports under `debug/` use this
loop directly.

Trust boundaries are drawn explicitly in figure F8 of
[`08-figures/figures.md`](08-figures/figures.md).

## 5. How to use this document set

For an examiner: read [`00-overview.md`](00-overview.md) (this
file), [`01-reproducibility.md`](01-reproducibility.md), and
[`02-architecture.md`](02-architecture.md) in order. That gives
the testbed in three documents. From there, jump to whichever
section the next claim in the thesis text rests on.

For a developer extending the testbed: start at
[`01-reproducibility.md`](01-reproducibility.md), get the
stack running, then jump to
[`07-customizations-vs-upstream.md`](07-customizations-vs-upstream.md)
to see what is local to this fork.

For a reviewer auditing the writing: the audit report at
[`_audit/audit-2026-05-01.md`](_audit/audit-2026-05-01.md)
documents the writing-rules sweep applied to every tracked
file in the repository.
