# 03.00 Communication Layers Overview

The testbed crosses five distinct communication layers between the
operator's browser and the orbital-state ground truth in 42. Each
layer has its own message format, transport, and trust assumptions.
This sub-directory devotes one document to each layer; the
introduction below names them in order and lists the figure that
each document pins to in
[`../08-figures/figures.md`](../08-figures/figures.md).

## 1. Reading order

Every layer document follows the same rough shape: the format on
the wire, the writer, the reader, the relevant configuration
files, and the failure modes the testbed has already documented.
The reading order matches the data-flow direction from sensors to
operator console:

| # | Document | Layer | Figure |
|---|----------|-------|--------|
| 1 | [`01-host-and-containers.md`](01-host-and-containers.md) | Host / container topology, networks, exposed ports | F2 |
| 2 | [`02-fsw-software-bus.md`](02-fsw-software-bus.md) | cFE Software Bus, CCSDS Message IDs, pub/sub | F3 |
| 3 | [`03-fsw-to-sim.md`](03-fsw-to-sim.md) | HWLIB <-> NOS Engine I2C / SPI / CAN / UART buses | F4 |
| 4 | [`04-sim-to-42-dynamics.md`](04-sim-to-42-dynamics.md) | 42 IPC sockets, ports 4278-4287, the tight-prefix invariant | F5 |
| 5 | [`05-fsw-to-gsw.md`](05-fsw-to-gsw.md) | CCSDS over CryptoLib, COSMOS uplink/downlink | F6 |
| 6 | [`06-telemetry-to-elk.md`](06-telemetry-to-elk.md) | File-tail ingest, Logstash, Elasticsearch, Kibana | F7 |

The system-overview figure F1 sits one layer above the layer
documents; F8 (trust boundaries) sits one layer below them and is
treated in [`../02-architecture.md`](../02-architecture.md) section 8.

## 2. Layer trust assumptions

Two of the layers carry an explicit cryptographic boundary; the
rest rely on the Docker network membership for confidentiality
and on the FSW process boundary for integrity. The pattern:

- **Browser <-> COSMOS** (layer 5): plain HTTP on the host
  loopback. The browser is trusted because it runs on the same
  host as the operator console.
- **COSMOS <-> FSW** (layer 5): CCSDS frames inside an SDLS
  authenticated wrapper through CryptoLib. This is the one
  modelled cryptographic hop in the testbed; without CryptoLib
  keys, an attacker on the wire cannot inject a valid command.
- **Inside cFE** (layer 2): the Software Bus is in-process; any
  loaded app can publish on any MID. Cross-app integrity rests on
  every loaded app being honest. `noisy_app` exists precisely to
  make the cost of that assumption visible.
- **FSW <-> sims** (layer 3) and **sims <-> 42** (layer 4): plain
  TCP inside the per-spacecraft Docker network. A simulator that
  joins `nos3_sc01` is trusted to publish honest hardware
  responses; a malicious simulator container is the canonical
  realisation of a component supply-chain attack.
- **Logs -> ELK** (layer 6): file-tail with no integrity check.
  An attacker who can write to the host-side log files (or who
  controls the Logstash filter set) can rewrite or drop attack
  evidence between the FSW and the dashboard.

The five attack-surface zones in figure F8 map directly onto the
trust boundaries above.

## 3. Why six documents and not one

A single combined document was rejected for two reasons. First,
each layer has its own tooling (`make config` for the FSW build,
`docker network create` for the container layer, the Inp_IPC.txt
table for 42, `logstash.conf` for ELK), and the reader who needs
to debug one layer rarely needs the others on screen. Second,
the layers diverge on protocol: an examiner reading the CCSDS hop
should not have to thread it together with the byte-by-byte
rgetc reader on the 42 socket path. Splitting the documents lets
each one cite the configuration files and source-line numbers
that matter for that layer, without diluting the others.

Cross-references between layer documents are explicit. The TT&C
component, for example, sits across layer 3 (UART bus into the
FSW, see
[`03-fsw-to-sim.md`](03-fsw-to-sim.md)), layer 4 (42 socket
prefix on port 4286, see
[`04-sim-to-42-dynamics.md`](04-sim-to-42-dynamics.md)), and the
dedicated app deep-dive at
[`../04-apps/tt_c.md`](../04-apps/tt_c.md). Each reference names
the file path or the figure ID; the components themselves are not
re-described in the layer documents.
