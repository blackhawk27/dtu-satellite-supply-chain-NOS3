# generic_tt_c - DTU custom

- **Source:** `nos3/components/generic_tt_c/fsw/`, `nos3/components/generic_tt_c/sim/`
- **Loaded by:** `cfe_es_startup.scr` line 16 (priority 76, stack 32768)
- **Pipe depth:** 10
- **MIDs:** topic IDs `0x50` (CMD/HK), `0x51` (REQ_HK / Device TLM). Resolved: CMD `0x1950`, HK TLM `0x0950`, Device TLM `0x0951`
- **IPC port:** **4286** (42 dynamics -> sim) with **tight prefix** `SC[0].GPS[0].PosW` + `SC[0].GPS[0].VelW` (count 2). See `nos3/cfg/InOut/Inp_IPC.txt`.

## Role

TT&C radio sim + FSW app. Maintains link-state, downlink-rate, and a status code reflecting LOS/AOS state.

## Load-bearing invariants

- Tight 42 IPC prefix on port 4286 (PosW + VelW). Widening floods the socket and crashes 42.
- Bracket-stripping in `generic_tt_c_data_point.cpp` (same `do_parsing` sanitiser as GNSS), plus an empty-string guard added in commit `c6c79140` (2026-05-03).

## ELK fields

`ttc_status_code`, `ttc_link_state`, `ttc_downlink_rate_bps`. Dashboard panel "TT&C Downlink Status" added in commit `6c6b2029`.

## Silent-failure modes

- An empty parsed line bypasses the guard prior to commit `c6c79140`, leaving the FSW with stale link state. Watch for a flat `ttc_link_state` that never transitions.
- Same broad-prefix 42 EAGAIN crash as GNSS if the IPC table is widened.

## Attack-surface notes

- Link-state spoofing: an attacker who can publish arbitrary `ttc_status_code` makes the dashboard report nominal link during a real outage (or vice-versa).
- The CCSDS framing layer (TT&C carries the radio bits) is the path used by `ci`/`to`; full SDLS auth is enforced there, not at this sim.
