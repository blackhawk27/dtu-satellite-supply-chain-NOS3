# 03.01 Host and Containers

This document describes the outermost communication layer: how the
host operating system, the Docker daemon, and the containers in the
testbed connect to each other and to the operator. It pins to figure
F2 in [`../08-figures/figures.md`](../08-figures/figures.md).

## 1. The host

The testbed targets Linux on x86_64. The supported variants are:

- A native Ubuntu / Debian host with Docker Engine and an X11
  server.
- WSL2 on Windows with Docker Desktop and the bundled WSLg X11
  server.

The author's working configuration is the WSL2 path, executed
inside the project's `.devcontainer/`. The
`load-bearing-edits` block in the project root notes file
explicitly forbids adding manual `runArgs` or `containerEnv`
WSLg passthrough to the dev container: Docker Desktop's
built-in WSL2 integration already forwards X to Windows, and
explicit passthrough silently breaks the 42 GUI windows. The
correct diagnostic ladder when 42's Cam / Map / Unit Sphere
windows fail to appear is `xeyes` from inside the container, not
adding mounts.

The host exposes three TCP ports to the operator's browser:

- `5601`: Kibana UI.
- `9200`: Elasticsearch HTTP API (used by the dashboard build
  scripts and by ad-hoc `curl` queries).
- `2902`, `5010`, `7779`: COSMOS HTTP and per-target
  uplink/downlink sockets. The exact port set depends on the
  COSMOS configuration generated at `make config` time.

All other inter-container traffic stays on the Docker bridge
networks; nothing else is published to the host.

## 2. Docker networks

Two Docker networks segregate the traffic:

- `nos3-core`: created up-front by the ELK compose stack or by
  the launch script. Hosts the shared services: NOS Engine time
  driver, COSMOS, and the three ELK containers (Elasticsearch,
  Logstash, Kibana). The network must exist before the ELK
  stack starts; `nos3/elk/docker-compose.yml` declares it as
  external.
- `nos3_sc01`: created at launch time by
  `nos3/scripts/ci_launch.sh`. Hosts the per-spacecraft set:
  the FSW container (`nos3-fsw`), the 42 dynamics container
  (`sc01-fortytwo`), and the per-component simulator
  containers (`nos3-generic-eps-sim`, `nos3-generic-tt_c-sim`,
  `nos3-generic-gnss-sim`, etc.). Multi-spacecraft missions
  would create one such network per spacecraft, hence the
  numeric suffix.

DNS inside each network resolves the short hostname. The FSW
container resolves `fortytwo` to the 42 container's IP on the
same network; Logstash resolves `nos3-elasticsearch` on
`nos3-core`. This is what makes the 42 IPC port table in
`nos3/cfg/InOut/Inp_IPC.txt` portable across hosts: it lists
host name `fortytwo`, which the Docker DNS resolves at
runtime.

A small set of containers join both networks because they
straddle the boundary:

- `nos3-cosmos`: needs to reach the FSW on `nos3_sc01` (for
  CCSDS uplink/downlink) and to publish ports back to the host.
- `nos3-logstash`: needs to read the file-tail logs that come
  out of the FSW and the simulator containers.

The dashed cross-network edges in figure F2 mark these dual-homed
containers.

## 3. Container inventory

A representative `docker ps` after `make launch && make start-elk`
shows the containers below. The table groups them by function;
the network membership is the column the host topology turns on.

| Container | Image | Network(s) | Role |
|-----------|-------|------------|------|
| `nos3-fsw` | `ivvitc/nos3-64:20251107` (build), runtime cFS | `nos3_sc01` | cFE binary plus loaded apps |
| `sc01-fortytwo` | 42 build container | `nos3_sc01` | NASA Goddard 42 dynamics |
| `nos3-generic-eps-sim` | sim container | `nos3_sc01` | Power subsystem simulator |
| `nos3-generic-gnss-sim` | sim container | `nos3_sc01` | GNSS receiver simulator |
| `nos3-generic-tt_c-sim` | sim container | `nos3_sc01` | TT&C radio simulator |
| `nos3-generic-adcs-sim` | sim container | `nos3_sc01` | ADCS simulator |
| `nos3-generic-reaction-wheel-sim` | sim container | `nos3_sc01` | Reaction-wheel simulator |
| `nos3-generic-imu-sim`, `mag-sim`, `css-sim`, `fss-sim`, `torquer-sim`, `thruster-sim`, `radio-sim`, `star-tracker-sim`, `arducam-sim`, `novatel-sim` | sim containers | `nos3_sc01` | Per-component sims |
| `nos3-cosmos` | COSMOS image | `nos3-core` + `nos3_sc01` | Operator console |
| `nos3-elasticsearch` | `docker.elastic.co/elasticsearch:...` | `nos3-core` | Index store |
| `nos3-logstash` | `docker.elastic.co/logstash:...` | `nos3-core` | Ingest pipeline |
| `nos3-kibana` | `docker.elastic.co/kibana:...` | `nos3-core` | Dashboard UI |

The image tags are pinned in `nos3/elk/docker-compose.yml` and in
the FSW build/run scripts under `nos3/scripts/`. The build
container `ivvitc/nos3-64:20251107` is the canonical tag at the
time of the import baseline; it is locked because it bundles the
toolchain (cFS dependencies, NOS Engine, mqttcpp, etc.).

## 4. The build container

The build itself runs inside a container, not on the host. The
helper scripts under `nos3/scripts/` source `scripts/env.sh` to
get the `DBOX` and `DFLAGS` variables; these expand to a
`docker run` invocation that bind-mounts the repo and runs
the requested target. The user-facing surface is plain `make`
from `nos3/`:

- `make config`: runs the configuration generator.
- `make build-fsw`, `make build-sim`, `make build-gsw`: invoke
  CMake and `make` inside the build container.
- `make`: alias that runs the three above and lays the result
  out in `nos3/fsw/build/`, `nos3/sims/`, and `nos3/gsw/`.

The host toolchain is therefore not in scope for the testbed:
the only requirement on the host is Docker plus a recent enough
kernel for the user namespace and bridge-network features the
container set relies on.

## 5. Failure modes

The first few hours of a fresh checkout typically expose two
failure modes at this layer:

- **Docker network missing**: `nos3-core` does not exist before
  ELK starts. `make launch` creates `nos3_sc01` but not
  `nos3-core`; `make start-elk` (or the
  `nos3/elk/docker-compose.yml` `external: true` declaration on
  the network) requires that it already exists. The fix is
  `docker network create nos3-core` once per host.
- **X11 forwarding broken**: 42 launches but its windows do not
  appear. The diagnostic ladder is in the project root notes
  file; in short, `xeyes` from inside the dev container is the
  ground truth. Adding `runArgs` to `.devcontainer/devcontainer.json`
  is the wrong fix and is explicitly forbidden by the
  load-bearing-edits register.

Beyond these two, container-layer failures rarely persist: the
sim containers are stateless across restarts, and the FSW
container takes its persistent state from
`nos3/components/mgr/fsw/cfs/` files in the bind-mounted
repo, not from a Docker volume.
