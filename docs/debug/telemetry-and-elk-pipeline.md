# Telemetry capture and the ELK pipeline

Getting numbers into Kibana has three links: the sims must emit to their
stdout logs, the host capture scripts must collect those logs and the
downlink, and Logstash must parse them with the right MID dictionaries.
A break in any link shows up as dark or partial dashboards.

The shared `nos3-legacy-core` Docker network must exist before
starting ELK. `scripts/ci_launch.sh` creates it via `$NETWORK_NAME`
and `make start-elk` also ensures it, so the normal path handles
this for you; only if you bring the stack up by hand do you need
`docker network create nos3-legacy-core`.

## sim_logs_capture container-name drift left logs empty

- Symptom: `omni_logs/nos3-*.log` were 60-byte files containing only
  `Error response from daemon: No such container: sc01-eps-sim`. Every
  dashboard panel reading `subsystem:nos3-eps` (or `nos3-gps`, `nos3-imu`,
  and so on) was dark.
- Root cause: `scripts/sim_logs_capture.sh` hard-coded container names like
  `sc01-eps-sim` and `sc01-rw-sim0`, but `ci_launch.sh` creates sim
  containers as `sc01_<sim-name>` (underscore prefix, hyphens within the
  name). The match never fired. The set was also incomplete, missing
  torquer, blackboard, truth42, css, radio, tt_c, and gnss.
- Fix: replace the SIMS map with the actual running container names and
  extend it to the full set (eps, fss, css, imu, gps, mag, startrk, radio,
  torquer, rw0/1/2, tt_c, gnss, blackboard, truth42). A comment in the file
  warns about the `sc01_` vs `sc01-` distinction. After the fix each log
  went from 60 B to 100 KB+ within seconds and Logstash began extracting
  per-sim fields (`gps_lat`, `eps_battery_wh`, `gnss_solution_type`, etc.).

## Logstash MID dictionaries and dashboards

- Symptom: with FSW alive and 42 publishing, most of the dashboards came
  up dark or partial.
- Root cause: two gaps. First, Logstash inlined a 138-entry MID dictionary,
  but `cfs_god_view.json` now emits 180+ MIDs (TT_C, GNSS, MGR,
  NOVATEL_DEVICE_TLM, SB_ALLSUBS, TIME_DATA, and others) that flowed into
  Elasticsearch as `msg_name=UNKNOWN_MSG` / `subsystem=UNKNOWN`, so any panel
  filtered by `subsystem.keyword:TT_C` or `:GNSS` returned zero docs.
  Second, only 6 of the 15 named dashboards existed as Kibana saved objects;
  the other 9 lived in
  `nos3/elk/dashboards/nos3-eo1-dashboards.ndjson` and had never been
  imported into this testbed's Kibana volume.
- Fix: `nos3/elk/logstash.conf` reads the MID dictionaries from
  `/etc/logstash/mid/mid_to_{name,layer,subsystem}.yml` and adds TT_C and
  GNSS grok extractors (pulling `gnss_{solution_type,num_sats_*,pdop,hdop,
  vdop}` and `tt_c_{elevation_deg,doppler_hz,slant_range_km,pkt_count,...}`).
  The YAMLs ship under `cfg/build/elk/` and stay in sync with
  `cfg/build/mid_registry.json`. `nos3/elk/docker-compose.yml` bind-mounts
  `../cfg/build/elk:/etc/logstash/mid:ro` (read-only, since the YAMLs are
  build output). Dashboards are imported with
  `curl -X POST .../api/saved_objects/_import?overwrite=true --form
  file=@.../nos3-eo1-dashboards.ndjson`, which is idempotent under
  `overwrite=true`.
- Verification on a representative run: `msg_name=UNKNOWN_MSG` count 0 (was
  thousands), populated `gps_lat` field (max altitude ~400 km matching the
  LEO orbit, avg latitude over Denmark), GNSS docs averaging
  `gnss_num_sats_total` near 10 (3-D fix), TT_C docs across HK_TLM and
  REQ_HK MIDs, and all named dashboards present in Kibana.

## Decoded downlink (tlm_hk_decoded.log) empty on a fresh clone (RESOLVED)

- Symptom (historical): telemetry flowed and `attack_logs/cfs_god_view.json`
  filled, but `omni_logs/tlm_hk_decoded.log` stayed empty on a fresh clone
  while it worked on the maintainer's machine.
- Root cause: `scripts/passive_listener.py` builds its decode table from
  `cfg/build/mid_registry.json` (`_load_decode_table`). If that file is
  missing the table is empty, raw packets are still written but nothing is
  decoded, and the table is loaded once at import so the listener must be
  restarted after the registry exists. `cfg/build` is gitignored, and
  `scripts/mid/gen_mid_registry.py` (run at `make config`) used to be broken
  on the current cFE: it greped for Draco-era
  `CFE_PLATFORM_*_TOPICID_TO_MIDV` macros that no longer exist, exited 1, and
  `config.sh` swallowed the failure. So a fresh clone never got
  `cfg/build/mid_registry.json`; the maintainer only had a stale copy that
  persisted from an older run.
- Resolution: `gen_mid_registry.py` is now fixed. The generator was extended
  to resolve MIDs from this cFE's actual definitions: literal hex defines AND
  the classic additive form
  (`<MID> = CFE_PLATFORM_{CMD,TLM}_MID_BASE + CFE_MISSION_*_MSG`). It no
  longer requires the absent `TOPICID_TO_MIDV` base masks (it logs a note and
  continues). It produces the full registry (185 MIDs, a superset of the
  committed seed, including the `noisy_app` MIDs), so a fresh clone now gets
  `cfg/build/mid_registry.json` correctly at `make config` and no longer
  depends on the seed for decoding. The committed
  `nos3/elk/seed_mid/mid_registry.json` remains as a belt-and-suspenders
  fallback (now also refreshed to 185 MIDs), the same pragmatic fallback the
  ELK YAMLs already use, and the Makefile MID-artifact block still copies it
  into `cfg/build/` if the generator ever fails to produce it. Applying this
  to an already-running stack still requires restarting `passive_listener.py`
  (re-run `make launch` / `make cosmos-gui`) so it reloads the table.

## make stop wipes nos3-telemetry-* indices by default

- `make stop` issues `DELETE http://localhost:9203/nos3-telemetry-*` before
  tearing down containers. Kibana dashboards, index patterns, and saved
  searches live in `.kibana_*` system indices and are not touched; saved
  runs under `~/nos3_runs/*` are untouched.
- `make save-run` sets `KEEP_TLM=1` so the save path preserves the indices.
  To keep telemetry without `save-run`, use `make stop KEEP_TLM=1`.
- If Elasticsearch is already down when `stop` runs, the curl silently fails
  (`|| true`). The delete affects only future runs; indices already on disk
  in `elk_es_data_vol` are purged on the next `make stop` while ELK is up.
