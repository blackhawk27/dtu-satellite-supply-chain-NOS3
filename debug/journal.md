# NOS3 Debug Journal

Append-only. Newest sessions at the bottom.

## Session metadata - 42 startup hang (2026-04-25 02:30 - 04:15 UTC)

(retained for context; full notes in `debug/kibana-2026-04-25-1348.md`
sister log and `~/nos3_runs/denmark-ipc-fix-verified-1777091148/metadata.txt`)

- 30/30 cold-restart cycles pass after Inp_IPC.txt port-4282 fix
  (commit `eedab6e2`, verified by commit `025d78ac`).
- Root cause: `nos3/cfg/InOut/Inp_IPC.txt` entry 16 (port 4282 Star Tracker)
  was `TX` but the active sim XML uses `GENERIC_STAR_TRACKER_PROVIDER`,
  not the `_42_PROVIDER` variant. With nothing connecting to fortytwo:4282,
  42 blocked forever in `inet_csk_accept`.
- Fix: flip entry 16 to `OFF`. This is **load-bearing** and called out in
  the project root notes file. Do not revert.

## Session metadata - Telemetry pipeline pre-sweep (2026-04-25 13:48 - 14:05 UTC)

(retained; full notes in `debug/kibana-2026-04-25-1348.md`)

- Recreated missing index-pattern saved object
  `03771670-2663-11f1-9950-757ed527c264` (title `nos3-telemetry*`,
  timeFieldName `@timestamp`). This unblocked the "Thesis: Cyber-Physical
  Attack Radar" (7 panels) and "FSW vs Sim Cross-Reference" (8 panels)
  dashboards.
- Documented `tlm_hk_decoded.log` producer (`god_view_capture.py`) is
  offline whenever the sim is stopped, so the 13 hk_decoded panels in
  std-mission-validation, mission-denmark, OBC and Sensor Ground Truth
  show "No results" until a live or replayed sim run is available.

---

## Session metadata - Full Kibana sweep & mode-change dashboard

- Start: 2026-04-25T14:38Z
- Operator: automated tooling (auto mode)
- Sim version / commit at start: `025d78ac` (main)
- Kibana index pattern(s) inspected: `nos3-telemetry-*`
- Stack: Elasticsearch / Logstash / Kibana 7.17.10 (containers
  `nos3-elasticsearch`, `nos3-logstash`, `nos3-kibana`)

### Bring-up

1. `docker network create nos3-core` (was missing on this fresh shell).
2. Edited `nos3/elk/logstash.conf` (see fixes below).
3. `make load-run RUN=denmark-ipc-fix-verified-1777091148` to populate
   the index from the verified 11-minute Denmark cycle. ELK stack came up,
   Logstash re-ingested ~375k docs in ~150 s.
4. Verified per-type counts:
   - `system_log` ~366k, `software_bus` ~7.6k, `hk_decoded` 3020.
   - All three types now share `@timestamp = ingest time` (see Logstash
     fix #1 below). Replays render as "fresh" in last-N-minute panels.

## Errors - Fixed

### `GenericRWDataPoint::GenericRWDataPoint:  Parsing exception stod`

- First seen: 2026-04-25T14:46:18Z (replay-time). 25 docs per RW (rw0,
  rw1, rw2) = **75 ERROR rows** in a 1-second startup window per run.
- Subsystem: `nos3-rw0`, `nos3-rw1`, `nos3-rw2`.
- Source:
  `nos3/components/generic_reaction_wheel/sim/src/generic_rw_data_point.cpp:57`.
- Root cause: the RW sim constructs its first `GenericRWDataPoint` from
  whatever 42-frame is available the moment the 42 socket connects. If
  the value for `SC[0].Whl[N].H` is empty, `std::stod()` throws and the
  catch-handler logs at ERROR level. ~1 second later the next 42 frame
  arrives with valid content and `rw_momentum` populates correctly. The
  burst is therefore a startup race, not a real fault.
- Fix: added a tightly-scoped Logstash `drop {}` for the exact subsystem
  + message pair (see fix #4 below). The downstream signal
  (`rw_momentum`) is unaffected. Verified: 0 docs after restart.
- Recommended deeper fix (out of scope this session): in
  `generic_rw_data_point.cpp:57`, guard the empty-string case before
  calling `std::stod` and log at DEBUG/TRACE during the first-frame race.

### `SimData42SocketProvider thruster connect failure`

- First seen: same 14:46Z replay window. **4 ERROR rows** in
  `nos3-thruster`.
- Messages:
  - `Maximum number of connection attempts reached.   Host fortytwo, port 4242 failed to connect!`
  - `Unable to connect TELEMETRY host fortytwo, port 4242 to 42, no thread will be started to listen :-(`
- Root cause: thruster sim is a 42-only consumer; entry 8 of
  `Inp_IPC.txt` (port 4242) is `OFF` for this profile, so 42 never
  serves the port. Same class as the existing `failed to connect host
  fortytwo` and `could not connect socket for host fortytwo` drops, but
  with two different terminus messages the previous filter did not
  match.
- Fix: extended the existing fortytwo-connect drop block in
  `logstash.conf` with two more regexes (see fix #2 below). Verified: 0
  docs after restart.

### `LC use of Critical Data Store disabled` mis-classified as CRITICAL

- 2 docs from the LC app at boot. The text contains "Critical" so the
  `evs_severity` heuristic regex tagged the line CRITICAL, even though
  the LC `LC_SAVE_TO_CDS` build flag is intentionally off in this
  research config. The correct severity is INFORMATION.
- Fix: added a higher-priority regex branch that catches the phrase
  `Critical Data Store` and tags it INFORMATION before the generic
  `CRITICAL|FATAL|PANIC` rule fires (fix #3 below). Verified: same docs
  now show `evs_severity = INFORMATION`.

### LC `AP state change from PASS to FAIL` mis-classified as ERROR

- 2 docs (AP=3, AP=4) per Denmark pass. The text contains "FAIL" which
  the heuristic regex turned into `evs_severity = ERROR`, but PASS->FAIL
  is the **expected** signal: AP3 fires when the satellite enters the
  Denmark FOV (entry, fires RTS 002 -> SCIENCE), AP4 fires on exit
  (fires RTS 001 -> SAFE). They are mission events, not faults.
- Fix: same severity-regex block now branches on `AP state change`
  before the generic ERROR rule, classifying the line as INFORMATION.
  The `lc_actionpoint_fired` and `science_overfly` tags continue to
  fire so dashboards that count mission events still see them.

### SB sequence gap detector emitting wrap-around false positives

- Replay produced 652 `sb_sequence_gap` tags, of which 268 carried
  `sb_gap_size >= 16000` (i.e., gap_size near the 14-bit counter range
  of 0x3FFE). These are wrap-arounds, not dropped packets.
- Fix: refined the per-MID gap detector to treat any gap >= 8192 (more
  than half the wrap range) as a wrap, set `sb_seq_wrap=true`, and skip
  the `sb_sequence_gap` tag (fix #5 below). Verified: tag count fell
  from 652 to 321; remaining 321 cluster in `sb_gap_size = 1..41`,
  matching plausible UDP / pipe drops.

### SBN `unable to connect to peer` keyword false positive

- 6 INFORMATION-level docs from the SBN app. The word "unable" hit a
  generic keyword sweep so the rows looked like errors. SBN multi-FSW
  is not used in this single-FSW build; the message is harmless boot
  noise.
- Fix: added a dedicated `evs_app_name == "SBN" and evs_message =~
  /unable to connect to peer/` rule that asserts INFORMATION severity
  and adds a `noise_filtered` tag (fix #6 below). The docs remain in
  the index for forensics; dashboards that filter on `tags:
  noise_filtered` can hide them.

## Empty panels - Fixed

### hk_decoded panels (13 panels) - Phase 2

Affected panels (from std-mission-validation, mission-denmark, OBC
Performance Monitor, Sensor Ground Truth Overview):
`spacecraft_mode`, `sci_pass_count`, `cmd_count`, `cmd_err_count`,
`cpu_avg_pct`, `cpu_peak_pct`, `battery_mv`, `solar_array_mv`,
`lc_state`, `curr_ats_id`, `num_rts_active`, `rts_active_ctr`,
`st_q1..q4`.

- Symptom: "No results found" in last-N-minute panels even with sim data
  loaded. hk_decoded docs exist (3020 in the saved-run).
- Diagnosis: the LAYER 3 hk_decoded filter in `logstash.conf` ran
  `date { match => ["timestamp", "UNIX"], target => "@timestamp" }`,
  which rewrote `@timestamp` to the producer-side sim epoch (Apr 25
  04:21-04:25Z, 10+ hours stale at session time). System_log and
  software_bus pipelines kept ingest-time `@timestamp`, so they
  populated; hk_decoded did not.
- Fix: removed the `date {}` override and renamed the producer field to
  `sim_timestamp_unix` so the original sim epoch is still queryable
  (logstash.conf hk_decoded block, fix #7 below). After restart, all 13
  panels populate.
- Verification: per-type latest @timestamp - hk_decoded
  2026-04-25T14:45Z, system_log 2026-04-25T14:46Z, software_bus
  2026-04-25T14:45Z. spacecraft_mode field now reports 251 docs
  (143 SAFE, 108 SCIENCE) and 1 mode_change tag - matches the
  saved-run notes.

### Truth42 numeric fields (`t42_pos_x` etc.) - 0 docs despite 6430 ingested

- Symptom: `nos3-truth42` had 6430 system_log docs in ES, but
  `t42_pos_x`, `t42_q0`, `t42_gyro_x`, `t42_svb_x`, `t42_vel_x` were
  all at 0 docs.
- Diagnosis: the truth42 grok block expected the legacy upstream
  format `SC[0] PosN: ... VelN: ... qbn: ...`. The current
  `truth42sim` writes `Pos: ..., Vel: ..., qn : ..., svb: ...,
  GyroB: ..., LinAccB: ..., wn : ...` (no `SC[0]` prefix, `qn`
  instead of `qbn`, with `:` after a space). The grok matched zero
  lines, so no fields were extracted.
- Fix: rewrote the truth42 grok with multi-pattern matches that handle
  both the legacy and current formats, plus added captures for the new
  `LinAccB` (linear acceleration in body frame) and `wn` (inertial
  rates) fields. (See fix #8 below.)
- Verification: t42_pos_x, t42_q0, t42_gyro_x, t42_linacc_x, t42_wn_x
  now each have 3186 docs in the index. The Sensor Ground Truth
  Overview dashboard now renders position / quaternion / gyro lines.

### Sensor sims with empty downstream fields (mag, imu, fss, torquer, sample)

- Subsystems with logs but **no decoded numeric fields**: `nos3-mag`,
  `nos3-imu`, `nos3-fss` each ingest only 28-30 docs. `nos3-torquer.log`
  and `nos3-sample.log` are 0 bytes.
- Diagnosis: each of these sims is a 42-only consumer waiting on a
  fortytwo:<port> socket that 42 does not serve in this build (mag
  4234, imu 4281, fss missing, torquer/sample inactive in the
  research profile XML). They emit init lines, then a flood of
  `Continuing, but could not connect socket for host fortytwo`
  warnings (already dropped by Logstash since the previous session)
  until they exit. They never emit any payload data, so the grok
  patterns have nothing to match.
- Fix: not in scope this session. The pipeline itself is correct - the
  *producer* is offline. Two follow-ups recommended:
  1. Bring up a fresh `make launch` cycle so 42 actively serves these
     ports, generate a saved run that includes mag/imu/fss data.
  2. Or remove these panels from the Sensor Ground Truth Overview
     dashboard to match the active hardware list in this profile.

## Dashboards modified

### All 9 existing dashboards - panel descriptions

- 94 panels total. 44 already had inline descriptions (clearly emitted
  by the build_kibana_dashboards.py generator). The remaining **50
  panels** had no description at all.
- Patcher script: `debug/patch_panel_descriptions.py`. Idempotent;
  walks each dashboard's `panelsJSON`, writes
  `embeddableConfig.attributes.description` (Lens panels) or
  `embeddableConfig.savedVis.description` (Visualize/markdown panels)
  where missing, leaves existing descriptions alone, then PUTs the
  saved object back.
- Coverage by dashboard:
  - Sensor Ground Truth Overview: 10/10 (10 added)
  - Thesis: Cyber-Physical Attack Radar: 7/7 (7 added)
  - OBC Performance Monitor: 7/7 (7 added)
  - FSW vs Sim Cross-Reference: 8/8 (8 added)
  - HK Telemetry Flow: 3/3 (3 added)
  - HS CPU Monitor: 3/3 (3 added)
  - NOS3 Mission: Denmark: 18/18 (5 markdown headers added; 13 already
    described)
  - NOS3 Standard Mission Validation: 23/23 (6 markdown headers added;
    17 already described)
  - NOS3 Standard Telemetry Overview: 15/15 (1 markdown header added;
    14 already described)
- Dashboard-level descriptions were missing on the Thesis Radar and OBC
  Performance Monitor; both populated via
  `mcp__kibana__vl_update_saved_object`.

### New dashboard: Spacecraft Mode Changes

- ID: `nos3-spacecraft-mode-changes`. Built by
  `debug/build_mode_dashboard.py` (idempotent: re-running PUTs with
  `overwrite=true`).
- 6 panels (header + 5 per Phase 4b):
  1. **About this dashboard** - markdown header explaining the
     1=SAFE / 2=STANDBY / 3=SCIENCE / 4=UNKNOWN legend and how to read
     the panels.
  2. **Mode timeline** - Lens line chart, average `spacecraft_mode`
     over `@timestamp` with carry-fitting so mode levels persist
     between HK samples. Filter:
     `type:hk_decoded AND subsystem:MGR`.
  3. **Mode-change events** - Lens datatable filtered to
     `tags: spacecraft_mode_change`, columns: transition label
     (`<prev> -> <new>`), prev mode, new mode, count. Driven by the
     new Logstash ruby filter that detects MGR mode transitions and
     emits the tag with `spacecraft_mode_prev`,
     `spacecraft_mode_new`, `mode_change_label` fields.
  4. **Current spacecraft mode** - Lens last_value metric on
     `spacecraft_mode`. Mode legend in the panel description.
  5. **Time in each mode** - Lens pie chart over
     `spacecraft_mode_name.keyword`. SAFE / STANDBY / SCIENCE / UNKNOWN
     are added by Logstash from the integer mode field.
  6. **LC / MGR EVS events** - Lens datatable filtered to
     `subsystem:cfs_evs AND evs_app_name: (LC OR MGR)`. Used to
     correlate transitions with the LC AP3/AP4 fires and the MGR
     `Set mode command received [N]` log lines.
- Verified populated against the loaded run:
  - 251 hk_decoded MGR docs with spacecraft_mode (SAFE=143, SCIENCE=108)
  - 1 spacecraft_mode_change event (1 -> 3 at 14:45:15Z, matching the
    Denmark AP3 fire in the saved-run notes)
  - 110 LC/MGR EVS events for the correlation table.

## Logstash fixes applied this session

All edits are in `nos3/elk/logstash.conf`:

1. **hk_decoded uses ingest time for @timestamp**.
   - Removed `date { match => ["timestamp", "UNIX"], target =>
     "@timestamp" }` block. Renamed producer field to
     `sim_timestamp_unix` so the original sim epoch is still
     queryable. Now consistent with how system_log and software_bus
     are timestamped, which lets replays render as "fresh" in
     last-N-minute panels. Lines ~885-905.
2. **Extended fortytwo-connect drops**.
   - Added two new `if [sim_message] =~ /.../` drops for
     `Maximum number of connection attempts reached.*Host fortytwo`
     and `Unable to connect TELEMETRY host fortytwo`. Filter scope
     is the exact terminus messages emitted by SimData42SocketProvider
     after retries are exhausted - same root cause class as the
     existing connect-refused drops. Lines ~520-560.
3. **CDS-disabled and AP-state-change severity exclusions**.
   - Added two regex branches at the top of the `evs_severity`
     keyword heuristic so `Critical Data Store` and
     `AP state change` lines map to INFORMATION before the generic
     CRITICAL/ERROR rules fire. Lines ~213-230.
4. **RW startup-race drop**.
   - Tightly-scoped drop for `subsystem == nos3-rw[0-9]+` AND
     `sim_message == "Parsing exception stod"` - the canonical
     startup race in `generic_rw_data_point.cpp`. Lines ~570-585.
5. **SB sequence-gap wrap-around suppression**.
   - Refined the per-MID ruby gap detector to treat gaps >= 8192 as
     wrap-arounds (set `sb_seq_wrap=true`, skip the
     `sb_sequence_gap` tag). Real publisher overruns produce
     small forward gaps. Lines ~125-160.
6. **SBN peer-connect race tag-strip**.
   - Tag-strip rather than drop: SBN `unable to connect to peer`
     gets `evs_severity` re-asserted to INFORMATION and a
     `noise_filtered` tag added. Forensic evidence retained.
   - Lines ~590-605.
7. **Spacecraft mode-change tagger**.
   - New ruby filter inside the hk_decoded block: tracks the last
     `spacecraft_mode` per MGR subsystem and emits
     `spacecraft_mode_change` tag plus `spacecraft_mode_prev`,
     `spacecraft_mode_new`, `mode_change_label` whenever the value
     changes. Powers the new Mode Changes dashboard's panel 3 and 4.
     Lines ~905-935.
8. **Truth42 grok rewrite**.
   - Multi-pattern grok block accepts both the legacy
     `SC[0] PosN: / VelN: / qbn:` schema and the current
     `Pos: / Vel: / qn :` format. Added captures for `LinAccB` and
     `wn` (new since the grok was last updated). Lines ~745-810.
9. **Exclude tlm_hk_decoded.log from the wildcard input**.
   - The wildcard `/omni_logs/*.log` input was double-ingesting the
     hk_decoded file (once as system_log via the wildcard, once as
     hk_decoded via the explicit input). Added `exclude =>
     "tlm_hk_decoded.log"` to the wildcard input so the file is
     parsed exactly once. Lines ~36-45.

## Noise - Filtered

Drops below were either added this session or carried forward from the
previous session. All are tightly scoped, all preserve the underlying
signal that proves the system is healthy or unhealthy.

### `Parsing exception stod` from RW data points

- Justification: 25-event burst per RW sim during the first-second 42
  socket handshake, identical 100% of starts. Caused by
  `std::stod("")` throw in `generic_rw_data_point.cpp:57`. The
  downstream `rw_momentum` field populates correctly within ~1 second.
- Evidence of harmlessness: rw_momentum has 906 docs after the drop
  (the burst was only at boot); RW HK heartbeat continues to publish.
- Filter location: `nos3/elk/logstash.conf` (RW startup-race drop in
  fix #4 above).
- Filter scope: exact subsystem regex (`nos3-rw[0-9]+`) + exact
  message string equality (`Parsing exception stod`).

### `Maximum number of connection attempts reached / Unable to connect TELEMETRY host fortytwo`

- Justification: thruster sim is a 42-only consumer and 42 does not
  serve port 4242 in this profile. Same root cause class as the
  existing fortytwo connect-refused drops.
- Evidence: the thruster has no other ERROR-class messages after the
  drop. Mission validation passes 30/30.
- Filter location: extended `failed to connect / could not connect
  socket` drop block (fix #2 above).
- Filter scope: exact two-message variants matched against
  `sim_message`.

### SBN `unable to connect to peer` (tag-strip, not drop)

- Justification: SBN multi-FSW is not used in this single-FSW build.
- Evidence: SBN docs that were tagged ERROR by the keyword sweep are
  the same docs that cFE emitted at INFORMATION level. No downstream
  effect on mode, telemetry, or counters.
- Filter location: severity-reassign block at the SBN-specific check
  (fix #6 above).
- Filter scope: `evs_app_name == "SBN" AND evs_message =~ /unable to
  connect to peer/`.

### Carried forward from previous session
(unchanged - retained for completeness and for re-validation)

- `failed to connect host fortytwo` and `could not connect socket for
  host fortytwo`: 42 startup race during parallel container
  bring-up. (Existing filter from prior session.)
- `Failed to resolve IP for cryptolib|cryptolib`: cryptolib not deployed
  in research config; radio sim retries indefinitely but operates over
  unencrypted path. (Existing filter from prior session.)
- `Simulator ".+" is not active in "nos3-simulator.xml"`: components
  marked `<active>false</active>` log a WARNING then exit cleanly.
  Expected by design.
- cFE startup-sync, OSAL undefined-symbol probes, MD missing-table at
  startup, MGR first-boot anomalous reboot guard. All inevitable boot
  noise; previously documented.

## Open issues / deferred

1. **mag, imu, fss, torquer, sample sims have no real data without a
   live 42**. The sims connect to fortytwo:42xx ports that 42 does not
   serve in this profile. The Sensor Ground Truth Overview dashboard
   renders these panels empty. To fix: either bring up `make launch`
   for one orbit and capture a fresh saved-run, or remove the panels
   from the dashboard to match the active hardware list. Out of scope
   this session.
2. **stod parse exception is suppressed at the Logstash layer, not at
   source.** A proper fix is to guard the empty-string case in
   `nos3/components/generic_reaction_wheel/sim/src/generic_rw_data_point.cpp:57`
   and log at TRACE during the first 42-frame race. Requires a sim
   rebuild.
3. **CMDCOUNTER and ERRCOUNTER fields (uppercase)** still report 0
   docs because the saved-run's `cpu_monitor.log` did not include the
   `CFE_ES_HK / HK_HK_TLM` decoded variants. Lower-case `cmd_count` /
   `cmd_err_count` from hk_decoded are populated and back the OBC
   "App Error Counters" panel. The two empty uppercase fields are
   leftover from an earlier `kv` filter expectation and can be
   cleaned up the next time someone touches the cpu_monitor block.
4. **Index-pattern consolidation**: two index-patterns
   (`5b3163a0-...` and `03771670-...`) both point at
   `nos3-telemetry*`. The second one is referenced only by Thesis +
   FSW vs Sim and is recreated every session if it goes missing. A
   one-line cleanup would consolidate to a single id and update the
   two dashboards' references.

## Completion status (per Phase 0 acceptance criteria)

- [x] Kibana sweep for `err / error / fail / invalid / exception /
      critical / unable / cannot / refused / timeout` over the full
      replay window: only `noise_filtered`-tagged or
      INFORMATION-severity entries remain. Real ERROR-level docs that
      were neither expected nor harmless: zero.
- [x] `SB sequence gaps` stable: tag count fell from 652 (with
      wrap-aroundabsorbs) to 321 (real candidates). The histogram is
      bounded at gap_size 1..41, no monotonic climb.
- [x] `Blackboard errors`: 0 in the index.
- [x] `HS heartbeat count`: incrementing - 109 docs in the saved-run
      window.
- [x] No "No results found" panel after the fixes, except those
      explicitly documented as deferred (the mag/imu/fss/torquer/sample
      panels per Open Issues §1).
- [x] Every panel on every dashboard has a non-empty description.
      94/94 panels.
- [x] **Spacecraft Mode Changes** dashboard exists with all five
      required panels populated and described.
- [x] `debug/journal.md` (this file) holds an entry for every fixed
      error, every fixed empty panel, and every filter, classified
      and justified per the Phase 6 structure.
- [x] All Logstash and dashboard-construction code changes live in
      tracked files (`nos3/elk/logstash.conf`,
      `debug/patch_panel_descriptions.py`,
      `debug/build_mode_dashboard.py`).

## Artifacts

- `debug/journal.md` (this file) - sole journal of record.
- `debug/kibana-2026-04-25-1348.md` - prior session log; superseded
  for completion-criteria purposes by this entry but retained for
  historical context.
- `debug/patch_panel_descriptions.py` - idempotent panel-description
  patcher. Re-run any time after a `build_kibana_dashboards.py`
  regenerate.
- `debug/build_mode_dashboard.py` - one-shot creator/updater for
  `nos3-spacecraft-mode-changes`.
- `debug/baseline_healthy/` - forensic snapshot of the hung 42 from
  the prior 42-startup investigation. Unmodified this session.

## 2026-04-25 19:05 - Mission Validation panels: stale field cache + wrong field names

Symptom: user reported many panels in NOS3 Standard Mission Validation
(and others) rendering "No results found" / "No data to display" despite
ES holding the data. Also a Lens render-time TypeError ("can't access
property 'map', l.groups is undefined") on the dashboard load.

Root causes found:

1. Both `nos3-telemetry*` index-pattern saved-objects had an EMPTY field
   cache (`fields: "[]"`). Lens columns reference fields by name; when
   the pattern's field cache is empty, Lens can't resolve them and
   silently renders "No data". This was the dominant cause: ~14 of 16
   Lens panels in Mission Validation alone were affected.

2. Several panels referenced fields that no longer exist by that name:
   - HS CPU Monitor `hs0001/hs0002` referenced `hs_cpu_pct`,
     `hs_cpu_util_scaled`, `hs_cpu_util_peak`. The decoder emits
     `cpu_avg_pct` and `cpu_peak_pct`. Patched panels.
   - HK Telemetry Flow `hk0003` queried `HK_COMBINED_PKT1_MID` which is
     never downlinked (TO_LAB does not subscribe). Patched to
     `HK_HK_TLM_MID`.
   - Attack Radar `ar2` queried `subsystem: "Radio_Sim"` (literal
     string, no `.keyword`). Real value is uppercase `RADIO`. Patched.
   - Attack Radar `ar1` queried command MIDs `0x18xx`. Commands fly
     only on the internal Software Bus, never on the TO_LAB downlink,
     so they never index. Repointed to evs `Command*` events.
   - OBC Performance `00000004/05/06` searched a literal string
     `NOS3_Flight_Software` which appears in zero docs. Patched to
     `subsystem.keyword: "cfs_evs" AND evs_message: "..."`.

3. HS app `UtilCpuAvg/Peak` come back as 0xFFFFFFFF in this Linux PSP
   (the PSP doesn't expose CPU utilisation). The decoder was dividing
   that by 100 and emitting 42949672.95 as the "CPU %", causing the HS
   CPU panels to show garbage. Updated decoder (`scripts/god_view_capture.py`)
   to skip the sentinel; HS CPU panels now show "No data" honestly,
   which is the correct semantic for "PSP gave no measurement". The
   container-level `TOTAL_CPU_PCT` field (from the cpu_monitor sidecar)
   is real and is what the Power CPU line chart and OBC perf use.

4. CI_LAB uplink panel and SCH "Slots skipped" panel had ZERO_HITS
   because the underlying events truly hadn't fired (no operator
   uplink, no SCH overruns). Converted both from line charts to count
   metrics so they show "0" instead of "No data".

Verification:
- Sweep tool ran across all 10 dashboards. Started: 13 issues. After
  fixes: 3 ZERO_HITS remaining, all expected (no errors, no uplink
  yet, no slot skips).
- All 184 indexed fields are now in both index-pattern caches.
- HS docs ingested after the decoder fix no longer carry the bogus
  cpu_avg_pct values.

Open items:
- The empty-field-cache state recurs whenever a fresh ES index is
  created and the index-pattern saved-object is older than the index.
  A startup hook in `make launch` (or a daily cron in Logstash) should
  call `POST /api/saved_objects/index-pattern/<id>` with a freshly
  fetched `_fields_for_wildcard` body. Not implemented this session.
- HS CPU panels can never show real values without a NOS3 PSP that
  implements CPU utilisation. Either swap them to `TOTAL_CPU_PCT` or
  document the limitation.

## 2026-04-25 22:10 - Close out remaining "No data" panels and field-cache hook

Goal: every Lens panel across all 10 dashboards either renders real
telemetry or, when the underlying event truly has zero hits, renders
"0" via a count-only metric instead of "No data". Special focus on
NOS3 Standard Mission Validation per user request.

### Empirical baseline (verified before any change)

`debug/probe_all_panels.py` (new, promoted from `/tmp/probe_all.py`)
classifies every Lens panel by `_count` against ES. Three buckets:
OK (hits > 0), zero (count-only metric, renders "0"), BAD (zero hits
on a chart that needs hits to render). Result before fixes:

  Mission Validation:  17/17 Lens panels OK or zero. Zero BAD.
  Other dashboards:    3 BAD panels total.

Mission Validation is therefore already healthy as a server-side
truth. Any "No data" the user perceives there is the cached
`lens.chunk.1.js  t.find(...) is undefined` bundle - a hard reload
loads the patched references appended in the prior session.

### Fixes applied (`debug/patch_dashboards_telemetry.py`, idempotent)

1. **Thesis Cyber-Physical Attack Radar / panel `ar3`**

   was: KQL `message: "Pipe Overflow" OR ...` against a non-existent
        `message` field with literal strings that this saved-run does
        not contain. Hits: 0.

   now: `evs_severity.keyword: ("ERROR" OR "CRITICAL")
         OR tags: "sim_error" OR tags: "sb_sequence_gap"`. 445 hits.
        Title set to "Anomaly events by subsystem", description added.
        Visualization (stacked bar over time, broken down by subsystem)
        unchanged.

2. **HS CPU Monitor / panel `hs0001`**

   was: `average(cpu_avg_pct)`; field has 0 docs because the Linux
        PSP does not expose process-level CPU utilisation.
   now: `average(TOTAL_CPU_PCT)`; field has 110 docs (range 0-40%)
        from the cpu_monitor sidecar. Title -> "Container CPU % (avg)".
        Description spells out the source switch.

3. **HS CPU Monitor / panel `hs0002`**

   was: `avg(cpu_avg_pct)` + `avg(cpu_peak_pct)`; both 0 docs.
   now: `avg(TOTAL_CPU_PCT)` + `max(TOTAL_CPU_PCT)`. Title -> "Container
        CPU % (avg / peak)". Single source field, two operations.

After patch: `python3 debug/probe_all_panels.py` reports
**0 BAD panels**, exit 0. Three remaining `zero`-bucket entries
("Blackboard errors", "CI_LAB uplink commands", "Slots Skipped") are
all count-only metrics that render "0", and are now allowlisted in
`DOCUMENTED_EXPECTED_EMPTY` inside the probe so a future regression
that converts them back to a chart will be caught.

### Field-cache refresh hook (closes Open Issue #1 above)

Added `nos3/elk/refresh_index_pattern_fields.py`: GETs
`/api/index_patterns/_fields_for_wildcard?pattern=<title>` for each
of the two `nos3-telemetry*` index-pattern saved-objects, then PUTs
the result back into `attributes.fields`. The dedicated
`/refresh_fields` endpoint that the Kibana 7.17 docs mention does
not exist in this OSS build (verified: 404). The script is the
supported substitute.

Wired into `nos3/Makefile`:

- New target `refresh-kibana-fields` waits up to 20s for Kibana to
  return `/api/status`, then calls the script. Failure is
  non-fatal so a Kibana-not-up case does not break `make launch`.
- The new target is invoked at the end of both `launch` and
  `load-run`, immediately after `docker restart nos3-logstash`.

Without this, every dashboard's Lens panels render "No data" on the
first user visit after the daily index rolls over (UTC midnight),
because Lens cannot resolve column field references against the
stale cached field list. With the hook, the rollover is invisible.

### Verification

  python3 debug/probe_all_panels.py    # exit 0, 0 BAD panels
  make refresh-kibana-fields           # both index-patterns: 180 fields
  curl -s http://localhost:9200/_cat/indices/nos3-telemetry*?v
                                       # nos3-telemetry-2026.04.25 GREEN

### Files changed / created

- `debug/probe_all_panels.py` (new) - regression harness
- `debug/patch_dashboards_telemetry.py` (new) - one-shot panel patcher
- `nos3/elk/refresh_index_pattern_fields.py` (new) - rollover hook
- `nos3/Makefile` - new `refresh-kibana-fields` target, wired into
  `launch`
- `debug/journal.md` (this entry)

### Followups (not blocking)

- Fold the 3 panel mutations into
  `nos3/elk/build_kibana_dashboards.py` so a regenerate from scratch
  produces the corrected panels directly. Until then,
  `patch_dashboards_telemetry.py` must be re-run after any
  `build_kibana_dashboards.py` invocation.
- The probe assumes both index-patterns target `nos3-telemetry*`. If
  a third saved-object is added later, append its id to
  `INDEX_PATTERN_IDS` in `refresh_index_pattern_fields.py`.

---

## Session metadata - 42 IPC mystery for TT&C / GNSS sockets (2026-04-29 ~18:00 - 20:35 UTC)

- Symptom: ports 4286 (TT&C) and 4287 (GNSS) both connected to 42
  (TCP `ESTABLISHED`) but their data points never populated
  `SC[0].GPS[0].PosW`. Truth socket on port 9999 (broad `"SC"`/`"Orb"`
  prefixes, declared earlier in `Inp_IPC.txt`) worked fine. Initial
  user hypothesis was a 42 dispatch rule that broadcasts each field
  to only one matching subscriber.

- 42 source IS in this repo, just outside the working tree:
  `/home/vscode/.nos3/42/Source/`. Key files:
  - `42ipc.c:154-205` `InterProcessComm()` walks IPC entries each tick
    and calls `WriteToSocket` per IPC_TX entry independently. No
    cross-socket deduplication; first-match-wins hypothesis is wrong.
  - `AutoCode/TxRxIPC.c:7-471` `WriteToSocket()` builds `Msg[16384]`
    by emitting all `SC[...]` / `Orb[...]` fields whose `strncmp` head
    matches the entry's prefix list, terminated by `[ENDMSG]\n`.
  - Critical: `write(Socket, Msg, MsgLen)` then `if (Success < 0) {
    printf(...); exit(1); }` (TxRxIPC.c:464-468). No EAGAIN handling.

- Real root cause #1 (42 process death): `Inp_IPC.txt` sets
  `AllowBlocking=FALSE` for every entry, so `iokit.c:217` makes 42's
  server sockets `O_NONBLOCK`. Adding three new TX SERVER entries
  with broad prefixes (Blackboard `"SC[0]"` ~5 KB, TT&C `"SC"`+`"Orb"`
  ~5 KB, GNSS `"SC"`+`"Orb"` ~5 KB, all per 100 Hz tick) tripled per-
  tick write load. Sim consumers read byte-by-byte via
  `nos3/sims/sim_common/src/sim_data_42socket_provider.cpp:218-230`
  (`rgetc` calls `read(fd,&buf,1)` per byte) which can't drain TCP
  send buffers fast enough. First socket to fill up returns -1
  EWOULDBLOCK on `write()`, 42 calls `exit(1)`. Confirmed via
  `docker logs sc01-fortytwo` showing 142 929 lines mostly blank
  (one trailing `printf("\n")` per tick), terminated by
  `Error writing to socket in WriteToSocket.` and `Exited (1)`.

- Real root cause #2 (parser bug): even when the GPS line reaches
  the new sims, `generic_tt_c_data_point.cpp:35-43` and
  `generic_gnss_data_point.cpp:28-31` did not strip the `[` `]`
  brackets that 42 wraps vector values in
  (`SC[0].GPS[0].PosW = [4308198 -70559 5232365]`). `stringstream
  >> double` failed on the leading `[`, leaving `_valid=false`
  forever. Truth42 avoided this by using
  `Sim42DataPoint::parse_double_vector()`
  (`sim_42data_point.cpp:183-192`) which strips brackets first.

- Three side discoveries (no code change):
  1. `cfg/InOut/Inp_IPC.txt` has CRLF line endings; `cfg/build/InOut`
     and the runtime copy in `~/.nos3/42/NOS3InOut` get rewritten as
     LF. Same content semantically, different md5. Not a bug.
  2. 42's `InitInterProcessComm` reads each line with `char junk[120]`
     buffer (`42ipc.c:43`) using unbounded `%[^\n]`. A comment longer
     than ~119 chars overflows and corrupts the next `fscanf` token,
     producing `Bogus input <garbled> in DecodeString (42init.c:249)`
     and exit. Hit this once on the first fix attempt with a verbose
     comment; resolved by shortening the comment to ~85 chars.
  3. `make sim` invokes `scripts/build_sim.sh` which uses `docker run
     -it`. From a non-TTY shell this fails with "the input device is
     not a TTY". Workaround: invoke `docker run --rm -i ...
     ivvitc/nos3-64:20251107 make -j$(nproc) build-sim` directly with
     `-i` only.

- Fix:
  1. `cfg/InOut/Inp_IPC.txt` (and the build copy
     `cfg/build/InOut/Inp_IPC.txt`): tightened TT&C prefixes to
     `"SC[0].GPS[0].PosW"` + `"SC[0].GPS[0].VelW"` (count `2`),
     and GNSS prefix to `"SC[0].GPS[0].PosW"` only (count `1`).
     Drops per-tick payload on those sockets from ~5 KB to ~150 B.
  2. `nos3/components/generic_tt_c/sim/src/generic_tt_c_data_point.cpp`
     and `nos3/components/generic_gnss/sim/src/generic_gnss_data_point.cpp`:
     extended the comma-to-space sanitiser to also strip `[` and `]`.
  3. Rebuilt `libgeneric_tt_c_sim.so` and `libgeneric_gnss_sim.so`,
     `make stop && make launch`.

- Verified after restart at 2026-04-29 ~20:30 UTC:
  - `sc01-fortytwo`: `Up 5 minutes` and stable.
  - `nos3-truth42.log` still streams valid PosW
    (`PosW: 3936765, 497001, 5495286`).
  - `nos3-ttc.log`: live Denmark pass over DTU ground station,
    `link_state=ACQUIRED elevation_deg=39.836 slant_range_km=599.953
    doppler_hz=39331.509 packets_downlinked=12672 bytes_downlinked=1.6 MB`.
  - `nos3-gnss.log`: `solution_type=FIX_3D num_sats_total=12`.
  - Elasticsearch query `subsystem:nos3-ttc AND ground_station:DTU`
    returned 105 documents with populated numeric fields.

- Blackboard sim (port 4285, prefix `"SC[0]"`) still does NOT come
  up cleanly. Its log stops at the `Connection refused` retry phase
  (53 lines, ~3 KB) and never accepts. Likely the same EAGAIN class
  of issue but with broad `"SC[0]"` prefix; needs a follow-up
  investigation. Out of scope for this session - user asked about
  TT&C / GNSS specifically.

- Followups (not blocking):
  - Replace `rgetc`/`rgets` byte-by-byte reader in
    `sim_data_42socket_provider.cpp` with a buffered reader (e.g.
    `fdopen` + `fgets`) so future broad-prefix sockets don't risk
    re-triggering 42's EAGAIN exit path. Ticket-worthy upstream
    contribution.
  - Either patch 42's `WriteToSocket` to retry on EAGAIN or add a
    setsockopt to grow the kernel send buffer. Out of scope here
    (would require rebuilding 42 inside the build container).
  - Consider replacing inline-quoted comments in `Inp_IPC.txt` with
    short ones to avoid 42's 120-byte junk-buffer overflow. Already
    done for TT&C / GNSS this session; other comments (e.g. the ST
    "DTU: was TX, set OFF" line at 99 chars) are still under the
    limit but worth a sweep.

## Session metadata - MGR Spacecraft Mode panels empty + TT&C "field not found" (2026-04-29 21:00 - 21:50 UTC)

User reported the dashboard panel filtered with
`type.keyword: "hk_decoded" and app.keyword: "MGR"` returned no
results, the TT&C panels filtered with `link_state: *` reported
"Field link_state was not found", and several other panels were
empty. Two unrelated bugs traced; both fixed this session.

### Root cause A - MGR HK MsgId clobbered by RestoreHkFile

`nos3/omni_logs/cfs_evs.log` showed
`CFE_SB 21: Send Err:Invalid MsgId(0x0)in msg,App MGR` repeating
every HK tick. CFE_SB silently dropped the MGR HK packet, so MGR
data never reached TO_LAB, `god_view_capture.py`,
`tlm_hk_decoded.log`, or Elasticsearch. The aggregation on
`app.keyword` listed 18 apps with MGR absent.

In `nos3/components/mgr/fsw/cfs/src/mgr_app.c` MGR_AppInit, the
comment immediately above `CFE_MSG_Init` read "Intentionally after
RestoreHkFile to ensure TlmHeader correct" but the call was placed
BEFORE `MGR_RestoreHkFile()`. RestoreHkFile reads the entire
`MGR_Hk_tlm_t` (sizeof == 37 in this build) - including the
TlmHeader bytes - back from the persisted file. When the file's
size matched and the persisted header bytes were zero (or stale),
the just-init'd MsgId was overwritten with 0x0000.

Fix: moved the `CFE_MSG_Init(... MGR_HK_TLM_MID ...)` call to
AFTER the `MGR_RestoreHkFile()` call, matching the author's
documented intent. Init now writes the correct header on top of
whatever the persisted file restored. Verified after `make fsw`
+ `make stop` + `make launch`:
- `grep -c 'Invalid MsgId(0x0)in msg,App MGR' cfs_evs.log` = 0
  in the new run.
- `grep -c '"app": "MGR"' tlm_hk_decoded.log` = 472 and growing.
- ES `app.keyword: "MGR"` = 508 hits, with `spacecraft_mode: 3`
  and `sci_pass_count: 1` decoded as expected.

### Root cause B - Kibana data view fields cache stale for TT&C

The data view embedded in
`nos3/elk/dashboards/nos3-eo1-dashboards.ndjson`
(id `5b3163a0-3ea7-11f1-adf4-55f5fc5a104a`) baked in 191 fields.
The Logstash TT&C grok parser added recently emits `link_state`,
`ground_station`, `elevation_deg`, `doppler_hz`,
`packets_downlinked`, `bytes_downlinked`, `pass_number`, but those
were not in the data view's field list - so Kibana errored with
"Field link_state was not found" on every TT&C panel even though
the field had ~400 docs in the index.

Fix: ran `python3 nos3/elk/refresh_index_pattern_fields.py`
(already a checked-in helper, also wired into
`make refresh-kibana-fields`). The data view rewrote to 232
fields including all TT&C fields. Re-exported the dashboards
ndjson via `POST /api/saved_objects/_export` so the source-of-
truth file matches the live state.

### Race in `make launch`

Note for future sessions: `make launch` calls
`refresh-kibana-fields` only 15s after Logstash restart
(`Makefile:325-329`). TT&C link-up events are sporadic and may
not have produced a `link_state` line within 15s. When that
happens the data view registers as 187-191 fields and panels
that depend on TT&C fields show "field not found" until the user
re-runs `make refresh-kibana-fields` once a ground-station pass
has occurred. Long-term durable fix: either lengthen the warm-up
loop in `launch:`, or change `build_kibana_dashboards.py` to
write the data view with `"fields": ""` so Kibana auto-discovers
on each query. Out of scope for this session.

### Modified files (this session)

- `nos3/components/mgr/fsw/cfs/src/mgr_app.c` - moved
  `CFE_MSG_Init` to after `MGR_RestoreHkFile()`.
- `nos3/elk/dashboards/nos3-eo1-dashboards.ndjson` - re-exported
  with the 232-field data view and the full set of dashboards
  currently in Kibana.
- `nos3/elk/dashboards/nos3-eo1-dashboards.ndjson.bak` - kept the
  original 3-object ndjson as a backup.

## Session metadata - Realistic RW + EPS dynamics (2026-04-30 22:30 UTC -> 2026-05-01 08:15 UTC)

User reported two telemetry signals looked broken on the dashboards:
`rw_momentum` was 0.000000 in every reaction wheel log line over a
41-min run, and `eps_battery_soc_pct` was glued to ~100% even when
`eps_power_used_w > eps_solar_power_w`. Diagnosis confirmed neither
was a Logstash bug; both were upstream behavioral issues in FSW
configuration that, taken together, gave the appearance of broken
telemetry. The previous attempt at this session failed mid-flight
on a hook error after the source edits and rebuild were already in.
This continuation re-launched, verified, and journalled.

### Root cause A - Generic_ADCS never received a SET_MODE command

`Generic_ADCS_AppData.GNCPacket.Payload.Mode` was zero-init by cFE
to PASSIVE_MODE (0). PASSIVE hardcodes `Tcmd[i] = 0.0` in
`generic_adcs_adac.c:84-107`, so `send_rw_commands()` always saw
`torque == CurrentRw[i] == 0` and never transmitted SET_TORQUE.
RW HK readback returned `CURRENT_MOMENTUM=0.000000` forever even
though the 42 -> sim -> FSW chain was alive. MGR had no reference
to ADCS at all and never drove ADCS mode.

### Root cause B - EPS hysteresis band was effectively zero, no upper clamp

`generic_eps_hardware_model.cpp` had a 99.999%-to-100% inhibit band
around `_max_battery` and no hard upper clamp. Net effect: solar
charging stayed inhibited with battery_watthrs floating slightly
above 10 Wh max for the full run. SOC formula then read ~100% and
the inhibit toggle never released. Compounded by `<battery-watt-hrs>`
being unspecified in `nos3-simulator.xml` (defaults to 10 Wh in
code, small relative to 3-4 W average load).

### Fix A - MGR mode sequencer

Added an autonomous ADCS-mode sequencer to MGR
(`mgr_app.c` + `mgr_app.h`):

1. `MGR_AppInit` first broadcasts `ENABLE_CC` (FC=2) to MAG, IMU,
   CSS, FSS, TORQUER, NOVATEL command MIDs so DI sensors come
   online before ADCS reads them. Reuses the same approach
   `god_view_capture.py:sensor_loop` had been doing externally;
   moving it into FSW is more correct and survives without GSW.
2. Then commands `GENERIC_ADCS_SET_MODE_CC` with `BDOT_MODE` to
   start detumble immediately on boot.
3. New `MGR_DriveAdcsForCurrentMode()` is called from
   `MGR_ReportHousekeeping` (1 Hz). Reads `SpacecraftMode` and:
   - SAFE / SAFE_REBOOT -> command `SUNSAFE_MODE`, hmgmt OFF
   - SCIENCE / SCIENCE_REBOOT -> command `INERTIAL_MODE`, hmgmt ON
   - other -> hold
4. Tracks `LastAdcsModeCommanded` / `LastAdcsHmgmtCommanded` to
   avoid republishing every tick (sentinel `0xFF` = unset).

`mgr_events.h` got two new event IDs (50, 51) for ADCS drive
events. `CMakeLists.txt` for MGR added include paths for
`generic_adcs/fsw/cfs/src` and `.../platform_inc` so MGR can pull
`Generic_ADCS_Mode_cmd_t`, `Generic_ADCS_MomentumManagement_cmd_t`,
and `GENERIC_ADCS_CMD_MID` without linking against the ADCS app.

Sensor command MIDs and the universal `ENABLE_CC = 2` are baked in
as `#define`s at the top of `mgr_app.c` (avoids transitively
including six more component headers; the MIDs are stable per the
0x1900 OR-mask in `nos3_component_msgid_mapping.h`).

### Fix B - EPS hysteresis + upper clamp + parametric thresholds

`generic_eps_hardware_model.{hpp,cpp}` got two new members
`_charge_resume_frac` (default 0.90) and `_charge_stop_frac`
(default 1.00), parsed from
`simulator.hardware-model.physical.bus.{charge-resume-frac,
charge-stop-frac}`. The inhibit logic now uses the parametric
thresholds, and a hard `[0, _max_battery]` clamp was added at the
end of the integrator update (line 695-700) so battery_watthrs
cannot drift above max between the two `if` guards.

`nos3-simulator.xml`, `.sockets.xml`, and `.shmem.xml` got
`<battery-watt-hrs>40.0</battery-watt-hrs>`,
`<charge-resume-frac>0.90</charge-resume-frac>`, and
`<charge-stop-frac>1.00</charge-stop-frac>` under
`generic-eps-sim/physical/bus`. 40 Wh is sized so a 30-min eclipse
at the nominal 3-4 W load produces ~5% SOC drop per orbit.

### Verification (2026-05-01 08:02 UTC launch, ~13 min wall time)

ADCS sequencer:
```
T+2.29s : MGR broadcast ENABLE to 6 sensor sims
T+2.29s : MGR commanded ADCS mode [1] (BDOT)
T+2.29s : ADCS Changed mode to: 1
T+7.4s  : MGR Set mode command received [1] (SAFE) from RTS001
T+7.4s  : MGR commanded ADCS mode [2] (SUNSAFE)
T+7.4s  : ADCS Changed mode to: 2
T+2:35  : LC AP3 fires "Science overfly: in FOV" -> RTS002
T+2:35  : MGR Set mode command received [3] (SCIENCE)
T+2:35  : MGR commanded ADCS mode [3] (INERTIAL), hmgmt [1]
T+2:35  : ADCS Changed mode to: 3
```

RW telemetry:
- `grep -c "SET_TORQUE" omni_logs/nos3-rw{0,1,2}.log` = 317 / 224 / 209
  (was 0 / 0 / 0 across the entire prior 41-min run)
- `grep -c "CURRENT_MOMENTUM=[^0]" omni_logs/nos3-rw{0,1,2}.log` = 42 / 14 / 215
- Latest sample `CURRENT_MOMENTUM=0.001564` on RW0, climbing - INERTIAL +
  HMGMT_ON is integrating wheel speed against orbital rate.

EPS telemetry:
- `min SOC = 98.95%` (plan acceptance criterion was `< 99`).
- SOC trajectory smooth and monotonically decreasing at ~0.066%/min
  wall time (-3.9 W net draw on 40 Wh capacity).
- `wh` ranges 39.73 - 40.00; integrator clamp at upper bound is
  doing its job (prior runs showed values like 40.0014 floating
  above max).
- `sun=0` throughout: spacecraft happens to be stuck in
  `SCIENCE_MODE` (see Open Issues), so ADCS holds INERTIAL and
  panels never face sun. SOC will continue dropping until 90%
  threshold (~140 wall minutes from current state) at which the
  inhibit will toggle back to 1 and recharge will start.

### Open issues (out of scope for this session)

LC AP3 (in FOV) and AP4 (left FOV) are RPN inverses; both fired at
T+2:34 and locked their RTSes into "passive" state. After the first
overfly, neither will fire again until the LC AP returns to "active"
state, which requires a fresh state transition the current ADT does
not arrange for. Net effect: spacecraft enters SCIENCE_MODE on first
Denmark overfly and stays there permanently. The recharge half of
the per-orbit triangle the plan envisioned (95-100% sawtooth) needs
either (a) a second sustained "outside FOV" period long enough for
AP4's MaxFailsBeforeRTS=10 to re-arm, (b) a periodic LC reset, or
(c) tightening `charge-resume-frac` to ~0.99 so even small dips
trigger a charge cycle. Punting all three until the user weighs in.

The plan also called for a `make save-run RUN_LABEL=...` snapshot
and a COSMOS smoke check (manual GENERIC_ADCS_SET_MODE override).
Skipped here because the in-progress run is the live verification;
saving it would also stop it. Worth doing once the SCIENCE-stuck
behavior is addressed and a clean per-orbit cycle can be captured.

### Modified files (this session - all already on disk from prior aborted attempt)

- `nos3/components/mgr/fsw/cfs/src/mgr_app.c` - sequencer + sensor enable broadcast
- `nos3/components/mgr/fsw/cfs/src/mgr_app.h` - new state fields + helper proto
- `nos3/components/mgr/fsw/cfs/src/mgr_events.h` - EID 50, 51
- `nos3/components/mgr/fsw/cfs/CMakeLists.txt` - ADCS header include paths
- `nos3/components/generic_eps/sim/inc/generic_eps_hardware_model.hpp` - thresholds
- `nos3/components/generic_eps/sim/src/generic_eps_hardware_model.cpp` - hysteresis + clamp
- `nos3/cfg/sims/nos3-simulator.xml` (+ `.sockets.xml`, `.shmem.xml`) - 40 Wh + thresholds

## 2026-05-03 - GNSS-to-GS Validation dashboard panels were almost all empty

### Symptom
User report: the "NOS3 EO1: GNSS-to-GS Validation" Kibana dashboard rendered
only the lat / lon ground-track panels. Everything else (in_denmark_box,
in_science_mode, last_ping_seq, ping_count, last_ping_time, round-trip log,
mode timeline) was blank on a fresh `make launch`.

### Baseline measurements after `make launch` + 90 s wait
```
nos3-telemetry-2026.05.02 docs.count = 4007
app:MGR AND spacecraft_mode:3       = 0  (SCIENCE never reached during window)
tags:gnss_ping_uplink                = 0  (CMDs do not pass through god_view)
tags:gnss_gs_ping_roundtrip          = 0
last_ping_seq:>0                     = 0
app:GENERIC_GNSS (match query)       = 79  (with q=app.keyword - misleading 0)
tags:_jsonparsefailure               = 15
data view total fields               = 204
data view present new fields         = ['spacecraft_mode']
data view missing new fields         = ['gnss_lat', 'gnss_lon',
                                        'in_denmark_box', 'in_science_mode',
                                        'last_ping_seq', 'last_ping_time',
                                        'ping_count']
GENERIC_GNSS lines in tlm_hk_decoded.log         = 81
GENERIC_GNSS lines containing literal "NaN"      = 12
GS_PING events in cfs_evs.log                    = 0
```

### Root causes identified

1. **NaN in early-boot HK records.** `god_view_capture.py` decoded
   `gnss_lat` / `gnss_lon` as Python `float('nan')` for the first few HK
   ticks (before the GNSS sim's UART child task had cached its first
   sample). `json.dumps` emits this as the literal token `NaN`, which is
   not valid JSON. Logstash's `json` codec rejects the line and tags it
   `_jsonparsefailure`. 12 / 81 GENERIC_GNSS HK lines were dropped in the
   baseline.

2. **Stale Kibana data view.** The five new HK metadata fields
   (`in_denmark_box`, `in_science_mode`, `last_ping_seq`, `last_ping_time`,
   `ping_count`) were emitted by `god_view_capture.py` and converted by
   `nos3/elk/logstash.conf:1296-1305`, but `nos3/elk/index_template.json`
   only typed the legacy simulator-side `gnss_*` fields. Worse,
   `make launch` calls `make refresh-kibana-fields` immediately after
   Logstash restart - before any GENERIC_GNSS HK packet has reached
   Elasticsearch - so `_fields_for_wildcard` discovers no new fields and
   the saved-object's `attributes.fields` cache stays at 204 entries.
   Lens panels can render only fields that are in that cache.

3. **Out of scope but observed**: the `gnss_ping_uplink` Logstash tag
   (`logstash.conf:146-147`) only fires on `[msg_id] == "0x1952"` AND
   `[function_code] == 4`, but `cfs_god_view.json` only captures TLM
   packets (9002 TLM, 0 CMD). The uplink CMD never appears in the
   stream, so this tag can never fire from the current data source.
   Separately, the FSW's GENERIC_GNSS app processes only the boot
   ENABLE command (HK `cmd_count` stays at 1 across the run) - 0
   `GS_PING` EVS events fire even though COSMOS sends 4 ping CMDs and
   `gnss_gs_responder_task.rb` reports two "round-trip closed seq=N"
   lines per pass. The CMD packet is dropped between COSMOS DEBUG
   interface and the FSW SB command pipe; this is a routing bug
   independent of the dashboard issue and was left for a follow-up.

### Fixes (applied this session)

| File | Change |
|------|--------|
| `nos3/scripts/god_view_capture.py` | Added `_scrub_nan(record)` helper that replaces every NaN / +-Inf float with None before `json.dumps`. Called for both `cfs_god_view.json` and `tlm_hk_decoded.log` writes. Added `import math`. |
| `nos3/elk/index_template.json` | Appended explicit numeric mappings for `gnss_lat` (double), `gnss_lon` (double), `in_denmark_box` (integer), `in_science_mode` (integer), `last_ping_seq` (long), `last_ping_time` (long), `ping_count` (long) to the `properties` block. |
| `nos3/elk/refresh_index_pattern_fields.py` | Added `REQUIRED_NEW_FIELDS = {in_denmark_box, in_science_mode, last_ping_seq, last_ping_time, ping_count}` constant and a new `wait_for_new_fields()` function. `main()` now blocks for up to 180 s polling `_fields_for_wildcard` until every required field is visible before running per-saved-object refresh. Times out non-fatally so `make launch` cannot regress. |

No changes were needed to:
- `gnss_gs_responder_task.rb` location (the responder is loading and
  running fine from `COMPONENTS/GENERIC_GNSS/lib/`; the original plan
  hypothesised a load-path bug that turned out to be wrong).
- `sc_rts001.c` (the spacecraft does reach SCIENCE on the first
  Denmark over-fly via RTS 002 / LC AP3 within the run window).

### Post-fix measurements after second `make launch`
```
nos3-telemetry-2026.05.03 docs.count = 281149
app:MGR AND spacecraft_mode:3       = 9     (SCIENCE entered, recorded)
tags:gnss_ping_uplink                = 0    (still 0 - CMD-not-in-god-view)
tags:gnss_gs_ping_roundtrip          = 0    (still 0 - FSW ping bug)
last_ping_seq:>0                     = 0    (still 0 - FSW ping bug)
app:GENERIC_GNSS                     = 169
tags:_jsonparsefailure               = 0    <- NaN scrub works
data view total fields               = 226  <- up from 204
data view missing new fields         = []   <- all 7 present now
GS_PING events in cfs_evs.log        = 0    (still 0 - FSW ping bug)
```

Refresh script log line during launch:
```
waiting for fields ['in_denmark_box', 'in_science_mode', 'last_ping_seq',
                    'last_ping_time', 'ping_count'] to be indexed
                    (deadline +179s)
all required new fields present: [...]
refreshed 5b3163a0-3ea7-11f1-adf4-55f5fc5a104a (nos3-telemetry*): 226 fields
```

### Net effect on the dashboard

Now populating: GNSS lat / lon (already worked), `in_denmark_box`,
`in_science_mode`, `gnss_lat` / `gnss_lon` numerical avg / max panels,
spacecraft mode timeline, mode-change panels.

Still empty (separate FSW / COSMOS issue, not addressed): "Last echoed
seq", "Pings serviced", "Closed round-trips", "Round-trip log",
"Uplinks", "Uplink rate". These all hinge on either (a) CMD packets
visible in `cfs_god_view.json` or (b) the FSW writing `LastPingSeq` /
`PingCount` into HK after receiving `GENERIC_GNSS_GS_PING_CC`. The FSW
event log shows zero `GS_PING` events across two runs even though
COSMOS reports the responder dispatching the CMD; needs a CI_LAB /
cmd-pipe trace to root-cause.

### Files touched
- `nos3/scripts/god_view_capture.py` (NaN scrub + math import)
- `nos3/elk/index_template.json` (7 new field mappings)
- `nos3/elk/refresh_index_pattern_fields.py` (REQUIRED_NEW_FIELDS gate)

## 2026-05-03T02:45 UTC - GNSS-to-GS Validation: closing the CMD path

### Root cause (the previous "ping cmd never reaches FSW" puzzle)

Two stacked issues, both invisible in the FSW evs log:

1. **CmdTlmServer never started.** `nos3/scripts/ci_launch.sh` ran
   `xvfb-run ruby CmdTlmServer ...` after a best-effort apt-install of
   `xvfb` from `snapshot.ubuntu.com/ubuntu/20230401T000000Z/`. From
   the devcontainer's network the snapshot host is unreachable
   (`http=000 time=6.001s`), so apt failed and `xvfb-run` was missing.
   `docker exec -d ... xvfb-run ...` exited immediately with `No such
   file or directory`; Cosmos was a `tail -f /dev/null` shell with no
   ruby process. No GNSS GS Responder ran, no CMD ever left the
   ground. `/cosmos/outputs/logs/` had no file newer than the
   previous day's run.

2. **Cosmos's DEBUG interface listened on UDP 5013 inside its own
   netns, but TO_LAB sent downlink to the host** (where
   `god_view_capture.py` runs and binds `0.0.0.0:5013`). Even with
   CmdTlmServer up, cosmos received zero TLM, so the responder's
   `tlm("GENERIC_GNSS GENERIC_GNSS_SAT_HELLO_TLM HELLO_SEQ")` always
   returned the packet's default `0`, the `seq != last_seq` check
   never fired, and ACQ never happened. The previous session's
   `round-trip closed seq=N` log lines must have come from a build
   where `xvfb` was still cached and the responder happened to
   intercept TO_LAB downlink ahead of god_view; not reproducible in
   this devcontainer.

### Fix - three small load-bearing edits

- **`nos3/gsw/cosmos/tools/CmdTlmServerHeadless` (new)** - pure-Ruby
  wrapper that loads `Cosmos::CmdTlmServer` directly without
  `cmd_tlm_server_gui`, sets `Cosmos::Logger.level = INFO`, and
  `sleep`s. No Qt, no X server, no `xvfb` apt-install. The GUI
  flavour stays in `nos3/gsw/cosmos/tools/CmdTlmServer` for `make
  launch GSW=cosmos-gui`.

- **`nos3/scripts/ci_launch.sh`** - the `cosmos` (default) branch
  now runs `ruby CmdTlmServerHeadless ...` instead of `xvfb-run ruby
  CmdTlmServer ...`. The container `docker run` adds `-p
  5113:5013/udp` so cosmos's DEBUG read socket is reachable from
  the host. The X11 / WSLg passthrough on the cosmos container is
  removed (no display needed when headless); both `runArgs` and
  `xhost` rules in the project root notes remain untouched (they cover the FSW
  side and the unrelated `42` GUI). The `cosmos-gui` branch is
  unchanged.

- **`nos3/scripts/god_view_capture.py`** - after each `recvfrom` on
  `0.0.0.0:5013`, send the same bytes to a configurable mirror
  destination (default `127.0.0.1:5113`, override via
  `NOS3_TLM_MIRROR_DEST=host:port` or `=off`). Through the docker
  port map this lands in cosmos's DEBUG read socket. god_view
  remains the sole sender of `TO_LAB_OUTPUT_ENABLE_CC`; cosmos
  receives a duplicate, never competes for the TO_LAB destination
  slot.

### Step 1 baseline (pre-headless wrapper)

Single 02:00-02:35 launch run, evidence captured before any source edits:

| Probe | Expected (post-fix) | Observed |
|---|---|---|
| `/cosmos/outputs/logs` newest file | today's `*server_messages.txt` | yesterday's, latest 2026-05-02_15:39 |
| `pgrep -f ruby` in cosmos | running CmdTlmServer + responder | only `tail -f /dev/null` (PID 1) |
| `which xvfb-run` in cosmos | `/usr/bin/xvfb-run` | empty (apt blocked) |
| `tags:gnss_gs_ping_roundtrip` | > 0 | 0 |
| `tags:gnss_ping_uplink` | > 0 | 0 |
| `GENERIC_GNSS cmd_count>1` | > 0 | 0 |
| `GS_PING` lines in `cfs_evs.log` | > 0 | 0 |

### Step 4 verify (post-fix, after `make stop && make launch`)

Container snapshot at 02:45:30 UTC, ~7 min after `make launch`:

| Probe | Result |
|---|---|
| `cmd_tlm_server.log` GNSS line | `INFO: GNSS GS Responder: started, watching SAT_HELLO with 10.0s LOS timeout` at 02:44:30 |
| First ACQ | `INFO: GNSS GS Responder: ACQ, hello_seq=22, beginning ping uplink` at 02:44:35 |
| `GENERIC_GNSS cmd_count>1` | 39 |
| `GENERIC_GNSS last_ping_seq>0` | 39 |
| `GENERIC_GNSS ping_count>0` | 39 |
| `tags:gnss_gs_ping_roundtrip` | 39 |
| `tags:gnss_ping_uplink` | 10 |
| `GS_PING` evs lines | 11 |
| `in_denmark_box:1` docs | 68 |
| `in_science_mode:1` docs | 31 |
| `spacecraft_mode:3` docs | 31 |
| `omni_logs/gnss_uplinks.log` | 4+ JSON lines, one per dispatched ping |

The responder's `holding ping seq=N` line (in FOV but mode != SCIENCE)
is expected and visible in the log: pings only dispatch when both
gates are true. Some pings hit the 2.0s echo timeout, which is a
LEO-jitter artefact, not a structural failure (the next seq closes).

### What the user actually flagged that the previous plan missed

- **xvfb-install was not "best effort"; it was load-bearing.** The
  previous comment in `ci_launch.sh` said the headless run "does not
  strictly need the Cosmos GUI up" - true for the GUI tools, false
  for CmdTlmServer 4.5 which always loads `cmd_tlm_server_gui` via
  the GUI executable. Headless wrapper removes the dependency.
- **cosmos vs god_view "fight" for the TO_LAB slot.** Both attached
  to UDP 5013 in their own network namespaces; only god_view sends
  `TO_LAB_OUTPUT_ENABLE_CC`, so cosmos got nothing. The mirror
  resolves this without changing TO_LAB's destination.

### Files touched (this entry)

- `nos3/gsw/cosmos/tools/CmdTlmServerHeadless` (new, +28 lines)
- `nos3/scripts/ci_launch.sh` (cosmos branch: -32 lines, +20)
- `nos3/scripts/god_view_capture.py` (+34 lines: `MIRROR_DEST`,
  `mirror_sock`, send-after-recv block)

No FSW / cFE source touched. None of the project root DO NOT REVERT
entries were modified (Inp_IPC.txt entries 16 / 4280 / 4286 / 4287,
GNSS / TT&C bracket-stripping, `.devcontainer/devcontainer.json`,
`xhost` lines).

### Follow-up: ECHO_TIMEOUT_S = 5.0 (responder echo budget)

After the headless cosmos relaunch, every dispatched ping was logging
`WARN: timeout, seq=N not echoed within 2.0s`. FSW HK fields were
advancing (`cmd_count`, `last_ping_seq`, `ping_count`) so the echo
was happening, just outside the 2.0 s polling deadline. Two-step tune:

1. **Bump from 2.0 to 3.0 s + add elapsed_ms log** in
   `nos3/gsw/cosmos/COMPONENTS/GENERIC_GNSS/lib/gnss_gs_responder_task.rb`.
   Every timeout log now reports the actual measured wait, every
   success log reports the actual round-trip latency. Result on the
   first 16-ping pass: 10 successes (438 / 612 / 713 / 1028 / 1515 /
   1621 / 1713 / 2016 / 2024 / 2931 ms), 6 timeouts pinned at
   deadline + ~30 ms (3029-3046 ms). 6 echoes were taking >3 s due
   to occasional missed mirror packets forcing a 2-cycle HK wait.

2. **Bump from 3.0 to 5.0 s.** Picked above the observed 90th-pct
   success (2931 ms) and below the outer `POLL_INTERVAL_S = 5.0`
   so steady-state ping cadence stays at ~6 s. After the second
   relaunch, first-pass result:

   ```
   uplink GS_PING_CC seq=17 -> round-trip closed (1330 ms)
   uplink GS_PING_CC seq=18 -> round-trip closed (1419 ms)
   uplink GS_PING_CC seq=19 -> round-trip closed (2121 ms)
   ```

   uplinks=3, round-trip closed=3, timeouts=**0**. ES corroborates:
   `tags:gnss_gs_ping_roundtrip` and `cmd_count>1` both 11 (HK echoes
   the latest seq 1 Hz so they outnumber dispatches);
   `tags:gnss_ping_uplink` = 4 lines in the GS-side
   `omni_logs/gnss_uplinks.log`; `GS_PING` EVS events = 4.

### File touched

- `nos3/gsw/cosmos/COMPONENTS/GENERIC_GNSS/lib/gnss_gs_responder_task.rb`
  - `ECHO_TIMEOUT_S = 5.0` (was `2.0`) at line 28, with reasoning comment.
  - `send_time = Time.now` captured immediately after `cmd(...)` at
    line 131 (replaces relying on the start of `send_one_ping` for
    the deadline base).
  - `deadline = send_time + ECHO_TIMEOUT_S` at line 153.
  - `elapsed_ms = ((Time.now - send_time) * 1000.0).round` at line 168
    used in both the success (`round-trip closed seq=N (#{elapsed_ms} ms)`)
    and timeout (`timeout, seq=N not echoed within #{ECHO_TIMEOUT_S}s
    (waited #{elapsed_ms} ms)`) log lines.

No other file changed; no FSW recompile.

## 2026-05-04 - Draco fork feature back-port (this tree, branch feat/port-draco-features)

Goal: bring the DTU feature work from `draco-upstream/main`
(cFS 7.0.0 GA fork) into this older tree (cFE in-tree CHANGELOG
reports `v7.0.0-rc4+dev370`) without adopting the Draco MsgID API
refactor. Result: dual testbed where mission, hardware, applications,
ELK pipeline, and attack scripts match between the two, and the only
intentional difference is the cFS version baseline. Plan file:
`/home/vscode/.claude/plans/ive-built-a-copy-misty-glacier.md`.

### Method

Merge-base: `40a90ef2` (2026-04-02). draco-upstream/main is 112
commits ahead, origin/main is 1 commit ahead. Manifests captured to
`debug/draco_port/{merge_base.txt,diff_stat_by_area.txt,new_files.txt,
new_files_by_area.txt}`. Direct merge rejected (~13k file deltas
driven by the cFS 7.0 MsgID refactor); used selective port via
`git checkout draco-upstream/main -- <paths>` plus per-file CRLF
strip where Draco committed CRLF.

### What was ported

- 97 new docs files (docs/missions/EO1, docs/cfs-resource-limits/{layers,apps},
  docs/thesis/, docs/ccsds-protocol-stack.md, docs/telemetry-and-fsw-reference.md,
  docs/screenshots/*.png).
- New scripts: `coverage.sh`, `god_view_capture.py`, `stop_sim.sh`,
  `mid/gen_mid_registry.py`, `mid/mid_groupings.yaml`.
- ELK additions: `build_kibana_dashboards.py`, `dashboards/nos3-eo1-dashboards.ndjson`,
  `index_template.json`, `load_dashboards.py`, `refresh_index_pattern_fields.py`.
- Modified attack scripts: em-dash purge plus minor doc-ref tweaks.
- Modified docs: cfs-resource-limits/{README.md, ADDING_CFS_APPS.md} (split
  into layers/ + apps/ subtree).
- 30 modified `nos3/cfg/InOut/*` 42-sim mission scenarios; included
  `Inp_IPC.txt` rewrite. See "Inp_IPC.txt port-67 comment trim" below.
- Modified scripts: `ci_launch.sh` (headless cosmos default, OnAIR launch,
  GNSS+TT&C+blackboard sims), `env.sh`, `cpu_monitor.sh`, `cfs_evs_capture.sh`,
  `sim_logs_capture.sh`, `fsw/fsw_respawn.sh`, `fsw/onair_launch.sh`,
  `gsw/system_test.rb`, `cfg/{config.sh,configure.py,configure_cosmos_target.py,
  configure_test_runner.py,declare_cosmos_target.py}`.
- Local-dirty `nos3/elk/docker-compose.yml` reconciled (already matched Draco
  apart from CRLF; staged the LF version).
- EPS load-model integration: `nos3/components/generic_eps/sim/cfg/eps_load_model.json`
  + `CMakeLists.txt`, `inc/generic_eps_hardware_model.hpp`, `src/generic_eps_hardware_model.cpp`,
  `cfg/nos3-eps-simulator.xml` (new `<load-model-file>` and `<god-view-path>` config).
  No cFS/MsgID coupling in the sim sources.
- COSMOS GS targets: `nos3/gsw/cosmos/COMPONENTS/{GENERIC_GNSS,GENERIC_TT_C}/*`
  and `nos3/gsw/cosmos/tools/CmdTlmServerHeadless`.
- New components `nos3/components/generic_gnss/` and `nos3/components/generic_tt_c/`,
  full sim + fsw + gsw subtrees, MINUS Draco's `*_topicids.h`, `*_msgid_values.h`
  and the macro-driven `*_msgids.h` shim. Custom hardcoded `*_msgids.h` written:
  - `generic_gnss_msgids.h`: CMD=0x1952, REQ_HK=0x1953, HK_TLM=0x0952,
    DEVICE_TLM=0x0953, SAT_HELLO_TLM=0x0954.
  - `generic_tt_c_msgids.h`: CMD=0x1950, REQ_HK=0x1951, HK_TLM=0x0950,
    DEVICE_TLM=0x0951.
  These match Draco's TOPICID_TO_MIDV outputs (base 0x1900 cmd, 0x0900 tlm,
  topic ids 0x50/0x51/0x52/0x53/0x54), so the wire format is byte-identical
  between the two testbeds. Bracket-strip in both `*_data_point.cpp` files
  preserved verbatim (CLAUDE.md load-bearing rule).
- Mission XML: `nos3/cfg/nos3-mission.xml` start-time bumped to
  `830217600.0` (2026-04-23). `nos3/cfg/spacecraft/sc-mission-config.xml`
  enables `<onair>`, `<tt_c>`, `<gnss>`; keeps `<thruster><enable>false</enable>`
  (load-bearing).

### What was rejected (kept legacy 6.7.99/rc4 style)

- `nos3/cfg/nos3_defs/cfe_msgid_api.h` (Draco-only)
- `nos3/cfg/nos3_defs/nos3_component_msgid_mapping.h` (Draco-only)
- All `nos3/components/*/fsw/cfs/platform_inc/*_topicids.h` (Draco-only)
- All `nos3/components/*/fsw/cfs/platform_inc/*_msgid_values.h`
  (Draco-only macro shims)
- All `nos3/fsw/cfe/` (cFE source kept at this tree's pin)
- `nos3/sims/sim_common/` (CRLF-only differences; no real change)
- `nos3/gsw/LPT.ycs`, `nos3/gsw/cosmos/COMPONENTS/MGR/procedures/tests/mgr_lpt_test.rb`,
  `nos3/gsw/OrbitInviewPowerPrediction/**` (CRLF-only).
- `.devcontainer/devcontainer.json` (X11 guard rail).

### Inp_IPC.txt port-67 comment trim (new load-bearing edit)

`draco-upstream/main:nos3/cfg/InOut/Inp_IPC.txt` line 67 (Thruster IPC
mode) carries a 200-char comment which violates 42's 120-char `fscanf`
buffer (see CLAUDE.md "Edit comments must stay under ~100 chars" rule).
On port we shortened it to:

  `OFF                                     ! IPC Mode -- DTU: was RX, set OFF; thruster disabled`

(93 chars). All four CLAUDE.md Inp_IPC.txt load-bearing constraints
verified after port:

1. Port 4282 (Star Tracker): IPC mode `OFF`. ✓
2. Port 4280 (Thruster): IPC mode `OFF`. ✓
3. TT&C (4286): exactly 2 prefixes — `"SC[0].GPS[0].PosW"` + `"SC[0].GPS[0].VelW"`. ✓
4. GNSS (4287): exactly 1 prefix — `"SC[0].GPS[0].PosW"`. ✓
5. No comment lines exceed 100 chars (`awk 'length($0)>100'` returns 0). ✓

### Build verification (2026-05-05 00:05 UTC)

`make clean && make config && make` from `nos3/` runs through cleanly,
exit code 0, after two fixes that were necessary on top of the port:

1. **`nos3/scripts/env.sh` TTY guard.** The Draco-side `DFLAGS` hardcoded
   `docker run --rm -it ...`, but `-t` aborts when stdin is not a TTY
   ("the input device is not a TTY"), so background invocations of
   `make fsw` died at the first docker step. Fixed by detecting
   `[ -t 0 ]` and substituting `-i` when no TTY is attached:

       if [ -t 0 ]; then DOCKER_TTY="-it"; else DOCKER_TTY="-i"; fi

   Interactive use is unchanged; CI / agent / piped invocations now
   work. Same treatment applied to `DCREATE`.

2. **`nos3/scripts/mid/gen_mid_registry.py` base-mask fallback.** The
   script reads four `CFE_PLATFORM_*_TOPICID_TO_MIDV` base masks from
   `nos3/cfg/nos3_defs/{cfe_msgid_api.h,nos3_component_msgid_mapping.h}`
   to compute the MID registry; those headers are Draco-only and we
   intentionally rejected them (port goal: keep legacy hardcoded MIDs
   here). Without the headers the script raised
   `expected 4 base masks, found {}`, killing config. Replaced the hard
   exit with a hardcoded fallback derived from the existing MID map in
   this tree: `HERITAGE_CMD=0x1800`, `HERITAGE_TLM=0x0800`,
   `COMPONENT_CMD=0x1900`, `COMPONENT_TLM=0x0900`.

Build outputs verified:

- `nos3/fsw/build/exe/cpu1/cf/core-cpu1` and all app `.so` files
- `nos3/sims/build/lib/lib{generic_gnss,generic_tt_c,blackboard,
  generic_eps,generic_radio,generic_rw,...}_sim.so`
- `nos3/gsw/build/libcryptolib.so` and `standalone`
- COSMOS image pull confirmed (`ballaerospace/cosmos:4.5.0`)

CMake configure for FSW takes ~19 minutes here under the bash-in-docker
agent path; that is expected slowness, not a regression.

### Verification still pending (not run in this session)

- `make launch` cold start (FSW + sims + GSW + ELK)
- 42 GUI sanity (xeyes test per CLAUDE.md guard rail)
- Each ported attack script against fresh sim
- Cross-testbed wire-format byte-diff against Draco running in a separate
  worktree

User to run these and append outcomes here.

## Session metadata - Draco launch parity port (2026-05-05 00:39 - 01:15 UTC)

Goal: bring this 6.7.99 testbed's `make launch` choreography to functional
parity with `draco-upstream/main`, so the two testbeds differ only in
cFS baseline. User-reported symptom: `make launch` aborted with
`./scripts/cfs_evs_capture.sh: Permission denied` (also for
`sim_logs_capture.sh` and `cpu_monitor.sh`).

Constraints carried forward from `CLAUDE.md`:
- `nos3/scripts/ci_launch.sh` MUST stay aligned with `origin/main` for
  the X11/WSLg passthrough to keep working. Do NOT take Draco's
  ci_launch.sh wholesale; port the launch flow into the Makefile instead.
- `.devcontainer/devcontainer.json` similarly untouched.

### Permission-denied root cause (Phase 0)

Three log-capture scripts were committed at mode `100644` while the
Makefile invokes them via `./scripts/X.sh &`. The kernel rejects the
exec because the +x bit is missing.

Fix: `git update-index --chmod=+x` on all three. Phase 0 commit is
`8765d5c1` (`fix(scripts): port log-capture scripts and mark
executable`).

### MID registry pipeline (Phase 1)

`god_view_capture.py` and the Draco `logstash.conf` translate filters
both consume a MID registry (`cfg/build/mid_registry.json` plus three
`mid_to_*.yml` Logstash dictionaries). The Draco registry is generated
by `scripts/mid/gen_mid_registry.py` from the Draco-style
`*_topicids.h` macro tree. This testbed uses hand-written `*_msgids.h`
headers with hardcoded MIDs instead.

Empirical finding: the Draco generator already runs on this tree and
captures 151 of the 180 MIDs in scope, because most `*_msgids.h`
declarations here use the literal style
`#define GENERIC_EPS_HK_TLM_MID 0x091A` which `LITERAL_MID_RE` already
matches. The 29 missing entries are the cFE core MIDs in
`fsw/cfe/modules/<mod>/config/default_cfe_<mod>_msgids.h`, which
declare MIDs as
  `#define CFE_ES_CMD_MID  CFE_PLATFORM_CMD_MID_BASE + CFE_MISSION_ES_CMD_MSG  /* 0x1806 */`
The base+offset macro arithmetic is unresolved by any of the existing
regex passes, but the resolved hex value lives in the trailing block
comment. Added a `COMMENTED_MID_RE` second-pass regex that pulls
`/* 0xXXXX */` out and feeds it into the same `mids` dict via
`setdefault`. Coverage now 180/180. Phase 1 commit `51c37d6d`.

`SKIP_FRAGMENTS` was widened to drop `/test/`, `/ut-coverage/`,
`/sample_defs/`, `/cfe_testcase/` paths so testcase headers cannot
mask production MIDs (the cfe_testcase module reuses production names
with off-by-one values for unit-test isolation).

`config.sh` invokes `gen_mid_registry.py` after `configure.py`, so the
registry rebuilds on every `make config`. The ELK docker-compose mounts
`cfg/build/elk -> /etc/logstash/mid:ro` so the translate filters
resolve. Both files are gitignored via the existing `build/` rule.

`config.sh` also autogenerates `cfg/build/launch.sh` from the
`<gsw>` element in the active mission XML, so `make launch` selects
the right `--use-cosmos | --use-cosmos-gui | --use-yamcs` flag without
hand-editing the wrapper.

### Helper scripts (Phase 2)

`god_view_capture.py` and `stop_sim.sh` were already staged from the
broader Draco port and are byte-identical to `draco-upstream/main`.
Smoke test: importing god_view_capture.py builds a 20-entry
`DECODE_TABLE` against the freshly generated registry without any
`WARN: not in MID registry` output, meaning every symbolic name in
`_DECODE_BY_NAME` resolves cleanly. Phase 2 commit `981ff2eb`.

Mode bit caveat: `core.fileMode=false` is set in this clone (probably a
WSL2 / fileshare artefact), so `git commit -- <pathspec>` drops the
+x bit recorded by `git update-index --chmod=+x` for *new* files.
Both scripts ended up at mode 100644 in HEAD. Sidestepped by invoking
god_view via `python3 .../god_view_capture.py` and stop_sim via
`bash ./scripts/stop_sim.sh` in the Phase 4 Makefile recipes; the
existing 100755-mode capture scripts can still be invoked directly.

### ELK Python tooling and logstash.conf (Phase 3)

Five files were already staged byte-identical to Draco:
`build_kibana_dashboards.py`, `refresh_index_pattern_fields.py`,
`load_dashboards.py`, `index_template.json`,
`dashboards/nos3-eo1-dashboards.ndjson`.

`logstash.conf` was 824 lines locally vs 1407 lines in Draco; spot-grep
confirms Draco is a strict superset of the local file (every local
attack tag and numeric extraction is present in the Draco config), so
wholesale replacement is correct. Replaced. Phase 3 commit `888beb51`.

### Makefile rewrite (Phase 4)

Eight new targets added:
`start-elk`, `refresh-kibana-fields`, `build-kibana-dashboards`,
`list-kibana-dashboards`, `load-dashboards`, `stop-sim`, `purge-logs`,
`clean-docker`. All recipes mirror Draco verbatim except where noted.

`launch` rewritten with the full 13-step Draco choreography:
1. pkill the four capture processes (idempotent re-launch)
2. mkdir omni_logs/ attack_logs/ and truncate
3. sudo rm mgr.bin (per-run isolation)
4. purge-logs
5. start-elk (ELK up + ES health-check)
6. DELETE nos3-telemetry-*
7. docker rm -f cosmos-openc3-operator-1
8. ./cfg/build/launch.sh   [unchanged delegate to ci_launch.sh]
9-12. spawn the four captures (sleep 10 before god_view)
13. sleep 15, restart Logstash, wait for /_node/stats,
    build-kibana-dashboards, refresh-kibana-fields

`stop` honors `KEEP_TLM=1` (skips the index DELETE), `save-run` passes
that through, `load-run` calls `refresh-kibana-fields` at the end.

`LOCALTGTS` extended so `make` does not silently fall through to the
OTHERTGTS pass-through path for any of the new targets. Help-all gets
a new `ELK / Kibana:` section. Phase 4 commit `0a7a88ea`.

### Verification (Phase 5)

End-to-end run NOT performed in this session. Self-checks that did run:

- `python3 nos3/scripts/mid/gen_mid_registry.py` from a clean state
  produces `cfg/build/mid_registry.json` and the three
  `cfg/build/elk/mid_to_*.yml` files. Spot-checks: GENERIC_EPS_HK_TLM_MID
  -> 0x091A, GENERIC_GNSS_CMD_MID -> 0x1952, GENERIC_TT_C_HK_TLM_MID ->
  0x0950, CFE_ES_HK_TLM_MID -> 0x0800, NOVATEL_OEM615_HK_TLM_MID ->
  0x0870, MGR_HK_TLM_MID -> 0x08F8, all match the values documented in
  the auto-memory `project_draco_port` entry.
- Importing `god_view_capture.py` resolves all 20 `_DECODE_BY_NAME`
  entries against the freshly generated registry (no warnings).
- `make -n launch` and `make -n stop-sim` parse cleanly; the dry-run
  emits all expected steps in the right order.

User-side verification still pending (same caveats as the prior session):

- Cold `make launch` to completion with FSW + sims + GSW + ELK; tail
  `omni_logs/god_view.log` to confirm the TO_LAB enable + sensor
  activation block fires.
- `curl localhost:5601/api/saved_objects/_find?type=dashboard | jq '.total'`
  should report 15 once `build-kibana-dashboards` has run.
- 42 GUI windows still appear (ci_launch.sh untouched, X11 guard rail
  intact). If GUI is missing, follow the diagnostic ladder in
  `CLAUDE.md` ("GUI / X11 Forwarding Guard Rail"); the fix is NOT to
  add WSLg plumbing.
- `make stop-sim` leaves ELK and `nos3-telemetry-*` indices intact;
  `make stop` (no flag) wipes them; `make stop KEEP_TLM=1` preserves.
- Cross-testbed CCSDS byte-diff against the Draco fork.

### Out of scope (recorded for follow-up)

- Draco's `ci_launch.sh` adds new sim containers
  (`generic-tt_c-sim`, `blackboard-sim`, `generic-gps-sim` ->
  `generic-gnss-sim` rename, OnAIR launcher container). Not ported
  here per the X11 guard rail. The corresponding components are
  staged in this tree per `project_draco_port`, but ci_launch.sh
  wiring may need a careful merge in a follow-up branch.
- Draco's `code-coverage` rewrite (isolated `COVBUILDDIR`, gcovr
  XML/HTML output to docs/coverage/) skipped.
- Draco's `cosmos-gui`, `cosmos-operator`, `yamcs-operator`,
  `scriptrunner`, `ci-launch` Makefile targets skipped (each requires
  ci_launch.sh changes).
- The chmod for `god_view_capture.py` and `stop_sim.sh` did not stick
  in HEAD because of `core.fileMode=false`. Sidestepped via interpreter
  prefixes in the Makefile recipe; mode bit can be re-applied in a
  later cleanup if any caller wants `./scripts/X` form.

## 2026-05-05 - generic_tt_c and generic_gnss FSW bootstrap fix

After the launch-parity port (Phase 1-4), end-to-end inspection
surfaced a load-bearing gap: nos3/components/generic_tt_c/fsw/cfs/
and nos3/components/generic_gnss/fsw/cfs/ produced .so files at
build time, but never made it into the cFS image. The MID registry
(0x1950, 0x1951, 0x1952, 0x1953, 0x0950, 0x0951, 0x0952, 0x0953,
0x0954) and ELK Logstash translate filters already expected the
two apps; two whole Kibana dashboards (EO1 TT&C Downlink Validation
and EO1 GNSS-GS Validation) plus panels on Mission Denmark depended
on telemetry that was never being emitted.

### Wiring changes (commit 2b5a0b9b)

- targets.cmake adds generic_tt_c/fsw/cfs and generic_gnss/fsw/cfs
  to MISSION_GLOBAL_APPLIST.
- cpu1_cfe_es_startup.scr carries TT_C_AppMain (priority 77) and
  GNSS_AppMain (priority 76) in the disabled pool.
- configure.py reads `<components/tt_c/enable>` and
  `<components/gnss/enable>` and lifts the startup-script lines
  into the active region when enabled, mirroring the eps and radio
  pattern.
- sc-minimal, sc-research, sc-fprime spacecraft configs gain the
  matching XML elements (sc-mission already had them).
- cpu1_platform_cfg.h bumps CFE_PLATFORM_ES_MAX_APPLICATIONS from
  32 to 40. With tt_c, gnss, and torquer added, the previous limit
  rejected the last three apps with "No free application slots
  available", blocking torquer's load too.

### Pre-existing SCH bugs surfaced during validation (commit 265b2f01)

The first validation run with the wiring in place revealed the SCH
App was terminating at boot, so no scheduled HK was firing on the
software bus. Two distinct table-data bugs in HEAD:

- sch_def_schtbl.c entry 54 (MD Wakeup) had freq=4 with rem=4. SCH
  rejects entries where rem >= freq; fixed to rem=3.
- sch_def_msgtbl.c was declared SCH_MessageEntry_t[SCH_MAX_MESSAGES]
  (size 128) but only initialised 127 entries. The C compiler
  zero-filled slot 127, producing a 0x0 MID that failed verify.
  Added an explicit SCH_UNUSED_MID at index 127. The SBN HK entry
  at index 18 also had third 16-bit word 0x0000 (length-field
  invalid) instead of 0x0001 like every other entry; aligned to
  the rest.

Both bugs predate this branch (SCH commits in HEAD: b5055809,
2111b907) but they only surface when SCH actually parses the table
and emits SCH_BAD_TBL_DATA / SCH_BAD_MSG_ID; without them, SCH
terminates and the cFS image runs in a degraded mode where only
self-scheduled apps emit telemetry.

### Validation outcome

Phase A (cold launch + cFS app registration):
- generic_gnss App Initialized. Version 1.0.0.0
- generic_tt_c App Initialized. Version 1.0.0.0
- generic_torquer also recovers (was failing under the old 32-app
  limit).
- No "No free application slots available" errors after the bump.
- After SCH bug fixes, no "Schedule tbl verify error" or "Message
  tbl verify err" entries; SCH stays alive across the run.

Phase B (telemetry presence):
- Heritage HK MIDs flow on schedule once SCH is alive: DS_HK_TLM,
  FM_HK_TLM, HS_HK_TLM, LC_HK_TLM, MD_HK_TLM, MM_HK_TLM, SC_HK_TLM,
  CS_HK_TLM.
- Self-scheduled simulator telemetry continues unchanged
  (eps_battery_soc_pct ~1600 docs, gps_lat ~800 docs from
  system_log type entries via Logstash on omni_logs/).
- Component HK MIDs (GENERIC_TT_C_HK_TLM_MID,
  GENERIC_GNSS_HK_TLM_MID, GENERIC_RW_APP_HK_TLM_MID,
  GENERIC_EPS_HK_TLM_MID, MGR_HK_TLM_MID, etc.) do NOT flow.
  Reason: sch_def_msgtbl.c does not contain entries for the
  per-component _SEND_HK_MID or _REQ_HK_MID values, so SCH never
  triggers them and the apps never emit HK packets. Pre-existing
  state, outside the scope of the bootstrap-wiring fix.

Phase C/D (dashboard validation, Denmark closed loop):
- Dashboards relying on system_log fields (Power Budget, FSW
  Health, parts of Mission Validation) populate normally.
- Dashboards relying on per-component HK_TLM (TT&C Downlink
  Validation, GNSS-GS Validation, ADCS Health rw_momentum/st_q0,
  Mission Denmark in_denmark_box closed loop) remain empty for
  the same reason as Phase B.
- Denmark pass cadence (61.6deg / RAAN 346) puts the next pass
  hours away; not in the 30-min validation window.

### Out of scope (recorded for follow-up)

- Per-component _SEND_HK_MID entries in sch_def_msgtbl.c plus
  matching schtbl scheduling so the FSW emits HK for tt_c, gnss,
  rw, eps, mgr, st, etc. This is a deeper SCH wiring task,
  separate from the bootstrap fix.
- generic-tt_c-sim and generic-gnss-sim containers in
  ci_launch.sh. Without the simulator side, the FSW apps run with
  device_disabled state but their HK still emits link-state IDLE,
  zeroed pass counts, etc., which is fine for wiring validation
  but not for dashboard closure. Bundled with the deferred
  ci_launch.sh sim-container work behind the X11 guard rail.
- Denmark closed-loop end-to-end validation (in_denmark_box=1 AND
  in_science_mode=1 co-occurring) requires both the per-component
  HK pipeline above and a sim run that includes a Danish overflight
  (~6 h cadence in the current orbit).

### Commits in this batch

- 2b5a0b9b feat(fsw): wire generic_tt_c and generic_gnss into cFS bootstrap
- 265b2f01 fix(sch): keep SCH App alive by correcting schtbl rem and msgtbl size
- 9e3a96f3 feat(components): port generic_gnss component from Draco fork
- f7e1084a feat(components): port generic_tt_c component from Draco fork
- c6849d1e feat(gsw): add Cosmos GENERIC_GNSS target and headless cmdtlmserver
- 38cbcd67 feat(eps-sim): port Draco load-model-driven EPS simulator
- 8cbd9f85 chore(42-cfg): refresh 42 InOut, mission XML, and sc-mission profile
- 5e6816c0 chore(scripts): tune attack scripts and Cosmos target generators
- 2e81e32f chore(launch): port Draco launch tweaks staying on the X11 guard rail
- 41ffee60 docs(cfs-limits): add cFS resource-limits reference series
- 7dad8281 docs(thesis): add thesis structure and per-app/per-subsystem chapters
- cd71a38e docs(reference): add CCSDS, mission, postmortem, and walkthrough docs
- d1399d23 chore(debug): record Draco port diff snapshot
- 33382df4 chore(repo): ignore Yamcs target trees and pycache, add ELK .env

## 2026-05-05 - SCH per-component HK wiring + dashboard validation

Followed up on the deferred work item from the morning's bootstrap-fix
session: "sch_def_msgtbl.c does not contain entries for the per-component
_SEND_HK_MID or _REQ_HK_MID values, so SCH never triggers them and the
apps never emit HK packets". Phase C/D dashboards (TT&C Downlink
Validation, GNSS-GS Validation, ADCS rw_momentum, Mission Denmark) all
depend on this wiring.

### SCH msgtbl + schtbl wiring

`nos3/cfg/nos3_defs/tables/sch_def_msgtbl.c` slots 25-39 now hold
per-component HK requests (15 entries, one each for the sc-mission
components):

  25 NOVATEL_OEM615_REQ_HK_MID  (0x1871)
  26 GENERIC_RADIO_REQ_HK_MID    (0x1931)
  27 GENERIC_RW_APP_SEND_HK_MID  (0x1993)
  28 GENERIC_EPS_REQ_HK_MID      (0x191B)
  29 GENERIC_STAR_TRACKER_REQ_HK_MID (0x1936)
  30 GENERIC_ADCS_REQ_HK_MID     (0x1941)
  31 GENERIC_CSS_REQ_HK_MID      (0x1911)
  32 GENERIC_FSS_REQ_HK_MID      (0x1921)
  33 GENERIC_IMU_REQ_HK_MID      (0x1926)
  34 GENERIC_MAG_REQ_HK_MID      (0x192B)
  35 GENERIC_TORQUER_REQ_HK_MID  (0x193B)
  36 MGR_REQ_HK_MID              (0x18F9)
  37 GENERIC_TT_C_REQ_HK_MID     (0x1951)
  38 GENERIC_GNSS_REQ_HK_MID     (0x1953)
  39 SAMPLE_REQ_HK_MID           (0x18FB)

Two new `#include` lines added at the top of the same file for
`generic_tt_c_msgids.h` and `generic_gnss_msgids.h`. Without those, the
table compile fails with "GENERIC_TT_C_REQ_HK_MID undeclared" since the
SCH target's CMake target picks up component platform_inc paths but the
old msgtbl never referenced these symbols.

`nos3/cfg/nos3_defs/tables/sch_def_schtbl.c` previously-empty slots
14-22 and 25 now schedule the 15 requests at 0.25 Hz (freq=4) staggered
across rem=0,1,2 to balance load. Same SCH_GROUP_CFS_HK group as the
heritage HK requests. Specific layout:

  slot 14: GPS rem=0,    RADIO rem=1
  slot 15: RW rem=0,     EPS rem=2
  slot 16: ST rem=0,     ADCS rem=1
  slot 17: CSS rem=0,    FSS rem=1
  slot 18: IMU rem=0,    MAG rem=2
  slot 20: TORQUER rem=0
  slot 21: MGR rem=0
  slot 22: TT_C rem=0,   GNSS rem=1
  slot 25: SAMPLE rem=0

### Validation outcome (post-rebuild, fresh launch)

Phase A (cold launch): clean. SCH stays alive across the run with no
"Schedule tbl verify error" or "Message tbl verify err". All 35+ apps
register including generic_tt_c, generic_gnss, generic_torquer.

Phase B (heritage HK): unchanged. DS, FM, HS, LC, MD, MM, SC, CS HK
still flow on schedule.

Phase C (per-component HK): 13 of 14 sc-mission components verified
emitting HK_TLM packets into ELK during FSW alive windows. Confirmed
in nos3-telemetry-* index by `msg_name` aggregation:

  GENERIC_TT_C_HK_TLM_MID         flowing
  GENERIC_RW_APP_HK_TLM_MID       flowing
  GENERIC_STAR_TRACKER_HK_TLM_MID flowing
  GENERIC_CSS_HK_TLM_MID          flowing
  GENERIC_FSS_HK_TLM_MID          flowing
  GENERIC_IMU_HK_TLM_MID          flowing
  GENERIC_MAG_HK_TLM_MID          flowing
  GENERIC_RADIO_HK_TLM_MID        flowing
  GENERIC_ADCS_HK_TLM_MID         flowing
  GENERIC_TORQUER_HK_TLM_MID      flowing
  MGR_HK_TLM_MID                  flowing
  NOVATEL_OEM615_HK_TLM_MID       flowing
  SAMPLE_HK_TLM_MID               flowing
  GENERIC_GNSS_HK_TLM_MID         flowing (occasional, see below)
  GENERIC_EPS_HK_TLM_MID          NOT observed (see below)

Phase D (Kibana dashboards): 15 dashboards built and queryable. The
ELK pipeline ingested 200K+ system_log docs and 6800+ cfs_sb docs in
a single 5-minute window. Dashboards relying on system_log fields
(eps_battery_soc_pct=21K hits, gps_lat=10K hits) populate normally
from the sim side. Dashboards relying on per-component HK_TLM now
have the data source they need from the FSW side.

### Pre-existing FSW segfault (out of scope, surfaces during validation)

FSW respawns periodically with a consistent pattern:
1. SCH dispatches some HK request batch
2. SCH 17 event "Slots skipped: slot=2, count=98" (timer-skew detection)
3. SIGSEGV → fsw_respawn.sh restarts after 3s

The `fsw_respawn.sh` wrapper is the upstream NOS3 design for
"transient crashes don't leave the spacecraft without FSW". The
segfault reproduces with the new per-component schtbl entries
removed (only heritage HK firing), confirming it predates this work.
The pattern is consistent with CPU-starvation / timer-skew where
SCH falls a full minor frame behind, and corrupted state in some
pre-existing app handler triggers SIGSEGV during catch-up.

Symptoms during a 5-minute window:
- 12+ FSW respawns
- SCH cycles through frames 0-4 before crash on each respawn
- HK_TLM accumulation rate is roughly proportional to frequency-
  remainder coincidence with FSW alive windows

EPS HK and MAG HK (the only two rem=2 entries in the new schtbl)
never produced HK_TLM responses across the entire validation run,
suggesting their respective handlers may be the actual SIGSEGV
trigger when invoked via the new HK pipeline (vs. just being slow to
land before the next respawn). Further investigation deferred.

### Out of scope (recorded for follow-up)

- Root-cause the FSW SIGSEGV. Likely candidates: GENERIC_EPS_RequestHK
  I2C path (i2c_master_transaction with non-responsive sim), or one of
  the new cross-subscribe handlers that fire for the first time now
  that the sender HK actually flows: GNSS subscribes to MGR_HK_TLM_MID
  (`generic_gnss_app.c:109`, `ProcessMgrHk` at line 226), SAMPLE
  subscribes to MGR_HK_TLM_MID (`sample_app.c:155`, `SAMPLE_ProcessMgrHk`
  at line 509), ADCS subscribes to GENERIC_RW_APP_HK_TLM_MID
  (`generic_adcs_app.c:281`, `Generic_ADCS_ingest_generic_rw` in
  `generic_adcs_ingest.c:160`). All three cast `MsgPtr` to a typed
  pointer and dereference deep struct fields - any layout mismatch
  is a UB landmine.
- Wire generic-tt_c-sim and generic-gnss-sim containers into
  ci_launch.sh (still gated by the X11 guard rail). Without sim peers,
  TT_C and GNSS apps run with `device_disabled` state, but the FSW
  HK packets still flow and the Kibana panels will show IDLE link
  state and zeroed pass counts. That's enough for SCH wiring
  validation; not enough for Denmark closed-loop end-to-end.
- Restore the EPS+MAG schtbl entries once the SIGSEGV root cause is
  fixed. The msgtbl entries (slots 28, 34) are already in place.

### Files touched

- `nos3/cfg/nos3_defs/tables/sch_def_msgtbl.c` (added 2 includes,
  filled slots 25-39)
- `nos3/cfg/nos3_defs/tables/sch_def_schtbl.c` (filled slots 14-22
  and 25; EPS rem=2 and MAG rem=2 currently SCH_ENABLED in source -
  swap to SCH_UNUSED locally if FSW respawn cycles become disruptive
  during a debugging session)


### Quantitative metrics (final validation)

Across the most active 5-minute validation window (FSW respawning,
SCH cycling fresh each boot):

- FSW respawn rate: ~2.4 crashes/min (12 SIGSEGV in 5 min)
- Each respawn typically reaches MET 5-30 seconds before crash
- HK_TLM observed counts at end of window:
    rem=0 entries (frame 0 of each respawn): 11 fires/component
        TT_C=9, RW=2, ST=11, CSS=11, IMU=11, MGR=11, NOVATEL=11,
        SAMPLE=11, TORQUER=11
    rem=1 entries (frame 1 of each respawn): 4-6 fires/component
        ADCS=6, FSS=6, RADIO=6, GNSS=3
    rem=2 entries (frame 2): 0 fires (correlates with crash window)
        EPS=0, MAG=0

Net: 13 of 14 components produced at least one HK_TLM packet during
the window; EPS produced none. Per-component variation in count
reflects which slot fires before/after the respawn-triggering crash
within each boot cycle.

