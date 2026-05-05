# 03.02 Flight Software: cFE Software Bus

This document describes the inter-app communication layer inside
the cFE flight binary. It pins to figure F3 in
[`../08-figures/figures.md`](../08-figures/figures.md).

## 1. The Software Bus contract

The Software Bus (SB) is a publish/subscribe message router
internal to cFE. Every cFS application that wants to talk to
another application creates a pipe and either subscribes to a
Message ID (MID) or publishes a packet whose header carries that
MID. The SB looks up subscribers by MID and copies the packet to
each subscriber's pipe. There is no out-of-band channel; if it is
not on the SB, no other app can see it.

The MID is a 16-bit value carried in the CCSDS primary header
the FSW emits and consumes. The two bits that matter for the
testbed are the high nibble:

- `0x0XXX`: telemetry packet (downlink direction).
- `0x1XXX`: command packet (uplink direction or app-to-app
  command).

The remaining bits identify the originating app. Two bases
co-exist on the testbed.

## 2. Two MID bases: heritage 0x18/0x08 vs DTU components 0x19/0x09

The heritage NASA cFS apps (CFE service apps, the cFS Tools
suite) live in the `0x1800 / 0x0800` base. The DTU-added
component apps live in the `0x1900 / 0x0900` base. The bases are
declared in the project headers cited below and are computed at
compile time, not hand-defined per app.

The heritage base is declared at
`nos3/cfg/nos3_defs/cfe_msgid_api.h:18`:

```
CFE_PLATFORM_CMD_TOPICID_TO_MIDV(topic) = (0x1800 | topic)
CFE_PLATFORM_TLM_TOPICID_TO_MIDV(topic) = (0x0800 | topic)
```

The component base is declared at
`nos3/cfg/nos3_defs/nos3_component_msgid_mapping.h:19`:

```
CFE_PLATFORM_COMPONENT_CMD_TOPICID_TO_MIDV(topic) = (0x1900 | topic)
CFE_PLATFORM_COMPONENT_TLM_TOPICID_TO_MIDV(topic) = (0x0900 | topic)
```

Each app's `*_msgid_values.h` defines a topic-ID byte and
applies one of the two macros. For example, `GENERIC_EPS_CMD_MID`
is `0x1900 | 0x1A = 0x191A` and `GENERIC_EPS_HK_TLM_MID` is
`0x0900 | 0x1A = 0x091A`. The two-base split is a DTU
convention: it lets the heritage apps compile unchanged against
upstream cFE while keeping the component MIDs from colliding
with NASA's reservation under cFE Draco. The migration to Draco
made the previous flat MID space ambiguous, which is why the
two-base scheme is now load-bearing for the build.

Topic-ID assignments for the DTU-added components, picked from
the per-app headers and listed once here so the SB topology in
figure F3 is reproducible from primary sources:

| App | Topic ID | CMD MID | TLM MID |
|-----|----------|---------|---------|
| GENERIC_EPS | 0x1A | 0x191A | 0x091A |
| GENERIC_GNSS | 0x70 | 0x1970 | 0x0970 |
| GENERIC_TT_C | 0x74 | 0x1974 | 0x0974 |
| GENERIC_ADCS | 0x40 | 0x1940 | 0x0940 |
| GENERIC_REACTION_WHEEL | 0x92 | 0x1992 | 0x0992 |
| GENERIC_IMU | 0x80 | 0x1980 | 0x0980 |
| GENERIC_MAG | 0x84 | 0x1984 | 0x0984 |
| GENERIC_CSS | 0x88 | 0x1988 | 0x0988 |
| GENERIC_TORQUER | 0x96 | 0x1996 | 0x0996 |
| MGR | 0xF8 | 0x19F8 | 0x09F8 |

Some apps in the testbed (`BEACON_APP`, `NOISY_APP`, `SAMPLE`)
predate the component-base convention or piggy-back on the
heritage base; their MIDs sit in `0x18FA`, `0x18FB`, `0x18FA`
respectively. The deep-dive documents under
[`../04-apps/`](../04-apps/) state each app's MID with a citation
to its `*_msgid_values.h`.

## 3. Pub/sub topology

Figure F3 in
[`../08-figures/figures.md`](../08-figures/figures.md) is the
authoritative drawing. Three patterns dominate the topology:

- **Wakeup edges**: the Scheduler app (SCH, `0x18B0`) emits a
  1 Hz wakeup MID that every periodic app subscribes to. The
  schedule table at `nos3/cfg/nos3_defs/tables/sch_def_schtbl.c`
  controls which MIDs go where. SCH is the heartbeat: if it
  stops, the periodic apps stop emitting HK.
- **Housekeeping aggregation**: each component app publishes its
  HK packet (`0x09xx`) on a wakeup. The Housekeeping aggregator
  HK (`0x1880`, `0x0880`) subscribes to that set and packs it
  into a combined HK telemetry packet that the Telemetry Output
  app TO_LAB downlinks. The combined HK packet is the primary
  surface visible to the operator.
- **Command routing**: ground commands enter through CI_LAB
  (`0x1884`); CI_LAB injects each packet onto the SB by MID, and
  the SB dispatches to whichever app subscribed to that
  command's MID.

The DTU additions to the topology, as drawn in F3:

- **MGR** (`0x19F8` / `0x09F8`): subscribes to mode-change
  commands, publishes a mission-state HK packet.
- **BEACON_APP** (`0x18FA`): subscribes to ping commands,
  publishes pong packets that the operator can read on the
  TO_LAB downlink.
- **NOISY_APP** (`0x18FB`): publishes spoofed `0x091A` packets
  on the SB. Because the SB does not authenticate publishers
  by app identity, those packets are indistinguishable to HK
  from the legitimate EPS publication. This edge in F3 is drawn
  dashed and red because it is the attack-injection edge for
  the noisy_app scenario.

## 4. Scheduler tables

The two configuration files that define the SB pub/sub topology
are:

- `nos3/cfg/nos3_defs/tables/sch_def_schtbl.c`: wall-clock
  schedule. Maps a slot in the 1 Hz scheduler frame to a MID to
  publish. Modifying this file adds, removes, or reorders the
  per-tick wakeup edges in figure F3.
- `nos3/cfg/nos3_defs/tables/sch_def_msgtbl.c`: message table.
  Maps the slot ID used by the schedule table to the actual
  CCSDS message contents.

Adding a periodic component app requires entries in both
tables. The DTU `mgr` and `beacon_app` apps each have schedule
slots; the deep-dives at
[`../04-apps/mgr.md`](../04-apps/mgr.md) and
[`../04-apps/beacon_app.md`](../04-apps/beacon_app.md) cite the
specific entries.

## 5. Stored commands and limit triggers

Two heritage apps add depth to the SB topology:

- **SC (Stored Command)** at `0x18A6`: holds time-tagged or
  triggered RTS (Relative Time Sequence) tables. RTS003 is
  defined in `nos3/cfg/nos3_defs/tables/sc_rts003.c` and is the
  EO1 mission's nominal-mode entry sequence.
- **LC (Limit Checker)** at `0x18A4`: subscribes to selected
  HK MIDs, applies watchpoint and actionpoint rules, and
  publishes triggers that SC can subscribe to.

LC-driven autonomy is one of the supply-chain attack-relevant
control paths: a tampered LC table can silence a watchpoint that
would otherwise notice attack-induced telemetry drift.

## 6. Why this layer is the attack-publication layer

Three properties of the SB matter for the supply-chain thesis:

- The SB does not authenticate publishers. Any loaded app can
  publish on any MID.
- The HK aggregator subscribes by MID, not by source app. If two
  apps publish the same `0x091A` MID, both packets reach HK and
  both look like legitimate EPS HK to a downstream consumer.
- The CryptoLib boundary at the FSW edge protects the operator
  console. It does not protect the SB itself: by the time the
  packet is on the SB, it has already been decrypted, and the
  attacker model assumes a foothold inside the FSW process.

Together those make the SB the natural injection layer for a
component supply-chain attack delivered through a tampered FSW
app. `noisy_app` is the in-band realisation of that scenario.

## 7. Cross-references

- App-level MID assignments and pipes: each
  [`../04-apps/<name>.md`](../04-apps/) document.
- Heritage-base derivation: `nos3/cfg/nos3_defs/cfe_msgid_api.h`.
- Component-base derivation:
  `nos3/cfg/nos3_defs/nos3_component_msgid_mapping.h`.
- Schedule and message tables:
  `nos3/cfg/nos3_defs/tables/sch_def_*.c`.
- Stored RTS:
  `nos3/cfg/nos3_defs/tables/sc_rts003.c`.
