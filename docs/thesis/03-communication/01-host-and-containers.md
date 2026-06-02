# Boundary 1: host to container

The outermost wire boundary in the system is between the operator's
host machine and the running Docker containers. The host is treated
as trusted; nothing inside this boundary attempts to authenticate
the operator, and the supply-chain threat model assumes the
attacker has no foothold here.

## Docker networks

NOS3 creates two bridge networks at launch time:

- **`nos3-core`** is external and shared. It is created on demand by
  the `start-elk` Makefile target (`docker network create
  nos3-core`) before any compose stack comes up. The ELK
  containers, the NOS Engine time driver, the ground software
  (COSMOS or OpenC3 depending on `<gsw>`), and the CryptoLib
  standalone all attach here.
- **`nos3-sc01`** is the per-spacecraft network. It is created by
  `scripts/ci_launch.sh` as part of bringing up the simulators and
  the cFS container. With `<number-spacecraft>` greater than one,
  additional networks (`nos3-sc02` and so on) would be created;
  the default profile only uses `nos3-sc01`.

Both are stock Docker bridge networks, not overlay networks. They
use the host's iptables-based filtering, so any iptables
configuration that breaks intra-bridge forwarding breaks NOS3
silently. The most common case is `net.bridge.bridge-nf-call-
iptables=1`, which causes the kernel to route bridge traffic through
the host's filter tables; that path drops the simulator-to-42 TCP
flows. `scripts/cfg/install-deps.sh` sets it to `0`, and
`scripts/ci_launch.sh` reasserts it on every launch in case the
Docker daemon flipped it back during start. This is the only
host-kernel tweak the stack requires.

Five containers are dual-homed across both networks: the COSMOS
operator container (`cosmos-openc3-operator-1`) and the four NOS
Engine helpers (`nos-time-driver`, `nos-terminal`,
`nos-udp-terminal`, `nos-sim-bridge`). Each is started on one
network and then attached to the other through
`docker network connect` calls in `scripts/ci_launch.sh`
(around lines 184-269). The cFS container itself sits only on
`nos3-sc01`; the FSW-to-GSW wire crosses the `nos3-sc01` bridge
because COSMOS is attached to it on its second hop.

## Published host ports

By default only three ports are published from any container to the
host loopback interface. Everything else is reachable only between
containers on the same network.

- **`localhost:9200`** is `nos3-elasticsearch`'s REST endpoint. The
  `make doctor` script and the per-launch index-cleanup curl both
  hit it. No authentication.
- **`localhost:5601`** is `nos3-kibana`. The operator's browser
  attaches here. No authentication; reverse-proxying with auth in
  front is a deployment-side concern.
- **`localhost:2900`** is the COSMOS / OpenC3 web UI. Brought up by
  the ground-software compose. COSMOS has its own login (default
  credentials are documented in the upstream README).

A few additional ports are bound by `scripts/ci_launch.sh` so that
the host-side capture scripts can reach them, notably the TO_LAB
telemetry UDP socket used by `god_view_capture.py`. These are
loopback only and are not part of the operator surface.

The 42 dynamics container does not publish anything to the host. Its
TCP IPC ports (the 42xx range described in
[04-sim-to-42.md](04-sim-to-42.md)) are internal to
`nos3-sc01` only.

## Operator surface

What the operator actually does across this boundary:

- Issues commands and reads telemetry through COSMOS on
  `http://localhost:2900`.
- Watches dashboards on `http://localhost:5601`.
- Optionally runs ad-hoc `curl` queries against
  `http://localhost:9200` for forensic spot checks. Every
  verification step in
  [01-reproducibility.md](../01-reproducibility.md) is shaped
  around this surface.
- Reads the four host-side capture files directly with `tail`,
  `jq`, or `grep`; the files are on disk under
  `nos3/attack_logs/` and `nos3/omni_logs/` and Logstash tails them
  the same way.

## Authentication and threat model

This boundary is the only one that crosses out of the simulated
spacecraft. In a deployment the host would be operator-controlled
and the published ports would be reverse-proxied; in this research
fork they are bound to loopback only. There is no in-stack
authentication on any of the three ports because there is no need
for one: the bind is loopback, the network is local-bridge, and the
threat model puts the adversary inside the spacecraft, not on the
host.

## Deviation from upstream

Two specifics differ from the import baseline (`55ad2148`):

- The `nos3-core` network's purpose was originally just to give the
  NOS Engine and the GSW a shared bus. This fork extends that to
  include the ELK stack, which is why
  `nos3/elk/docker-compose.yml` declares the network `external:
  true` rather than creating its own.
- The early `DISPLAY` guard in `scripts/ci_launch.sh` (added in
  this fork) lives here, at the host boundary, because the
  failure mode it prevents is purely host-side. The guard is
  load-bearing: without it, a shell with no `DISPLAY` runs ~180
  lines of `ci_launch.sh` before aborting on `xhost +local:*`
  with no actionable message. The motivation is operator
  ergonomics, not architecture.
