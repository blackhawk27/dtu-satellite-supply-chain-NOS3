# Surgical updates to the upstream mission documentation

This document proposes the minimum set of edits to the upstream
NOS3 mission documentation (the `docs/wiki/` Sphinx tree under
`nos3/docs/wiki/`, published as the canonical NOS3 ReadTheDocs
site) that would make the upstream description match what this
fork actually runs. The proposals are written as diff-style
descriptions, not as full rewrites; the intent is that the
upstream maintainer can apply each one as a small, focused
change.

The full divergence list against the import baseline lives in
[07-customizations-vs-upstream.md](07-customizations-vs-upstream.md).
The document here is the operator-facing subset: the changes
that affect what the user sees when reading the wiki.

## Update 1: `Home.md`

The upstream landing page lists NOS3's purpose and points the
reader at the install / build / run quickstart. No DTU edit is
proposed here; the page is correct for the upstream project as
distributed.

## Update 2: `NOS3_Architecture.md`

The upstream architecture page describes the container model,
the FSW / sim / GSW split, and the time-driver lockstep. Two
proposed inserts:

- **Insert under "Container model".** Add one paragraph noting
  that NOS3 forks may add additional Docker networks beyond
  `nos3-sc01` for shared services. Reference the `nos3-legacy-core`
  shared core network as the pattern for cross-spacecraft shared
  services, which in this fork also hosts the telemetry aggregator
  (ELK) alongside the NOS Engine driver, COSMOS, and CryptoLib on
  one external network. This is a small change to the upstream page;
  the shared-services network exists in upstream as well, but is not
  described.
- **Insert under "Trust boundaries" (new subsection).** The
  upstream page is silent on what the system trusts and where.
  Propose adding a four-bullet subsection along the lines of
  the trust-boundaries discussion in
  [02-architecture.md](02-architecture.md): host to Docker
  daemon, Docker network boundary, ground software to flight
  software (CryptoLib-mediated), and the Software Bus's lack
  of internal authentication. The point of the insert is not
  to advertise the bus as insecure, but to make explicit what
  the upstream documentation today leaves as folklore.

## Update 3: `NOS3_Flight_Software.md`

The upstream FSW page lists the cFS apps and their MIDs. Two
proposed inserts:

- **Insert under the app list.** Note that NOS3 forks frequently
  enable or disable individual apps via the spacecraft profile
  XML; the upstream page implies the listed set is canonical.
  A one-paragraph note pointing at
  `cfg/spacecraft/sc-*.xml` as the authoritative source would
  resolve a recurring user-question on the upstream issues
  tracker.
- **Insert: a "Bus integrity" subsection.** The upstream page
  describes the Software Bus as the inter-app message bus
  without commenting on the integrity guarantees. Propose
  adding a short subsection that names what the bus does not
  check (publisher identity, message provenance, sequence
  monotonicity, rate). The wording can be neutral; the goal
  is to make the trust assumption explicit for a researcher
  reading the wiki before building a security extension.

## Update 4: `NOS3_Simulators.md`

The upstream simulators page lists the per-component sims and
the NOS Engine carrier. Two proposed inserts:

- **Insert under "42 dynamics".** Add a paragraph documenting
  42's non-blocking-write exit semantics and the implications
  for IPC subscription prefix design. The upstream page
  presents the IPC layer as transparent, which is true at
  steady state but is what makes the broad-prefix failure
  mode in [03-communication/04-sim-to-42.md](03-communication/04-sim-to-42.md)
  hard to diagnose for new users. A short note that "42 exits
  on EAGAIN; keep subscription prefixes tight" would save
  several hours of investigation per affected user.
- **Insert under "NOS Engine".** Document the bridge-`netfilter`
  sysctl requirement
  (`net.bridge.bridge-nf-call-iptables=0`) explicitly. The
  upstream page assumes a host where the sysctl is already
  zero; on a Docker Desktop install or a recent Ubuntu host
  the default is one, and simulator-to-42 traffic is silently
  filtered. The DTU fork reasserts the sysctl on every launch
  through `ci_launch.sh`; documenting it upstream would let
  users without that fork's launcher fix the symptom.

## Update 5: `NOS3_Components.md` and `NOS3_Generating_Component.md`

The upstream pages describe how a hardware-component repository
is structured and how to generate a new one. One proposed
insert:

- **Insert under "Component repository structure".** Document
  the 42-data-point parser pattern. Components that consume 42
  truth need an empty-frame deferral guard and bracket-aware
  vector parsing; without those, the data point stays
  `INVALID` indefinitely. The upstream `generic_*` template
  components have the bracket handling but not the deferral
  guard, which means new components copied from them
  inherit the unguarded state. The insert would describe the
  pattern and reference the cleanest existing example
  (`nos3/components/generic_gnss/sim/src/generic_gnss_data_point.cpp`).

## Update 6: `NOS3_Getting_Started.md` and `NOS3_Install_Build_Run_QuickStart.md`

The upstream getting-started pages walk through clone, prep,
build, launch. Two proposed inserts:

- **Insert at the end of "First launch".** Add a one-paragraph
  diagnostic note: "If Kibana shows no data, run `make doctor`
  for a layer-by-layer health check." The diagnostic is a
  DTU addition (commit `36e32fb6`); a similar end-to-end
  diagnostic does not exist upstream, but the layered shape
  generalises.
- **Insert under "Verifying the build".** Add the curl
  verification commands from Phase 5 and Phase 6 of
  [01-reproducibility.md](01-reproducibility.md). The upstream
  page tells the user the launch should succeed but does not
  give the operator a self-verification path. Three curl
  commands and the expected outputs would close the gap.

## Update 7: `Scenario_cFS.md` and `Scenario_ADCS_Walkthrough.md`

The upstream scenario pages walk the user through controlled
operations against the simulated spacecraft. One proposed
insert:

- **Insert as a new scenario page.** A "Scenario:
  Telemetry-only observability" walkthrough that takes the
  user from `make launch` through opening a Kibana dashboard
  and running a per-component health check. The page is
  motivated by this fork's experience that operators spend
  significant time trying to inspect the running stack and
  the canonical answer ("read the per-simulator log files
  by hand") does not scale. Even without the full ELK
  pipeline, a documented `docker logs` and `jq` workflow
  against the per-component containers would be a
  significant operator-experience improvement.

## Update 8: `NOS3_42.md`

The upstream 42 page describes the dynamics engine, its
configuration files, and its outputs. One proposed insert:

- **Insert under "IPC configuration".** Document the comment-
  buffer overflow in 42's `InitInterProcessComm`
  (`42ipc.c:43`): the `char junk[120]` buffer plus the
  unbounded `%[^\n]` `fscanf` mean that any comment longer
  than about 100 characters causes the next field to be
  read into the trailing comment text and `DecodeString`
  exits with `Bogus input <garbled>`. The DTU fork records
  this inline at the top of `cfg/InOut/Inp_IPC.txt`; the
  upstream 42 documentation does not. The fix is structurally
  one line in 42 itself, but until that lands, a documentation
  note is the cheapest way to avoid the failure mode.

## Changes that should NOT be backported

For completeness, a list of edits this fork applies that the
upstream documentation should explicitly NOT inherit:

- The DTU orbit (Denmark overfly) and ground-station roster
  (Svalbard, DTU). These are research-specific.
- The `noisy_app` and `beacon_app` additions and the entire
  attack-radar Kibana dashboard. These belong in a separate
  fork or a separate documentation tree, not in the upstream
  NOS3 wiki.
- The MID-registry seed fallback. The upstream generator is
  presumably going to be fixed against Draco at some point;
  documenting the workaround in the upstream wiki would
  ossify the workaround as the recommended path.
- The `to_lab_sub.c` widening to include every component MID.
  Upstream NOS3's curated subscription set is the right
  default for an operator workflow; widening it makes sense
  only when the wider telemetry is going into an aggregator.

These exclusions are part of the proposal: the
mission-documentation patch is intentionally smaller than the
full divergence list.
