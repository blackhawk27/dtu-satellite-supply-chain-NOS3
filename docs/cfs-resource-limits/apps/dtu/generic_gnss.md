# generic_gnss - DTU custom

- **Source:** `nos3/components/generic_gnss/fsw/`, `nos3/components/generic_gnss/sim/`
- **Loaded by:** `cfe_es_startup.scr` line 17 (priority 77, stack 32768)
- **Pipe depth:** 10
- **MIDs:** topic IDs `0x52` (CMD/HK), `0x53` (REQ_HK / Device TLM), `0x54` (Sat-Hello TLM). Resolved: CMD `0x1952`, HK TLM `0x0952`, Device TLM `0x0953`, Sat-Hello TLM `0x0954`
- **IPC port:** **4287** (42 dynamics -> sim) with **tight prefix** `SC[0].GPS[0].PosW` (count 1). See `nos3/cfg/InOut/Inp_IPC.txt`.

## Role

GNSS receiver app + sim. The sim accepts position from 42 over a TCP socket, emulates a UART NMEA-like stream to the FSW component, and the FSW app re-publishes parsed lat/lon/alt + ECEF on SB. The 2026-05-03 work added the GS Hello-Flow loop: the cosmos-side `gnss_gs_responder_task.rb` polls HK, dispatches a `BEACON_PING_CC`, and waits up to `ECHO_TIMEOUT_S = 5.0` for the reply.

## Load-bearing invariants

- Tight 42 IPC prefix on port 4287 (`PosW` only). Widening to `SC` or `Orb` floods the socket and crashes 42.
- Bracket-stripping in `generic_gnss_data_point.cpp` (the comma sanitiser at the start of `do_parsing` includes `[`/`]`). Without it, vector values like `[x y z]` from 42 fail `stringstream >> double` and the data point is `INVALID` forever.

## ELK fields

`gps_lat`, `gps_lon`, `gps_alt`, `gps_ecef_x/y/z`, `gps_abs_time`, plus the post-2026-05-03 additions: `gnss_lat`, `gnss_lon`, `in_denmark_box`, `last_ping_seq`, `last_ping_time`, `ping_count`.

## Silent-failure modes

- If 42 emits nothing on port 4287 (e.g. the orbit propagator stalled), the sim quietly holds the last value; the FSW HK looks healthy until a freshness tile flips.
- Bracket-strip regression makes every parsed line fail; HK still emits but lat/lon stay zero.

## Attack-surface notes

- Position spoofing: the 42-side socket has no auth. An attacker on the host can inject arbitrary position; the FSW believes it is over Denmark / Antarctica / etc.
- The GS responder's `ECHO_TIMEOUT_S = 5.0` was tuned to the 90th percentile of observed echo latency; reducing it without re-measurement turns transient delay into false-alarm timeouts.
