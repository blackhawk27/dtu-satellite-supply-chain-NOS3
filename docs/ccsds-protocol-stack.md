# CCSDS Protocol Stack in the NOS3-Draco FSW

This document is a developer reference for every CCSDS-family protocol
that the flight software in this repository touches. It is meant to be
read once front-to-back to learn the stack, then referred back to when
you need to decode a specific byte on the wire or trace where a frame
is built. Every byte layout is sourced directly from the headers in
this repo. File:line citations are given so you can verify any claim.

CCSDS stands for Consultative Committee for Space Data Systems. CCSDS
publishes "Blue Books" (recommended standards) that civil space
agencies follow. NOS3 implements a small subset of those Blue Books.
This doc covers the subset that is actually present in the FSW. Things
the Blue Books cover that this repo does not implement are listed at
the end.

## How to read this doc

- **Bit numbering.** CCSDS numbers bits MSB-first within each octet.
  When a row says "bits 7:5", that means the three most-significant
  bits of the byte. We follow the CCSDS convention everywhere.
- **Endianness.** All CCSDS protocols specify big-endian (network
  byte order) on the wire. Multi-byte integers are transmitted MSB
  first. The cFE structures store these as `uint8` arrays, not as
  native ints, precisely so the layout does not depend on host
  endianness.
- **Status markers.** Each section is tagged:
  - *ACTIVE*: built into the default FSW path, exercised on every run.
  - *EXAMPLE-ONLY*: implementation is in the tree but only wired up
    in one of the alternate `examples/` builds (typically `udp_tf`
    or `multi_tf`). The default `udp` build does not use it.
  - *LIBRARY-ONLY*: the protocol's structures or helpers exist (often
    inside CryptoLib), but no FSW code path calls them in this repo.
  - *IMPLICIT*: the protocol is conceptually present but the FSW does
    not emit a fully spec-compliant header for it.

## Stack at a glance

```
                 +------------------------------------+
                 |      Application Data (cFS app)    |
                 +------------------------------------+
                                  |
                 +------------------------------------+
   ALWAYS  ----> |  CCSDS Space Packet (133.0-B)      |   <- Layer 1 envelope
                 |  Pri Hdr [6B] + Sec Hdr + payload  |
                 +------------------------------------+
                                  |
            +------------- branch ---------------+
            |                                    |
   default udp build                  udp_tf example build
            |                                    |
            v                                    v
  +------------------+              +-------------------------+
  |  UDP/IP datagram |              |  TC/TM Transfer Frame   |  Layer 2
  |  (Space Packet   |              |  (132.0-B / 232.0-B)    |
  |   carried raw)   |              |  + optional SDLS hdr/   |
  +------------------+              |    trailer (355.0-B)    |
                                    |  + optional COP-1 CLCW  |
                                    +-------------------------+
                                                |
                                    +-------------------------+
                                    |  UDP/IP datagram        |
                                    +-------------------------+

   Side paths (do not replace the main stack):
     - CFDP PDUs (727.0-B) ride on Space Packets via the CF cFS app.
     - DTN bundles (RFC 5050 / 734.2-B) ride out through SBN's DTN
       protocol module via ION, bypassing the TM/TC chain entirely.
```

## Glossary of every abbreviation in this doc

| Abbrev | Expansion | One-line meaning |
|---|---|---|
| AOS | Advanced Orbiting Systems | A frame protocol (CCSDS 732.0-B) - not used here |
| APID | Application Process ID | 11-bit "where this packet goes" identifier inside a Space Packet |
| ARQ | Automatic Repeat reQuest | Generic name for retransmit-on-loss schemes; COP-1 is one |
| BP | Bundle Protocol | DTN store-and-forward protocol (RFC 5050 / CCSDS 734.2-B) |
| CCSDS | Consultative Committee for Space Data Systems | The standards body |
| CDS | CCSDS Day Segmented (time code) | One of the two CCSDS time-code formats |
| cFE | core Flight Executive | The runtime under cFS apps (ES, EVS, SB, TBL, TIME modules) |
| cFS | core Flight System | NASA's open-source flight software framework |
| CFDP | CCSDS File Delivery Protocol | File transfer over space links (727.0-B) |
| CI | Command Ingest | Generic name for the uplink-side cFS app |
| CI_LAB | Command Ingest LAB | The lightweight reference CI app shipped with cFE |
| CLCW | Communications Link Control Word | 4-byte status word the spacecraft sends back to ground in COP-1 |
| COP-1 | Communications Operation Procedure-1 | ARQ flow control over a TC link (232.1-B) |
| CRC | Cyclic Redundancy Check | The integrity check used in FECF / OCF |
| CUC | CCSDS Unsegmented (time) Code | One of the two CCSDS time-code formats |
| DTN | Delay-Tolerant Networking | Store-and-forward networking for space, hosts the BP |
| EDS | Electronic Data Sheets | Schema describing packet field layouts |
| EID | Entity ID (CFDP) | Per-node identifier in a CFDP transaction |
| EIN | Endpoint Identifier (DTN) | Where a bundle is delivered (e.g. `ipn:1.1`) |
| EOF | End Of File (CFDP) | A specific CFDP File Directive PDU type |
| EVS | Event Services (cFE) | cFE module that emits human-readable events |
| FARM-1 | Frame Acceptance and Reporting Mechanism | The receiver half of COP-1 |
| FECF | Frame Error Control Field | Optional 16-bit CRC trailer on TC/TM frames |
| FHP | First Header Pointer | Field in TM frame indicating where the first packet starts |
| FSR | Frame Security Report | Alternative content of the TM OCF (instead of CLCW) |
| GSW | Ground Software | The off-spacecraft side (COSMOS, OpenC3, YAMCS, AIT) |
| GVCID | Global Virtual Channel ID | TFVN + SCID + VCID + MAPID, used by SDLS to scope a key |
| ION | Interplanetary Overlay Network | NASA-JPL's reference DTN/BP implementation |
| IV | Initialization Vector | Crypto input used by SDLS for AEAD modes |
| MAC | Message Authentication Code | Crypto integrity tag in SDLS trailer |
| MAP | Multiplexer Access Point | Sub-channel of a TC virtual channel |
| MAPID | MAP ID | 6-bit ID of a MAP within a VC |
| MCFC | Master Channel Frame Count | TM frame counter, one per master channel |
| MCID | Master Channel ID | TFVN + SCID; identifies a flow at the master level |
| MID | Message ID | cFS's 32-bit handle that names a Software Bus message |
| OCF | Operational Control Field | Optional 4-byte TM trailer, carries CLCW or FSR |
| OSAL | Operating System Abstraction Layer | cFE's portability shim |
| PDU | Protocol Data Unit | Generic name for "the unit a protocol moves" |
| POF | Packet Order Flag | TM frame status bit |
| PSP | Platform Support Package | cFE's per-platform shim under OSAL |
| PUS | Packet Utilization Standard (ECSS) | A European parallel to cFS messaging, mostly absent here |
| SB | Software Bus | cFE's pub/sub message router |
| SBN | Software Bus Network | cFS app that bridges SBs across nodes |
| SCID | Spacecraft ID | 10-bit field in TC/TM primary header |
| SDLP | Space Data Link Protocol | The frame layer; TC-SDLP and TM-SDLP are the two we use |
| SDLS | Space Data Link Security | The encrypt+MAC layer riding inside TC/TM frames (355.0-B) |
| SDR | Stable Data Repository | ION's persistent bundle store |
| SDU | Service Data Unit | What a layer hands to the layer below |
| SHF | Secondary Header Flag | The 1-bit "is a secondary header present" field |
| SLID | Segment Length ID | TM frame status bits, fixed to 11 in this repo |
| SPI | Security Parameter Index | 16-bit "which Security Association applies" field in SDLS |
| SPP | Space Packet Protocol | CCSDS 133.0-B, the universal envelope |
| TAI | International Atomic Time | The cFE default epoch for timestamps |
| TC | Telecommand | Uplink direction, ground to spacecraft |
| TFVN | Transfer Frame Version Number | 2-bit field at the top of a TC/TM frame |
| TLV | Type-Length-Value | Encoding used inside CFDP PDUs |
| TM | Telemetry | Downlink direction, spacecraft to ground |
| TO | Telemetry Output | Generic name for the downlink-side cFS app |
| TO_LAB | Telemetry Output LAB | The lightweight reference TO app shipped with cFE |
| TSN | Transaction Sequence Number (CFDP) | Per-CFDP-transaction identifier |
| VCFC | Virtual Channel Frame Count | TM frame counter, one per VC |
| VCID | Virtual Channel ID | 6-bit (TC) or 3-bit (TM) sub-channel within a master channel |

# Layer 1: CCSDS Space Packet Protocol (CCSDS 133.0-B)

Status: **ACTIVE.** This is the universal envelope. Every cFS Software
Bus message is a Space Packet, every command from the ground is a
Space Packet, every telemetry packet downlinked is a Space Packet.
If you can only learn one CCSDS protocol from this doc, learn this one.

## Why this repo uses it

cFS is built on a publish/subscribe message bus (the Software Bus).
Each message has a header that names it, sizes it, and orders it. cFS
chose CCSDS Space Packets as that header so that the same bytes that
flow between apps inside the spacecraft also fly straight out the
radio with no reformatting. CI and TO are thin shims: they read or
write CCSDS Space Packets exactly as they appear on the bus.

## Primary header (6 bytes, big-endian)

Source of truth: `nos3/fsw/cfe/modules/msg/fsw/inc/ccsds_hdr.h:51-68`.

```
     bit  7   6   5   4   3   2   1   0
        +---+---+---+---+---+---+---+---+
Byte 0: |   Ver  | T |SHF|     APID      |   StreamId[0]
        +---+---+---+---+---+---+---+---+
Byte 1: |              APID             |   StreamId[1]
        +---+---+---+---+---+---+---+---+
Byte 2: | SegFl |     Sequence Count    |   Sequence[0]
        +---+---+---+---+---+---+---+---+
Byte 3: |        Sequence Count         |   Sequence[1]
        +---+---+---+---+---+---+---+---+
Byte 4: |    Packet Length (high)       |   Length[0]
        +---+---+---+---+---+---+---+---+
Byte 5: |    Packet Length (low)        |   Length[1]
        +---+---+---+---+---+---+---+---+
```

Field by field:

| Field | Bits | Meaning |
|---|---|---|
| Ver | StreamId[0] bits 7:5 | CCSDS version. 0 = v1, 1 = v2 (with 4-byte extended header) |
| T (Type) | StreamId[0] bit 4 | 0 = telemetry, 1 = command |
| SHF | StreamId[0] bit 3 | Secondary Header Flag. 0 = absent, 1 = present |
| APID | StreamId[0] bits 2:0 + StreamId[1] | 11-bit Application Process ID |
| SegFl | Sequence[0] bits 7:6 | Segmentation flags. 0=continuation, 1=first, 2=last, 3=unsegmented |
| Sequence Count | Sequence[0] bits 5:0 + Sequence[1] | 14-bit per-APID rolling counter |
| Packet Length | Length[0..1] | (total packet size in bytes) - 7 |

Two gotchas that catch every newcomer:

1. **The length field is `total_size - 7`, not `total_size`.** The
   "-7" is "-6 (the primary header) -1 (because the field is defined
   as length-minus-one)". So a 22-byte packet carries `0x000F` in
   bytes 4-5. Confirm in `nos3/fsw/cfe/modules/msg/fsw/src/cfe_msg_ccsdspri.c`
   (`CFE_MSG_SIZE_OFFSET = 7`).
2. **The "complete packet" segmentation flag is `11b`, not `00b`.**
   cFS sets all messages to `11b` (`0xC000` in the raw word). A
   value of `00b` means "this is a continuation segment", which cFS
   never emits.

## Command secondary header (2 bytes)

Source of truth: `nos3/fsw/cfe/modules/msg/option_inc/default_cfe_msg_sechdr.h:60-68`.

```
     bit  7   6   5   4   3   2   1   0
        +---+---+---+---+---+---+---+---+
Byte 0: | R |    Function Code (FC)     |
        +---+---+---+---+---+---+---+---+
Byte 1: |          Checksum             |
        +---+---+---+---+---+---+---+---+
```

| Field | Bits | Meaning |
|---|---|---|
| R | bit 7 | Reserved, must be 0 |
| Function Code | bits 6:0 | 7-bit command sub-type, 0..127, app-specific |
| Checksum | byte 1 | 8-bit XOR over the entire packet, with this byte zeroed during the calc |

The function code is how a single APID can carry many commands. For
example, the housekeeping APID for an app might accept FC 0
("noop"), FC 1 ("reset counters"), FC 2 ("send hk"), and so on. The
checksum is generated by `nos3/fsw/cfe/modules/msg/fsw/src/cfe_msg_sechdr_checksum.c`.

## Telemetry secondary header (6 bytes)

Source of truth: `nos3/fsw/cfe/modules/msg/option_inc/default_cfe_msg_sechdr.h:73-76`.

```
        +-------------------------------+
Bytes 0-3: |     Seconds (uint32, big-endian)     |
        +-------------------------------+
Bytes 4-5: |  Subseconds (uint16, big-endian)     |
        +-------------------------------+
```

The telemetry secondary header is just a `uint8 Time[6]`. The first
four bytes are the seconds count and the last two are the subsecond
fraction, both big-endian. There is no "P-field" or other CCSDS time
code preamble in front of it. See the "CCSDS Time Code" section below
for why this is *similar to but not formally* a CCSDS Unsegmented
Time Code.

cFS fills this field via `CFE_SB_TimeStampMsg()` immediately before
transmit. Concrete example: `nos3/fsw/apps/beacon_app/fsw/src/beacon_app.c:44-45`
calls `CFE_SB_TimeStampMsg()` then `CFE_SB_TransmitMsg()` back-to-back.

## Extended header (CCSDS v2, 4 bytes, optional)

Source of truth: `nos3/fsw/cfe/modules/msg/fsw/inc/ccsds_hdr.h:73-84`. Used
when the build is configured for v2 message IDs.

```
     bit  7   6   5   4   3   2   1   0
        +---+---+---+---+---+---+---+---+
Byte 0: |  EDS Version  | E | P | SubID |   Subsystem[0]
        +---+---+---+---+---+---+---+---+
Byte 1: |          Subsystem ID         |   Subsystem[1]
        +---+---+---+---+---+---+---+---+
Byte 2: |        System ID (high)       |   SystemId[0]
        +---+---+---+---+---+---+---+---+
Byte 3: |        System ID (low)        |   SystemId[1]
        +---+---+---+---+---+---+---+---+
```

| Field | Bits | Meaning |
|---|---|---|
| EDS Version | bits 15:11 of Subsystem | 5-bit packet-definition schema version |
| E (Endian) | bit 10 of Subsystem | 0 = big-endian payload, 1 = little-endian |
| P (Playback) | bit 9 of Subsystem | 0 = original, 1 = playback / replay |
| Subsystem ID | bits 8:0 of Subsystem | 9-bit qualifier, mission-defined |
| System ID | SystemId[0..1] | 16-bit spacecraft / system identifier |

## How the Message ID maps to these bits (V2 build)

cFS does not expose APID directly to apps. It exposes a 32-bit
`CFE_SB_MsgId_t`. In the V2 build (`nos3/fsw/cfe/modules/msg/fsw/src/cfe_msg_msgid_v2.c`),
the 16-bit value inside that handle is composed as:

```
     bit 15      ...      8   7    6 ... 0
        +-------------------+----+--------+
        |   Subsystem (8b)  | T  | APID lsb (7b) |
        +-------------------+----+--------+
            ^                ^         ^
            |                |         |
   Subsystem[1] of            |     bottom 7 bits of APID
   the extended header        |     (StreamId[1] & 0x7F)
                       1 = command,
                       0 = telemetry
```

So when you see `0x18FA` in code, that is "subsystem 0x18, command,
APID-lsb 0x7A". The `0x1` vs `0x0` nibble distinction (commands 
`0x1XXX` vs telemetry `0x0XXX`)
is *not* the cFS V1/V2 split, it is just a convention where mission
designers reserve different subsystem-byte ranges for command and
telemetry MIDs.

## Where Space Packets are built and sent in this repo

- `nos3/fsw/cfe/modules/msg/fsw/src/cfe_msg_init.c:34` -
  `CFE_MSG_Init()` zeros a buffer and writes a default primary
  header into it. Every cFS app calls this once per message type at
  startup.
- `nos3/fsw/cfe/modules/sb/fsw/src/cfe_sb_api.c` - `CFE_SB_TransmitMsg()`
  enforces the SB routing and hands the packet onward.
- `nos3/fsw/apps/beacon_app/fsw/src/beacon_app.c:44-45` - canonical
  example of "stamp, then transmit".
- `nos3/fsw/cfe/modules/msg/fsw/src/cfe_msg_ccsdspri.c` - all the
  primary-header bit-twiddling lives in one file.
- `nos3/fsw/cfe/modules/msg/fsw/src/cfe_msg_sechdr_checksum.c` -
  command checksum generator.

## Worked example: decoding a 22-byte command on the wire

Suppose this hex shows up on UDP port 5012 (CI_LAB's listen port):

```
18 FA C0 00 00 0F 02 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

Decode:

| Bytes | Value | Meaning |
|---|---|---|
| `18 FA` | StreamId | Ver=0, Type=1 (command), SHF=1, APID=`0x0FA` |
| `C0 00` | Sequence | SegFl=11b (unsegmented), SeqCount=0 |
| `00 0F` | Length | 0x000F = 15, so total packet size = 15 + 7 = 22 bytes |
| `02` | FC | Function code 2 |
| `00` | Checksum | XOR over the whole packet |
| 14 bytes | payload | command-specific |

That is enough to look up the cFS app whose command APID is `0x0FA`
(it is `SAMPLE_APP` if you grep `nos3/cfg`), see what FC 2 does, and
verify the checksum.

# Layer 2: CCSDS TC Space Data Link Protocol (CCSDS 232.0-B)

Status: **EXAMPLE-ONLY (udp_tf variant).** The default CI app
(`ci_lab`) reads raw Space Packets from a UDP socket on port 5012
(see `nos3/fsw/apps/ci_lab/fsw/inc/ci_lab_app.h:40` and
`ci_lab_app.c:153`). The TC frame path is wired up in the alternate
`ci/fsw/examples/udp_tf/` build, which wraps each uplink packet in a
TC Transfer Frame and (optionally) runs CryptoLib over it.

## Why this repo carries it

The TC SDLP is the CCSDS-blessed envelope for ground-to-spacecraft
links. It adds a small header that lets the spacecraft validate that
a frame is actually addressed to it (SCID), demux to a virtual
channel (VCID), check ordering (frame sequence number), and detect
bit errors (the optional FECF). Real missions need this; lab simulators
generally do not, which is why it lives behind the `udp_tf` example.

## Primary header (5 bytes)

Two structures describe this header in the repo. They are equivalent;
the CryptoLib one is bit-precise and the io_lib one wraps it in
helper getters.

CryptoLib bit-precise: `nos3/components/cryptolib/include/crypto_structs.h:340-355`.
io_lib opaque: `nos3/fsw/apps/io_lib/fsw/public_inc/tctf.h:56-61`.

```
     bit  7   6   5   4   3   2   1   0
        +---+---+---+---+---+---+---+---+
Byte 0: | TFVN  | B | C |Spr|  SCID(hi) |   Octet[0]
        +---+---+---+---+---+---+---+---+
Byte 1: |          SCID (low)           |   Octet[1]
        +---+---+---+---+---+---+---+---+
Byte 2: |     VCID      | Frame Len(hi) |   Octet[2]
        +---+---+---+---+---+---+---+---+
Byte 3: |       Frame Length (low)      |   Octet[3]
        +---+---+---+---+---+---+---+---+
Byte 4: |     Frame Sequence Number     |   Sequence
        +---+---+---+---+---+---+---+---+
```

| Field | Bits | Meaning |
|---|---|---|
| TFVN | byte 0 bits 7:6 | Transfer Frame Version Number, always 00 for TC |
| B (Bypass) | byte 0 bit 5 | 0 = Type-A (sequence-checked), 1 = Type-B (bypass FARM-1) |
| C (Ctrl Cmd) | byte 0 bit 4 | 0 = Type-D (data frame), 1 = Type-C (control, e.g. SetVR) |
| Spare | byte 0 bits 3:2 | Must be 00 |
| SCID | byte 0 bits 1:0 + byte 1 | 10-bit Spacecraft ID |
| VCID | byte 2 bits 7:2 | 6-bit Virtual Channel ID, 0..63 |
| Frame Length | byte 2 bits 1:0 + byte 3 | Total frame length in octets, max 1024 |
| Frame Seq Num | byte 4 | N(S), the per-VC sequence number |

Optional **2-byte FECF trailer** at the very end of the frame
(CRC-16). Whether it is present is fixed per physical channel and
controlled at compile time by `TCTF_FRAME_ERROR_CONTROL_INCLUDED`
(default `false`) in `tctf.h:28`.

Optional **1-byte segment header** between the primary header and
the data field, present when the channel uses a service that needs
MAP demultiplexing or segmentation. Layout:

```
     bit  7   6   5   4   3   2   1   0
        +---+---+---+---+---+---+---+---+
        | Seq Flags |       MAP ID      |
        +---+---+---+---+---+---+---+---+
```

The 6-bit MAP ID (Multiplexer Access Point) is the second-level
demux below VCID. Sequence-flag values are enumerated at
`tctf.h:91-98` (`TCTF_CONTINUING_SEGMENT=0`, `TCTF_FIRST_SEGMENT=1`,
`TCTF_LAST_SEGMENT=2`, `TCTF_NO_SEGMENTATION=3`).

What rides inside: Space Packets when the service is MAPP / VCP,
or arbitrary octets when the service is MAPA / VCA / VCF / MCF.
Service enums at `tctf.h:45-53`.

Where the framing happens: `nos3/fsw/apps/ci/fsw/examples/udp_tf/ci_custom.c:345`
calls `Crypto_TC_ProcessSecurity()` to peel the SDLS layer off the
inbound TC frame; the io_lib `TCTF_*` accessors then read the fields.

# Layer 2: CCSDS TM Space Data Link Protocol (CCSDS 132.0-B)

Status: **EXAMPLE-ONLY (udp_tf variant).** Mirror of the TC story:
the default `to_lab` ships raw Space Packets to ground over UDP
(`nos3/fsw/apps/to_lab/fsw/src/to_lab_app.c:601`, `OS_SocketSendTo`),
and the `to/fsw/examples/udp_tf/` build wraps them in TM Transfer
Frames first.

## Why this repo carries it

TM-SDLP is the downlink twin of TC-SDLP. It groups one or more
Space Packets (or idle data) into a fixed-length frame, stamps it
with a master-channel and virtual-channel frame counter, and (when
SDLS is enabled) authenticates / encrypts the contents.

## Primary header (6 bytes)

Source: `nos3/components/cryptolib/include/crypto_structs.h:500-521`
(bit-precise) and `nos3/fsw/apps/io_lib/fsw/public_inc/tmtf.h:65-71`
(opaque).

```
     bit  7   6   5   4   3   2   1   0
        +---+---+---+---+---+---+---+---+
Byte 0: | TFVN  |          SCID         |   Id[0]
        +---+---+---+---+---+---+---+---+
Byte 1: |   SCID    |   VCID    |OCFf|  ?|   Id[1]
        +---+---+---+---+---+---+---+---+
Byte 2: |   Master Channel Frame Count  |   McFrameCount
        +---+---+---+---+---+---+---+---+
Byte 3: |   Virtual Channel Frame Count |   VcFrameCount
        +---+---+---+---+---+---+---+---+
Byte 4: |TFSH|SF |POF|SLID| FHP (high)  |   DataFieldStatus[0]
        +---+---+---+---+---+---+---+---+
Byte 5: |     First Header Pointer (lo) |   DataFieldStatus[1]
        +---+---+---+---+---+---+---+---+
```

| Field | Bits | Meaning |
|---|---|---|
| TFVN | byte 0 bits 7:6 | Transfer Frame Version Number, 00 for TM |
| SCID | byte 0 bits 5:0 + byte 1 bits 7:6 | 10-bit Spacecraft ID |
| VCID | byte 1 bits 5:3 | 3-bit Virtual Channel ID, 0..7 |
| OCFf | byte 1 bit 2 | 1 = an OCF trailer is present |
| MCFC | byte 2 | 8-bit master-channel rolling frame counter |
| VCFC | byte 3 | 8-bit per-VC rolling frame counter |
| TFSH | byte 4 bit 7 | Secondary header present in this frame |
| SF | byte 4 bit 6 | Sync flag, 0 = packets, 1 = VCA SDU |
| POF | byte 4 bit 5 | Packet Order Flag, must be 0 when SF=0 |
| SLID | byte 4 bits 4:3 | Segment Length ID, must be 11 when SF=0 |
| FHP | byte 4 bits 2:0 + byte 5 | 11-bit First Header Pointer |

The First Header Pointer points at the offset of the first complete
Space Packet inside the frame data field. Two reserved values:
`0x7FE` ("frame contains only idle data") and `0x7FF` ("no first
header in this frame, the data is mid-packet"). See
`tmtf.h:61-62`.

Optional fields after the primary header, in order:

1. Variable-length **secondary header** (1..63 bytes). First byte is
   `0x00..0x3F` (length-minus-one) plus a top-bit version flag.
2. The data field (Space Packets, or idle data, or VCA SDU).
3. **Operational Control Field** (4 bytes), present iff OCFf=1.
   Carries either a CLCW (see COP-1) or a Frame Security Report.
4. **FECF** (2-byte CRC-16), present iff the physical channel says so.

The CRC polynomial is `0x11021` and the init register is `0xFFFF`,
defined at `tmtf.h:47-48`.

Where the framing happens: `nos3/fsw/apps/to/fsw/examples/udp_tf/to_custom.c:343`
hands the outbound buffer to `Crypto_TM_ApplySecurity()` after the
Space Packets have been laid into the frame data field.

# Layer 2.5: SDLS - Space Data Link Security Protocol (CCSDS 355.0-B)

Status: **EXAMPLE-ONLY, via CryptoLib.** Wired up only in the
`udp_tf` example builds of CI and TO. Library lives at
`nos3/components/cryptolib/`.

## Why it exists

TC and TM frames are plaintext with at best a CRC. SDLS adds an
authenticated-encryption layer between the frame primary header and
the data field, so that a recipient can verify the frame came from a
key-holder, was not modified, and (if the chosen Security Association
is in encrypt-and-authenticate mode) was not read in flight.

## TC Security Header

Source: `crypto_structs.h:362-372`.

```
+---------+---------+---------+---------+
|  Seg    |   SPI   |        IV         |
| Hdr (n) |  (2B)   |     (variable)    |
+---------+---------+---------+---------+
|  IV-len |   SN    |  SN-len |   Pad   |
|  (1B)   |  (var)  |  (1B)   |  (var)  |
+---------+---------+---------+---------+
| Pad-len |
|  (1B)   |
+---------+
```

| Field | Size | Meaning |
|---|---|---|
| Segment Header | `TC_SH_SIZE` | Optional segment-header passthrough |
| SPI | 2 bytes | Security Parameter Index, names the Security Association |
| IV | `IV_SIZE` | Initialization Vector for the AEAD cipher |
| iv_field_len | 1 byte | How many of the IV bytes are actually used |
| SN | `SN_SIZE` | Anti-replay sequence number |
| sn_field_len | 1 byte | Used SN bytes |
| Pad | `PAD_SIZE` | Padding for block ciphers |
| pad_field_len | 1 byte | Used pad bytes |

## TC Security Trailer

Source: `crypto_structs.h:378-383`.

```
+----------+----------+----------+
|   MAC    | mac-len  |   FECF   |
|  (var)   |  (1B)    |  (2B)    |
+----------+----------+----------+
```

The FECF here replaces the optional TC-frame FECF; it is moved
into the SDLS scope so the MAC and the CRC can be computed in a
single pass.

## TM Security Header and Trailer

TM is simpler because TM frames have no segment header.

Header (`crypto_structs.h:527-531`): `SPI` (2B) + `IV` (var).

Trailer (`crypto_structs.h:537-542`): `MAC` (var) + `OCF` (4B) +
`FECF` (2B). The OCF inside the trailer is the same CLCW or FSR
discussed in TM-SDLP and COP-1.

## GVCID, the security key scope

A Security Association is keyed on a Global Virtual Channel ID:
`TFVN | SCID | VCID | MAPID`. This is the tuple that lets one
spacecraft hold different keys for different downlink streams. See
`crypto_structs.h:54-60` (`crypto_gvcid_t`) and
`crypto_structs.h:67-101` (`SecurityAssociation_t`).

Wire-up: `Crypto_TC_ProcessSecurity()` at `ci/fsw/examples/udp_tf/ci_custom.c:345`
on the inbound side, `Crypto_TM_ApplySecurity()` at
`to/fsw/examples/udp_tf/to_custom.c:343` on the outbound side.

# Layer 2 control: COP-1 (CCSDS 232.1-B)

Status: **EXAMPLE-ONLY (multi_tf), disabled in udp_tf.**

COP-1 is the ARQ protocol that runs over a TC link. The spacecraft
runs the receiver half (FARM-1) and the ground runs the sender half
(FOP-1). The spacecraft tells the ground "I expect frame N(R) next"
in a 4-byte CLCW that piggybacks on a TM frame's OCF. The header
file is `nos3/fsw/apps/io_lib/fsw/public_inc/cop1.h` and the
sliding-window width is fixed at 126 frames (`cop1.h:38`).

## CLCW format (4 bytes)

The opaque struct in io_lib (`cop1.h:45-51`) breaks the 4 bytes into
`Status`, `Channel`, `Flags`, and `Report`. The bit-precise version
in CryptoLib (`crypto_structs.h:457-473`) is:

```
     bit  7   6   5   4   3   2   1   0
        +---+---+---+---+---+---+---+---+
Byte 0: |CWT| CVN  | Status Field |COPiE|   "Status"
        +---+---+---+---+---+---+---+---+
Byte 1: |          VCID         | spare0|   "Channel"
        +---+---+---+---+---+---+---+---+
Byte 2: |NRf|NBL|LO |WT |RT | FARM-B(2) |   "Flags"
        +---+---+---+---+---+---+---+---+
Byte 3: |             N(R)              |   "Report"
        +---+---+---+---+---+---+---+---+
```

| Field | Meaning |
|---|---|
| CWT | Control Word Type, 0 = CLCW, 1 = FSR |
| CVN | CLCW Version Number, must be 00 |
| Status Field | Reserved, mission-specific |
| COP-in-Effect | 01 = COP-1 in use |
| VCID | The VC this CLCW reports on |
| NRf | No-RF-Available flag |
| NBL | No-Bit-Lock flag |
| LO | Lockout flag (FARM-1 has hard-rejected) |
| WT | Wait flag (receiver buffer full) |
| RT | Retransmit flag (the ground should resend) |
| FARM-B | 2-bit retransmit counter |
| N(R) | The next frame sequence number FARM-1 wants to see |

Wire-up: `nos3/fsw/apps/ci/fsw/examples/multi_tf/ci_custom.c:153-157`
calls `COP1_InitClcw()` and then `COP1_ProcessFrame()` per inbound TC
frame. The `udp_tf` example deliberately bypasses COP-1 because it
runs Type-B (bypass) frames where ARQ is not needed.

## What goes in the OCF when SDLS is on

Same 4 bytes, but the Control Word Type bit selects between CLCW
(`CWT=0`) and Frame Security Report (`CWT=1`). The FSR layout is
at `crypto_structs.h:480-490`. FSR carries SDLS error counters and
the last SPI used.

# Side path: CFDP - CCSDS File Delivery Protocol (CCSDS 727.0-B)

Status: **ACTIVE.** The cFS CF app at `nos3/fsw/apps/cf/` is enabled
in `nos3/cfg/spacecraft/sc-mission-config.xml`. Default config in
`nos3/cfg/nos3_defs/tables/cf_def_config.c` enables Class-2
(acknowledged) transactions.

## Why this repo uses it

CFDP is the standard way to move whole files between the spacecraft
and ground. It rides on top of Space Packets (carried by the SB
internally and by the regular CI/TO link externally), so it is *not*
an alternative to the Space Packet protocol; it is an application
layered on top of it.

## PDU header (variable length, minimum 4 bytes fixed plus 3+ bytes variable)

Source: `nos3/fsw/apps/cf/fsw/src/cf_cfdp_pdu.h:137-144`. Bit
positions in the flags byte come from `nos3/fsw/apps/cf/fsw/src/cf_codec.c:70-75`.

```
+-------------------+
| flags byte (1B)   |
+-------------------+
| length      (2B)  |   total PDU length
+-------------------+
| eid_tsn_lengths(1B)|  packed lengths-minus-one
+-------------------+
|  Source Entity ID  |  variable, 1..8 bytes
+-------------------+
| Transaction Seq # |  variable, 1..8 bytes
+-------------------+
|  Dest Entity ID   |  variable, 1..8 bytes
+-------------------+
|  ... PDU body ... |
+-------------------+
```

Flags byte (`cf_codec.c:70-75`):

```
     bit  7   6   5   4   3   2   1   0
        +---+---+---+---+---+---+---+---+
        |   Version | T | D | M | C | L |
        +---+---+---+---+---+---+---+---+
```

| Field | Bits | Meaning |
|---|---|---|
| Version | 7:5 | CFDP version, 0 for v1, 1 for v2 |
| T (Type) | 4 | 0 = File Directive PDU, 1 = File Data PDU |
| D (Direction) | 3 | 0 = toward receiver, 1 = toward sender |
| M (Mode) | 2 | 0 = acknowledged (Class-2), 1 = unacknowledged (Class-1) |
| C (CRC) | 1 | 0 = no CRC, 1 = CRC appended |
| L (Large) | 0 | 0 = 32-bit file offsets, 1 = 64-bit |

`eid_tsn_lengths` byte (`cf_codec.c:84-85`):

```
     bit  7   6   5   4   3   2   1   0
        +---+---+---+---+---+---+---+---+
        | r | EID len-1 | r |TSN len -1 |
        +---+---+---+---+---+---+---+---+
```

So if the byte is `0x33`, the entity-ID fields are 4 bytes each and
the transaction sequence number is 4 bytes.

Two PDU body shapes ride this header:

- **File Directive PDU.** First body byte is the directive code
  (EOF / FIN / ACK / METADATA / NAK / PROMPT / KEEPALIVE). Layouts
  defined in `cf_cfdp_pdu.h`.
- **File Data PDU.** Body is `offset` (32 or 64 bits depending on
  the L flag) plus the file data bytes.

## Where it shows up at the GSW boundary

YAMCS native CFDP service: `nos3/gsw/yamcs/nos3/src/main/yamcs/etc/yamcs.nos3.yaml:29-61`,
ground entity ID 21, spacecraft entity ID 24, max PDU 484 bytes.

COSMOS targets: `nos3/gsw/cosmos/config/system/system.txt:13-19`
declares `CFDP`, `PDU`, and `CFDP_TEST` targets with PDU_TX / PDU_RX
streams.

# Side path: DTN Bundle Protocol (RFC 5050 / CCSDS 734.2-B)

Status: **ACTIVE only when SBN is configured with the DTN module.**
This is a parallel path, not part of the regular ground link.

## Why this repo carries it

The Software Bus Network (SBN) is a cFS app that bridges Software
Buses across nodes. It supports several inter-node transports as
plug-in modules; DTN is one of them, with ION as the backing
implementation. The use case is a multi-spacecraft scenario where
you do not have continuous connectivity between SBs.

## Where it lives

Module: `nos3/fsw/apps/sbn/modules/protocol/dtn/fsw/src/sbn_dtn_if.c`.

Key calls (lines 48-68):

- `bp_attach()` - attach to the local ION daemon.
- `ionStartAttendant()` - start the SDR-watching thread.
- `bp_open(NetData->EIN, &NetData->SAP)` - open a BP endpoint
  identified by an EIN like `ipn:1.1`.
- ION's SDR (Stable Data Repository) holds bundles in flight.

## Why we do not byte-table the BP header here

Unlike the fixed-layout headers above, BP bundles are constructed
from **blocks** with self-describing-numeric (SDNV) length prefixes
and CBOR encoding in BPv7. Decoding them by hand is not feasible from
a single table; the relevant references are RFC 5050 (BPv6) and RFC
9171 (BPv7) plus CCSDS 734.2-B. The SBN DTN module never exposes the
raw bundle to cFS apps - it hands cFS-side `SBN_NetMsg_t` payloads
to ION and lets ION do the framing.

# Implicit: CCSDS Time Code Format (CCSDS 301.0-B)

Status: **IMPLICIT.** cFS embeds a 4-byte seconds + 2-byte
subseconds stamp in every telemetry secondary header. This is the
same shape as a CCSDS Unsegmented Time Code (CUC) with no P-field
preamble, but it is not strictly a CUC: there is no P-field
(Preamble Field) and the epoch is whatever the cFE TIME service is
configured for (TAI by default in this build).

In other words: byte-for-byte the cFS timestamp will look like a
CUC with 4-octet coarse and 2-octet fine resolution, but the doc
that defines it is the cFS message format, not CCSDS 301.0-B.

If you ever need to feed these timestamps to a CCSDS-301-aware
ground tool, prepend a one-byte P-field of the form
`0bP1 1 1 1 0 0 0 0 0` (extension bit, time code ID `001` for CUC,
4 octets coarse, 2 octets fine) and you are emitting a strictly
compliant CUC.

# What is NOT in this FSW

- **AOS Space Data Link Protocol (CCSDS 732.0-B).** Only library
  symbols inside CryptoLib reference AOS (`crypto_structs.h:43`).
  No AOS framing path exists in the FSW. Status: *LIBRARY-ONLY*.
- **Encapsulation Packet Protocol (CCSDS 133.1-B).** Not present.
  The "idle packet" mechanism uses a regular Space Packet with a
  reserved APID, not the encapsulation protocol.
- **Asynchronous Message Service (CCSDS 735.1-B).** Not present.
- **ECSS PUS.** A `ECSS_PUS_t` struct exists in
  `crypto_structs.h:406-415` for compatibility with early CryptoLib
  test traffic. Not used by the cFS apps.

# GSW transport summary

| Layer above UDP | Default `udp` build | `udp_tf` example build | YAMCS / COSMOS |
|---|---|---|---|
| Outbound TM | raw Space Packets via `to_lab` `OS_SocketSendTo`, `to_lab_app.c:601` | TM Frame + optional SDLS, `to_custom.c:343` | YAMCS reads on UDP 6011 with `CfsPacketPreprocessor` (`yamcs.nos3.yaml:63-99`) |
| Inbound TC | raw Space Packets to `ci_lab` UDP port 5012, `ci_lab_app.h:40` and `ci_lab_app.c:153` | TC Frame + optional SDLS + optional COP-1, `ci_custom.c:345` | YAMCS sends on UDP 6010 via cryptolib host with `CfsCommandPostprocessor` |
| Truth (42 dynamics) | raw 42 truth packets on UDP 5111 | same | YAMCS truth42 link |
| File transfer | CFDP PDUs ride inside Space Packets, on top of either path | same | YAMCS native CFDP (`yamcs.nos3.yaml:29-61`), COSMOS CFDP target |

# How to read a packet on the wire (worked example)

Capture on `localhost:6011`:

```
hexdump -C   #   from a `nc -lu 6011 | hexdump -C` listener,
              #   while the sim is running
```

You will see entries like:

```
08 70 C0 0F 00 25 ...
```

Decode:

1. `08 70` - StreamId. `0x08` = `0000 1000` so Ver=0, Type=0
   (telemetry), SHF=1, APID-hi=000. `0x70` is APID-lo. Combined APID
   = `0x070`. The `0x0070` MID is GPS housekeeping in the default
   `sc-mission-config.xml`.
2. `C0 0F` - Sequence. SegFl=11b (unsegmented), SeqCount=0x000F=15.
   This is the 16th GPS HK packet since startup.
3. `00 25` - Length. 0x25=37, total size = 37+7 = 44 bytes.
4. The next 6 bytes are the telemetry secondary header: 4 bytes
   seconds, 2 bytes subseconds, big-endian.
5. The remaining 32 bytes are the GPS HK payload, whose layout is
   in the GPS component's `gps_msg.h`.

If anything in this chain fails, that is where to look first. APID
not found in the cFS app inventory? `nos3/cfg/build/...` was probably
generated from a different spacecraft profile than you think. Length
mismatch? You forgot the "-7" rule.

# References (in this repo)

- `nos3/fsw/cfe/modules/msg/fsw/inc/ccsds_hdr.h` - Space Packet
  primary and extended header structs.
- `nos3/fsw/cfe/modules/msg/option_inc/default_cfe_msg_sechdr.h` -
  cFS command and telemetry secondary headers.
- `nos3/fsw/cfe/modules/msg/fsw/src/cfe_msg_ccsdspri.c` - all
  primary-header field accessors and the `-7` size offset.
- `nos3/fsw/cfe/modules/msg/fsw/src/cfe_msg_msgid_v2.c` - 16-bit
  Message ID encoding for the V2 build.
- `nos3/fsw/cfe/modules/msg/fsw/src/cfe_msg_sechdr_checksum.c` -
  command checksum.
- `nos3/fsw/apps/io_lib/fsw/public_inc/tctf.h` - TC Transfer Frame
  helpers.
- `nos3/fsw/apps/io_lib/fsw/public_inc/tmtf.h` - TM Transfer Frame
  helpers.
- `nos3/fsw/apps/io_lib/fsw/public_inc/cop1.h` - COP-1 / CLCW.
- `nos3/components/cryptolib/include/crypto_structs.h` - bit-precise
  TC, TM, SDLS, CLCW, and FSR layouts.
- `nos3/fsw/apps/cf/fsw/src/cf_cfdp_pdu.h` - CFDP PDU header struct.
- `nos3/fsw/apps/cf/fsw/src/cf_codec.c` - bit positions of the
  CFDP flags byte and entity-ID-length byte.
- `nos3/fsw/apps/sbn/modules/protocol/dtn/fsw/src/sbn_dtn_if.c` -
  DTN / ION bridge.
- `nos3/fsw/apps/to_lab/fsw/src/to_lab_app.c` - default TM downlink.
- `nos3/fsw/apps/ci_lab/fsw/src/ci_lab_app.c` and
  `nos3/fsw/apps/ci_lab/fsw/inc/ci_lab_app.h` - default TC uplink.
- `nos3/gsw/yamcs/nos3/src/main/yamcs/etc/yamcs.nos3.yaml` - GSW
  port map and CFDP entity IDs.

# References (CCSDS Blue Books)

These are the standards the code claims compliance with. The repo
only ships the headers, not the Blue Books themselves; the books
are public on the CCSDS website by number.

- CCSDS 133.0-B - Space Packet Protocol
- CCSDS 132.0-B - TM Space Data Link Protocol
- CCSDS 232.0-B - TC Space Data Link Protocol
- CCSDS 232.1-B - Communications Operation Procedure-1 (COP-1)
- CCSDS 355.0-B - Space Data Link Security Protocol (SDLS)
- CCSDS 727.0-B - CCSDS File Delivery Protocol (CFDP)
- CCSDS 734.2-B - Bundle Protocol Specification (DTN)
- CCSDS 301.0-B - Time Code Formats
