#!/usr/bin/env python3
"""
Build the two "Standard" Kibana dashboards for NOS3 and push them via the
Kibana saved-objects REST API.  Re-runs overwrite in place.

Dashboards:
  - nos3-std-telemetry-overview  - What telemetry is flowing, from where.
  - nos3-std-mission-validation  - Is the mission actually running?

Every panel has a title + description.  Markdown section headers split the
mission dashboard into Health / Power / Attitude / Uplink / Events rows so
the viewer can read it top-to-bottom.

Panels only query fields that are known to populate in the current pipeline;
dead fields (cmd_count/cmd_err_count from hk_decoded, mgr_mode EVS) were
replaced with sources that actually carry data:
  - spacecraft_mode  (hk_decoded, MGR_HK_TLM_MID, god_view_capture.py line 107)
  - blackboard_error tag (~168k docs per run, simulator side error stream)
  - sb_sequence_gap tag
"""
import json
import subprocess
import sys
import uuid
from pathlib import Path
from urllib.parse import quote

KIBANA = "http://localhost:5601"
IP = "5b3163a0-3ea7-11f1-adf4-55f5fc5a104a"   # nos3-telemetry* data view
VERSION = "7.17.10"

# Mirror of the purge list in load_dashboards.py: dashboards (and other
# saved objects) that previous versions of this script created and that
# must be deleted from any live Kibana, since put_so()/_import?overwrite=true
# never delete orphans on their own. Keep these two files in sync.
OBSOLETE_OBJECTS = [
    ("dashboard", "nos3-eo1-ttnc-downlink-validation"),
    # The 8 nos3-tsvb-* visualizations and 4 UUID-style dashboards are
    # leftovers from the ndjson era. The Python builders own everything
    # now, so these are pure orphans on any pre-conversion Kibana volume.
    ("visualization", "nos3-tsvb-rw-momenta"),
    ("visualization", "nos3-tsvb-imu-acc"),
    ("visualization", "nos3-tsvb-imu-gyro"),
    ("visualization", "nos3-tsvb-mag"),
    ("visualization", "nos3-tsvb-fss"),
    ("visualization", "nos3-tsvb-startrk"),
    ("visualization", "nos3-tsvb-cpu-per-app"),
    ("visualization", "nos3-tsvb-cpu-total"),
    ("dashboard", "139082b0-270a-11f1-931f-8d88664861d1"),
    ("dashboard", "4f71e500-257d-11f1-842b-a56e252a1ce9"),
    ("dashboard", "52ed72e0-270c-11f1-931f-8d88664861d1"),
    ("dashboard", "fe2e00e0-270a-11f1-931f-8d88664861d1"),
]
OBSOLETE_TITLE_SWEEPS = [
    ("lens", "TTNC"),
    ("visualization", "TTNC"),
]


# ---------- helpers ---------------------------------------------------------

def sid(prefix):
    return prefix + "-" + uuid.uuid4().hex[:8]


def ensure_data_view(dv_id, title, time_field="@timestamp"):
    """Create the index-pattern saved object if Kibana does not already
    have one with this id. Fresh-clone case: the ES docker volume is
    empty, so Kibana's saved-object index has no nos3-telemetry* data
    view; every dashboard panel built later references this id and
    renders "could not find the index pattern" until it exists.
    Idempotent: if the saved object is already there, this is a no-op.
    Field population is left to refresh_data_view_fields()."""
    r = subprocess.run(
        ["curl", "-sS", "-o", "/dev/null", "-w", "%{http_code}",
         f"{KIBANA}/api/saved_objects/index-pattern/{dv_id}",
         "-H", "kbn-xsrf: true"],
        capture_output=True, text=True)
    code = r.stdout.strip()
    if code == "200":
        return
    if code != "404":
        print(f"[WARN] ensure_data_view: unexpected status {code} for "
              f"{dv_id}; will still attempt create", file=sys.stderr)
    attrs = {
        "title": title,
        "timeFieldName": time_field,
        "fields": "[]",
        "fieldAttrs": "{}",
        "runtimeFieldMap": "{}",
        "typeMeta": "{}",
    }
    r = subprocess.run(
        ["curl", "-sS", "-X", "POST",
         f"{KIBANA}/api/saved_objects/index-pattern/{dv_id}",
         "-H", "kbn-xsrf: true",
         "-H", "Content-Type: application/json",
         "-d", json.dumps({"attributes": attrs})],
        capture_output=True, text=True)
    try:
        resp = json.loads(r.stdout)
    except Exception:
        print("[WARN] ensure_data_view: non-JSON response:",
              r.stdout[:300], file=sys.stderr)
        return
    if "error" in resp:
        print(f"[WARN] ensure_data_view: create failed for {dv_id}: "
              f"{resp}", file=sys.stderr)
        return
    print(f"[OK] Created data view {dv_id} (title='{title}', "
          f"timeFieldName='{time_field}')")


def refresh_data_view_fields(dv_id, index_pattern):
    """Pull the live field list from Elasticsearch (via Kibana's
    _fields_for_wildcard endpoint) and write it back into the data view's
    `fields` attribute. Without this, new Logstash fields stay invisible to
    panels and Kibana shows "Field X was not found" until a manual
    "Refresh field list" click in the UI."""
    url = (f"{KIBANA}/api/index_patterns/_fields_for_wildcard"
           f"?pattern={index_pattern}"
           "&meta_fields=_source&meta_fields=_id&meta_fields=_type"
           "&meta_fields=_index&meta_fields=_score")
    r = subprocess.run(["curl", "-sS", url, "-H", "kbn-xsrf: true"],
                       capture_output=True, text=True)
    try:
        fresh = json.loads(r.stdout).get("fields", [])
    except Exception:
        print("[WARN] could not parse _fields_for_wildcard response, skipping refresh",
              file=sys.stderr)
        return
    if not fresh:
        print(f"[WARN] _fields_for_wildcard returned 0 fields for "
              f"{index_pattern}; skipping refresh", file=sys.stderr)
        return

    r = subprocess.run(["curl", "-sS",
                        f"{KIBANA}/api/saved_objects/index-pattern/{dv_id}"],
                       capture_output=True, text=True)
    existing = json.loads(r.stdout)
    if "error" in existing:
        print(f"[WARN] data view {dv_id} not found, skipping refresh",
              file=sys.stderr)
        return
    attrs = existing["attributes"]
    attrs["fields"] = json.dumps(fresh)

    r = subprocess.run(["curl", "-sS", "-X", "PUT",
                        f"{KIBANA}/api/saved_objects/index-pattern/{dv_id}",
                        "-H", "kbn-xsrf: true",
                        "-H", "Content-Type: application/json",
                        "-d", json.dumps({"attributes": attrs})],
                       capture_output=True, text=True)
    resp = json.loads(r.stdout)
    if "error" in resp:
        print("[WARN] data view refresh failed:", resp, file=sys.stderr)
        return
    print(f"[OK] Data view fields refreshed: {len(fresh)} fields")


def put_so(so_type, so_id, attributes, references=None, overwrite=True):
    body = {"attributes": attributes}
    if references is not None:
        body["references"] = references
    url = f"{KIBANA}/api/saved_objects/{so_type}/{so_id}"
    if overwrite:
        url += "?overwrite=true"
    r = subprocess.run(
        ["curl", "-sS", "-X", "POST", url,
         "-H", "kbn-xsrf: true",
         "-H", "Content-Type: application/json",
         "-d", json.dumps(body)],
        capture_output=True, text=True)
    try:
        resp = json.loads(r.stdout)
    except Exception:
        print("RAW:", r.stdout[:500], file=sys.stderr)
        raise
    if "error" in resp:
        print("ERROR for", so_type, so_id, ":", resp, file=sys.stderr)
        sys.exit(1)
    return resp


# ---------- obsolete-saved-object purge ------------------------------------
# Mirrors load_dashboards.py so that whichever path the user runs
# (`make build-kibana-dashboards` here, or `make load-dashboards` there)
# leaves the same final state in Kibana.

def delete_obsolete(url, items):
    """DELETE each (type, id) pair from Kibana, ignoring 404s."""
    for so_type, so_id in items:
        r = subprocess.run(
            ["curl", "-sS", "-o", "/dev/null", "-w", "%{http_code}",
             "-X", "DELETE",
             f"{url}/api/saved_objects/{so_type}/{so_id}",
             "-H", "kbn-xsrf: true"],
            capture_output=True, text=True)
        code = r.stdout.strip()
        if code in ("200", "404"):
            print(f"[purge] removed obsolete {so_type}/{so_id} ({code})")
        else:
            print(f"[purge] WARN {so_type}/{so_id} -> {code}", file=sys.stderr)


def delete_by_search(url, so_type, query):
    """Find every saved object of `so_type` whose title matches `query`
    and DELETE it. Used to sweep orphan child panels (lens, viz) that
    were left behind when a parent dashboard was removed in an earlier
    refactor."""
    find_url = (f"{url}/api/saved_objects/_find"
                f"?type={quote(so_type)}"
                f"&search_fields=title"
                f"&search={quote(query)}"
                f"&per_page=1000")
    r = subprocess.run(
        ["curl", "-sS", find_url, "-H", "kbn-xsrf: true"],
        capture_output=True, text=True)
    try:
        body = json.loads(r.stdout)
    except json.JSONDecodeError:
        print(f"[purge] WARN _find {so_type} '{query}' returned non-JSON",
              file=sys.stderr)
        return
    hits = body.get("saved_objects", []) or []
    deleted = 0
    for hit in hits:
        so_id = hit.get("id")
        if not so_id:
            continue
        d = subprocess.run(
            ["curl", "-sS", "-o", "/dev/null", "-w", "%{http_code}",
             "-X", "DELETE",
             f"{url}/api/saved_objects/{so_type}/{so_id}",
             "-H", "kbn-xsrf: true"],
            capture_output=True, text=True)
        code = d.stdout.strip()
        if code in ("200", "404"):
            deleted += 1
        else:
            print(f"[purge] WARN sweep {so_type}/{so_id} -> {code}",
                  file=sys.stderr)
    print(f"[purge] swept {deleted} orphan {so_type}(s) matching \"{query}\"")


# ---------- orphan dashboard prune -----------------------------------------
# build_kibana_dashboards.py owns every dashboard listed in REGISTRY (defined
# at the bottom of the file). Any dashboard currently in Kibana whose ID is
# not in REGISTRY is treated as a leftover from a renamed / removed builder
# and DELETEd. This keeps the dashboard count converging on exactly the size
# of REGISTRY across rebuilds.

def list_kibana_dashboards():
    """Return [{'id': ..., 'title': ...}, ...] for every dashboard in Kibana."""
    out = []
    page = 1
    while True:
        url = (f"{KIBANA}/api/saved_objects/_find"
               f"?type=dashboard&per_page=100&page={page}&fields=title")
        r = subprocess.run(["curl", "-sS", url, "-H", "kbn-xsrf: true"],
                           capture_output=True, text=True)
        try:
            body = json.loads(r.stdout)
        except json.JSONDecodeError:
            print("[prune] WARN _find returned non-JSON; aborting prune",
                  file=sys.stderr)
            return out
        objs = body.get("saved_objects", []) or []
        out.extend({"id": o["id"], "title": o.get("attributes", {}).get("title", "")}
                   for o in objs)
        if len(out) >= body.get("total", len(out)) or not objs:
            break
        page += 1
    return out


def prune_orphan_dashboards(expected_ids):
    """DELETE any dashboard in Kibana whose ID is not in expected_ids.
    Returns (pruned_count, list_of_pruned_ids)."""
    pruned = []
    for d in list_kibana_dashboards():
        if d["id"] in expected_ids:
            continue
        print(f"[PRUNE] Orphan dashboard {d['id']} (\"{d['title']}\")")
        r = subprocess.run(
            ["curl", "-sS", "-o", "/dev/null", "-w", "%{http_code}",
             "-X", "DELETE",
             f"{KIBANA}/api/saved_objects/dashboard/{d['id']}",
             "-H", "kbn-xsrf: true"],
            capture_output=True, text=True)
        code = r.stdout.strip()
        if code in ("200", "404"):
            pruned.append(d["id"])
        else:
            print(f"[PRUNE] WARN delete {d['id']} -> {code}", file=sys.stderr)
    return len(pruned), pruned


# ---------- Lens column / state builders -----------------------------------

def lens_refs(layer_id):
    return [
        {"type": "index-pattern", "id": IP, "name": "indexpattern-datasource-current-indexpattern"},
        {"type": "index-pattern", "id": IP, "name": f"indexpattern-datasource-layer-{layer_id}"},
    ]


def col_count(label="Count of records"):
    return {"label": label, "dataType": "number", "operationType": "count",
            "isBucketed": False, "scale": "ratio", "sourceField": "Records"}


def col_sum(field, label):
    return {"label": label, "dataType": "number", "operationType": "sum",
            "sourceField": field, "isBucketed": False, "scale": "ratio"}


def col_avg(field, label):
    return {"label": label, "dataType": "number", "operationType": "average",
            "sourceField": field, "isBucketed": False, "scale": "ratio"}


def col_max(field, label):
    return {"label": label, "dataType": "number", "operationType": "max",
            "sourceField": field, "isBucketed": False, "scale": "ratio"}


def col_last_value(field, label):
    return {"label": label, "dataType": "string", "operationType": "last_value",
            "sourceField": field, "isBucketed": False, "scale": "ordinal",
            "params": {"sortField": "@timestamp"}}


def col_last_value_num(field, label):
    return {"label": label, "dataType": "number", "operationType": "last_value",
            "sourceField": field, "isBucketed": False, "scale": "ratio",
            "params": {"sortField": "@timestamp"}}


def col_last_value_bool(field, label):
    return {"label": label, "dataType": "boolean", "operationType": "last_value",
            "sourceField": field, "isBucketed": False, "scale": "ordinal",
            "params": {"sortField": "@timestamp"}}


def col_terms(field, size, order_by_id, label=None, other_bucket=True):
    return {"label": label or f"Top values of {field}",
            "dataType": "string", "operationType": "terms",
            "isBucketed": True, "scale": "ordinal", "sourceField": field,
            "params": {"size": size,
                       "orderBy": {"type": "column", "columnId": order_by_id},
                       "orderDirection": "desc",
                       "otherBucket": other_bucket, "missingBucket": False}}


def col_date_hist():
    return {"label": "@timestamp", "dataType": "date",
            "operationType": "date_histogram", "sourceField": "@timestamp",
            "isBucketed": True, "scale": "interval",
            "params": {"interval": "auto"}}


def col_filters(filters_list):
    return {"label": "Filters", "dataType": "string",
            "operationType": "filters", "isBucketed": True, "scale": "ordinal",
            "params": {"filters": filters_list}}


def build_lens(title, description, vis_type, visualization, columns, column_order,
               query="", filters=None, layer_id=None):
    layer_id = layer_id or sid("lyr")
    return {
        "title": "",
        "description": description,
        "visualizationType": vis_type,
        "type": "lens",
        "references": lens_refs(layer_id),
        "state": {
            "visualization": visualization,
            "query": {"query": query, "language": "kuery"},
            "filters": filters or [],
            "datasourceStates": {
                "indexpattern": {
                    "layers": {
                        layer_id: {
                            "columns": columns,
                            "columnOrder": column_order,
                            "incompleteColumns": {}
                        }
                    }
                }
            }
        },
    }, layer_id


# ---------- Panel type factories -------------------------------------------

def metric_panel(title, description, metric_col, query=""):
    layer_id = sid("lyr")
    col_id = sid("m")
    viz = {
        "accessor": col_id,
        "layerId": layer_id,
        "layerType": "data",
        "textAlign": "center",
        "titlePosition": "bottom",
        "size": "xl",
    }
    attrs, _ = build_lens(title, description, "lnsMetric", viz,
                          {col_id: metric_col}, [col_id],
                          query=query, layer_id=layer_id)
    return attrs


def pie_panel(title, description, terms_field, slice_label, query="",
              shape="donut", size=10):
    layer_id = sid("lyr")
    terms_id = sid("t")
    count_id = sid("c")
    columns = {terms_id: col_terms(terms_field, size, count_id, slice_label),
               count_id: col_count()}
    viz = {
        "shape": shape,
        "palette": {"type": "palette", "name": "default"},
        "layers": [{
            "layerId": layer_id,
            "layerType": "data",
            "primaryGroups": [terms_id],
            "metrics": [count_id],
            "numberDisplay": "percent",
            # Kibana 7.17.x lnsPie hangs on low-cardinality donuts when
            # categoryDisplay is "default" (inside-slice labels). The legend
            # already carries the same labels, so hide the inside copy.
            "categoryDisplay": "hide",
            "legendDisplay": "show",
            "legendPosition": "right",
            "nestedLegend": False,
            "percentDecimals": 1,
            "emptySizeRatio": 0.4,
        }],
    }
    attrs, _ = build_lens(title, description, "lnsPie", viz,
                          columns, [terms_id, count_id],
                          query=query, layer_id=layer_id)
    return attrs


def bar_panel(title, description, terms_field, size=15, horizontal=True,
              query="", break_by=None, label=None):
    layer_id = sid("lyr")
    terms_id = sid("t")
    count_id = sid("c")
    columns = {terms_id: col_terms(terms_field, size, count_id, label),
               count_id: col_count()}
    order = [terms_id, count_id]
    layer = {
        "layerId": layer_id,
        "accessors": [count_id],
        "position": "top",
        "seriesType": "bar_horizontal" if horizontal else "bar",
        "showGridlines": False,
        "layerType": "data",
        "xAccessor": terms_id,
    }
    if break_by:
        split_id = sid("s")
        columns[split_id] = col_terms(break_by, 5, count_id, break_by)
        order.insert(1, split_id)
        layer["splitAccessor"] = split_id
    viz = {
        "legend": {"isVisible": True, "position": "right"},
        "valueLabels": "hide",
        "fittingFunction": "None",
        "axisTitlesVisibilitySettings": {"x": True, "yLeft": True, "yRight": True},
        "tickLabelsVisibilitySettings": {"x": True, "yLeft": True, "yRight": True},
        "labelsOrientation": {"x": 0, "yLeft": 0, "yRight": 0},
        "gridlinesVisibilitySettings": {"x": True, "yLeft": True, "yRight": True},
        "preferredSeriesType": "bar_horizontal" if horizontal else "bar",
        "layers": [layer],
    }
    attrs, _ = build_lens(title, description, "lnsXY", viz,
                          columns, order, query=query, layer_id=layer_id)
    return attrs


def line_panel(title, description, metric_cols, query="", break_by=None,
               series_type="line", layer_id=None):
    layer_id = layer_id or sid("lyr")
    date_id = sid("d")
    columns = {date_id: col_date_hist()}
    accessors = []
    order = [date_id]
    for col, lbl in metric_cols:
        cid = sid("y")
        col = dict(col)
        col["label"] = lbl
        columns[cid] = col
        accessors.append(cid)
        order.append(cid)
    layer = {
        "layerId": layer_id,
        "accessors": accessors,
        "position": "top",
        "seriesType": series_type,
        "showGridlines": False,
        "layerType": "data",
        "xAccessor": date_id,
    }
    if break_by:
        split_id = sid("s")
        columns[split_id] = col_terms(break_by, 8, accessors[0], break_by)
        # Lens 7.17 layer state expects bucketed columns first, then metrics.
        # Insert split right after the date bucket so the order is
        # [date, split, *metrics]; appending after metrics renders empty.
        order.insert(1, split_id)
        layer["splitAccessor"] = split_id
    viz = {
        "legend": {"isVisible": True, "position": "right"},
        "valueLabels": "hide",
        "fittingFunction": "None",
        "axisTitlesVisibilitySettings": {"x": True, "yLeft": True, "yRight": True},
        "tickLabelsVisibilitySettings": {"x": True, "yLeft": True, "yRight": True},
        "labelsOrientation": {"x": 0, "yLeft": 0, "yRight": 0},
        "gridlinesVisibilitySettings": {"x": True, "yLeft": True, "yRight": True},
        "preferredSeriesType": series_type,
        "layers": [layer],
    }
    attrs, _ = build_lens(title, description, "lnsXY", viz,
                          columns, order, query=query, layer_id=layer_id)
    return attrs


def table_panel(title, description, columns_spec, query=""):
    layer_id = sid("lyr")
    columns = {}
    order = []
    viz_cols = []
    metric_id = sid("m")
    columns[metric_id] = col_count()
    for spec in columns_spec:
        cid = sid(spec["op"][:1])
        if spec["op"] == "terms":
            columns[cid] = col_terms(spec["field"], spec.get("size", 30),
                                      metric_id, spec.get("label"))
        elif spec["op"] == "count":
            columns[cid] = col_count(spec.get("label", "Count"))
        order.append(cid)
        viz_cols.append({"columnId": cid})
    if metric_id not in order:
        order.append(metric_id)
        viz_cols.append({"columnId": metric_id})
    viz = {"layerId": layer_id, "layerType": "data", "columns": viz_cols}
    attrs, _ = build_lens(title, description, "lnsDatatable", viz,
                          columns, order, query=query, layer_id=layer_id)
    return attrs


def filters_bar_panel(title, description, filters_list, horizontal=True, query=""):
    layer_id = sid("lyr")
    f_id = sid("f")
    c_id = sid("c")
    columns = {f_id: col_filters(filters_list), c_id: col_count()}
    order = [f_id, c_id]
    stype = "bar_horizontal" if horizontal else "bar"
    layer = {
        "layerId": layer_id,
        "accessors": [c_id],
        "position": "top",
        "seriesType": stype,
        "showGridlines": False,
        "layerType": "data",
        "xAccessor": f_id,
    }
    viz = {
        "legend": {"isVisible": True, "position": "right"},
        "valueLabels": "show",
        "fittingFunction": "None",
        "axisTitlesVisibilitySettings": {"x": True, "yLeft": True, "yRight": True},
        "tickLabelsVisibilitySettings": {"x": True, "yLeft": True, "yRight": True},
        "labelsOrientation": {"x": 0, "yLeft": 0, "yRight": 0},
        "gridlinesVisibilitySettings": {"x": True, "yLeft": True, "yRight": True},
        "preferredSeriesType": stype,
        "layers": [layer],
    }
    attrs, _ = build_lens(title, description, "lnsXY", viz,
                          columns, order, query=query, layer_id=layer_id)
    return attrs


# ---------- Panel wrapper --------------------------------------------------

def lens_panel(attrs, title, x, y, w, h):
    panel_index = sid("pnl")
    return {
        "version": VERSION,
        "type": "lens",
        "gridData": {"x": x, "y": y, "w": w, "h": h, "i": panel_index},
        "panelIndex": panel_index,
        "embeddableConfig": {
            "attributes": attrs,
            "enhancements": {},
            "hidePanelTitles": False,
        },
        "title": title,
    }


def markdown_panel(md_text, title, x, y, w, h, hide_title=True):
    panel_index = sid("md")
    return {
        "version": VERSION,
        "type": "visualization",
        "gridData": {"x": x, "y": y, "w": w, "h": h, "i": panel_index},
        "panelIndex": panel_index,
        "embeddableConfig": {
            "savedVis": {
                "title": title,
                "description": "",
                "type": "markdown",
                "params": {"fontSize": 12, "openLinksInNewTab": False, "markdown": md_text},
                "uiState": {},
                "data": {"aggs": [], "searchSource": {"query": {"query": "", "language": "kuery"}, "filter": []}}
            },
            "hidePanelTitles": hide_title,
            "enhancements": {}
        },
        "title": title,
    }


def collect_dashboard_refs(panels):
    refs = []
    for p in panels:
        if p["type"] == "lens":
            pi = p["panelIndex"]
            attrs = p["embeddableConfig"]["attributes"]
            for r in attrs.get("references", []):
                refs.append({"type": r["type"], "id": r["id"],
                              "name": f"{pi}:{r['name']}"})
    return refs


# ===========================================================================
# Dashboard 1 - NOS3 Standard Telemetry Overview
# ===========================================================================

def build_std_telemetry_overview():
    panels = []

    md = (
"""### NOS3 Standard Telemetry Overview

**What is flowing through the simulation, and from where.**  Three ingest layers feed this view:

- **Software Bus (SB)** - every CCSDS packet forwarded by TO_LAB. Fields: `msg_id`, `msg_name`, `layer`, `subsystem`, `msg_direction`, `sequence`.
- **Simulator log lines** - per-container stdout from hardware sims (EPS, GPS, IMU, RW, MAG, FSS, Star Tracker, Truth42, Torquer, Radio, blackboard, cpu_monitor). Parsed into numeric fields by Logstash.
- **Decoded HK** - structured payloads extracted from SB housekeeping by `god_view_capture.py`.

Pick a time range top-right. For mission-health, open **NOS3 Standard Mission Validation**."""
    )
    panels.append(markdown_panel(md, "About this dashboard", 0, 0, 48, 7))

    # Row 2: three rate metrics
    m1 = metric_panel("Software Bus packets",
                      "Live count of every CCSDS packet on the cFS Software Bus in the selected time range.",
                      col_count("SB packets"),
                      query='type.keyword: "software_bus"')
    panels.append(lens_panel(m1, "Software Bus packets", 0, 7, 16, 8))

    m2 = metric_panel("Simulator log lines",
                      "Log events ingested from omni_logs/*.log (per-container hardware-sim stdout).",
                      col_count("Sim log lines"),
                      query='type.keyword: "system_log"')
    panels.append(lens_panel(m2, "Simulator log lines", 16, 7, 16, 8))

    m3 = metric_panel("Decoded HK records",
                      "Structured housekeeping records decoded from SB telemetry by god_view_capture.py.",
                      col_count("HK records"),
                      query='type.keyword: "hk_decoded"')
    panels.append(lens_panel(m3, "Decoded HK records", 32, 7, 16, 8))

    # Row 3: bar charts - layer / direction / packet category. Kibana
    # 7.17.x lnsPie hangs on low-cardinality donuts so the donut variants
    # are rendered as horizontal bars via lnsXY instead.
    p1 = bar_panel("SB traffic by attack layer",
                   "Share of Software Bus packets grouped by cFS attack-layer label.",
                   "layer.keyword", size=5, horizontal=True,
                   query='type.keyword: "software_bus"', label="Attack layer")
    panels.append(lens_panel(p1, "SB traffic by attack layer", 0, 15, 16, 14))

    p2 = bar_panel("Direction split (CMD vs TLM)",
                   "Commands (0x18xx/0x19xx) versus Telemetry (0x08xx/0x09xx) on the Software Bus.",
                   "msg_direction.keyword", size=5, horizontal=True,
                   query='type.keyword: "software_bus"', label="Direction")
    panels.append(lens_panel(p2, "Direction split (CMD vs TLM)", 16, 15, 16, 14))

    # Packet category via name-suffix filters
    cat_panel = filters_bar_panel(
        "Packet category breakdown",
        "Share of Software Bus traffic by CCSDS packet category per docs/telemetry-and-fsw-reference.md section 3.",
        [{"label": "Housekeeping (HK)", "input": {"query": 'msg_name.keyword: *_HK_TLM_MID', "language": "kuery"}},
         {"label": "Device TLM", "input": {"query": 'msg_name.keyword: *_DEVICE_TLM_MID', "language": "kuery"}},
         {"label": "Combined HK", "input": {"query": 'msg_name.keyword: HK_COMBINED_PKT*', "language": "kuery"}},
         {"label": "Events", "input": {"query": 'msg_name.keyword: CFE_EVS_*EVENT_MSG_MID', "language": "kuery"}},
         {"label": "Commands", "input": {"query": 'msg_name.keyword: *_CMD_MID', "language": "kuery"}},
         {"label": "Req-HK", "input": {"query": 'msg_name.keyword: *_REQ_HK_MID or msg_name.keyword: *_SEND_HK_MID', "language": "kuery"}},
         {"label": "Diagnostic dwell", "input": {"query": 'msg_name.keyword: *DWELL*', "language": "kuery"}},
         {"label": "Other", "input": {"query": 'type.keyword: "software_bus" and NOT msg_name.keyword: (*_HK_TLM_MID or *_DEVICE_TLM_MID or HK_COMBINED_PKT* or CFE_EVS_*EVENT_MSG_MID or *_CMD_MID or *_REQ_HK_MID or *_SEND_HK_MID or *DWELL*)', "language": "kuery"}}],
        horizontal=True,
        query='type.keyword: "software_bus"')
    panels.append(lens_panel(cat_panel, "Packet category breakdown", 32, 15, 16, 14))

    # Row 4: two bars
    b1 = bar_panel("Top 15 MIDs on the Software Bus",
                   "Which CCSDS message IDs appear most often. Sudden shifts indicate spam, hang, or misconfiguration.",
                   "msg_name.keyword", size=15, horizontal=True,
                   query='type.keyword: "software_bus"', label="MID name")
    panels.append(lens_panel(b1, "Top 15 MIDs on the Software Bus", 0, 29, 24, 15))

    b2 = bar_panel("Busiest simulator subsystems",
                   "Top subsystems by log-line volume. Each subsystem is a simulator container.",
                   "subsystem.keyword", size=15, horizontal=False,
                   query='type.keyword: "system_log"', label="Subsystem")
    panels.append(lens_panel(b2, "Busiest simulator subsystems", 24, 29, 24, 15))

    # Row 5: two line charts
    l1 = line_panel("SB packet rate by attack layer",
                    "Rate of Software Bus traffic broken down by attack-layer label. Watch for surges.",
                    [(col_count(), "Packets")],
                    query='type.keyword: "software_bus"',
                    break_by="layer.keyword",
                    series_type="line")
    panels.append(lens_panel(l1, "SB packet rate by attack layer", 0, 44, 24, 15))

    # Log-level mix - signals only (not DEBUG flood)
    l2 = line_panel(
        "Log levels (ERROR / WARNING / INFO)",
        "Signal channel - simulator log lines excluding DEBUG. Spikes here deserve attention.",
        [(col_count(), "Lines")],
        query='type.keyword: "system_log" and log_level.keyword: ("ERROR" or "WARNING" or "INFO")',
        break_by="log_level.keyword",
        series_type="line")
    panels.append(lens_panel(l2, "Log levels (ERROR / WARN / INFO)", 24, 44, 24, 15))

    # Row 6: DEBUG-only log chart + data-integrity row
    l3 = line_panel(
        "DEBUG log volume (separated)",
        "DEBUG alone - typically 99% of log volume. Kept in its own panel so the ERROR/WARN/INFO chart stays readable.",
        [(col_count(), "DEBUG lines")],
        query='type.keyword: "system_log" and log_level.keyword: "DEBUG"',
        break_by="subsystem.keyword",
        series_type="line")
    panels.append(lens_panel(l3, "DEBUG log volume (separated)", 0, 59, 24, 15))

    # Data-integrity counters
    di_panel = filters_bar_panel(
        "Data-integrity events",
        "Count of anomaly tags added by Logstash. blackboard_error is a simulator-side error stream. sb_sequence_gap means a CCSDS sequence-counter gap was seen on the SB. cs_checksum_failure indicates the Checksum app detected a code/data miscompare.",
        [{"label": "blackboard_error", "input": {"query": 'tags: "blackboard_error"', "language": "kuery"}},
         {"label": "sb_sequence_gap", "input": {"query": 'tags: "sb_sequence_gap"', "language": "kuery"}},
         {"label": "cs_checksum_failure", "input": {"query": 'tags: "cs_checksum_failure"', "language": "kuery"}},
         {"label": "mm_memory_poke", "input": {"query": 'tags: "mm_memory_poke"', "language": "kuery"}},
         {"label": "mm_memory_fill", "input": {"query": 'tags: "mm_memory_fill"', "language": "kuery"}},
         {"label": "sb_pipe_overflow", "input": {"query": 'tags: "sb_pipe_overflow"', "language": "kuery"}}],
        horizontal=True)
    panels.append(lens_panel(di_panel, "Data-integrity events", 24, 59, 24, 15))

    # Row 7: table + messages-per-app histogram
    t1 = table_panel("Per-MID activity (top 30)",
                     "MID name, attack layer, subsystem, and packet count. Drill target for anything unusual above.",
                     [{"op": "terms", "field": "msg_name.keyword", "size": 30, "label": "MID name"},
                      {"op": "terms", "field": "layer.keyword", "size": 5, "label": "Layer"},
                      {"op": "terms", "field": "subsystem.keyword", "size": 30, "label": "Subsystem"}],
                     query='type.keyword: "software_bus"')
    panels.append(lens_panel(t1, "Per-MID activity (top 30)", 0, 74, 28, 20))

    # Messages per application - SB based, not HK counters (which are 0)
    b3 = bar_panel(
        "SB packets per subsystem (who talks most)",
        "Total Software Bus packets grouped by subsystem. Unlike HK cmd_count (often 0 on a cold run), this always has data and answers 'who is publishing most'.",
        "subsystem.keyword", size=25, horizontal=True,
        query='type.keyword: "software_bus"', label="Subsystem")
    panels.append(lens_panel(b3, "SB packets per subsystem", 28, 74, 20, 20))

    panelsJSON = json.dumps(panels, separators=(",", ":"))
    refs = collect_dashboard_refs(panels)

    attrs = {
        "title": "NOS3 Standard Telemetry Overview",
        "hits": 0,
        "description": "What telemetry is flowing through the simulation, and from where. Software Bus rates by attack layer, direction, and packet category; top MIDs; simulator log activity split by severity; data-integrity counters; per-MID table. Companion to 'NOS3 Standard Mission Validation'.",
        "panelsJSON": panelsJSON,
        "optionsJSON": json.dumps({"useMargins": True, "syncColors": False, "hidePanelTitles": False}),
        "version": 1,
        "timeRestore": False,
        "kibanaSavedObjectMeta": {"searchSourceJSON": json.dumps({"query": {"query": "", "language": "kuery"}, "filter": []})},
    }
    return attrs, refs


# ===========================================================================
# Dashboard 2 - NOS3 Standard Mission Validation
# ===========================================================================

def build_std_mission_validation():
    panels = []

    md_top = (
"""### NOS3 Standard Mission Validation

**Is the mission actually running?**  Read top to bottom - each coloured section checks a different subsystem.

- **Health** - spacecraft mode, heartbeats, CPU load, errors.
- **Power** - battery state of charge and the solar / load balance.
- **Attitude** - reaction-wheel momentum and star-tracker quaternion.
- **Uplink** - ground-to-spacecraft CI_LAB commands received.
- **Events** - counted fault and integrity events, plus an EVS tail.

For raw telemetry volumes and breakdowns, open **NOS3 Standard Telemetry Overview**."""
    )
    panels.append(markdown_panel(md_top, "About this dashboard", 0, 0, 48, 7))

    # ---- HEALTH row -------------------------------------------------------
    panels.append(markdown_panel(
        "## Health\nSpacecraft mode, heartbeat rate, error signals. "
        "If mode shows `--` the pipeline hasn't decoded an MGR HK yet; if heartbeats stall, "
        "something is wrong with HS or the scheduler.",
        "Health", 0, 7, 48, 4))

    m_mode = metric_panel(
        "Spacecraft mode (decoded MGR HK)",
        "Last value of spacecraft_mode from the decoded MGR_HK_TLM packet (god_view_capture.py line 107). 1=SAFE, 3=SCIENCE.",
        col_last_value_num("spacecraft_mode", "spacecraft_mode"),
        query='type.keyword: "hk_decoded" and app.keyword: "MGR"')
    panels.append(lens_panel(m_mode, "Spacecraft mode", 0, 11, 12, 8))

    m_hs = metric_panel(
        "HS heartbeat count",
        "Count of HS_HK_TLM_MID packets. Should be non-zero and roughly constant at 1 Hz.",
        col_count("HS HK packets"),
        query='msg_name.keyword: "HS_HK_TLM_MID"')
    panels.append(lens_panel(m_hs, "HS heartbeat count", 12, 11, 12, 8))

    m_bb = metric_panel(
        "Blackboard errors",
        "Events tagged blackboard_error by Logstash. Actively-firing signal from the blackboard simulator; worth a look when non-zero.",
        col_count("blackboard errors"),
        query='tags: "blackboard_error"')
    panels.append(lens_panel(m_bb, "Blackboard errors", 24, 11, 12, 8))

    m_gap = metric_panel(
        "SB sequence gaps",
        "CCSDS sequence-counter discontinuities observed on the Software Bus (tag sb_sequence_gap). Indicates packet drops.",
        col_count("Sequence gaps"),
        query='tags: "sb_sequence_gap"')
    panels.append(lens_panel(m_gap, "SB sequence gaps", 36, 11, 12, 8))

    # ---- POWER row --------------------------------------------------------
    panels.append(markdown_panel(
        "## Power\nBattery state of charge, solar / load balance, and the solar-array-enabled flag. "
        "If SOC drops below 30 % or the solar flag sticks at 0, the mission is in trouble.",
        "Power", 0, 19, 48, 4))

    m_soc = metric_panel(
        "Latest battery SOC (%)",
        "Maximum recent eps_battery_soc_pct computed by Logstash from eps_battery_wh (20 Wh full).",
        col_max("eps_battery_soc_pct", "SOC (%)"),
        query='eps_battery_soc_pct: *')
    panels.append(lens_panel(m_soc, "Latest battery SOC (%)", 0, 23, 12, 12))

    m_solar = metric_panel(
        "Solar array enabled (1/0)",
        "Last observed eps_solar_array_enabled (boolean) ordered by @timestamp. 1 means the array is connected right now; 0 means it is currently off.",
        col_last_value_bool("eps_solar_array_enabled", "Solar enabled"),
        query='eps_solar_array_enabled: *')
    panels.append(lens_panel(m_solar, "Solar array enabled", 12, 23, 12, 12))

    pwr = line_panel(
        "Power balance (solar / used / battery)",
        "Solar input (W), load consumption (W) and battery charge (Wh) over time. Positive (solar - load) means charging; negative means draining.",
        [(col_avg("eps_solar_power_w", "Solar (W)"), "Solar (W)"),
         (col_avg("eps_power_used_w", "Load (W)"), "Load (W)"),
         (col_avg("eps_battery_wh", "Battery (Wh)"), "Battery (Wh)")],
        query='subsystem.keyword: "nos3-eps"',
        series_type="line")
    panels.append(lens_panel(pwr, "Power balance (solar / used / battery)", 24, 23, 24, 12))

    pkt_rate = line_panel(
        "SB packet rate by direction",
        "Software Bus packet rate split CMD vs TLM. CMD bursts show ground uplinks; flat TLM means healthy 1 Hz HKs.",
        [(col_count(), "Packets")],
        query='type.keyword: "software_bus"',
        break_by="msg_direction.keyword",
        series_type="line")
    panels.append(lens_panel(pkt_rate, "SB packet rate by direction", 0, 35, 48, 12))

    # ---- ATTITUDE row -----------------------------------------------------
    panels.append(markdown_panel(
        "## Attitude\nReaction wheel momentum and star-tracker quaternion. "
        "RW sustained drift = saturation; quaternion jitter = attitude lost.",
        "Attitude", 0, 47, 48, 4))

    rw = line_panel(
        "Reaction wheel momentum (when 42 is connected)",
        "Average rw_momentum over time (Nms). Empty when the RW sim is still retrying the 42 TELEMETRY connection (see sim_connectivity_warning tag); in that case use 'Reaction wheel heartbeat' on the right.",
        [(col_avg("rw_momentum", "Momentum (Nms)"), "Momentum (Nms)")],
        query='rw_momentum: *',
        series_type="line")
    panels.append(lens_panel(rw, "Reaction wheel momentum", 0, 51, 24, 14))

    rw_hk = line_panel(
        "Reaction wheel HK heartbeat",
        "Packet count of GENERIC_RW_APP_HK_TLM_MID on the Software Bus. Confirms the RW FSW app is alive even when 42-dynamics isn't streaming back momentum values.",
        [(col_count(), "RW HK packets")],
        query='msg_name.keyword: "GENERIC_RW_APP_HK_TLM_MID"',
        series_type="bar_stacked")
    panels.append(lens_panel(rw_hk, "Reaction wheel HK heartbeat", 24, 51, 24, 14))

    st = line_panel(
        "Star tracker quaternion (when 42 is connected)",
        "Four quaternion components (q0..q3). Empty if the star-tracker sim is still retrying port 4282 on fortytwo; in that case check 'Star tracker HK heartbeat' on the right.",
        [(col_avg("st_q0", "q0"), "q0"),
         (col_avg("st_q1", "q1"), "q1"),
         (col_avg("st_q2", "q2"), "q2"),
         (col_avg("st_q3", "q3"), "q3")],
        query='st_q0: *',
        series_type="line")
    panels.append(lens_panel(st, "Star tracker quaternion", 0, 65, 24, 14))

    st_hk = line_panel(
        "Star tracker HK heartbeat",
        "Packet count of GENERIC_STAR_TRACKER_HK_TLM_MID on the Software Bus. Confirms the star tracker FSW app is alive even if the sim is still connecting to 42.",
        [(col_count(), "ST HK packets")],
        query='msg_name.keyword: "GENERIC_STAR_TRACKER_HK_TLM_MID"',
        series_type="bar_stacked")
    panels.append(lens_panel(st_hk, "Star tracker HK heartbeat", 24, 65, 24, 14))

    # ---- UPLINK row -------------------------------------------------------
    panels.append(markdown_panel(
        "## Uplink\nGround-link sanity. CI_LAB is the uplink stub; GPS altitude is a side-check "
        "that the spacecraft position is being computed.",
        "Uplink", 0, 79, 48, 4))

    gps = line_panel(
        "GPS altitude",
        "Orbital altitude from the GPS fix. Smooth LEO trace = valid fix; flat-line = no fix.",
        [(col_avg("gps_alt", "Altitude (m)"), "Altitude (m)")],
        query='gps_alt: *',
        series_type="line")
    panels.append(lens_panel(gps, "GPS altitude", 0, 83, 24, 14))

    ci_rate = line_panel(
        "CI_LAB uplink commands",
        "Rate of packets whose MID name starts with CI_LAB (ground uplink commands). Non-zero during active uplink windows.",
        [(col_count(), "Uplink cmds")],
        query='msg_name.keyword: CI_LAB_*',
        series_type="line")
    panels.append(lens_panel(ci_rate, "CI_LAB uplink commands", 24, 83, 24, 14))

    # ---- EVENTS row -------------------------------------------------------
    panels.append(markdown_panel(
        "## Events\nTagged mission and fault events, plus a live EVS tail. "
        "Most attack-indicator tags (MM poke, CS failure) are zero on clean runs; blackboard_error and sb_sequence_gap are the always-on signals.",
        "Events", 0, 97, 48, 4))

    events = filters_bar_panel(
        "Mission and fault events",
        "Tagged events in the current time range - dominant ones show above the smaller attack-indicator counts.",
        [{"label": "blackboard_error", "input": {"query": 'tags: "blackboard_error"', "language": "kuery"}},
         {"label": "sb_sequence_gap", "input": {"query": 'tags: "sb_sequence_gap"', "language": "kuery"}},
         {"label": "science_overfly", "input": {"query": 'tags: "science_overfly"', "language": "kuery"}},
         {"label": "lc_actionpoint_fired", "input": {"query": 'tags: "lc_actionpoint_fired"', "language": "kuery"}},
         {"label": "hs_app_failure", "input": {"query": 'tags: "hs_app_failure"', "language": "kuery"}},
         {"label": "cs_checksum_failure", "input": {"query": 'tags: "cs_checksum_failure"', "language": "kuery"}},
         {"label": "mm_memory_poke", "input": {"query": 'tags: "mm_memory_poke"', "language": "kuery"}},
         {"label": "mm_memory_fill", "input": {"query": 'tags: "mm_memory_fill"', "language": "kuery"}},
         {"label": "sb_pipe_overflow", "input": {"query": 'tags: "sb_pipe_overflow"', "language": "kuery"}}],
        horizontal=True)
    panels.append(lens_panel(events, "Mission and fault events", 0, 101, 48, 14))

    evs_tbl = table_panel(
        "Recent cFE EVS events",
        "Most recent cFE Event Service lines by severity, app, and message. Use it as a live event tail during triage.",
        [{"op": "terms", "field": "evs_severity.keyword", "size": 5, "label": "Severity"},
         {"op": "terms", "field": "evs_app_name.keyword", "size": 20, "label": "App"},
         {"op": "terms", "field": "evs_message.keyword", "size": 50, "label": "Message"}],
        query='subsystem.keyword: "cfs_evs"')
    panels.append(lens_panel(evs_tbl, "Recent cFE EVS events", 0, 115, 48, 20))

    panelsJSON = json.dumps(panels, separators=(",", ":"))
    refs = collect_dashboard_refs(panels)

    attrs = {
        "title": "NOS3 Standard Mission Validation",
        "hits": 0,
        "description": "Is the mission running? Mode, heartbeats, error counts, battery SOC + power balance, CPU, SB direction mix, RW momentum, star-tracker quaternion, GPS altitude, CI_LAB uplink, tagged mission/fault events, EVS tail. Grouped into Health / Power / Attitude / Uplink / Events sections. Companion to 'NOS3 Standard Telemetry Overview'.",
        "panelsJSON": panelsJSON,
        "optionsJSON": json.dumps({"useMargins": True, "syncColors": False, "hidePanelTitles": False}),
        "version": 1,
        "timeRestore": True,
        "timeFrom": "now-1h",
        "timeTo": "now",
        "refreshInterval": {"pause": False, "value": 30000},
        "kibanaSavedObjectMeta": {"searchSourceJSON": json.dumps({"query": {"query": "", "language": "kuery"}, "filter": []})},
    }
    return attrs, refs


# ===========================================================================
# Dashboard 3 - NOS3 Mission: Denmark overfly
# ===========================================================================

# Shared KQL predicate for "sub-satellite point inside the Denmark FOV bbox".
# Matches the LC watchpoints in nos3/cfg/nos3_defs/tables/lc_def_wdt.c
# (LAT_SOUTH_BOUND, LAT_NORTH_BOUND, LON_WEST_BOUND, LON_EAST_BOUND).
DK_KQL = 'gps_lat >= 55 and gps_lat <= 58 and gps_lon >= 8 and gps_lon <= 16'


def build_mission_denmark():
    panels = []

    md_top = (
"""### NOS3 Mission: Denmark overfly

**Is the satellite above Denmark right now, and did the geofence flip the spacecraft into SCIENCE mode?**

- **Where it is**: live GPS lat / lon / alt.
- **Geofence**: LC watchpoints 6-9 compare `gps_lat` and `gps_lon` against the Denmark bbox (55-58 N, 8-16 E). AP 3 fires RTS 2 (SCIENCE) on entry; AP 4 fires RTS 1 (SAFE) on exit.
- **Mode**: `spacecraft_mode` from decoded MGR HK. 1 = SAFE (idle), 3 = SCIENCE (mission active).

Boot the sim with the default Orb_LEO.txt and watch the satellite enter the bbox around t=2-3 min, fire SCIENCE, cross Denmark in ~1-2 min, and flip back to SAFE on exit."""
    )
    panels.append(markdown_panel(md_top, "About this dashboard", 0, 0, 48, 7))

    # ---- WHERE row (current position + mode) ------------------------------
    panels.append(markdown_panel(
        "## Where is the satellite\nLast GPS fix and current spacecraft mode.",
        "Where", 0, 7, 48, 3))

    m_lat = metric_panel(
        "Current GPS latitude (deg)",
        "Last value of gps_lat. Denmark bbox south-north bounds are 55 and 58 deg.",
        col_last_value_num("gps_lat", "Lat (deg)"),
        query='gps_lat: *')
    panels.append(lens_panel(m_lat, "Current latitude", 0, 10, 12, 8))

    m_lon = metric_panel(
        "Current GPS longitude (deg)",
        "Last value of gps_lon. Denmark bbox west-east bounds are 8 and 16 deg.",
        col_last_value_num("gps_lon", "Lon (deg)"),
        query='gps_lon: *')
    panels.append(lens_panel(m_lon, "Current longitude", 12, 10, 12, 8))

    m_alt = metric_panel(
        "Current GPS altitude (m)",
        "Last value of gps_alt. Default Orb_LEO.txt sets a 400 km circular orbit, so this should sit near 4.0e5.",
        col_last_value_num("gps_alt", "Alt (m)"),
        query='gps_alt: *')
    panels.append(lens_panel(m_alt, "Current altitude", 24, 10, 12, 8))

    m_mode = metric_panel(
        "Spacecraft mode (1=SAFE, 3=SCIENCE)",
        "Last value of spacecraft_mode from decoded MGR_HK_TLM. Flips 1 -> 3 when the Denmark geofence AP fires RTS 2, and 3 -> 1 when AP 4 fires RTS 1.",
        col_last_value_num("spacecraft_mode", "Mode"),
        query='type.keyword: "hk_decoded" and app.keyword: "MGR"')
    panels.append(lens_panel(m_mode, "Spacecraft mode", 36, 10, 12, 8))

    # ---- INSIDE BBOX row --------------------------------------------------
    panels.append(markdown_panel(
        "## Is it inside the Denmark bbox\n"
        "Samples whose lat/lon both fall inside 55-58 N, 8-16 E. Non-zero means the satellite is currently over Denmark (or was, in the selected time range).",
        "InsideBox", 0, 18, 48, 3))

    m_inside = metric_panel(
        "Samples inside Denmark bbox",
        "Count of telemetry events whose gps_lat / gps_lon lie inside the Denmark bbox in the selected time range. Zero outside passes.",
        col_count("Inside-box samples"),
        query=DK_KQL)
    panels.append(lens_panel(m_inside, "Samples inside Denmark bbox", 0, 21, 16, 8))

    m_pass = metric_panel(
        "SCIENCE pass count (MGR HK)",
        "Max value of sci_pass_count from decoded MGR HK. MGR increments it each time a SCIENCE pass is completed.",
        col_max("sci_pass_count", "SCIENCE passes"),
        query='sci_pass_count: *')
    panels.append(lens_panel(m_pass, "SCIENCE pass count", 16, 21, 16, 8))

    m_overfly_tag = metric_panel(
        "LC 'Science overfly' events",
        "EVS events tagged science_overfly (emitted by LC AP 3 on entry and AP 4 on exit). One pass produces two tagged events.",
        col_count("science_overfly events"),
        query='tags: "science_overfly"')
    panels.append(lens_panel(m_overfly_tag, "LC Science overfly events", 32, 21, 16, 8))

    # ---- TIMELINE rows ----------------------------------------------------
    panels.append(markdown_panel(
        "## Timeline\nHow the pass unfolds. Mode should step up to 3 during the pass, lat/lon should briefly enter the Denmark bands, and the inside-bbox rate chart should show a narrow spike.",
        "Timeline", 0, 29, 48, 3))

    mode_timeline = line_panel(
        "Spacecraft mode over time",
        "Max spacecraft_mode per bucket (the value is a uint8 enum: 1=SAFE, 2=STANDBY, 3=SCIENCE, 4=UNKNOWN, so averaging it would produce nonsense fractions). Jumps from 1 to 3 on bbox entry, back to 1 on exit; max preserves any SCIENCE excursion within a bucket.",
        [(col_max("spacecraft_mode", "Mode"), "Mode")],
        query='type.keyword: "hk_decoded" and app.keyword: "MGR"',
        series_type="line")
    panels.append(lens_panel(mode_timeline, "Spacecraft mode over time", 0, 32, 48, 14))

    lat_line = line_panel(
        "GPS latitude over time",
        "Sub-satellite latitude in deg. Denmark band is 55-58. When the trace is inside that band AND longitude is in 8-16, the geofence fires.",
        [(col_avg("gps_lat", "Lat"), "Lat (deg)")],
        query='gps_lat: *',
        series_type="line")
    panels.append(lens_panel(lat_line, "GPS latitude over time", 0, 46, 24, 14))

    lon_line = line_panel(
        "GPS longitude over time",
        "Sub-satellite longitude in deg. Denmark band is 8-16 E. Combine visually with the lat chart to see both axes inside the bbox.",
        [(col_avg("gps_lon", "Lon"), "Lon (deg)")],
        query='gps_lon: *',
        series_type="line")
    panels.append(lens_panel(lon_line, "GPS longitude over time", 24, 46, 24, 14))

    inside_rate = line_panel(
        "Event rate while inside Denmark bbox",
        "Rate of telemetry events whose lat/lon are inside the Denmark bbox. Expected: a single narrow bar ~1-2 min wide per pass, zero otherwise.",
        [(col_count(), "Inside-bbox events")],
        query=DK_KQL,
        series_type="bar_stacked")
    panels.append(lens_panel(inside_rate, "Time inside Denmark bbox", 0, 60, 48, 14))

    # ---- EVENTS row -------------------------------------------------------
    panels.append(markdown_panel(
        "## Events\nWhich LC APs fired and which mode transitions MGR logged. Each pass should produce: LC AP3 PASS->FAIL (entry), MGR Set mode 3, LC AP4 PASS->FAIL (exit), MGR Set mode 1.",
        "Events", 0, 74, 48, 3))

    ev_bar = filters_bar_panel(
        "Mission event counts",
        "LC and MGR event counts in the selected time range. Counts are per EVS line, so a full pass should give 1 of each of the first two (entry) + 1 of each of the last two (exit).",
        [{"label": "LC AP3 entry: in FOV", "input": {"query": 'evs_app_name.keyword: "LC" and evs_message.keyword: *in\\ FOV*', "language": "kuery"}},
         {"label": "LC AP4 exit: left FOV", "input": {"query": 'evs_app_name.keyword: "LC" and evs_message.keyword: *left\\ FOV*', "language": "kuery"}},
         {"label": "MGR -> SCIENCE (3)", "input": {"query": 'mgr_mode_num: 3', "language": "kuery"}},
         {"label": "MGR -> SAFE (1)", "input": {"query": 'mgr_mode_num: 1', "language": "kuery"}},
         {"label": "LC actionpoint fired", "input": {"query": 'tags: "lc_actionpoint_fired"', "language": "kuery"}}],
        horizontal=True)
    panels.append(lens_panel(ev_bar, "Mission event counts", 0, 77, 24, 14))

    evs_tbl = table_panel(
        "Recent LC / MGR EVS events",
        "Live EVS tail filtered to LC and MGR apps. Useful to watch the pass unfold line by line.",
        [{"op": "terms", "field": "evs_app_name.keyword", "size": 3, "label": "App"},
         {"op": "terms", "field": "evs_severity.keyword", "size": 3, "label": "Severity"},
         {"op": "terms", "field": "evs_message.keyword", "size": 40, "label": "Message"}],
        query='subsystem.keyword: "cfs_evs" and evs_app_name.keyword: ("LC" or "MGR")')
    panels.append(lens_panel(evs_tbl, "Recent LC / MGR EVS events", 24, 77, 24, 14))

    panelsJSON = json.dumps(panels, separators=(",", ":"))
    refs = collect_dashboard_refs(panels)

    attrs = {
        "title": "NOS3 Mission: Denmark",
        "hits": 0,
        "description": "Is the satellite above Denmark and has the geofence flipped it to SCIENCE mode? Live GPS lat/lon/alt, spacecraft mode timeline, samples inside the bbox, SCIENCE pass counter, LC actionpoint events, and an MGR/LC EVS tail.",
        "panelsJSON": panelsJSON,
        "optionsJSON": json.dumps({"useMargins": True, "syncColors": False, "hidePanelTitles": False}),
        "version": 1,
        "timeRestore": False,
        "kibanaSavedObjectMeta": {"searchSourceJSON": json.dumps({"query": {"query": "", "language": "kuery"}, "filter": []})},
    }
    return attrs, refs


# ---------- Saved search for EVS error tail --------------------------------

def build_evs_error_search():
    """
    Kibana 7.17 saved-search schema. Provides a standalone Discover pane
    pre-filtered to ERROR/CRITICAL EVS lines.
    """
    attrs = {
        "title": "NOS3 cFE EVS - ERROR / CRITICAL tail",
        "description": "Pre-filtered Discover view: only EVS events of severity ERROR or CRITICAL. Use during triage.",
        "hits": 0,
        "columns": ["evs_severity", "evs_app_name", "evs_event_id", "evs_message"],
        "sort": [["@timestamp", "desc"]],
        "version": 1,
        "kibanaSavedObjectMeta": {
            "searchSourceJSON": json.dumps({
                "query": {"query": 'subsystem.keyword: "cfs_evs" and evs_severity.keyword: ("ERROR" or "CRITICAL")', "language": "kuery"},
                "filter": [],
                "indexRefName": "kibanaSavedObjectMeta.searchSourceJSON.index"
            })
        },
    }
    refs = [{"id": IP, "type": "index-pattern", "name": "kibanaSavedObjectMeta.searchSourceJSON.index"}]
    return attrs, refs


# ===========================================================================
# Dashboard 4 - EO1 Power Budget
# ===========================================================================
#
# Fields are produced by nos3/elk/logstash.conf from EPS sim log lines:
# eps_solar_power_w, eps_power_used_w, eps_battery_wh, eps_battery_mv,
# eps_battery_soc_pct, eps_power_balance_w, eps_solar_array_enabled.

def build_eo1_power_budget():
    panels = []
    panels.append(markdown_panel(
        "## EO1 Power Budget\n"
        "Solar generation vs load and battery state. Panels read EPS sim "
        "fields extracted by Logstash. Use this dashboard during a science "
        "pass to confirm the battery survives the pass and during eclipse "
        "to confirm SAFE-mode load shedding.",
        "Header", 0, 0, 48, 4))

    m_soc = metric_panel(
        "Battery state of charge (%)",
        "Last value of eps_battery_soc_pct (Logstash-computed: battery_wh/20.0*100). 100% = full, 0% = empty.",
        col_last_value_num("eps_battery_soc_pct", "SoC %"),
        query='eps_battery_soc_pct: *')
    panels.append(lens_panel(m_soc, "Battery SoC", 0, 4, 12, 8))

    m_bal = metric_panel(
        "Power balance (W)",
        "Last eps_power_balance_w = solar - used. Negative means draining the battery; positive means charging.",
        col_last_value_num("eps_power_balance_w", "Balance W"),
        query='eps_power_balance_w: *')
    panels.append(lens_panel(m_bal, "Net power balance", 12, 4, 12, 8))

    m_sol = metric_panel(
        "Solar generation (W)",
        "Last eps_solar_power_w. Drops to ~0 in eclipse and during SAFE-mode if WP5 trips with the array off.",
        col_last_value_num("eps_solar_power_w", "Solar W"),
        query='eps_solar_power_w: *')
    panels.append(lens_panel(m_sol, "Solar generation", 24, 4, 12, 8))

    m_load = metric_panel(
        "Load (W)",
        "Last eps_power_used_w. Should drop noticeably on transitions to SAFE.",
        col_last_value_num("eps_power_used_w", "Load W"),
        query='eps_power_used_w: *')
    panels.append(lens_panel(m_load, "Load", 36, 4, 12, 8))

    pwr_line = line_panel(
        "Solar generation vs load (W)",
        "Two-series time chart. The gap between solar and load is the net charge/discharge.",
        [(col_avg("eps_solar_power_w", "Solar W"), "Solar W"),
         (col_avg("eps_power_used_w", "Load W"), "Load W")],
        query='eps_solar_power_w: * or eps_power_used_w: *',
        series_type="line")
    panels.append(lens_panel(pwr_line, "Solar vs load over time", 0, 12, 48, 14))

    bat_line = line_panel(
        "Battery state-of-charge over time (%)",
        "eps_battery_soc_pct trace. Watch for monotonic decline during eclipse and recovery during sun pointing.",
        [(col_avg("eps_battery_soc_pct", "SoC %"), "SoC %")],
        query='eps_battery_soc_pct: *',
        series_type="line")
    panels.append(lens_panel(bat_line, "Battery SoC over time", 0, 26, 24, 14))

    mv_line = line_panel(
        "Battery voltage over time (mV)",
        "eps_battery_mv trace. Pairs with WP0 (< 14800 mV -> AP0 -> RTS 1 SAFE) and WP1 (> 22200 mV alert).",
        [(col_avg("eps_battery_mv", "mV"), "Voltage mV")],
        query='eps_battery_mv: *',
        series_type="line")
    panels.append(lens_panel(mv_line, "Battery voltage", 24, 26, 24, 14))

    array_line = line_panel(
        "Solar array enabled (1/0)",
        "eps_solar_array_enabled trace. WP5 watches the solar array voltage; this is a related but distinct signal.",
        [(col_last_value_num("eps_solar_array_enabled", "Array on"), "Array on")],
        query='eps_solar_array_enabled: *',
        series_type="bar_stacked")
    panels.append(lens_panel(array_line, "Solar array enable state", 0, 40, 48, 10))

    panelsJSON = json.dumps(panels, separators=(",", ":"))
    refs = collect_dashboard_refs(panels)
    attrs = {
        "title": "NOS3 EO1: Power Budget",
        "hits": 0,
        "description": "Solar generation, load, battery state-of-charge and voltage, and solar-array enable state for the EO1 mission. Use during SCIENCE passes and eclipse transitions.",
        "panelsJSON": panelsJSON,
        "optionsJSON": json.dumps({"useMargins": True, "syncColors": False, "hidePanelTitles": False}),
        "version": 1,
        "timeRestore": False,
        "kibanaSavedObjectMeta": {"searchSourceJSON": json.dumps({"query": {"query": "", "language": "kuery"}, "filter": []})},
    }
    return attrs, refs


# ===========================================================================
# Dashboard 5 - EO1 ADCS Health
# ===========================================================================
#
# Fields: st_q0..q3 (star tracker quaternion), rw_momentum (reaction wheel),
# t42_gyro_x/y/z (truth body rates), spacecraft_mode (MGR HK).

def build_eo1_adcs_health():
    panels = []
    panels.append(markdown_panel(
        "## EO1 ADCS Health\n"
        "Attitude estimate, body rates, and reaction-wheel momentum. ADCS "
        "is mode-aware (sun-pointing in SAFE, nadir in SCIENCE). WP4 "
        "watches RW DeviceEnabled and triggers RTS 003 (torquer "
        "desaturation) when the wheel goes offline.",
        "Header", 0, 0, 48, 4))

    m_mode = metric_panel(
        "Spacecraft mode (1=SAFE, 3=SCIENCE)",
        "Last spacecraft_mode from MGR HK. ADCS pointing target follows this.",
        col_last_value_num("spacecraft_mode", "Mode"),
        query='type.keyword: "hk_decoded" and app.keyword: "MGR"')
    panels.append(lens_panel(m_mode, "Mode", 0, 4, 12, 8))

    m_rw = metric_panel(
        "Reaction wheel momentum",
        "Last rw_momentum value. Watched by WP4 (offline detection).",
        col_last_value_num("rw_momentum", "Momentum"),
        query='rw_momentum: *')
    panels.append(lens_panel(m_rw, "RW momentum", 12, 4, 12, 8))

    m_q0 = metric_panel(
        "Star tracker quaternion q0 (scalar)",
        "Last st_q0. Should sit near 1.0 when nearly aligned with the inertial frame.",
        col_last_value_num("st_q0", "q0"),
        query='st_q0: *')
    panels.append(lens_panel(m_q0, "ST q0", 24, 4, 12, 8))

    m_gx = metric_panel(
        "Body rate x (rad/s)",
        "Last t42_gyro_x. SCIENCE mode should drive this near zero (nadir-pointing tracking).",
        col_last_value_num("t42_gyro_x", "wx"),
        query='t42_gyro_x: *')
    panels.append(lens_panel(m_gx, "Body rate x", 36, 4, 12, 8))

    rw_line = line_panel(
        "Reaction wheel momentum over time",
        "Watch for saturation (continuous trend) and torquer-driven desats (sudden steps after AP2 fires).",
        [(col_avg("rw_momentum", "Momentum"), "Momentum")],
        query='rw_momentum: *',
        series_type="line")
    panels.append(lens_panel(rw_line, "RW momentum over time", 0, 12, 48, 14))

    rates = line_panel(
        "Body rates over time (rad/s)",
        "t42_gyro_x/y/z. SCIENCE pass should show damped rates while controller tracks nadir.",
        [(col_avg("t42_gyro_x", "wx"), "wx"),
         (col_avg("t42_gyro_y", "wy"), "wy"),
         (col_avg("t42_gyro_z", "wz"), "wz")],
        query='t42_gyro_x: *',
        series_type="line")
    panels.append(lens_panel(rates, "Body rates", 0, 26, 24, 14))

    quat = line_panel(
        "Star tracker quaternion over time",
        "st_q0..q3 traces. Quaternion magnitude should stay at 1.0 when ST has a fix.",
        [(col_avg("st_q0", "q0"), "q0"),
         (col_avg("st_q1", "q1"), "q1"),
         (col_avg("st_q2", "q2"), "q2"),
         (col_avg("st_q3", "q3"), "q3")],
        query='st_q0: *',
        series_type="line")
    panels.append(lens_panel(quat, "ST quaternion", 24, 26, 24, 14))

    adcs_evs = table_panel(
        "Recent ADCS / RW EVS events",
        "Tail of EVS for ADCS, RW and torquer apps. RTS 003 (torquer desat) fires here when AP2 latches.",
        [{"op": "terms", "field": "evs_app_name.keyword", "size": 5, "label": "App"},
         {"op": "terms", "field": "evs_severity.keyword", "size": 4, "label": "Severity"},
         {"op": "terms", "field": "evs_message.keyword", "size": 30, "label": "Message"}],
        query='subsystem.keyword: "cfs_evs" and evs_app_name.keyword: ("GENERIC_ADCS" or "GENERIC_RW" or "GENERIC_TORQUER")')
    panels.append(lens_panel(adcs_evs, "ADCS EVS tail", 0, 40, 48, 14))

    panelsJSON = json.dumps(panels, separators=(",", ":"))
    refs = collect_dashboard_refs(panels)
    attrs = {
        "title": "NOS3 EO1: ADCS Health",
        "hits": 0,
        "description": "Attitude, body rates, reaction-wheel momentum, and ADCS/RW/torquer EVS events for the EO1 mission.",
        "panelsJSON": panelsJSON,
        "optionsJSON": json.dumps({"useMargins": True, "syncColors": False, "hidePanelTitles": False}),
        "version": 1,
        "timeRestore": False,
        "kibanaSavedObjectMeta": {"searchSourceJSON": json.dumps({"query": {"query": "", "language": "kuery"}, "filter": []})},
    }
    return attrs, refs


# ===========================================================================
# Dashboard 6 - EO1 FSW Health
# ===========================================================================
#
# Fields: TOTAL_CPU_PCT, NUM_PIDS, hs_failed_app, hs_cpu_util_peak,
# evs_severity, evs_app_name, evs_message, lc_actionpoint_fired tag.

def build_eo1_fsw_health():
    panels = []
    panels.append(markdown_panel(
        "## EO1 FSW Health\n"
        "CPU load, process count, HS failure detector, and a CRITICAL+ EVS "
        "tail. AP1 watches CPU > 75 percent (alert-only). New AP5 watches "
        "HS ResetsPerformed > 0 and fires RTS 4 (force SAFE).",
        "Header", 0, 0, 48, 4))

    m_cpu = metric_panel(
        "Total CPU (%)",
        "Last TOTAL_CPU_PCT from cpu_monitor. WP2/AP1 alert if > 75.",
        col_last_value_num("TOTAL_CPU_PCT", "CPU %"),
        query='TOTAL_CPU_PCT: *')
    panels.append(lens_panel(m_cpu, "Total CPU", 0, 4, 12, 8))

    m_pids = metric_panel(
        "Process count",
        "Last NUM_PIDS from cpu_monitor. Sudden drops mean an app crashed.",
        col_last_value_num("NUM_PIDS", "PIDs"),
        query='NUM_PIDS: *')
    panels.append(lens_panel(m_pids, "PIDs", 12, 4, 12, 8))

    m_hs = metric_panel(
        "HS resets performed",
        "Cumulative HS_ResetsPerformed (uint16, wire offset 24 of HS_HK_TLM payload). When > 0, new WP10/AP5 fires RTS 4 to drop SAFE.",
        col_last_value_num("hs_resets_performed", "Resets"),
        query='hs_resets_performed: *')
    panels.append(lens_panel(m_hs, "HS resets", 24, 4, 12, 8))

    m_failed = metric_panel(
        "HS failed-app slot",
        "Last hs_failed_app value (which monitored app slot HS flagged as failed).",
        col_last_value_num("hs_failed_app", "Slot"),
        query='hs_failed_app: *')
    panels.append(lens_panel(m_failed, "HS failed app", 36, 4, 12, 8))

    cpu_line = line_panel(
        "Total CPU and process count over time",
        "Two-series chart. Sustained TOTAL_CPU_PCT above 75 should produce repeated AP1 alerts.",
        [(col_avg("TOTAL_CPU_PCT", "CPU %"), "CPU %"),
         (col_avg("NUM_PIDS", "PIDs"), "PIDs")],
        query='TOTAL_CPU_PCT: *',
        series_type="line")
    panels.append(lens_panel(cpu_line, "CPU and PIDs over time", 0, 12, 48, 14))

    ap_bar = filters_bar_panel(
        "Actionpoint firings (this run)",
        "Counts per LC AP. AP3 = SCIENCE_OVERFLY (entry), AP4 = SCIENCE_EXIT, AP0 = battery-low SAFE, AP5 = HS-failure SAFE, AP6 = orbit-low SAFE, AP7 = solar-off SAFE.",
        [{"label": "AP0 batt-low", "input": {"query": 'tags: "lc_actionpoint_fired" and evs_message.keyword: *Batt\\ volt*', "language": "kuery"}},
         {"label": "AP3 SCIENCE entry", "input": {"query": 'tags: "lc_actionpoint_fired" and evs_message.keyword: *in\\ FOV*', "language": "kuery"}},
         {"label": "AP4 SCIENCE exit",  "input": {"query": 'tags: "lc_actionpoint_fired" and evs_message.keyword: *left\\ FOV*', "language": "kuery"}},
         {"label": "AP5 HS failure",    "input": {"query": 'tags: "lc_actionpoint_fired" and evs_message.keyword: *HS\\ reset*', "language": "kuery"}},
         {"label": "AP6 orbit-low",     "input": {"query": 'tags: "lc_actionpoint_fired" and evs_message.keyword: *Orbit\\ <\\ 400*', "language": "kuery"}},
         {"label": "AP7 solar-off",     "input": {"query": 'tags: "lc_actionpoint_fired" and evs_message.keyword: *Solar\\ array\\ off*', "language": "kuery"}}],
        horizontal=True)
    panels.append(lens_panel(ap_bar, "Actionpoint firings", 0, 26, 24, 14))

    crit = table_panel(
        "Recent CRITICAL / ERROR EVS events",
        "Live tail of EVS lines at severity ERROR or CRITICAL. Use during triage to find the first failure.",
        [{"op": "terms", "field": "evs_app_name.keyword", "size": 10, "label": "App"},
         {"op": "terms", "field": "evs_severity.keyword", "size": 4, "label": "Severity"},
         {"op": "terms", "field": "evs_message.keyword", "size": 40, "label": "Message"}],
        query='subsystem.keyword: "cfs_evs" and evs_severity.keyword: ("ERROR" or "CRITICAL")')
    panels.append(lens_panel(crit, "Critical EVS tail", 24, 26, 24, 14))

    panelsJSON = json.dumps(panels, separators=(",", ":"))
    refs = collect_dashboard_refs(panels)
    attrs = {
        "title": "NOS3 EO1: FSW Health",
        "hits": 0,
        "description": "CPU load, process count, HS failure detection, LC actionpoint firings, and a CRITICAL/ERROR EVS tail for the EO1 mission.",
        "panelsJSON": panelsJSON,
        "optionsJSON": json.dumps({"useMargins": True, "syncColors": False, "hidePanelTitles": False}),
        "version": 1,
        "timeRestore": False,
        "kibanaSavedObjectMeta": {"searchSourceJSON": json.dumps({"query": {"query": "", "language": "kuery"}, "filter": []})},
    }
    return attrs, refs


# ===========================================================================
# Dashboard 7 - EO1 TT&C Downlink Validation
# ===========================================================================
#
# Fields populated by the new generic_tt_c sim (Logstash subsystem:TTC):
#   link_state, ground_station, elevation_deg, slant_range_km, doppler_hz,
#   pass_number, pass_count, packets_downlinked, bytes_downlinked,
#   packets_dropped.
#
# EO1 primary ground station is DTU Lyngby (55.79 N, 12.52 E) per the
# thesis (docs/thesis/09-glossary.md). Svalbard (78.23 N, 15.40 E) acts as
# a high-latitude backup that picks up high-inclination passes when DTU is
# below the 5 deg mask. See nos3/cfg/InOut/Inp_Sim.txt.

def build_eo1_ttc_downlink_validation():
    panels = []
    panels.append(markdown_panel(
        "## EO1 TT&C Downlink Validation\n"
        "Ground-station link telemetry: elevation, slant range, Doppler, "
        "pass tracking, and downlink throughput counters. Link is `ACQUIRED` "
        "when elevation >= 5 deg over the active ground station "
        "(DTU Lyngby primary; Svalbard / GSFC / etc. as backup when DTU is "
        "below the 5 deg mask). Use this dashboard to "
        "verify pass coverage matches OrbitInviewPowerPrediction expectations "
        "and to spot dropped-packet bursts during low-elevation portions of "
        "a pass.",
        "Header", 0, 0, 48, 4))

    # ── Top metric strip ────────────────────────────────────────────────
    m_link = metric_panel(
        "Current link state",
        "Last link_state value from the TT&C sim. ACQUIRED when a ground "
        "station is currently above the 5 deg elevation mask "
        "(elev >= elev_mask_deg in generic_tt_c_hardware_model.cpp); "
        "IDLE otherwise. This is a pure geometry signal and is INDEPENDENT "
        "of spacecraft mode - HK telemetry flows whenever the link is up. "
        "SCIENCE mode only enables DS file recording and the SAT_HELLO / "
        "GS_PING_CC round-trip, not the radio downlink itself. See the "
        "Spacecraft mode tile to the right for the orthogonal payload state.",
        col_last_value("link_state.keyword", "Link"),
        query='subsystem.keyword: "TTC"')
    panels.append(lens_panel(m_link, "Link state", 0, 4, 8, 8))

    m_link_code = metric_panel(
        "Link state code",
        "Numeric link state: 0=IDLE/OFFLINE, 1=ACQUIRING, 2=ACQUIRED/IN_PASS, 3=LOSING, -1=unknown.",
        col_last_value_num("link_state_code", "Link code"),
        query='link_state_code: *')
    panels.append(lens_panel(m_link_code, "Link code", 8, 4, 5, 8))

    m_gs = metric_panel(
        "Active ground station",
        "Last ground_station value: which station the TT&C app is currently tracking.",
        col_last_value("ground_station.keyword", "GS"),
        query='subsystem.keyword: "TTC"')
    panels.append(lens_panel(m_gs, "Ground station", 13, 4, 7, 8))

    m_passcount = metric_panel(
        "Total passes seen",
        "Last pass_count from the TT&C sim. Increments on each IDLE -> ACQUIRED transition.",
        col_last_value_num("pass_count", "Passes"),
        query='pass_count: *')
    panels.append(lens_panel(m_passcount, "Pass count", 20, 4, 6, 8))

    m_passnum = metric_panel(
        "Pass number (current)",
        "Last pass_number. Same as pass_count when a pass is active; one less when IDLE.",
        col_last_value_num("pass_number", "Pass#"),
        query='pass_number: *')
    panels.append(lens_panel(m_passnum, "Pass#", 26, 4, 6, 8))

    # Spacecraft mode tile placed adjacent to the link state tile so the
    # decoupling between (radio visibility) and (payload mode) is visible
    # at a glance. Same lastValue idiom as the GNSS-to-GS dashboard so
    # users see the identical Status semantics across both views.
    m_mode = metric_panel(
        "Spacecraft mode",
        "Latest spacecraft_mode_name from MGR HK (SAFE / STANDBY / SCIENCE / "
        "OTHER). Independent of the Link state tile to the left: ACQUIRED + "
        "SAFE is the consistent, expected state when a ground station is "
        "overhead but the spacecraft is not in a Denmark science pass.",
        col_last_value("spacecraft_mode_name.keyword", "Mode"),
        query='spacecraft_mode_name: *')
    panels.append(lens_panel(m_mode, "Spacecraft mode", 32, 4, 8, 8))

    # Freshness tile: count of TTC HK docs over the dashboard time range.
    # With timeRestore=True / timeFrom=now-30m and 1 Hz TTC HK, a healthy
    # sim shows >>0. A value near 0 means TTC HK has stalled and the
    # lastValue tiles to the left may be showing stale cached state.
    m_fresh = metric_panel(
        "TTC HK docs (in time range)",
        "Count of TTC HK docs in the dashboard time range (default last "
        "30 min, 1 Hz HK -> hundreds expected). If near 0, the lastValue "
        "tiles to the left are showing stale cached state, not live data - "
        "check god_view_capture and the TT&C sim.",
        col_count("Docs"),
        query='subsystem.keyword: "TTC" and link_state: *')
    panels.append(lens_panel(m_fresh, "Data freshness", 40, 4, 8, 8))

    # ── Live link metrics ───────────────────────────────────────────────
    m_elev = metric_panel(
        "Elevation (deg)",
        "Last elevation_deg over the active GS. >=5 deg defines an acquired pass.",
        col_last_value_num("elevation_deg", "Elev"),
        query='elevation_deg: *')
    panels.append(lens_panel(m_elev, "Elevation", 0, 12, 12, 8))

    m_range = metric_panel(
        "Slant range (km)",
        "Last slant_range_km. Minimum near pass culmination; rising before AOS, falling after LOS.",
        col_last_value_num("slant_range_km", "Slant"),
        query='slant_range_km: *')
    panels.append(lens_panel(m_range, "Slant range", 12, 12, 12, 8))

    m_dop = metric_panel(
        "Doppler shift (Hz)",
        "Last doppler_hz. Positive on approach, negative on recession; zero crossing near culmination.",
        col_last_value_num("doppler_hz", "Doppler"),
        query='doppler_hz: *')
    panels.append(lens_panel(m_dop, "Doppler", 24, 12, 12, 8))

    m_drop = metric_panel(
        "Packets dropped",
        "Last packets_dropped accumulator. Should stay near zero during normal passes; sustained growth is a red flag.",
        col_last_value_num("packets_dropped", "Dropped"),
        query='packets_dropped: *')
    panels.append(lens_panel(m_drop, "Dropped", 36, 12, 12, 8))

    # ── Throughput totals ───────────────────────────────────────────────
    m_pkts = metric_panel(
        "Packets downlinked",
        "Last packets_downlinked accumulator across all passes since sim start.",
        col_last_value_num("packets_downlinked", "Pkts"),
        query='packets_downlinked: *')
    panels.append(lens_panel(m_pkts, "Packets downlinked", 0, 20, 24, 8))

    m_bytes = metric_panel(
        "Bytes downlinked",
        "Last bytes_downlinked accumulator across all passes since sim start.",
        col_last_value_num("bytes_downlinked", "Bytes"),
        query='bytes_downlinked: *')
    panels.append(lens_panel(m_bytes, "Bytes downlinked", 24, 20, 24, 8))

    # ── Bar: time spent ACQUIRED vs IDLE ────────────────────────────────
    # Was a donut pie originally, but Kibana 7.17.x lnsPie hangs on
    # low-cardinality donuts (only IDLE / ACQUIRED here). Using lnsXY
    # via bar_panel sidesteps the donut codepath while answering the
    # same "share of HK ticks per link_state" question.
    link_pie = bar_panel(
        "Link state distribution",
        "Share of TTC HK ticks spent in each link_state. A 60deg-incl orbit at 400 km will see DTU passes a few times per day; ACQUIRED share is small (single-digit %), with Svalbard padding the high-latitude fraction.",
        terms_field="link_state.keyword",
        size=5, horizontal=True,
        query='subsystem.keyword: "TTC"',
        label="State")
    panels.append(lens_panel(link_pie, "Link state distribution", 0, 28, 24, 14))

    # ── Bar: per-ground-station contact share ───────────────────────────
    gs_bar = bar_panel(
        "Pass contacts by ground station",
        "Distribution of TTC HK ticks spent ACQUIRED grouped by ground_station. Confirms DTU is the dominant contact station and that Svalbard / GSFC are picking up the off-DTU passes the 5 deg mask exposes.",
        terms_field="ground_station.keyword",
        size=10,
        horizontal=True,
        query='subsystem.keyword: "TTC" and link_state.keyword: "ACQUIRED"')
    panels.append(lens_panel(gs_bar, "Contacts by GS", 24, 28, 24, 14))

    # ── Time series: elevation + slant range ────────────────────────────
    geom_line = line_panel(
        "Elevation and slant range over time",
        "elevation_deg (left axis, deg) and slant_range_km (right axis, km). The classic dome-shaped curves during a pass.",
        [(col_avg("elevation_deg", "Elevation deg"), "Elevation deg"),
         (col_avg("slant_range_km", "Slant range km"), "Slant range km")],
        query='elevation_deg: *',
        series_type="line")
    panels.append(lens_panel(geom_line, "Pass geometry over time", 0, 42, 48, 14))

    # ── Time series: doppler + dropped ──────────────────────────────────
    dop_line = line_panel(
        "Doppler shift over time",
        "doppler_hz trace. S-shape curve during each pass: positive on approach, zero at culmination, negative on recession.",
        [(col_avg("doppler_hz", "Doppler Hz"), "Doppler Hz")],
        query='doppler_hz: *',
        series_type="line")
    panels.append(lens_panel(dop_line, "Doppler over time", 0, 56, 24, 14))

    drop_line = line_panel(
        "Dropped packets over time",
        "packets_dropped accumulator. Flat = clean link. Steps up during low-margin portions of a pass.",
        [(col_max("packets_dropped", "Dropped"), "Dropped")],
        query='packets_dropped: *',
        series_type="line")
    panels.append(lens_panel(drop_line, "Dropped packets", 24, 56, 24, 14))

    # ── Recent passes table ─────────────────────────────────────────────
    # table_panel only supports terms + count, so this groups HK-tick rows
    # per pass_number and ground_station with the row count as a coarse
    # "duration" indicator (more rows = longer pass at higher elevation
    # since the sim emits at a fixed cadence).
    pass_tbl = table_panel(
        "Recent passes (rows = HK ticks during ACQUIRED window)",
        "One row per pass_number x ground_station with the HK-tick count during the pass. Higher counts indicate longer / higher-elevation passes.",
        [{"op": "terms", "field": "pass_number", "size": 20, "label": "Pass#"},
         {"op": "terms", "field": "ground_station.keyword", "size": 7, "label": "GS"}],
        query='subsystem.keyword: "TTC" and link_state.keyword: "ACQUIRED"')
    panels.append(lens_panel(pass_tbl, "Recent passes", 0, 70, 48, 14))

    panelsJSON = json.dumps(panels, separators=(",", ":"))
    refs = collect_dashboard_refs(panels)
    attrs = {
        "title": "NOS3 EO1: TT&C Downlink Validation",
        "hits": 0,
        "description": (
            "Link-budget telemetry from the TT&C sim: link state, active "
            "ground station, elevation, slant range, Doppler shift, pass "
            "tracking, and downlink throughput counters. Canonical GS is "
            "DTU Lyngby for the EO1 mission; Svalbard handles high-inclination "
            "overpasses when DTU is below the 5 deg mask. "
            "Link state and Spacecraft mode are intentionally side-by-side "
            "in the Status row: link state is a pure geometry signal "
            "(ground station above 5 deg elevation), independent of payload "
            "mode. ACQUIRED + SAFE is a normal state. The Data freshness "
            "tile counts TTC HK docs in the current time range - if near 0 "
            "the lastValue tiles are showing stale cached state."
        ),
        "panelsJSON": panelsJSON,
        "optionsJSON": json.dumps({"useMargins": True, "syncColors": False, "hidePanelTitles": False}),
        "version": 1,
        "timeRestore": True,
        "timeFrom": "now-30m",
        "timeTo": "now",
        "refreshInterval": {"pause": False, "value": 10000},
        "kibanaSavedObjectMeta": {"searchSourceJSON": json.dumps({"query": {"query": "", "language": "kuery"}, "filter": []})},
    }
    return attrs, refs


# ===========================================================================
# Dashboard 8 - NOS3 EO1: GNSS-to-GS Validation
# ===========================================================================
# Closed-loop validation: GPS sees Denmark -> MGR flips to SCIENCE -> GENERIC_GNSS
# publishes DATA TLM -> COSMOS responder issues GS_PING_CC -> FSW echoes
# LAST_PING_SEQ via HK. The dashboard surfaces every link in that chain so a
# regression is traceable to one stage.
#
# Field sources (all already extracted by nos3/elk/logstash.conf):
#   - MGR HK: spacecraft_mode (int), spacecraft_mode_name (kw),
#             in_denmark_box (0/1), in_science_mode (0/1)
#   - GENERIC_GNSS HK: gnss_lat / gnss_lon (float deg),
#             last_ping_seq (int), ping_count (int)
#   - GNSS sim line: gps_lat / gps_lon / gps_alt (float deg, m),
#             gnss_truth_lat / gnss_truth_lon (float deg)
#   - Tags: gnss_ping_uplink (uplink emitted),
#           gnss_gs_ping_roundtrip (HK echo seen)

def build_eo1_gnss_gs_validation():
    panels = []

    # ── Row 0: dashboard header ─────────────────────────────────────────
    panels.append(markdown_panel(
        "## EO1 GNSS-to-GS Validation\n"
        "Closed-loop sanity check for the Denmark science scenario:\n\n"
        "1. **GPS** sees a fix inside the Denmark bbox (lat 55-58, lon 8-16).\n"
        "2. **MGR** flips `spacecraft_mode` 1 -> 3 (SAFE -> SCIENCE).\n"
        "3. **GENERIC_GNSS** starts publishing DATA TLM.\n"
        "4. The **COSMOS responder** issues `GS_PING_CC` (tag `gnss_ping_uplink`).\n"
        "5. **FSW** echoes the sequence number back as `last_ping_seq` in HK "
        "(tag `gnss_gs_ping_roundtrip`).\n\n"
        "Each row below covers one stage. A row that stays empty pinpoints "
        "where the chain broke.",
        "About this dashboard", 0, 0, 48, 7))

    # ── Row 1: status header ─────────────────────────────────────────────
    panels.append(markdown_panel(
        "### Status",
        "StatusHdr", 0, 7, 48, 2))

    # ── Row 2: closed-loop status metrics ────────────────────────────────
    m_in_dk = metric_panel(
        "In Denmark bbox",
        "Last in_denmark_box value (1 if GNSS truth is inside Denmark, 0 otherwise). "
        "Source: GENERIC_GNSS HK.",
        col_last_value_num("in_denmark_box", "In DK"),
        query='in_denmark_box: *')
    panels.append(lens_panel(m_in_dk, "In Denmark bbox", 0, 9, 10, 8))

    m_in_sci = metric_panel(
        "In SCIENCE mode",
        "Last in_science_mode value (1 if MGR has flipped to SCIENCE). "
        "Source: GENERIC_GNSS HK mirror of MGR mode.",
        col_last_value_num("in_science_mode", "In SCI"),
        query='in_science_mode: *')
    panels.append(lens_panel(m_in_sci, "In SCIENCE mode", 10, 9, 10, 8))

    m_mode = metric_panel(
        "Spacecraft mode",
        "Latest spacecraft_mode_name from MGR HK (SAFE / STANDBY / SCIENCE / OTHER).",
        col_last_value("spacecraft_mode_name.keyword", "Mode"),
        query='spacecraft_mode_name: *')
    panels.append(lens_panel(m_mode, "Spacecraft mode", 20, 9, 10, 8))

    m_seq = metric_panel(
        "Last echoed seq",
        "Last last_ping_seq value echoed by FSW via GENERIC_GNSS HK. "
        "Increments each successful round trip.",
        col_last_value_num("last_ping_seq", "Last seq"),
        query='last_ping_seq: *')
    panels.append(lens_panel(m_seq, "Last echoed seq", 30, 9, 10, 8))

    # Freshness tile: count of GENERIC_GNSS HK docs over the dashboard
    # time range. With timeRestore=True / timeFrom=now-30m and 1 Hz HK,
    # a healthy sim shows ~1800. A value near 0 means the sim or
    # god_view_capture has stalled and the four lastValue tiles to the
    # left may be showing stale cached state, not live truth.
    m_fresh = metric_panel(
        "GNSS HK docs (in time range)",
        "Count of GENERIC_GNSS HK docs in the dashboard time range "
        "(default last 30 min, 1 Hz HK -> ~1800 expected). "
        "If this is near 0, the four Status tiles to the left are "
        "showing stale lastValue cached state, not live data - "
        "check god_view_capture and FSW.",
        col_count("Docs"),
        query='in_denmark_box: *')
    panels.append(lens_panel(m_fresh, "Data freshness", 40, 9, 8, 8))

    # ── Row 3: round-trip header ────────────────────────────────────────
    panels.append(markdown_panel(
        "### Round-trip counters",
        "RTHdr", 0, 17, 48, 2))

    # ── Row 4: round-trip totals ────────────────────────────────────────
    m_uplinks = metric_panel(
        "Uplinks (GS_PING_CC sent)",
        "Total GS_PING_CC uplinks observed in the cFS Software Bus. "
        "Source: tag gnss_ping_uplink (Logstash adds it on the cmd path).",
        col_count("Uplinks"),
        query='tags: "gnss_ping_uplink"')
    panels.append(lens_panel(m_uplinks, "Uplinks", 0, 19, 16, 8))

    m_pings = metric_panel(
        "Pings serviced (max ping_count)",
        "Highest ping_count seen on GENERIC_GNSS HK. Cumulative since FSW boot.",
        col_max("ping_count", "Pings"),
        query='ping_count: *')
    panels.append(lens_panel(m_pings, "Pings serviced", 16, 19, 16, 8))

    m_rt = metric_panel(
        "Closed round-trips",
        "Distinct HK ticks tagged gnss_gs_ping_roundtrip (last_ping_seq>0). "
        "Each represents an uplink whose echo made it back through HK telemetry.",
        col_count("Round-trips"),
        query='tags: "gnss_gs_ping_roundtrip"')
    panels.append(lens_panel(m_rt, "Closed round-trips", 32, 19, 16, 8))

    # ── Row 5: position header ──────────────────────────────────────────
    panels.append(markdown_panel(
        "### Position - GNSS report vs. truth",
        "PosHdr", 0, 27, 48, 2))

    # ── Row 6: GNSS-reported vs truth latitude / longitude time series ──
    pos_lat = line_panel(
        "GNSS lat - reported vs truth",
        "gps_lat (GNSS sim's reported geodetic latitude) overlaid with "
        "gnss_truth_lat (42 dynamics ground truth). Should track tightly; "
        "any sustained delta exposes GNSS-side drift or units bug.",
        [(col_avg("gps_lat", "GNSS lat (deg)"), "GNSS lat (deg)"),
         (col_avg("gnss_truth_lat", "Truth lat (deg)"), "Truth lat (deg)")],
        query='gps_lat: * or gnss_truth_lat: *',
        series_type="line")
    panels.append(lens_panel(pos_lat, "GNSS lat", 0, 29, 24, 14))

    pos_lon = line_panel(
        "GNSS lon - reported vs truth",
        "gps_lon overlaid with gnss_truth_lon. Same fault model as the lat "
        "panel: an unexplained offset hints at a frame mismatch.",
        [(col_avg("gps_lon", "GNSS lon (deg)"), "GNSS lon (deg)"),
         (col_avg("gnss_truth_lon", "Truth lon (deg)"), "Truth lon (deg)")],
        query='gps_lon: * or gnss_truth_lon: *',
        series_type="line")
    panels.append(lens_panel(pos_lon, "GNSS lon", 24, 29, 24, 14))

    # ── Row 7: timeline header ──────────────────────────────────────────
    panels.append(markdown_panel(
        "### Timeline",
        "TLHdr", 0, 43, 48, 2))

    # ── Row 8: mode timeline + uplink rate ──────────────────────────────
    # Use col_avg(spacecraft_mode) instead of stacked-bar count-by-name so that
    # an isolated singleton HK packet (e.g. a stale line replayed by Logstash
    # from a prior run, or a one-off boot transient) renders as a brief 1/N
    # bucket spike rather than a tall stacked bar that misleadingly looks like
    # a sustained pass. Same convention as build_spacecraft_mode_changes()
    # below. Y values: 1=SAFE, 2=SAFE_REBOOT, 3=SCIENCE, 4=SCIENCE_REBOOT.
    mode_tl = line_panel(
        "Mode timeline",
        "Average of spacecraft_mode binned by @timestamp. Y values: 1=SAFE, "
        "2=SAFE_REBOOT, 3=SCIENCE, 4=SCIENCE_REBOOT. A real SCIENCE pass shows "
        "a sustained step at y=3; an isolated 03:19-style singleton shows as "
        "a narrow spike. Cross-check with the AND-gate panel below: real "
        "passes have in_denmark_box=1 AND in_science_mode=1 simultaneously.",
        [(col_avg("spacecraft_mode", "Mode"), "Mode")],
        query='type.keyword: "hk_decoded" and subsystem.keyword: "MGR"',
        series_type="line")
    panels.append(lens_panel(mode_tl, "Mode timeline", 0, 45, 24, 14))

    uplink_rate = line_panel(
        "Uplink rate (GS_PING_CC)",
        "Count of gnss_ping_uplink events over time. Bursts during a Denmark "
        "pass; quiet outside SCIENCE mode.",
        [(col_count(), "Uplinks")],
        query='tags: "gnss_ping_uplink"',
        series_type="line")
    panels.append(lens_panel(uplink_rate, "Uplink rate", 24, 45, 24, 14))

    # ── Row 9: GS uplink AND-gate + SAT_HELLO beacon rate ──────────────
    # gnss_gs_responder_task.rb gates uplinks on (SAT_HELLO advancing) AND
    # (mode == SCIENCE). When the Uplink rate panel above is empty during a
    # window where the satellite "should" be uplinking, these two panels
    # answer "which leg of the AND was missing".
    gate = line_panel(
        "GS uplink AND-gate (in_denmark_box & in_science_mode)",
        "Two time-aligned 0/1 series. The COSMOS GS responder uplinks "
        "GS_PING_CC only when BOTH are 1 simultaneously. If the Uplink rate "
        "panel is empty during a Denmark window, look here first to see "
        "which leg of the AND was missing. Source: GENERIC_GNSS HK (mirrors "
        "MGR mode + own FOV calc). col_max keeps the line at 1 if any "
        "sample in a bucket is 1.",
        [(col_max("in_denmark_box", "in_denmark_box"),  "in_denmark_box"),
         (col_max("in_science_mode", "in_science_mode"), "in_science_mode")],
        query='in_denmark_box: * or in_science_mode: *',
        series_type="line")
    panels.append(lens_panel(gate, "GS uplink AND-gate", 0, 59, 24, 14))

    sat_hello_rate = line_panel(
        "SAT_HELLO beacon rate",
        "Count per bucket of GENERIC_GNSS_SAT_HELLO_TLM_MID packets observed "
        "on the cFS Software Bus. FSW emits this beacon at 1 Hz only while "
        "in_denmark_box==1; rate drops to 0 the moment the FOV closes. The "
        "COSMOS GS responder uses the underlying SAT_HELLO sequence number "
        "as its FOV gate. Empty before a Denmark pass = expected; empty "
        "during a pass = check god_view_capture.py is running.",
        [(col_count(), "SAT_HELLO/bin")],
        query='msg_name.keyword: "GENERIC_GNSS_SAT_HELLO_TLM_MID" and type.keyword: "software_bus"',
        series_type="bar_stacked")
    panels.append(lens_panel(sat_hello_rate, "SAT_HELLO rate", 24, 59, 24, 14))

    # ── Row 10: round-trip log header ───────────────────────────────────
    panels.append(markdown_panel(
        "### Round-trip log",
        "TblHdr", 0, 73, 48, 2))

    # ── Row 11: round-trip table ────────────────────────────────────────
    rt_log = table_panel(
        "Round-trip log",
        "One row per (last_ping_seq, spacecraft_mode_name) pair tagged "
        "gnss_gs_ping_roundtrip. Confirms ping echoes only happen in SCIENCE "
        "mode and lets you see which sequence numbers were lost.",
        [{"op": "terms", "field": "last_ping_seq", "size": 50, "label": "Seq"},
         {"op": "terms", "field": "spacecraft_mode_name.keyword",
          "size": 5, "label": "Mode"}],
        query='tags: "gnss_gs_ping_roundtrip"')
    panels.append(lens_panel(rt_log, "Round-trip log", 0, 75, 48, 14))

    panelsJSON = json.dumps(panels, separators=(",", ":"))
    refs = collect_dashboard_refs(panels)
    attrs = {
        "title": "NOS3 EO1: GNSS-to-GS Validation",
        "hits": 0,
        "description": (
            "Closed-loop validation: GPS sees Denmark -> MGR flips to SCIENCE -> "
            "generic_gnss publishes DATA TLM -> COSMOS responder sends GS_PING_CC -> "
            "FSW echoes LAST_PING_SEQ. Surfaces each stage so a regression is "
            "traceable to a single link in the chain. When the Uplink rate panel "
            "is unexpectedly empty during a Denmark window, the GS uplink AND-gate "
            "panel below shows which gate (in_denmark_box or in_science_mode) was "
            "missing - the responder requires BOTH simultaneously. "
            "The Data freshness tile counts GNSS HK docs in the current time "
            "range; if that is near 0 the Status tiles are showing stale "
            "lastValue cached state, not live truth."
        ),
        "panelsJSON": panelsJSON,
        "optionsJSON": json.dumps({"useMargins": True, "syncColors": False, "hidePanelTitles": False}),
        "version": 1,
        "timeRestore": True,
        "timeFrom": "now-30m",
        "timeTo": "now",
        "refreshInterval": {"pause": False, "value": 10000},
        "kibanaSavedObjectMeta": {"searchSourceJSON": json.dumps({"query": {"query": "", "language": "kuery"}, "filter": []})},
    }
    return attrs, refs


# ===========================================================================
# Dashboard 9 - HK Telemetry Flow (Housekeeping App)
# ===========================================================================

def build_hk_telemetry_flow():
    panels = []

    msgs = line_panel(
        "HK Software Bus Messages by Type",
        "Top MIDs published by HK app, broken down by msg_name. Normal: a "
        "small set of HK_*_TLM_MID and HK_COMBINED_*_MID at HK rate. Sudden "
        "disappearance means the HK app stalled.",
        [(col_count(), "Count")],
        query='subsystem.keyword: "HK" and type.keyword: "software_bus"',
        break_by="msg_name.keyword",
        series_type="line")
    panels.append(lens_panel(msgs, "HK Software Bus Messages by Type", 0, 0, 48, 15))

    hb = line_panel(
        "HK Housekeeping TLM Heartbeat (HK_HK_TLM_MID)",
        "HK_HK_TLM_MID = 0x0890. Rolling count of HK app heartbeats. Should "
        "monotonically tick at the HK cycle rate. Flat or decreasing means "
        "HK is hung.",
        [(col_count(), "HK TLM Count")],
        query='msg_name.keyword: "HK_HK_TLM_MID"',
        series_type="bar_stacked")
    panels.append(lens_panel(hb, "HK Heartbeat", 0, 15, 24, 15))

    combined = line_panel(
        "HK Combined Packet 1 Output Rate (HK_COMBINED_PKT1_MID)",
        "HK_COMBINED_PKT1_MID = 0x089B. Aggregated downlink packet that "
        "bundles multiple subsystem HKs. Drop indicates the HK aggregator "
        "is failing to produce combined output.",
        [(col_count(), "Combined PKT1 Count")],
        query='msg_name.keyword: "HK_COMBINED_PKT1_MID"',
        series_type="bar_stacked")
    panels.append(lens_panel(combined, "HK Combined Packet 1", 24, 15, 24, 15))

    panelsJSON = json.dumps(panels, separators=(",", ":"))
    refs = collect_dashboard_refs(panels)
    attrs = {
        "title": "HK Telemetry Flow (Housekeeping App)",
        "hits": 0,
        "description": "Housekeeping app Software Bus activity: message rates split by msg_name, HK TLM heartbeat, and combined output packet rate. Confirms HK is aggregating and publishing.",
        "panelsJSON": panelsJSON,
        "optionsJSON": json.dumps({"useMargins": True, "syncColors": False, "hidePanelTitles": False}),
        "version": 1,
        "timeRestore": False,
        "kibanaSavedObjectMeta": {"searchSourceJSON": json.dumps({"query": {"query": "", "language": "kuery"}, "filter": []})},
    }
    return attrs, refs


# ===========================================================================
# Dashboard 10 - HS CPU Monitor (Health & Safety App)
# ===========================================================================

def build_hs_cpu_monitor():
    panels = []

    cpu_avg = line_panel(
        "Container CPU % (avg)",
        "Average container CPU% over time, sourced from cpu_monitor sidecar "
        "TOTAL_CPU_PCT. Used in place of HS-app cpu_avg_pct (always empty on "
        "this Linux PSP because OS_CpuLoad is unimplemented).",
        [(col_avg("TOTAL_CPU_PCT", "Avg CPU %"), "Avg CPU %")],
        query='TOTAL_CPU_PCT: *',
        series_type="line")
    panels.append(lens_panel(cpu_avg, "Container CPU % (avg)", 0, 0, 48, 15))

    cpu_avg_peak = line_panel(
        "Container CPU % (avg / peak)",
        "Container CPU% from cpu_monitor TOTAL_CPU_PCT. Avg uses average(); "
        "peak uses max() of the same field.",
        [(col_avg("TOTAL_CPU_PCT", "Avg CPU %"), "Avg CPU %"),
         (col_max("TOTAL_CPU_PCT", "Peak CPU %"), "Peak CPU %")],
        query='TOTAL_CPU_PCT: *',
        series_type="line")
    panels.append(lens_panel(cpu_avg_peak, "Container CPU % (avg / peak)", 0, 15, 32, 15))

    hb = line_panel(
        "HS Heartbeat Rate (Software Bus)",
        "HS_HK_TLM_MID = 0x08AD. Software Bus heartbeat from the Health & "
        "Safety app. Steady at the HS HK cycle rate; absence means HS is "
        "dead and the watchdog is not firing.",
        [(col_count(), "Count")],
        query='subsystem.keyword: "HS" and type.keyword: "software_bus"',
        break_by="msg_name.keyword",
        series_type="line")
    panels.append(lens_panel(hb, "HS Heartbeat Rate", 32, 15, 16, 15))

    panelsJSON = json.dumps(panels, separators=(",", ":"))
    refs = collect_dashboard_refs(panels)
    attrs = {
        "title": "HS CPU Monitor (Health & Safety App)",
        "hits": 0,
        "description": "CPU utilisation measured by the cpu_monitor sidecar via /proc/stat delta (TOTAL_CPU_PCT). Shows current load, running peak, and HS heartbeat rate on the Software Bus.",
        "panelsJSON": panelsJSON,
        "optionsJSON": json.dumps({"useMargins": True, "syncColors": False, "hidePanelTitles": False}),
        "version": 1,
        "timeRestore": False,
        "kibanaSavedObjectMeta": {"searchSourceJSON": json.dumps({"query": {"query": "", "language": "kuery"}, "filter": []})},
    }
    return attrs, refs


# ===========================================================================
# Dashboard 11 - Spacecraft Mode Changes
# ===========================================================================

def build_spacecraft_mode_changes():
    panels = []

    md = (
"""### Spacecraft Mode Changes

**Mode legend** (`spacecraft_mode` from decoded MGR HK):
- **1 = SAFE** (green, nominal idle)
- **2 = STANDBY** (yellow, reserved - rarely used)
- **3 = SCIENCE** (green, mission active)
- **4 = UNKNOWN** (red, out-of-range; investigate)

Logstash adds the tag `spacecraft_mode_change` (and the fields `spacecraft_mode_prev`, `spacecraft_mode_new`, `mode_change_label`) the moment the value transitions. The Mode-Change Events panel below lists each transition; the EVS tail at the bottom helps explain **why** each transition fired (LC AP3/AP4 entry/exit, ground command, fault response, etc.)."""
    )
    panels.append(markdown_panel(md, "About this dashboard", 0, 0, 48, 5))

    timeline = line_panel(
        "Mode timeline",
        "Average of spacecraft_mode binned by @timestamp. Step changes mark "
        "transitions. Y values: 1=SAFE, 2=STANDBY, 3=SCIENCE, 4=UNKNOWN.",
        [(col_avg("spacecraft_mode", "Mode"), "Mode")],
        query='type.keyword: "hk_decoded" and subsystem.keyword: "MGR"',
        series_type="line")
    panels.append(lens_panel(timeline, "Mode timeline", 0, 5, 48, 14))

    events = table_panel(
        "Mode-change events",
        "Transition labels seen in the time range (mode_change_label = "
        "'<prev> -> <new>') and how many times each fired.",
        [{"op": "terms", "field": "mode_change_label.keyword",
          "size": 20, "label": "Transition"}],
        query='tags: "spacecraft_mode_change"')
    panels.append(lens_panel(events, "Mode-change events", 0, 19, 24, 14))

    cur = metric_panel(
        "Current spacecraft mode",
        "Most-recent spacecraft_mode value observed. Pair with the timeline "
        "above to confirm whether this is the steady state.",
        col_last_value_num("spacecraft_mode", "Mode"),
        query='type.keyword: "hk_decoded" and subsystem.keyword: "MGR"')
    panels.append(lens_panel(cur, "Current spacecraft mode", 24, 19, 12, 14))

    share = pie_panel(
        "Time in each mode",
        "Share of MGR HK samples in each mode across the selected window. "
        "MGR publishes HK at a steady rate, so sample share approximates "
        "wall-clock time spent in each mode.",
        terms_field="spacecraft_mode_name.keyword",
        slice_label="Mode",
        query='type.keyword: "hk_decoded" and subsystem.keyword: "MGR"',
        shape="pie", size=6)
    panels.append(lens_panel(share, "Time in each mode", 36, 19, 12, 14))

    evs = table_panel(
        "LC / MGR EVS events",
        "Live tail of cFE EVS events from LC and MGR. LC AP3/AP4 fires "
        "trigger the mode transitions seen above; MGR logs 'Set mode "
        "command received [N]' for each commanded transition.",
        [{"op": "terms", "field": "evs_app_name.keyword",
          "size": 3, "label": "App"},
         {"op": "terms", "field": "evs_severity.keyword",
          "size": 4, "label": "Severity"},
         {"op": "terms", "field": "evs_message.keyword",
          "size": 30, "label": "Message"}],
        query='subsystem.keyword: "cfs_evs" and evs_app_name.keyword: ("LC" or "MGR")')
    panels.append(lens_panel(evs, "LC / MGR EVS events", 0, 33, 48, 14))

    panelsJSON = json.dumps(panels, separators=(",", ":"))
    refs = collect_dashboard_refs(panels)
    attrs = {
        "title": "Spacecraft Mode Changes",
        "hits": 0,
        "description": "How and when spacecraft_mode transitions over the selected time range. Reads decoded MGR HK telemetry and the Logstash-emitted spacecraft_mode_change tag. Mode legend: 1=SAFE, 2=STANDBY, 3=SCIENCE, 4=UNKNOWN.",
        "panelsJSON": panelsJSON,
        "optionsJSON": json.dumps({"useMargins": True, "syncColors": False, "hidePanelTitles": False}),
        "version": 1,
        "timeRestore": False,
        "kibanaSavedObjectMeta": {"searchSourceJSON": json.dumps({"query": {"query": "", "language": "kuery"}, "filter": []})},
    }
    return attrs, refs


# ===========================================================================
# Dashboard 12 - Sensor Ground Truth Overview
# ===========================================================================

def build_sensor_ground_truth():
    panels = []

    eps = line_panel(
        "EPS Power Balance",
        "Live solar input (eps_solar_power_w), total load (eps_power_used_w), "
        "and battery state of charge (eps_battery_wh) from omni_logs/nos3-eps.log. "
        "Normal: solar > used in sun, balanced over an orbit.",
        [(col_avg("eps_solar_power_w", "Solar Power (W)"), "Solar Power (W)"),
         (col_avg("eps_power_used_w", "Power Used (W)"), "Power Used (W)")],
        query='eps_solar_power_w: *',
        series_type="line")
    panels.append(lens_panel(eps, "EPS Power Balance", 0, 0, 24, 15))

    bat = line_panel(
        "Battery State",
        "eps_battery_wh (charge) and eps_battery_mv (millivolts) over time. "
        "Normal: ~25.2 V at full charge. Concerning: voltage sliding below "
        "22 V or sustained negative balance.",
        [(col_avg("eps_battery_wh", "Battery Charge (Wh)"), "Battery Charge (Wh)"),
         (col_avg("eps_battery_mv", "Battery Voltage (mV)"), "Battery Voltage (mV)")],
        query='eps_battery_wh: *',
        series_type="area")
    panels.append(lens_panel(bat, "Battery State", 24, 0, 24, 15))

    gps_track = line_panel(
        "GPS Ground Track (lat/lon)",
        "Sub-satellite point in degrees parsed from omni_logs/nos3-gps.log. "
        "Normal: continuous LEO trace covering +/-50 deg latitude per orbit. "
        "Empty: GPS sim is not connected to 42.",
        [(col_avg("gps_lat", "Latitude (deg)"), "Latitude (deg)"),
         (col_avg("gps_lon", "Longitude (deg)"), "Longitude (deg)")],
        query='gps_lat: *',
        series_type="line")
    panels.append(lens_panel(gps_track, "GPS Ground Track", 0, 15, 24, 15))

    gps_alt = line_panel(
        "GPS Altitude",
        "gps_alt in metres above WGS-84. Normal: ~4.0e5 (400 km circular LEO "
        "from Orb_LEO.txt). Concerning: rapid drift or values outside 3.5-4.5e5.",
        [(col_avg("gps_alt", "Altitude (m)"), "Altitude (m)")],
        query='gps_alt: *',
        series_type="area")
    panels.append(lens_panel(gps_alt, "GPS Altitude", 24, 15, 24, 15))

    rw = line_panel(
        "Reaction Wheel Momenta",
        "rw_momentum in N*m*s split by subsystem (nos3-rw0/rw1/rw2). "
        "Normal: small, bounded oscillations during attitude control; "
        "saturation = sustained drift toward the wheel limit.",
        [(col_avg("rw_momentum", "Momentum (Nms)"), "Momentum (Nms)")],
        query='rw_momentum: *',
        break_by="subsystem.keyword",
        series_type="line")
    panels.append(lens_panel(rw, "Reaction Wheel Momenta", 0, 30, 24, 15))

    imu_acc = line_panel(
        "IMU Accelerations",
        "Body-frame linear acceleration components (m/s^2) from the IMU sim. "
        "Normal: near-zero during free flight; transients during torquer or "
        "thruster commanding.",
        [(col_avg("imu_acc_x", "Acc X (m/s^2)"), "Acc X (m/s^2)"),
         (col_avg("imu_acc_y", "Acc Y (m/s^2)"), "Acc Y (m/s^2)"),
         (col_avg("imu_acc_z", "Acc Z (m/s^2)"), "Acc Z (m/s^2)")],
        query='imu_acc_x: *',
        series_type="line")
    panels.append(lens_panel(imu_acc, "IMU Accelerations", 24, 30, 24, 15))

    imu_gyro = line_panel(
        "IMU Gyro Rates",
        "Body-rate components (rad/s) from the IMU sim. Normal: a few "
        "millirad/s during attitude maintenance; ramped during slews.",
        [(col_avg("imu_gyro_x", "Gyro X (rad/s)"), "Gyro X (rad/s)"),
         (col_avg("imu_gyro_y", "Gyro Y (rad/s)"), "Gyro Y (rad/s)"),
         (col_avg("imu_gyro_z", "Gyro Z (rad/s)"), "Gyro Z (rad/s)")],
        query='imu_gyro_x: *',
        series_type="line")
    panels.append(lens_panel(imu_gyro, "IMU Gyro Rates", 0, 45, 24, 15))

    mag = line_panel(
        "Magnetometer",
        "Magnetic field components (T) reported by the magnetometer sim. "
        "Normal: smooth orbital sinusoids; spikes can indicate torquer "
        "saturation or sim noise injection.",
        [(col_avg("mag_x", "Mag X (T)"), "Mag X (T)"),
         (col_avg("mag_y", "Mag Y (T)"), "Mag Y (T)"),
         (col_avg("mag_z", "Mag Z (T)"), "Mag Z (T)")],
        query='mag_x: *',
        series_type="line")
    panels.append(lens_panel(mag, "Magnetometer", 24, 45, 24, 15))

    fss = line_panel(
        "FSS Sun Angles",
        "Fine sun sensor angles (rad) - alpha and beta. Visible only when "
        "the spacecraft is sun-illuminated; saturates / clips when the sun "
        "is outside the FSS field of view.",
        [(col_avg("fss_alpha", "Alpha (rad)"), "Alpha (rad)"),
         (col_avg("fss_beta", "Beta (rad)"), "Beta (rad)")],
        query='fss_alpha: *',
        series_type="line")
    panels.append(lens_panel(fss, "FSS Sun Angles", 0, 60, 24, 15))

    st = line_panel(
        "Star Tracker Quaternion",
        "Star tracker attitude quaternion (q0 scalar, q1/q2/q3 vector). "
        "Normal: smooth evolution following attitude. Empty: 42 has not "
        "yet supplied truth attitude (sim_connectivity_warning tag).",
        [(col_avg("st_q0", "q0"), "q0"),
         (col_avg("st_q1", "q1"), "q1"),
         (col_avg("st_q2", "q2"), "q2"),
         (col_avg("st_q3", "q3"), "q3")],
        query='st_q0: *',
        series_type="line")
    panels.append(lens_panel(st, "Star Tracker Quaternion", 24, 60, 24, 15))

    panelsJSON = json.dumps(panels, separators=(",", ":"))
    refs = collect_dashboard_refs(panels)
    attrs = {
        "title": "Sensor Ground Truth Overview",
        "hits": 0,
        "description": "Simulator sensor ground-truth values for all hardware subsystems (EPS, GPS, RW, IMU, MAG, FSS, Star Tracker). Baseline for false-telemetry detection: compare these sim-side values with the Software Bus TLM rates on the FSW vs Sim Cross-Reference dashboard.",
        "panelsJSON": panelsJSON,
        "optionsJSON": json.dumps({"useMargins": True, "syncColors": False, "hidePanelTitles": False}),
        "version": 1,
        "timeRestore": False,
        "kibanaSavedObjectMeta": {"searchSourceJSON": json.dumps({"query": {"query": "", "language": "kuery"}, "filter": []})},
    }
    return attrs, refs


# ===========================================================================
# Dashboard 13 - Thesis: Cyber-Physical Attack Radar
# ===========================================================================

def build_thesis_attack_radar():
    panels = []

    layer_rate = line_panel(
        "Software Bus packet rate by attack layer",
        "Packet rate split by cFS attack layer (FSW_CORE / FSW_HERITAGE / "
        "COMPONENT / RESEARCH / UNKNOWN). Baseline composition is dominated "
        "by FSW_HERITAGE and COMPONENT. A surge in FSW_CORE or UNKNOWN is a "
        "radar trigger.",
        [(col_count(), "Packets")],
        query='type.keyword: "software_bus"',
        break_by="layer.keyword",
        series_type="line")
    panels.append(lens_panel(layer_rate, "SB rate by attack layer", 0, 0, 24, 15))

    cmd_mids = line_panel(
        "Top apps while EVS Command events fire",
        "evs_app_name stack during cfs_evs Command events. Used to spot "
        "noisy_app or other attack-driven Command spikes.",
        [(col_count(), "Events")],
        query='subsystem.keyword: "cfs_evs" and evs_message: ("Command" or "command")',
        break_by="evs_app_name.keyword",
        series_type="bar_stacked")
    panels.append(lens_panel(cmd_mids, "Top apps (EVS Commands)", 24, 0, 24, 15))

    radio = line_panel(
        "RADIO subsystem activity",
        "Log volume from the RADIO subsystem. Spike correlates with the "
        "noisy_app DoS or any ground-side flooding that hits the radio sim.",
        [(col_count(), "Lines")],
        query='subsystem.keyword: "RADIO"',
        series_type="area")
    panels.append(lens_panel(radio, "RADIO subsystem activity", 0, 15, 24, 15))

    anom = line_panel(
        "Anomaly events by subsystem",
        "Stacked count of anomaly indicators broken down by subsystem: "
        "ERROR/CRITICAL EVS events plus Logstash-tagged sim_error and "
        "sb_sequence_gap docs. Empty bars are honest: no anomalies.",
        [(col_count(), "Anomalies")],
        query='evs_severity.keyword: ("ERROR" or "CRITICAL") or tags: "sim_error" or tags: "sb_sequence_gap"',
        break_by="subsystem.keyword",
        series_type="bar_stacked")
    panels.append(lens_panel(anom, "Anomaly events by subsystem", 24, 15, 24, 15))

    cpu_max = line_panel(
        "CPU Utilisation (max TOTAL_CPU_PCT)",
        "TOTAL_CPU_PCT max envelope from cpu_monitor. Spikes correlate with "
        "noisy_app DoS or with checksum / dwell-table churn.",
        [(col_max("TOTAL_CPU_PCT", "Max CPU %"), "Max CPU %")],
        query='TOTAL_CPU_PCT: *',
        series_type="line")
    panels.append(lens_panel(cpu_max, "CPU max", 0, 30, 24, 15))

    bat = line_panel(
        "Battery (peak Wh)",
        "Battery state-of-charge max(eps_battery_wh) bound. Used as a "
        "physical-sanity reference (real attacks should not move the cell).",
        [(col_max("eps_battery_wh", "Battery (Wh)"), "Battery (Wh)")],
        query='eps_battery_wh: *',
        series_type="area")
    panels.append(lens_panel(bat, "Battery peak Wh", 24, 30, 24, 15))

    tag_heat = line_panel(
        "Software Bus tag heat-map",
        "Volume of Software Bus docs over time. Stack on attack tags "
        "(blackboard_error, sb_pipe_overflow, sb_sequence_gap, "
        "cs_checksum_failure, mm_memory_poke, md_jam_dwell, attack_armed, "
        "attack_loaded) for the heat view.",
        [(col_count(), "Packets")],
        query='type.keyword: "software_bus"',
        break_by="tags.keyword",
        series_type="bar_stacked")
    panels.append(lens_panel(tag_heat, "SB tag heat-map", 0, 45, 48, 15))

    panelsJSON = json.dumps(panels, separators=(",", ":"))
    refs = collect_dashboard_refs(panels)
    attrs = {
        "title": "Thesis: Cyber-Physical Attack Radar",
        "hits": 0,
        "description": "Thesis radar: cyber-physical attack indicators. Layer-aware view of the cFS Software Bus (FSW_CORE / FSW_HERITAGE / COMPONENT / RESEARCH / UNKNOWN), CPU pressure, EPS battery state, RADIO traffic, anomaly counts, and the Logstash anomaly-tag heat-map.",
        "panelsJSON": panelsJSON,
        "optionsJSON": json.dumps({"useMargins": True, "syncColors": False, "hidePanelTitles": False}),
        "version": 1,
        "timeRestore": False,
        "kibanaSavedObjectMeta": {"searchSourceJSON": json.dumps({"query": {"query": "", "language": "kuery"}, "filter": []})},
    }
    return attrs, refs


# ===========================================================================
# Dashboard 14 - OBC Performance Monitor (CPU / SCH / TIME)
# ===========================================================================

def build_obc_perf_monitor():
    panels = []

    cpu_thr = line_panel(
        "CPU % by Thread",
        "Per-thread CPU_PCT from cpu_monitor (Docker stats of the FSW "
        "container). Normal: ~5% steady on cFE_ES, single-digit on each "
        "app. Concerning: any single thread sustained >25%.",
        [(col_avg("CPU_PCT", "Avg CPU %"), "Avg CPU %")],
        query='cpu_monitor and THREAD_NAME: *',
        break_by="THREAD_NAME.keyword",
        series_type="line")
    panels.append(lens_panel(cpu_thr, "CPU % by Thread", 0, 0, 48, 15))

    cpu_total = line_panel(
        "Total CPU % + Num PIDs",
        "Container-wide TOTAL_CPU_PCT alongside NUM_PIDS. Normal: ~30% CPU "
        "and ~25 PIDs. A NUM_PIDS jump means a fork bomb or app respawn "
        "loop.",
        [(col_avg("TOTAL_CPU_PCT", "Avg Total CPU %"), "Avg Total CPU %"),
         (col_avg("NUM_PIDS", "Avg Num PIDs"), "Avg Num PIDs")],
        query='cpu_monitor',
        series_type="line")
    panels.append(lens_panel(cpu_total, "Total CPU % + Num PIDs", 0, 15, 24, 15))

    err = line_panel(
        "App Error Counters",
        "cmd_count and cmd_err_count from hk_decoded across apps. Normal: "
        "cmd_err_count stays at 0. Any climbing error counter indicates "
        "rejected commands.",
        [(col_avg("cmd_count", "Avg cmd_count"), "Avg cmd_count"),
         (col_avg("cmd_err_count", "Avg cmd_err_count"), "Avg cmd_err_count")],
        query='cmd_count: *',
        series_type="line")
    panels.append(lens_panel(err, "App Error Counters", 24, 15, 24, 15))

    skipped = metric_panel(
        "SCH slots skipped (count)",
        "SCH SkippedSlotCount events from the cFE EVS log. Normal: 0. "
        "Non-zero means SCH could not run the activity table on time, "
        "usually because another app stole the major frame.",
        col_count("Slots skipped"),
        query='subsystem.keyword: "cfs_evs" and evs_message: "Slots skipped"')
    panels.append(lens_panel(skipped, "SCH slots skipped", 0, 30, 16, 15))

    multi = line_panel(
        "Multiple Slots Executed",
        "SCH MultipleSlotsCount events. Normal: 0. Non-zero means SCH ran "
        "more than one minor frame to catch up after a stall, indicating "
        "CPU contention.",
        [(col_count(), "Count")],
        query='subsystem.keyword: "cfs_evs" and evs_message: "Multiple slots"',
        series_type="bar_stacked")
    panels.append(lens_panel(multi, "Multiple Slots Executed", 16, 30, 16, 15))

    mfsync = line_panel(
        "Major Frame Sync",
        "SCH SyncToMET counter; should tick every major frame (default "
        "1 Hz). Flat or decreasing = SCH lost sync to the cFE TIME service.",
        [(col_count(), "Count")],
        query='subsystem.keyword: "cfs_evs" and evs_message: "Major Frame Sync"',
        series_type="bar_stacked")
    panels.append(lens_panel(mfsync, "Major Frame Sync", 32, 30, 16, 15))

    pids = line_panel(
        "Process Count Over Time",
        "NUM_PIDS from cpu_monitor. Steady ~25 in nominal flight; visible "
        "spikes during reboots or attack scenarios that fork helpers.",
        [(col_avg("NUM_PIDS", "Avg Num PIDs"), "Avg Num PIDs")],
        query='cpu_monitor',
        series_type="area")
    panels.append(lens_panel(pids, "Process Count Over Time", 0, 45, 48, 15))

    panelsJSON = json.dumps(panels, separators=(",", ":"))
    refs = collect_dashboard_refs(panels)
    attrs = {
        "title": "OBC Performance Monitor (A: CPU Budget / B: SCH Health / C: TIME)",
        "hits": 0,
        "description": "Onboard Computer performance monitor. Three panes: A=CPU budget (per-thread CPU_PCT, container-wide TOTAL_CPU_PCT, NUM_PIDS); B=SCH health (SkippedSlotCount, MultipleSlotsCount); C=TIME (Major Frame Sync to MET). Steady-state expectation: ~30% CPU, ~25 PIDs, zero skipped or multi slots.",
        "panelsJSON": panelsJSON,
        "optionsJSON": json.dumps({"useMargins": True, "syncColors": False, "hidePanelTitles": False}),
        "version": 1,
        "timeRestore": False,
        "kibanaSavedObjectMeta": {"searchSourceJSON": json.dumps({"query": {"query": "", "language": "kuery"}, "filter": []})},
    }
    return attrs, refs


# ===========================================================================
# Dashboard 15 - FSW vs Sim Cross-Reference
# ===========================================================================

def build_fsw_vs_sim_xref():
    panels = []

    eps_rate = line_panel(
        "EPS: S/W Bus TLM Rate",
        "Packet rate of GENERIC_EPS_HK_TLM_MID (msg_id 0x091A). FSW "
        "heartbeat for EPS; should be steady at the HK rate. Drop = EPS "
        "app failure or SB pipe overflow.",
        [(col_count(), "Packets")],
        query='type.keyword: "software_bus" and msg_id: "0x091A"',
        series_type="bar_stacked")
    panels.append(lens_panel(eps_rate, "EPS S/W Bus rate", 0, 0, 24, 15))

    rw_rate = line_panel(
        "RW: S/W Bus TLM Rate",
        "Packet rate of GENERIC_RW_APP_HK_TLM_MID (msg_id 0x0993). One "
        "publish per RW per HK cycle. Drop indicates RW app stall or "
        "pipe overflow.",
        [(col_count(), "Packets")],
        query='type.keyword: "software_bus" and msg_id: "0x0993"',
        series_type="bar_stacked")
    panels.append(lens_panel(rw_rate, "RW S/W Bus rate", 24, 0, 24, 15))

    gps_rate = line_panel(
        "GPS: S/W Bus TLM Rate",
        "Packet rate of NOVATEL_OEM615_HK_TLM_MID (msg_id 0x0870 or "
        "0x0871). Steady at the GPS app HK rate. Drop = GPS app failure.",
        [(col_count(), "Packets")],
        query='type.keyword: "software_bus" and (msg_id: "0x0870" or msg_id: "0x0871")',
        series_type="bar_stacked")
    panels.append(lens_panel(gps_rate, "GPS S/W Bus rate", 0, 15, 24, 15))

    sb_total = line_panel(
        "FSW: Software Bus Total Rate",
        "All software_bus docs per second. Reference for noisy_app "
        "saturation studies; baseline ~30-50 pps.",
        [(col_count(), "Packets")],
        query='type.keyword: "software_bus"',
        series_type="bar_stacked")
    panels.append(lens_panel(sb_total, "FSW SB total rate", 24, 15, 24, 15))

    cpu = line_panel(
        "FSW: CPU Usage",
        "TOTAL_CPU_PCT from cpu_monitor of the FSW container. Normal "
        "~30%. Compare against SB rate spikes.",
        [(col_avg("TOTAL_CPU_PCT", "Total CPU %"), "Total CPU %")],
        query='TOTAL_CPU_PCT: *',
        series_type="line")
    panels.append(lens_panel(cpu, "FSW CPU Usage", 0, 30, 24, 15))

    eps_sim = line_panel(
        "EPS: Sim Battery & Power",
        "Side-by-side eps_battery_wh and eps_solar_power_w from the EPS "
        "sim log. Cross-checks the FSW HK numbers above.",
        [(col_avg("eps_battery_wh", "Battery Charge (Wh)"), "Battery Charge (Wh)"),
         (col_avg("eps_solar_power_w", "Solar Power (W)"), "Solar Power (W)")],
        query='eps_battery_wh: *',
        series_type="area")
    panels.append(lens_panel(eps_sim, "EPS Sim Battery & Power", 24, 30, 24, 15))

    rw_sim = line_panel(
        "RW: Sim Momenta (by wheel)",
        "rw_momentum for nos3-rw0 / rw1 / rw2. If FSW HK rate is steady "
        "but sim momenta are flat-zero, the sim is not getting 42 frames "
        "and the root cause is upstream of the FSW.",
        [(col_avg("rw_momentum", "Momentum (Nms)"), "Momentum (Nms)")],
        query='rw_momentum: *',
        break_by="subsystem.keyword",
        series_type="line")
    panels.append(lens_panel(rw_sim, "RW Sim Momenta", 0, 45, 24, 15))

    gps_sim = line_panel(
        "GPS: Sim Altitude",
        "gps_alt from the sim log. Compared against the FSW HK packet "
        "rate to validate FSW is publishing real samples, not stale ones.",
        [(col_avg("gps_alt", "Altitude (m)"), "Altitude (m)")],
        query='gps_alt: *',
        series_type="line")
    panels.append(lens_panel(gps_sim, "GPS Sim Altitude", 24, 45, 24, 15))

    panelsJSON = json.dumps(panels, separators=(",", ":"))
    refs = collect_dashboard_refs(panels)
    attrs = {
        "title": "FSW vs Sim Cross-Reference",
        "hits": 0,
        "description": "Software Bus TLM message rates (FSW) alongside simulator ground-truth sensor values for EPS, RW, GPS. Divergence between the FSW HK rate and the sim-side numeric value indicates a supply-chain or telemetry-injection attack.",
        "panelsJSON": panelsJSON,
        "optionsJSON": json.dumps({"useMargins": True, "syncColors": False, "hidePanelTitles": False}),
        "version": 1,
        "timeRestore": False,
        "kibanaSavedObjectMeta": {"searchSourceJSON": json.dumps({"query": {"query": "", "language": "kuery"}, "filter": []})},
    }
    return attrs, refs


# ---------- registry + CLI -------------------------------------------------
# Single source of truth: every saved object this script owns. The CLI selects
# from this list via --only / --exclude; prune_orphan_dashboards() uses the
# dashboard subset as the expected set.

REGISTRY = [
    # (id, kibana_type, builder_func, short_label)
    ("nos3-std-telemetry-overview",     "dashboard", build_std_telemetry_overview,
     "Standard telemetry overview"),
    ("nos3-std-mission-validation",     "dashboard", build_std_mission_validation,
     "Standard mission validation"),
    ("nos3-mission-denmark",            "dashboard", build_mission_denmark,
     "Mission: Denmark"),
    ("nos3-eo1-power-budget",           "dashboard", build_eo1_power_budget,
     "EO1 power budget"),
    ("nos3-eo1-adcs-health",            "dashboard", build_eo1_adcs_health,
     "EO1 ADCS health"),
    ("nos3-eo1-fsw-health",             "dashboard", build_eo1_fsw_health,
     "EO1 FSW health"),
    ("nos3-eo1-ttc-downlink-validation", "dashboard", build_eo1_ttc_downlink_validation,
     "EO1 TT&C downlink validation"),
    ("nos3-eo1-gnss-gs-validation",     "dashboard", build_eo1_gnss_gs_validation,
     "EO1 GNSS-to-GS validation"),
    ("nos3-hk-telemetry-flow",          "dashboard", build_hk_telemetry_flow,
     "HK telemetry flow"),
    ("nos3-hs-cpu-monitor",             "dashboard", build_hs_cpu_monitor,
     "HS CPU monitor"),
    ("nos3-spacecraft-mode-changes",    "dashboard", build_spacecraft_mode_changes,
     "Spacecraft mode changes"),
    ("nos3-sensor-ground-truth",        "dashboard", build_sensor_ground_truth,
     "Sensor ground truth overview"),
    ("nos3-thesis-attack-radar",        "dashboard", build_thesis_attack_radar,
     "Thesis: cyber-physical attack radar"),
    ("nos3-obc-perf-monitor",           "dashboard", build_obc_perf_monitor,
     "OBC performance monitor"),
    ("nos3-fsw-vs-sim-xref",            "dashboard", build_fsw_vs_sim_xref,
     "FSW vs sim cross-reference"),
    ("nos3-evs-error-tail",             "search",    build_evs_error_search,
     "cFE EVS - ERROR / CRITICAL tail (saved search)"),
]


def cli_print_catalog():
    print("Available saved objects:")
    n_dash = sum(1 for _, t, _, _ in REGISTRY if t == "dashboard")
    n_search = sum(1 for _, t, _, _ in REGISTRY if t == "search")
    for so_id, so_type, _, label in REGISTRY:
        print(f"  [{so_type:9s}] {so_id:36s} - {label}")
    print(f"\nTotal: {n_dash} dashboards + {n_search} saved search "
          f"= {len(REGISTRY)} object(s).")


def cli_filter(only=None, exclude=None):
    """Apply --only / --exclude to REGISTRY. Returns the filtered list, in
    REGISTRY order. Unknown IDs are an error."""
    known = {so_id for so_id, _, _, _ in REGISTRY}

    def check(ids, label):
        bad = [i for i in ids if i not in known]
        if bad:
            print(f"[ERR] unknown {label} ID(s): {', '.join(bad)}",
                  file=sys.stderr)
            print(f"      run with --list to see the catalog.",
                  file=sys.stderr)
            sys.exit(2)

    if only:
        check(only, "--only")
        keep = set(only)
        return [e for e in REGISTRY if e[0] in keep]
    if exclude:
        check(exclude, "--exclude")
        drop = set(exclude)
        return [e for e in REGISTRY if e[0] not in drop]
    return list(REGISTRY)


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(
        description="Build NOS3 Kibana dashboards via the Saved Objects API.")
    grp = parser.add_mutually_exclusive_group()
    grp.add_argument("--only", metavar="ID[,ID...]",
                     help="Build only the listed saved-object IDs.")
    grp.add_argument("--exclude", metavar="ID[,ID...]",
                     help="Build everything EXCEPT the listed IDs.")
    parser.add_argument("--list", action="store_true",
                        help="Print the catalog of available IDs and exit.")
    parser.add_argument("--no-prune", action="store_true",
                        help="Do not delete orphan dashboards in Kibana.")
    parser.add_argument("--no-refresh", action="store_true",
                        help="Skip the data-view field-cache refresh step.")
    args = parser.parse_args()

    if args.list:
        cli_print_catalog()
        sys.exit(0)

    only = [s.strip() for s in args.only.split(",")] if args.only else None
    exclude = [s.strip() for s in args.exclude.split(",")] if args.exclude else None
    selected = cli_filter(only=only, exclude=exclude)

    ensure_data_view(IP, "nos3-telemetry*", "@timestamp")

    if not args.no_refresh:
        refresh_data_view_fields(IP, "nos3-telemetry*")

    delete_obsolete(KIBANA, OBSOLETE_OBJECTS)
    for so_type, query in OBSOLETE_TITLE_SWEEPS:
        delete_by_search(KIBANA, so_type, query)

    n_dash = 0
    n_search = 0
    for so_id, so_type, builder, label in selected:
        attrs, refs = builder()
        resp = put_so(so_type, so_id, attrs, refs)
        if so_type == "dashboard":
            n_dash += 1
            print(f"[OK] Dashboard {n_dash:>2}: {resp['id']:<36} "
                  f"updated {resp.get('updated_at','')}")
        else:
            n_search += 1
            print(f"[OK] Saved search: {resp['id']:<36} "
                  f"updated {resp.get('updated_at','')}")

    # Prune orphan dashboards. Only safe when running the FULL set; partial
    # builds would otherwise delete the dashboards the user did not select.
    if args.no_prune or only or exclude:
        if not args.no_prune:
            print("[skip-prune] partial build; orphan-dashboard prune disabled "
                  "(re-run without --only/--exclude to prune).")
    else:
        expected = {so_id for so_id, t, _, _ in REGISTRY if t == "dashboard"}
        pruned, _ = prune_orphan_dashboards(expected)
        print(f"[OK] Pruned {pruned} orphan dashboard(s); "
              f"expected total = {len(expected)}")
