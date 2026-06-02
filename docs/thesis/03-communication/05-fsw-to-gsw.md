# Boundary 5: flight software to ground software

This boundary models the link between an operating spacecraft and
its ground station. The cFS side terminates at the `CI_LAB` and
`TO_LAB` apps; the ground side is COSMOS (or OpenC3, YAMCS, or
AIT, depending on `<gsw>`). Of the six wire boundaries in this
testbed it is the only one where authentication is even available:
the COSMOS configuration in
`gsw/cosmos/config/tools/cmd_tlm_server/cmd_tlm_server.txt`
declares TWO parallel UDP interfaces, one direct ("DEBUG") and
one through CryptoLib ("RADIO"). Both are active at runtime; only
the RADIO path applies the CCSDS Space Data Link frame wrap.

## The two endpoints inside cFS

Two cFS apps sit on the bus side of this boundary:

- **`ci_lab`** (command ingest) opens a UDP socket on a fixed
  port. Incoming CCSDS command packets are decoded, validated for
  CCSDS primary-header sanity, and republished onto the Software
  Bus under the MID encoded in the packet. From there they are
  delivered to whatever cFS app subscribed to that MID.
- **`to_lab`** (telemetry output) subscribes to a curated list of
  telemetry MIDs (defined in
  `nos3/cfg/nos3_defs/tables/to_lab_sub.c` for this fork; the
  table is copied into `nos3/cfg/build/nos3_defs/tables/to_lab_sub.c`
  by `make config`). Every time a subscribed MID is published, `to_lab`
  reassembles the packet and writes it to a UDP socket aimed at
  the ground.

Both apps are upstream cFS sample apps. They are simple, well
understood, and emphatically not flight-grade; the upstream
README is explicit that `ci_lab` and `to_lab` exist for testbed
use. That suits this fork's purposes: a flight-grade
implementation would presumably be hardened against the same
spoof and replay shapes the supply-chain threat model studies, and
swapping it in would change the experiment.

## The wire format

The packets on the wire are CCSDS Space Packets (Space Packet
Protocol, the same shape as on the Software Bus) wrapped in CCSDS
Space Data Link frames once CryptoLib is in the path. Field
layout:

- **CCSDS primary header.** Six bytes. Carries the MID (called
  APID in the CCSDS standard), the packet type bit (command vs
  telemetry), the sequence count, and the data length.
- **CCSDS secondary header.** Present on every NOS3 packet. For
  command packets it carries the function code; for telemetry
  packets it carries a 6-byte mission elapsed time stamp.
- **Payload.** App-defined struct, packed in little-endian on this
  build target.

The Space Data Link wrapping is applied by CryptoLib at the link
boundary. The frame format is one of the variants defined in CCSDS
232 / 354: a transfer frame header, a security header, the encoded
Space Packets, and a security trailer (MAC).

## The two parallel paths

`gsw/cosmos/config/tools/cmd_tlm_server/cmd_tlm_server.txt` is the
authoritative declaration of how COSMOS reaches the spacecraft.
Two `INTERFACE` lines coexist:

- `INTERFACE DEBUG udp_interface.rb nos-fsw 5012 5013 nil nil 128 nil nil`
  (line 6). A plain UDP socket directly to the cFS container.
  No CryptoLib in the path. The COSMOS targets `CI_DEBUG`,
  `CFS`, `CMD_UTIL`, every `GENERIC_*`, `MGR`, `NOVATEL_OEM615`,
  `SAMPLE`, `TO_DEBUG`, and `GENERIC_ADCS` ride this interface
  (lines 8-25). Commands leave COSMOS as plain CCSDS Space
  Packets and arrive at `ci_lab` on UDP 5012 unmodified.
- `INTERFACE RADIO udp_interface.rb cryptolib 6010 6011 nil nil 128 nil nil`
  (line 27). UDP to the CryptoLib standalone container. The
  corresponding `*_RADIO` mirror targets (lines 28-44) ride this
  interface. CryptoLib wraps each command in a CCSDS Space Data
  Link frame, applies the SA, and forwards to FSW on the radio
  side; the inverse happens for downlink.

The DEBUG path is the one a default operator workflow typically
uses (it is the path the headless COSMOS cmd_tlm_server brings
up first, and `god_view_capture.py` listens directly on its
return port 5013). The RADIO path is enabled in parallel; the
operator picks per command which interface to send through by
naming the target either as `CFS.CMD` or `CFS_RADIO.CMD`. Both
paths terminate at the same `ci_lab` / `to_lab` apps inside cFS.

### CryptoLib configuration

`components/cryptolib/` is the encryption and authentication
sidecar. It is built standalone (`build_cryptolib.sh`,
`-DSTANDALONE_TCP=1`,
`-DSA_INTERNAL=1 -DMC_INTERNAL=1 -DKEY_INTERNAL=1
-DCRYPTO_LIBGCRYPT=1 -DSA_FILE=OFF`, all pinned in `nos3/Makefile`
line 79). The four port variables (`CRYPTO_RX_GROUND_PORT=6010`,
`CRYPTO_TX_GROUND_PORT=6011`, `CRYPTO_TX_RADIO_PORT=8010`,
`CRYPTO_RX_RADIO_PORT=8011`) are set in `nos3/scripts/env.sh`
lines 66-69. The wiring is:

- COSMOS sends a CCSDS command packet to CryptoLib's
  `RX_GROUND_PORT` (6010). CryptoLib applies the Space Data Link
  wrap and forwards the framed packet to the cFS `CI_LAB` UDP
  port via `TX_RADIO_PORT` (8010).
- `TO_LAB` sends CCSDS telemetry to CryptoLib's `RX_RADIO_PORT`
  (8011). CryptoLib unwraps and forwards to COSMOS on
  `TX_GROUND_PORT` (6011).

The security associations and key material are internal to
CryptoLib (`-DSA_INTERNAL=1`, `-DKEY_INTERNAL=1`); for the
supply-chain threat model they are treated as a fixed property
of the link, not an attack surface. The DEBUG path is the
inverse: a wire boundary that is deliberately undefended for
testbed convenience, and one of the load-bearing attack surfaces
for the threat-model chapter.

### The `ci` app: a second uplink listener

A separate `ci` app (distinct from `ci_lab`) is also built into
this FSW image (`nos3/fsw/apps/ci/`). The upstream sample
configurations under `nos3/fsw/apps/ci/fsw/examples/` pin
`CI_CUSTOM_UDP_PORT = 5010`; the research attack scripts under
`nos3/scripts/attack/` (`attack_noisy_trigger.py`,
`attack_md_jam.py`, `attack_syn_leak.py`) target this port by
default. Whether the production `ci_platform_cfg.h` used in the
build actually binds 5010 has not been independently verified at
HEAD, but the attack scripts assume so. From a threat-model
standpoint the relevance is structural: the spacecraft accepts
uplinks on more than one UDP port, and only one of them
(`ci_lab` on 5012) is what COSMOS's DEBUG interface targets.

## What `god_view_capture.py` is actually capturing

`god_view_capture.py` does not attach to CryptoLib's output. It
attaches to the TO_LAB UDP socket *before* CryptoLib gets it, i.e.
on the cFS side of the boundary. This is the whole reason it is
useful: it sees the unencrypted CCSDS stream that `to_lab` puts on
the wire, with all the MIDs and sequence counters intact, and
writes one JSON line per packet to
`nos3/attack_logs/cfs_god_view.json`.

This makes `cfs_god_view.json` an in-spacecraft observer, not a
ground observer. The distinction matters: an attacker who
suppresses the radio link would still appear in `cfs_god_view.json`
because the file does not depend on the radio link succeeding. The
research model is "the analyst has a privileged tap inside the
spacecraft", not "the analyst has the ground link". That is a
strong assumption and it is called out as a research assumption in
[00-overview.md](../00-overview.md).

## Authentication and the attacker

The boundary itself authenticates: CryptoLib will reject a packet
with a bad MAC or a replayed sequence number. The threat model
relevant to this boundary therefore is not "forge a packet"; it is
"abuse the legitimate channel". Two specific abuses are visible in
the research notes:

- **Command-mass injection from a compromised ground.** If the
  ground side is compromised, the attacker can issue legitimately
  authenticated CCSDS commands. CryptoLib lets them through; cFS
  acts on them. This is out of scope for the supply-chain
  threat model.
- **Telemetry exfiltration from a compromised FSW app.** The
  compromised app publishes attacker-chosen data under a MID that
  is in `to_lab_sub.c`. CryptoLib authenticates the resulting
  frame; the ground sees a legitimate, signed packet whose payload
  is whatever the attacker wanted to leak. This is in scope, and
  it is one of the shapes the Logstash pipeline tagging surfaces
  via the `attack_app` and `attack_loaded` tags.

## Deviation from upstream

The wire protocol, the two endpoint apps, and CryptoLib itself are
unchanged from the import baseline. Two changes are observable at
this boundary:

- `nos3/cfg/nos3_defs/tables/to_lab_sub.c` is a DTU-expanded
  subscription set rather than the upstream sample's minimal
  default. DTU commit `8cad7cc8` ("Added all msgid's to
  to_lab_sub.c, so now they all show up i Kibana") widened it
  to enumerate every component MID, which is what makes the
  full bus surface visible in `cfs_god_view.json` and Kibana.
  The file lives in the mission-tables tree, not in the
  blackboard component; see
  [../04-apps/blackboard.md](../04-apps/blackboard.md) for a
  note correcting earlier mis-attributions.
- The CryptoLib build flags in the top-level Makefile
  (`STANDALONE_TCP=1`, `SA_INTERNAL=1`, `MC_INTERNAL=1`,
  `KEY_INTERNAL=1`, `CRYPTO_LIBGCRYPT=1`) are pinned here rather
  than left as user-configurable. The pinning makes runs
  reproducible across machines without operator choice on the
  link security profile.
