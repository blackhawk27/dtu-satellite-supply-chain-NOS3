# Build and load: bootstrapping the pipeline on a fresh clone

This document covers how the ELK pipeline is brought up on a
clone, how it is rebuilt when something is broken, and how
archived runs are loaded back into Kibana. It complements
[../01-reproducibility.md](../01-reproducibility.md) Phase 5
(the launch-time orchestration) and Phase 7 (save and load run)
with the per-component detail.

## What gets created where

The pipeline has four classes of persistent state, each with a
different lifetime:

- **Source files in the repository.** The Logstash pipeline
  (`nos3/elk/logstash.conf`), the index template
  (`nos3/elk/index_template.json`), the dashboard builder
  (`nos3/elk/build_kibana_dashboards.py`), the field-cache
  refresh helper (`nos3/elk/refresh_index_pattern_fields.py`),
  and the seed MID registry (`nos3/elk/seed_mid/*.yml`). These
  travel with the repository.
- **Build-time configuration.** `make config` writes
  `nos3/cfg/build/elk/mid_to_name.yml`,
  `mid_to_layer.yml`, and `mid_to_subsystem.yml`. These are
  consumed by Logstash through the bind mount declared in
  `nos3/elk/docker-compose.yml`. They are regenerated on every
  `make config`; if the regenerator fails the `make start-elk`
  fallback copies the seed files into place.
- **Per-run data.** The four capture files
  (`attack_logs/cfs_god_view.json`,
  `omni_logs/cfs_evs.log`, `omni_logs/nos3-*.log`,
  `omni_logs/cpu_monitor.log`, plus the Layer 3 and Layer 4
  files `tlm_hk_decoded.log` and `gnss_uplinks.log`). Wiped at
  the start of every `make launch`; archived by
  `make save-run`.
- **Container state.** The Elasticsearch data volume
  (`es_data_vol`), the in-memory Kibana saved objects, and the
  Logstash sincedb. The data volume survives
  `docker compose down`; the indices it stores are wiped by
  `make stop` unless `KEEP_TLM=1` is set; the saved objects
  are rebuilt on every `make launch`; sincedb is set to
  `/dev/null` so it does not survive a container restart.

The four lifetimes intersect at `make launch`, which is the
single chokepoint at which the pipeline transitions from
"installed" to "running".

## Cold-start sequence

The first `make launch` after a clone runs through the
following sub-steps, in order. The Makefile target
orchestrates this; the description below names each step so
that a failure at any of them can be identified.

1. **`make start-elk`.** Creates the `nos3-core` Docker network
   if it does not exist, ensures the MID-registry YAMLs are
   present (regenerating them, or falling back to the seed),
   brings up the three ELK containers from
   `nos3/elk/docker-compose.yml`, and waits up to forty seconds
   for Elasticsearch to leave the red cluster state. The
   fallback to the seed is the single most common operational
   step: `scripts/mid/gen_mid_registry.py` is the upstream
   generator and silently fails on this cFE because its grep
   targets Draco-era topic macros, so `nos3/Makefile:375-384`
   copies the YAMLs from `nos3/elk/seed_mid/` into
   `cfg/build/elk/` instead.
2. **Index delete.** `make launch` deletes every
   `nos3-telemetry-*` index so the new run starts against an
   empty store. If a previous run was archived through
   `make save-run` (which sets `KEEP_TLM=1`), the archived
   indices are preserved.
3. **Simulator and FSW launch.** `scripts/ci_launch.sh`. Out of
   scope for this document; the pipeline does not depend on
   it succeeding, but its outputs are what feed the four
   capture files.
4. **Capture-script attach.** `make launch` starts the four
   host-side capture processes
   (`god_view_capture.py`, `cfs_evs_capture.sh`,
   `sim_logs_capture.sh`, `cpu_monitor.sh`) in the
   background. The Layer 3 and Layer 4 files are produced as
   side effects of the same processes; nothing additional has
   to be started.
5. **Logstash restart.** Logstash is restarted so it picks up
   the freshly created log files. The pipeline's
   `sincedb_path => "/dev/null"` setting means it reads each
   file from the beginning, and the index template's
   `refresh_interval => 5s` means the first documents become
   visible in Kibana roughly five seconds later.
6. **Dashboard build.** `make build-kibana-dashboards` runs
   `elk/build_kibana_dashboards.py` against the Kibana
   Saved Objects API. The script creates the
   `nos3-telemetry*` index pattern if it does not exist,
   imports or updates each saved object in its `REGISTRY`,
   and prunes any orphan dashboards (saved objects in
   Kibana that are not in `REGISTRY`).
7. **Field-cache refresh.** `make refresh-kibana-fields` runs
   `elk/refresh_index_pattern_fields.py` to rewrite the
   index pattern's cached field list. Without this, panels
   that filter on freshly-added fields render "No data".

The sequence is deliberately idempotent. A re-run of
`make launch` while the stack is up reuses the existing ELK
containers, restarts the capture scripts, deletes the
`nos3-telemetry-*` indices, restarts Logstash, and rebuilds the
dashboards. Nothing requires a `docker compose down` to
recover.

## The MID-registry seed fallback

The seed under `nos3/elk/seed_mid/` is the file set that the
load-bearing edit at the top of `make start-elk` copies in when
the generator fails. The three files are:

- `mid_to_name.yml`: MID hex string to human-readable name
  (`"0x091A": "GENERIC_EPS_HK_TLM_MID"`).
- `mid_to_layer.yml`: MID to taxonomy layer
  (`FSW_CORE`, `FSW_HERITAGE`, `COMPONENT`, `RESEARCH`,
  `UNKNOWN`).
- `mid_to_subsystem.yml`: MID to subsystem name
  (`EPS`, `GPS`, `ADCS`, `MGR`, and so on).

The seed was generated by `scripts/mid/gen_mid_registry.py`
against an earlier cFE that still emitted the expected
`CFE_PLATFORM_*_TOPICID_TO_MIDV` macros. Subsequent cFE updates
removed those macros without updating the generator, so the
generator silently fails on this cFE and the seed has been
checked in as the long-term fallback. The seed will go stale
relative to new component MIDs; the recovery is to either fix
the generator or update the seed by hand. The seeding logic
itself is at `nos3/Makefile:375-384` and is load-bearing for
fresh-clone launches (without it, Logstash crashes on startup
because the `translate` filters cannot open the dictionaries).

## Rebuilding dashboards selectively

The dashboard builder accepts two flags that the Makefile
exposes:

- `make build-kibana-dashboards DASHBOARDS=id1,id2`. Rebuild
  only the named saved objects. The IDs match the first
  element of each `REGISTRY` entry; the full list is printable
  with `make list-kibana-dashboards`.
- `make build-kibana-dashboards SKIP=id1,id2`. Rebuild every
  saved object except the named ones.

The two flags are mutually exclusive at the Python level
(`cli_filter` errors out if both are set). The Makefile passes
whichever is non-empty; the verification command
`make list-kibana-dashboards` prints the canonical catalogue
along with a label for each entry.

The legacy `make load-dashboards` target imports a checked-in
`elk/dashboards/nos3-eo1-dashboards.ndjson` through Kibana's
`_import` endpoint. It is retained for backwards compatibility
but is not the source of truth; the Python builder
(`build_kibana_dashboards.py`) owns every saved object in
production.

## Loading an archived run

`make save-run` produces a directory under
`$HOME/nos3_runs/<label>/` containing the four capture files
and a `metadata.txt` header. `make load-run RUN=<label>` is
the reverse:

1. Wipes `omni_logs/*.log` and copies the archived versions
   back in.
2. Copies `cfs_god_view.json` back into `attack_logs/`.
3. Deletes every `nos3-telemetry-*` index in Elasticsearch.
4. Restarts Logstash so the pipeline re-reads the restored
   files from the beginning.
5. Refreshes the index-pattern field cache.

The target deliberately does not start the simulator. The
intent is "make a previously-recorded run inspectable in
Kibana on this machine"; the run is fully described by the
archived files. The verification commands from
[../01-reproducibility.md](../01-reproducibility.md) Phase 6
apply: the `_count` queries against `software_bus` and
`system_log` must return non-zero within roughly thirty
seconds of `make load-run` completing.

## Diagnostics: `make doctor`

`make doctor` runs `scripts/elk_doctor.sh`, a single-purpose
diagnostic that walks the entire chain layer by layer and
reports `PASS` or `FAIL` plus a one-line fix-suggestion for
each. The layers it checks, in order:

0. **Build-time prerequisites.** Are the MID-registry YAMLs
   under `cfg/build/elk/` present and non-empty.
1. **Containers.** Are `nos3-elasticsearch`, `nos3-logstash`,
   and `nos3-kibana` running.
2. **Capture processes.** Are the four host-side capture
   scripts running.
3. **Log file sizes.** Are the four capture files non-empty.
4. **Elasticsearch.** Is the cluster reachable and is at
   least one `nos3-telemetry-*` index present.
5. **Kibana data view.** Is the `nos3-telemetry*` index
   pattern saved object present.
6. **Dashboards.** Is the catalogue from `REGISTRY` fully
   present in Kibana.

The exit code is the number of failing layers, so a `make
doctor` exit of `2` means two layers are wrong; the operator
reads the per-layer FAIL lines to identify which. The script
is side-effect-free and safe to run any time.

The "build-time prerequisites" layer is the first check
because a missing MID-registry YAML masquerades as a "no
nos3-telemetry-* index" symptom three layers later (Logstash
crashes on startup with "File does not exist or cannot be
opened"); putting it first makes the actual cause visible.
