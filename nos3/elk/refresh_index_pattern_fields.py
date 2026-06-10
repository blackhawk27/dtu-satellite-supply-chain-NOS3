#!/usr/bin/env python3
"""
refresh_index_pattern_fields.py - re-populate the field cache on every
nos3-telemetry* index-pattern saved object.

Kibana 7.17 OSS does not expose a dedicated /refresh_fields endpoint
(verified: 404). The supported path is:
  1. GET  /api/index_patterns/_fields_for_wildcard?pattern=<title>
  2. PUT  /api/saved_objects/index-pattern/<id> with the new fields
     array stringified into attributes.fields.

Without this, every dashboard's Lens panels render "No data" the
moment a fresh daily index is created and the saved-object's cached
field list goes stale (the dominant cause of empty panels in the
prior debug session, see debug/journal.md 2026-04-25 19:05).

Called from the `launch` target of nos3/Makefile so the cache is
always fresh after Kibana comes up.

Idempotent: safe to re-run any time. Exits 0 on success, non-zero
only when Kibana is not reachable, the underlying nos3-telemetry-*
index does not yet exist, or every index-pattern refresh failed.
"""
import json
import os
import sys
import time
import urllib.request
from urllib.error import HTTPError, URLError

KB = "http://localhost:5601"
ES = "http://localhost:9200"

# Display timezone for every Kibana date axis. Stored timestamps stay true UTC
# epoch (sc_timestamp is rebased to wall-clock-now in passive_listener.py, not
# shifted to a local offset); this setting only changes how Kibana renders
# them, forcing Danish local time (CET/CEST) regardless of the viewer's browser
# timezone. Override with NOS3_KIBANA_TZ if a different locale is ever needed.
KIBANA_TZ = os.environ.get("NOS3_KIBANA_TZ", "Europe/Copenhagen")

INDEX_PATTERN_IDS = [
    "5b3163a0-3ea7-11f1-adf4-55f5fc5a104a",
]

# Title (Kibana index-pattern) and underlying ES index glob. Same data
# view, but the saved-object .attributes.title is "nos3-telemetry*" and
# the ES index glob is "nos3-telemetry-*". Both are needed below.
INDEX_PATTERN_TITLE = "nos3-telemetry*"
INDEX_PATTERN_TIME_FIELD = "@timestamp"
ES_INDEX_PATTERN = "nos3-telemetry-*"
KIBANA_READY_TIMEOUT = 60
INDEX_WAIT_TIMEOUT = 120
# Fields added by recent god_view_capture.py / logstash work that arrive only
# once GENERIC_GNSS HK packets reach Elasticsearch. _fields_for_wildcard
# discovers fields lazily from indexed docs, so a refresh that fires before
# the first HK packet lands will silently bake them out of the cached field
# list. Wait for them to surface before running the refresh.
REQUIRED_NEW_FIELDS = {
    "in_denmark_box", "in_science_mode",
    "last_ping_seq", "last_ping_time", "ping_count",
    # GNSS-to-GS Validation dashboard position-overlay panels reference
    # these via lens sourceField. If the cached field list is frozen
    # before they appear, the panels render
    # "Field gnss_truth_lat was not found" even though ES has the docs.
    "gnss_truth_lat", "gnss_truth_lon", "gnss_truth_alt_m",
    "gps_lat", "gps_lon",
    # MGR HK decoded fields. Many dashboards (mode-changes, ADCS,
    # mission-overview) query spacecraft_mode / spacecraft_mode_name.
    # On a fresh clone these may appear later than GENERIC_GNSS HK
    # because MGR has to transition out of its startup state before
    # publishing the first usable mode value. Wait for them so the
    # cached field list never bakes them out.
    "spacecraft_mode", "spacecraft_mode_name",
    # passive_listener.py adds spacecraft time (decoded from the cFE telemetry
    # secondary header) and an explicit host receive-time field. Wait for both
    # so the cached field list exposes sc_timestamp (date) before any panel or
    # ad-hoc query tries to use it as a time axis.
    "sc_timestamp", "host_timestamp",
}
NEW_FIELDS_WAIT_TIMEOUT = 180
RETRY_CODES = (404, 408, 425, 500, 502, 503, 504)
RETRY_DELAYS = (1, 2, 4, 4, 4)


def _open(method, url, body=None, timeout=10):
    req = urllib.request.Request(
        url,
        headers={"kbn-xsrf": "true", "Content-Type": "application/json"},
        method=method,
    )
    if body is not None:
        req.data = json.dumps(body).encode()
    return urllib.request.urlopen(req, timeout=timeout)


def kb(method, path, body=None, retries=len(RETRY_DELAYS),
       retry_codes=RETRY_CODES):
    """HTTP call to Kibana with bounded retry on transient codes.

    Re-raises HTTPError/URLError after exhausting retries so the
    caller can decide what to do with each endpoint."""
    last_exc = None
    for attempt in range(retries + 1):
        try:
            with _open(method, f"{KB}{path}", body=body) as resp:
                data = resp.read().decode()
                if not data:
                    return {}
                return json.loads(data)
        except HTTPError as e:
            last_exc = e
            if e.code not in retry_codes or attempt == retries:
                raise
        except URLError as e:
            last_exc = e
            if attempt == retries:
                raise
        time.sleep(RETRY_DELAYS[min(attempt, len(RETRY_DELAYS) - 1)])
    if last_exc:
        raise last_exc
    return {}


def kibana_ready(timeout=KIBANA_READY_TIMEOUT):
    """Return True once Kibana reports overall=available/green.

    `/api/status` returning 200 is necessary but not sufficient: the
    legacy index_patterns plugin can still 404 while migrations or
    plugin init are running. Wait for `status.overall` to settle."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with _open("GET", f"{KB}/api/status", timeout=5) as resp:
                doc = json.loads(resp.read().decode())
        except (HTTPError, URLError):
            time.sleep(2)
            continue
        overall = (doc.get("status") or {}).get("overall") or {}
        # 7.17 reports state="green" / level="available". Accept either.
        state = overall.get("state") or overall.get("level")
        if state in ("green", "available"):
            return True
        time.sleep(2)
    return False


def wait_for_index(pattern=ES_INDEX_PATTERN, timeout=INDEX_WAIT_TIMEOUT):
    """Return True once at least one ES index matches the pattern.

    `_fields_for_wildcard` returns 4xx when no matching indices exist,
    which surfaces as a confusing 404/400 in the script. Gate on real
    index existence instead so we either succeed or fail with a clear
    message."""
    deadline = time.time() + timeout
    url = f"{ES}/_cat/indices/{pattern}?h=index"
    while time.time() < deadline:
        try:
            with _open("GET", url, timeout=5) as resp:
                body = resp.read().decode().strip()
            if body:
                return True
        except (HTTPError, URLError):
            pass
        time.sleep(2)
    return False


def ensure_one(ip_id, title=INDEX_PATTERN_TITLE,
               time_field=INDEX_PATTERN_TIME_FIELD):
    """Create the index-pattern saved object if it is missing.

    Fresh-clone case: the ES docker volume is empty, so Kibana's
    saved-object index has no nos3-telemetry* data view; this script
    used to fail with `saved object missing (404)` and point the user
    at `make load-dashboards`. Now it self-heals by POSTing a minimal
    saved object with the canonical id/title/timeFieldName, so the
    subsequent refresh path can populate fields normally."""
    try:
        kb("GET", f"/api/saved_objects/index-pattern/{ip_id}", retries=1)
        return True
    except HTTPError as e:
        if e.code != 404:
            print(f"  WARN {ip_id}: GET saved-object failed ({e.code})")
            return False
    except URLError as e:
        print(f"  WARN {ip_id}: GET saved-object unreachable: {e}")
        return False

    attrs = {
        "title": title,
        "timeFieldName": time_field,
        "fields": "[]",
        "fieldAttrs": "{}",
        "runtimeFieldMap": "{}",
        "typeMeta": "{}",
    }
    try:
        kb("POST", f"/api/saved_objects/index-pattern/{ip_id}",
           {"attributes": attrs})
    except HTTPError as e:
        print(f"  WARN {ip_id}: create failed ({e.code}); cannot refresh")
        return False
    except URLError as e:
        print(f"  WARN {ip_id}: create unreachable: {e}")
        return False
    print(f"  created missing data view {ip_id} (title='{title}', "
          f"timeFieldName='{time_field}')")
    return True


def refresh_one(ip_id):
    """Refresh the cached fields array on one index-pattern saved
    object. Returns True on success, False on a handled failure."""
    if not ensure_one(ip_id):
        return False
    try:
        sobj = kb("GET", f"/api/saved_objects/index-pattern/{ip_id}",
                  retries=2)
    except HTTPError as e:
        print(f"  WARN {ip_id}: GET saved-object failed ({e.code})")
        return False
    except URLError as e:
        print(f"  WARN {ip_id}: GET saved-object unreachable: {e}")
        return False

    title = sobj["attributes"]["title"]
    meta = "_source,_id,_type,_index,_score"
    qs = f"?pattern={title}&meta_fields={meta.replace(',', '&meta_fields=')}"

    try:
        ffw = kb("GET", f"/api/index_patterns/_fields_for_wildcard{qs}")
    except HTTPError as e:
        print(f"  WARN {ip_id}: _fields_for_wildcard returned {e.code} "
              f"for pattern '{title}'. Logstash may not be indexing yet.")
        return False
    except URLError as e:
        print(f"  WARN {ip_id}: _fields_for_wildcard unreachable: {e}")
        return False

    fields = ffw.get("fields", [])
    if not fields:
        print(f"  WARN {ip_id}: _fields_for_wildcard returned 0 fields "
              f"for '{title}'. Skipping (would clobber cached fields).")
        return False

    new_attrs = {"fields": json.dumps(fields)}
    try:
        kb("PUT", f"/api/saved_objects/index-pattern/{ip_id}",
           {"attributes": new_attrs})
    except HTTPError as e:
        print(f"  WARN {ip_id}: PUT failed ({e.code}). Kibana saved-"
              f"object index may still be migrating; retry shortly.")
        return False
    except URLError as e:
        print(f"  WARN {ip_id}: PUT unreachable: {e}")
        return False

    print(f"  refreshed {ip_id} ({title}): {len(fields)} fields")
    return True


def wait_for_new_fields(timeout=NEW_FIELDS_WAIT_TIMEOUT):
    """Block until _fields_for_wildcard reports every name in
    REQUIRED_NEW_FIELDS, or `timeout` seconds elapse.

    `_fields_for_wildcard` is the same source the saved-object refresh
    uses, so once a field is visible here, the subsequent PUT will bake
    it into the cached field list. Print progress and return True on
    success, False on timeout (do not exit the process either way; the
    refresh is still useful for the fields that did land in time)."""
    deadline = time.time() + timeout
    qs = (f"?pattern={ES_INDEX_PATTERN}"
          "&meta_fields=_source&meta_fields=_id&meta_fields=_type"
          "&meta_fields=_index&meta_fields=_score")
    last_missing = None
    while time.time() < deadline:
        try:
            ffw = kb("GET",
                     f"/api/index_patterns/_fields_for_wildcard{qs}",
                     retries=1)
        except (HTTPError, URLError):
            time.sleep(5)
            continue
        names = {f["name"] for f in ffw.get("fields", [])}
        missing = REQUIRED_NEW_FIELDS - names
        if not missing:
            print(f"  all required new fields present: "
                  f"{sorted(REQUIRED_NEW_FIELDS)}")
            return True
        if missing != last_missing:
            print(f"  waiting for fields {sorted(missing)} to be "
                  f"indexed (deadline +{int(deadline - time.time())}s)")
            last_missing = missing
        time.sleep(5)
    print(f"  WARN: timed out waiting for fields {sorted(last_missing or REQUIRED_NEW_FIELDS)}; "
          f"refreshing with whatever is currently visible")
    return False


def set_timezone(tz=KIBANA_TZ):
    """Force Kibana's display timezone (advanced setting dateFormat:tz) so
    every date axis renders in `tz` regardless of the viewer's browser.

    Stored timestamps remain true UTC epoch; this only affects display.
    Persisted in the .kibana index, so it survives restarts and only needs
    re-asserting in case a fresh Kibana volume reset it. Idempotent. Returns
    True on success, False on a handled failure (never raises: a display-only
    setting must not block the field-cache refresh)."""
    try:
        kb("POST", "/api/kibana/settings/dateFormat:tz",
           {"value": tz}, retries=3)
        print(f"  set Kibana dateFormat:tz = {tz}")
        return True
    except (HTTPError, URLError) as e:
        print(f"  WARN: could not set dateFormat:tz={tz}: {e}. "
              f"Set it manually under Stack Management > Advanced Settings.")
        return False


def main():
    if not kibana_ready():
        print("refresh_index_pattern_fields: Kibana did not become "
              "ready within "
              f"{KIBANA_READY_TIMEOUT}s. Re-run once Kibana is fully up.")
        return 1

    # Display timezone does not depend on any index existing, so assert it
    # as soon as Kibana is up.
    set_timezone()

    if not wait_for_index():
        print(f"refresh_index_pattern_fields: no '{ES_INDEX_PATTERN}' "
              f"index found in Elasticsearch within {INDEX_WAIT_TIMEOUT}s. "
              "Re-run 'make refresh-kibana-fields' once Logstash has "
              "produced telemetry.")
        return 1

    wait_for_new_fields()

    successes = 0
    for ip in INDEX_PATTERN_IDS:
        if refresh_one(ip):
            successes += 1
    return 0 if successes == len(INDEX_PATTERN_IDS) else 1


if __name__ == "__main__":
    sys.exit(main())
