# 06. Mission Doc Surgical Inserts (Applied)

This document is the index of inserts that were applied to `docs/missions/NOS3-DTU-EO1-mission-implementation.md` (the canonical mission spec) so the doc stays a single-source-of-truth for the testbed as it stands. Inserts 1-4 landed in the slice 1 baseline window. Inserts 5-9 landed on 2026-05-03 alongside the GS Hello-Flow / GNSS dashboard work and the cfs-resource-limits split.

The mission doc was sanitised in Slice 1's audit (writing rules: AI-tooling references rewritten, em-dashes removed; see [`_audit/audit-2026-05-01.md`](_audit/audit-2026-05-01.md)). Every insert below is content additive on top of that clean baseline.

## 1. ELK pipeline cross-reference

**Where**: top of section 8 ("Logstash Telemetry Parsers"). One block pointing to `docs/thesis/05-elk/` reading order: `00-pipeline.md` -> `01-logstash-filters.md` -> `02-elasticsearch-mappings.md` -> `03-kibana-dashboards.md` -> `04-build-and-load.md`.

## 2. Per-component deep-dive cross-reference

**Where**: top of "Component Walkthroughs `[L3]`". One block pointing each component subsection at its matching `docs/thesis/04-apps/<name>.md` deep-dive. The summary level in the mission doc remains the canonical operational view; the thesis deep-dive is the security / supply-chain framing.

## 3. IPC port-map updates (rows 8, 16, 19, 20)

**Where**: section "IPC Port Map `[L2]`".

- Row 19 added: TT&C IPC at `fortytwo:4286`, tight prefix `SC[0].GPS[0].PosW` + `SC[0].GPS[0].VelW` (count = 2). Marked load-bearing.
- Row 20 added: GNSS IPC at `fortytwo:4287`, tight prefix `SC[0].GPS[0].PosW` (count = 1). Marked load-bearing.
- Row 8 reworded: Thruster IPC moved from `RX` to `OFF`; load-bearing wording added with the same backpressure pathology as row 16.
- Row 16 reworded: Star Tracker IPC `OFF` is load-bearing; rewording references both `debug/journal.md` and the project root notes file's "DO NOT REVERT" register.
- Introductory sentence updated from "18 sockets" to "20 sockets".

## 4. Reproducibility appendix link

**Where**: end of "Reading Guide" section. Bullet pointing at `docs/thesis/01-reproducibility.md` for readers who only need clone-to-launch.

## 5. Telemetry Mirror Path subsection (2026-05-03)

**Where**: new subsection 2.4.1 inserted after the "Cross-boundary data movement" five-bullet list.

Documents:
- The COSMOS netns isolation problem (UDP 5013 listener inside cosmos container).
- `nos3/scripts/god_view_capture.py` mirror via `NOS3_TLM_MIRROR_DEST`.
- `nos3/gsw/cosmos/tools/CmdTlmServerHeadless` replacing `xvfb-run ruby CmdTlmServer`.
- `ci_launch.sh -p 5113:5013/udp` port map.

## 6. Step-by-Step inserts: Bracket-strip, GS Hello-Flow, CmdTlmServerHeadless (2026-05-03)

**Where**: section 6 ("Step-by-Step: What Was Changed and Why"), three new subsections appended after the existing nine.

- **6.10 Bracket-stripping UART parsers**: covers the comma-and-bracket sanitiser in `generic_gnss_data_point.cpp` and `generic_tt_c_data_point.cpp`, and the empty-string guard added in commit `c6c79140`.
- **6.11 GS Hello-Flow Responder + ECHO_TIMEOUT_S=5.0**: covers `gnss_gs_responder_task.rb`, the `send_time` capture pattern, the empirical p90 reasoning (2.93 s), and the elapsed_ms log emission.
- **6.12 CmdTlmServerHeadless + Mirror Socket plumbing**: ties together the COSMOS-side and host-side halves with verification numbers (cmd_count 1 -> 39, 11 GS_PING events, 39 round-trip-closed log lines, 68 docs in Denmark box, 31 in SCIENCE mode).

## 7. Component walkthroughs: GNSS, TT&C, Beacon (2026-05-03)

**Where**: "Component Walkthroughs `[L3]`" section. Three new subsections appended after the existing 14.

- **GNSS** (`generic_gnss`): covers the bracket-stripper, port 4287 tight prefix, MIDs (CMD `0x1952`, HK TLM `0x0952`, Device TLM `0x0953`, Sat-Hello TLM `0x0954`), GS Hello-Flow integration, and the seven new ELK fields.
- **TT&C** (`generic_tt_c`): covers the bracket + empty-string guard, port 4286 tight prefix, MIDs (CMD `0x1950`, HK TLM `0x0950`, Device TLM `0x0951`), and the `ttc_status_code` / `ttc_link_state` / `ttc_downlink_rate_bps` fields.
- **Beacon** (`beacon_app`): covers `BEACON_PING_CC`, `pingcounter` / `cmdcounter` / `errcounter`, and the Hello-Flow ack-source role.

## 8. V7 verification procedure (2026-05-03)

**Where**: "Verification Procedures" section. New procedure renumbers the cross-check from V7 to V8 and inserts the new V7.

- **V7. Verify the GS Hello-Flow round-trip**: a curl + jq one-liner that queries Kibana for `ping_count > 0`, checks `last_ping_seq` advancing, and validates the AND-gate panel state. Includes the typical fault chain (CmdTlmServerHeadless -> NOS3_TLM_MIRROR_DEST -> beacon_app loaded) for triage when the check fails.
- **V8. Cross-check** (formerly V7): expanded with three new symbols to grep for: `ECHO_TIMEOUT_S`, `NOS3_TLM_MIRROR_DEST`, `CmdTlmServerHeadless`.

## 9. Glossary, Known Gaps, Quick Reference updates (2026-05-03)

- **Glossary** got 9 new entries: GNSS GS Responder, Hello-Flow, Mirror Socket, CmdTlmServerHeadless, ECHO_TIMEOUT_S, Freshness Tile, AND-gate Panel, in_denmark_box, in_science_mode.
- **L1 Mission in One Page** got a "Closed-loop liveness check" paragraph and an updated one-line summary.
- **Known Gaps** got a "Closed by post-thesis commits" subsection listing six items closed by the 16 commits between `2b3af0fc` and `7afa0107`. The "HS -> SC integration" gap collapsed into AP4. The "Kibana pre-built dashboards" gap closed via commits `0dcccd58` and `7afa0107`. A new gap was added: `lc_def_adt.c` / `lc_def_wdt.c` working-tree dirty as of 2026-05-03.
- **Quick Reference** got 10 new file-location rows: GS responder, CmdTlmServerHeadless, god_view + mirror, ci_launch, index template, refresh script, dashboards NDJSON, TT&C and GNSS UART parsers, and the new cfs-resource-limits tree.

## 10. Cross-references

- The slice 1 audit baseline: [`_audit/audit-2026-05-01.md`](_audit/audit-2026-05-01.md).
- The thesis-side reproducibility doc: [`01-reproducibility.md`](01-reproducibility.md).
- The thesis-side ELK walkthrough: [`05-elk/`](05-elk/).
- The thesis-side per-app deep-dives: [`04-apps/`](04-apps/).
- The thesis-side communication-layer documents: [`03-communication/`](03-communication/).
- The customizations-vs-upstream listing: [`07-customizations-vs-upstream.md`](07-customizations-vs-upstream.md).
- The cfs-resource-limits split (post-2026-05-03): `docs/cfs-resource-limits/layers/` and `docs/cfs-resource-limits/apps/`.
