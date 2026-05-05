# 05.04 Build and Load Tooling

This document describes the three Python helpers under
`nos3/elk/` that build, load, and refresh the Kibana dashboards
and the data view's field cache. The split into three scripts
reflects three operational situations:

- A new dashboard panel is being authored:
  `build_kibana_dashboards.py`.
- A clean Kibana volume needs the dashboards restored from the
  checked-in NDJSON: `load_dashboards.py`.
- A new index or new field landed and the dashboards render
  "No data": `refresh_index_pattern_fields.py`.

## 1. `build_kibana_dashboards.py`

Roughly 1585 lines. The script's docstring summarises:

> Build the two "Standard" Kibana dashboards for NOS3 and push
> them via the Kibana saved-objects REST API. Re-runs overwrite
> in place.

Key constants (top of file):

```
KIBANA = "http://localhost:5601"
IP = "5b3163a0-3ea7-11f1-adf4-55f5fc5a104a"   # data view
VERSION = "7.17.10"
```

The `IP` constant is the canonical data view ID; every panel
references it. A second list, `OBSOLETE_OBJECTS`, names
saved objects that previous versions of the script created and
that must be deleted from any live Kibana on next run, since
`overwrite=true` does not remove orphans on its own. A
sibling `OBSOLETE_TITLE_SWEEPS` list does the same for
sub-objects (Lens panels, visualisations) by title prefix.

The script's flow:

1. `refresh_data_view_fields(IP, "nos3-telemetry-*")`: pull the
   field list from a live Elasticsearch and write it back to
   the data view's saved object. This is the same operation
   `refresh_index_pattern_fields.py` runs standalone.
2. Sweep the obsolete-objects list, calling `delete_so()` on
   each.
3. Run the title-prefix sweep across types `lens` and
   `visualization`.
4. Emit each panel as a saved object via `put_so()`. The
   build script splits dashboard construction into per-panel
   helpers; the helpers each emit a Lens or Vega definition
   with the data view ID hardcoded.
5. Emit the two dashboard saved objects, each referencing the
   panel saved-object IDs.
6. Export the saved objects to NDJSON and write them to
   `nos3/elk/dashboards/nos3-eo1-dashboards.ndjson`.

The output NDJSON is the artefact the load script reads.

The script communicates with Kibana through `subprocess.run`
calls to `curl` (encapsulated in helpers like `put_so`,
`delete_so`, `get_so`). The reason is purely operational: the
helpers ran against multiple Kibana versions while the testbed
was being stood up, and `curl` produced clearer diagnostics
than the Python `urllib` path.

## 2. `load_dashboards.py`

165 lines. The docstring summarises:

> Load pre-built Kibana dashboards from a checked-in .ndjson
> into a running Kibana instance via the Saved Objects Import
> API.

The reason for two scripts (build vs load):
`build_kibana_dashboards.py` constructs dashboards
programmatically against a live Kibana, but those saved objects
vanish when the Kibana volume is wiped (for example,
`docker compose down -v`). To make the dashboards reproducible
across rebuilds without the developer re-running the build
script every time, the build artefact (the NDJSON) is checked
into the repo and the load script re-imports it on each
launch.

Configurable knobs:

- `KIBANA_URL` (env): defaults to `http://localhost:5601`.
- `DEFAULT_NDJSON`: the dashboards NDJSON, one path level up
  in `dashboards/`.
- `KIBANA_WAIT_SECS` (env, default 60): how long to wait for
  Kibana to become reachable on launch.

Like the build script, `load_dashboards.py` carries its own
copy of `OBSOLETE_OBJECTS` so a stale dashboard saved object
from an earlier ndjson version does not survive a re-import
silently. The two lists must stay in sync; this is documented
in the source.

Exit codes:

- `0`: all objects imported (or already present).
- `1`: import failed.

## 3. `refresh_index_pattern_fields.py`

82 lines. The docstring summarises:

> Re-populate the field cache on every nos3-telemetry*
> index-pattern saved object.

The reason this script exists is a Kibana 7.17 OSS limitation:
the OSS edition does not expose a dedicated `/refresh_fields`
endpoint (verified 404 in the source comment). The supported
path is:

1. `GET /api/index_patterns/_fields_for_wildcard?pattern=<title>`
   to get the live field list.
2. `PUT /api/saved_objects/index-pattern/<id>` with the new
   fields array stringified into `attributes.fields`.

Without this, every dashboard's Lens panels render "No data"
the moment a fresh daily index is created and the saved
object's cached field list goes stale. The dominant cause of
empty-panel symptoms in earlier debug sessions traced back to
this; the project's debug journal (`debug/journal.md` entry
2026-04-25 19:05) records the bisection.

The script is idempotent: safe to re-run any time. It exits 0
on success, non-zero only if Kibana is unreachable or the PUT
fails. The Makefile's `launch` target invokes it after Kibana
comes up.

## 4. The data view ID

`5b3163a0-3ea7-11f1-adf4-55f5fc5a104a` is the canonical data
view ID. Three places hardcode it:

- `build_kibana_dashboards.py` constant `IP`.
- `refresh_index_pattern_fields.py` constant
  `INDEX_PATTERN_IDS`.
- The dashboards NDJSON references this ID for every panel.

A previous saved-object cleanup (referenced in the comments at
lines 31-33 of the refresh script) removed an obsolete legacy
ID. The single-ID convention is enforced by the build script's
output and verified by the load script's import.

## 5. The Makefile integration

The `nos3/Makefile` targets that involve these scripts:

- `make start-elk` brings up the ELK compose stack.
- `make launch` invokes the FSW + sims + GSW + ELK launch
  script, which calls `load_dashboards.py` and
  `refresh_index_pattern_fields.py` after Kibana comes up.
- A developer building a new dashboard panel runs
  `build_kibana_dashboards.py` against a live Kibana, exports
  the result, and commits the NDJSON.

## 6. Failure modes

The most common build/load failures:

- **Kibana not reachable**: `KIBANA_WAIT_SECS` exceeded. The
  load script exits with a clear diagnostic; either Kibana
  is still booting or its container is unhealthy.
- **Stale ndjson schema**: a Kibana version bump rejects an
  older NDJSON with a schema-validation error. The fix is a
  one-shot rebuild against the new Kibana with
  `build_kibana_dashboards.py`, then re-export.
- **Orphan child object**: a Lens panel or visualisation from
  a deleted dashboard remains in Kibana and shadows a new
  panel with the same title. Add an entry to
  `OBSOLETE_OBJECTS` or `OBSOLETE_TITLE_SWEEPS` in both
  scripts and re-run.
- **Field cache empty**: a panel that recently consumed a
  newly-indexed field renders "No data". Run
  `refresh_index_pattern_fields.py`. If it exits 0 and the
  panel still empty, the field is not in any document yet
  (Logstash filter has not been re-run, or the Logstash
  pipeline is dropping the line in question).

## 7. Cross-references

- The dashboards panels themselves: [`03-kibana-dashboards.md`](03-kibana-dashboards.md).
- The Logstash filter that produces the fields:
  [`01-logstash-filters.md`](01-logstash-filters.md).
- The mappings that constrain field types:
  [`02-elasticsearch-mappings.md`](02-elasticsearch-mappings.md).
- The Makefile launch flow:
  [`../01-reproducibility.md`](../01-reproducibility.md).
