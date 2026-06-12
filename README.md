# DTU satellite supply-chain testbed (NOS3 fork)

This repository is a DTU research fork of NASA's NOS3 (NASA Operational
Simulator for Space Systems). On top of the NOS3 simulation it adds two
things for studying supply-chain attacks against spacecraft flight
software:

1. An ELK (Elasticsearch / Logstash / Kibana) telemetry-analysis pipeline
   that ingests the Software Bus "god view" and per-simulator logs and
   exposes them as dashboards.
2. A covert-opcode "piggyback" attack fixture (`noisy_app`) and supporting
   tooling that runs the same attack on this fork and on the Draco baseline
   for comparison.

The simulation lives under `nos3/`; the ELK configuration under
`nos3/elk/`. All `make` commands run from `nos3/`.

## Documentation (source of truth)

The authoritative documentation is under `docs/`. It is written to be read
without running the system.

- **[docs/thesis/](docs/thesis/)** is the canonical narrative. Start with
  its [reading order](docs/thesis/README.md): overview, reproducibility,
  architecture, the wire-level communication boundaries, per-application
  deep dives, the ELK pipeline, the divergence-from-upstream list, figures,
  and glossary.
- **[docs/debug/](docs/debug/)** records known errors, fixes, and the
  load-bearing constraints (build traps, simulator IPC ports that must stay
  off, GUI/X11, telemetry pipeline). Read the relevant page before changing
  `cfg/InOut/Inp_IPC.txt`, the simulator parsers, `scripts/ci_launch.sh`,
  the cFS startup script, or the CS checksum config.
- **[docs/security/](docs/security/)** and **[docs/poc/](docs/poc/)** hold
  the threat analysis, per-attack analyses, and proof-of-concept runbooks.

## Quickstart (clone to launch)

Prerequisites: a VS Code Remote Containers devcontainer with
Docker-in-Docker (the X11 plumbing is provided by the VS Code server; see
[docs/debug/gui-x11-forwarding.md](docs/debug/gui-x11-forwarding.md)). The
build runs inside the `ivvitc/nos3-64:20251107` image, so no host toolchain
is required.

```bash
cd nos3
make config        # generate build configs from cfg/nos3-mission.xml
make prep          # pull the build image and set up ~/.nos3 (builds 42)
make               # build FSW + simulators + GSW (a clean FSW build is 30-40 min)
make launch        # start the full simulation stack

# ELK is separate from the simulation stack:
cd elk && docker compose up -d     # or, from nos3/: make start-elk
```

A clone-to-launch walkthrough with a verification step after each phase is
in [docs/thesis/01-reproducibility.md](docs/thesis/01-reproducibility.md).

## Where things run

- Kibana: `http://localhost:5604`  (Elasticsearch: `http://localhost:9203`).
  These non-default ports let this "legacy" stack coexist with the RTEMS and
  Draco testbeds; the container/network names are prefixed `nos3-legacy-`.
- COSMOS ground software UI: `http://localhost:2900`.

The `noisy_app` attack fixture is a toggle in
`nos3/cfg/nos3_defs/cpu1_cfe_es_startup.scr`: `CFE_APP` (its shipped state)
loads it on every boot; set the row to `OFF_APP` to leave it present in the
image but not loaded.

## Upstream

This fork tracks NASA NOS3. Upstream documentation under `nos3/docs/wiki/`
and the cFS / F-prime framework docs are left as-is; the DTU additions and
divergences are catalogued in
[docs/thesis/07-customizations-vs-upstream.md](docs/thesis/07-customizations-vs-upstream.md).
NOS3 is released under NOSA 1.3.
