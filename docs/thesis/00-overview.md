# Overview

## The upstream project

NOS3 (NASA Operational Simulator for Space Systems) is a software
testbed maintained by NASA's Katherine Johnson IV&V Facility. Its
purpose is to run real flight software (FSW) against simulated
hardware so that developers can do integration, verification, and
operations training without a flight unit. A NOS3 deployment is not a
single binary; it is a small distributed system consisting of:

- a flight-software process (NASA's Core Flight System, cFS) running
  the same C code that would run on the spacecraft,
- one hardware simulator per peripheral (EPS, GPS, reaction wheel,
  star tracker, IMU, magnetometer, sun sensors, torquers, radio, and
  others), each speaking its component's real wire protocol over a
  software bus,
- the 42 dynamics engine, which propagates orbit and attitude and
  feeds those vectors into the hardware simulators,
- a ground software stack (COSMOS by default) that issues commands
  and decodes telemetry exactly as it would in operations,
- the NOS Engine time driver, which steps every component in
  lockstep so the simulation is deterministic and replayable.

Everything is containerised. The build itself runs in a pinned Docker
image (`ivvitc/nos3-64:20251107`) so the host machine never installs
a cFS toolchain. Components communicate over user-defined Docker
networks. The mission configuration is XML; running `make config`
expands it into per-spacecraft launch scripts under `nos3/cfg/build/`
that the rest of the build consumes.

## What this fork adds

This repository is a DTU research fork. It imports upstream NOS3 in a
single flattening commit (the import baseline, `55ad2148`, recorded
in detail in [07-customizations-vs-upstream.md](07-customizations-vs-upstream.md))
and then adds three things that the upstream does not have:

1. **A telemetry-capture pipeline.** The upstream emits logs to stdout
   and to ad-hoc files; nothing aggregates them and nothing keeps a
   structured record of every Software Bus message. This fork adds
   four capture surfaces (`cfs_god_view.json`, `cfs_evs.log`,
   `nos3-*.log` per simulator, `cpu_monitor.log`), wires them into a
   Logstash ingest pipeline (`nos3/elk/logstash.conf`) and stands up
   an Elasticsearch and Kibana pair so dashboards can run against a
   live simulation. The pipeline name throughout these documents is
   "ELK". It is described end-to-end in
   [05-elk/](05-elk/).

2. **Attacker-side flight-software apps.** A purpose-built cFS app,
   `noisy_app`, lives next to the genuine cFS apps under
   `nos3/fsw/apps/`. It compiles on the standard build path and the
   resulting binary ships in every clean FSW image, but the startup
   script registers it as `OFF_APP` so cFE skips it at boot under
   the default profile. The operator-side arming step is a
   single-keyword flip (`OFF_APP` -> `CFE_APP`) plus a rebuild. The
   app models a compromised payload: it emits broadcast-storm
   command sequences, forges EPS housekeeping telemetry, and
   exercises the kinds of Software Bus abuse a supply-chain attacker
   would have access to from inside the FSW trust boundary. See
   [04-apps/noisy_app.md](04-apps/noisy_app.md).

3. **A research-grade reproducibility harness.** A `make doctor`
   target, a `save-run` / `load-run` archive flow, deterministic
   index rotation, a checked-in MID-registry seed for when the
   upstream generator silently fails, and a `_audit/` slot for
   reviewing this document set against the running code. These are
   small in isolation but they are what makes attack runs comparable
   across machines and across time.

## The research and threat model

The reason any of this matters is the threat model. Modern small
satellites depend on a long supply chain: cFS itself, the cFS apps
that fly with it, the hardware-driver components, and the ground
software that issues commands. Several of those links are open
source, several are vendor-supplied, and several are written by the
mission integrator. Any one of them can ship a compromised binary
into the spacecraft. The flight software trusts everything inside the
Software Bus implicitly: any task that can `CFE_SB_TransmitMsg` a
message with the right MID is, by construction, indistinguishable
from a legitimate publisher of that message. There is no
authentication on the bus and no provenance on a packet.

The testbed exists to answer two questions in that setting:

- **Can we instrument a cFS-based satellite enough to detect a
  malicious cFS app from telemetry alone?** That is, without
  privileged access to the FSW process and without binary attestation
  on the apps. The ELK pipeline is the substrate for that
  experiment: every Software Bus message becomes one document, every
  simulator log line becomes another, and the question becomes a
  Kibana query.
- **What does a realistic malicious cFS app look like, and what does
  it leave behind?** `noisy_app` is the centrepiece exhibit, and
  related cFS attack mechanics (CVE-2025-25371 chain, OSAL
  traversal, MM table-overwrite, MD dwell misuse, SYN heap leak,
  ADCS LICM hardware-blinding) are catalogued elsewhere in this
  document set alongside the telemetry signatures each one
  produces.

This is intentionally not a fault-injection testbed (the upstream
already does that) nor a cryptography testbed (CryptoLib exists for
that, and we leave its model alone). It is a supply-chain testbed:
the attacker is assumed to be inside the trust boundary, and the
defender's only tool is the telemetry record.

## What makes this variant distinct

NOS3 has split into several flavours in recent years. This
repository is the **legacy cFS variant with the DTU supply-chain
testbed bolted on**. The distinguishing properties:

- **cFS, not Draco.** This fork tracks the cFE topic and MID macro
  layout that predates the Draco redesign. One concrete consequence
  is that `scripts/mid/gen_mid_registry.py` ships an upstream-era
  grep against `CFE_PLATFORM_*_TOPICID_TO_MIDV` macros that this cFE
  does not emit, so `make config` silently produces no MID YAMLs.
  The fork compensates with a checked-in seed under
  `nos3/elk/seed_mid/` and a Makefile fallback. The Draco-targeted
  generator is intentionally left in place so the divergence is
  visible and so future updates can re-enable it.
- **cFS, not F'.** A `sc-fprime-config.xml` profile exists upstream
  and is preserved here, but every claim in these documents is about
  the default `sc-mission-config.xml` profile (cFS + COSMOS, single
  spacecraft, generic-component instruments, ADCS enabled). The
  F-prime path is out of scope.
- **No zero-trust architecture.** A separate NOS3 variant is
  exploring a ZTA-style overlay (mutual auth on the bus, per-message
  signing, capability-bound subscriptions). This repository does not
  implement that. The Software Bus stays as upstream left it
  precisely so the attack experiments are realistic against a
  current operational stack.
- **Built around a specific orbit and overfly.** The spacecraft
  profile pins to a Denmark-overfly orbit
  (`nos3/cfg/InOut/Orb_LEO.txt`, 60 degree inclination, 346 degree
  RAAN, 61.6 degree true anomaly). This is not an architectural
  choice so much as a research convenience, but downstream telemetry
  (`gps_lat`, `gps_lon`, `gps_alt`) only makes sense in that
  context.

Figure F1 in [08-figures/figures.md](08-figures/figures.md) draws
this layout end-to-end: containers, networks, trust boundaries, and
the two telemetry sinks. Subsequent documents reference it by
number rather than embedding it again.
