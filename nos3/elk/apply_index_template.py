#!/usr/bin/env python3
"""
apply_index_template.py - register nos3-telemetry index template with
Elasticsearch and patch any pre-existing nos3-telemetry-* index so its
mappings include the full field set declared in index_template.json.

Background
----------
nos3/elk/index_template.json declares a fixed schema (spacecraft_mode,
spacecraft_mode_name, gps_*, eps_*, rw_*, ...). Without this template
registered, ES uses dynamic mapping: a field only exists on an index
after the first doc carrying that field is ingested. Kibana's data
view caches its field list from /api/index_patterns/_fields_for_wildcard,
which in turn reads from index mappings. So on a fresh clone, if MGR
HK has not produced a packet yet, spacecraft_mode is absent from the
cached field list and every dashboard panel that references it
renders "Field spacecraft_mode was not found".

Registering the template fixes this for all *new* indices. Updating
the mapping on existing indices fixes the current day's index without
needing to drop telemetry.

Idempotent: safe to re-run any time.
"""
import json
import os
import sys
import time
import urllib.request
from urllib.error import HTTPError, URLError

ES = os.environ.get("ES_URL", "http://localhost:9200")
TEMPLATE_NAME = "nos3-telemetry"
INDEX_PATTERN = "nos3-telemetry-*"

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
TEMPLATE_FILE = os.path.join(SCRIPT_DIR, "index_template.json")

ES_READY_TIMEOUT = 60


def es_call(method, path, body=None, timeout=15):
    req = urllib.request.Request(
        f"{ES}{path}",
        headers={"Content-Type": "application/json"},
        method=method,
    )
    if body is not None:
        req.data = json.dumps(body).encode()
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        data = resp.read().decode()
        return json.loads(data) if data else {}


def wait_for_es(timeout=ES_READY_TIMEOUT):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            doc = es_call("GET", "/_cluster/health")
            if doc.get("status") in ("yellow", "green"):
                return True
        except (HTTPError, URLError):
            pass
        time.sleep(2)
    return False


def load_template():
    with open(TEMPLATE_FILE, "r", encoding="utf-8") as f:
        return json.load(f)


def put_template(template_body):
    """Register the composable index template. Applies to any
    nos3-telemetry-* index created from this point forward."""
    es_call("PUT", f"/_index_template/{TEMPLATE_NAME}", template_body)
    print(f"  PUT /_index_template/{TEMPLATE_NAME} -> ok")


def patch_existing_indices(template_body):
    """For each nos3-telemetry-* index that already exists, PUT the
    template's `mappings.properties` onto it. ES merges new fields
    into the existing mapping (existing fields are left untouched;
    ES rejects mapping-type changes, which we never make here)."""
    try:
        out = es_call("GET",
                      f"/_cat/indices/{INDEX_PATTERN}?h=index&format=json")
    except HTTPError as e:
        if e.code == 404:
            print(f"  no {INDEX_PATTERN} indices yet (will pick up template on first create)")
            return
        raise

    indices = [row["index"] for row in out] if isinstance(out, list) else []
    if not indices:
        print(f"  no {INDEX_PATTERN} indices yet (will pick up template on first create)")
        return

    properties = (template_body.get("template", {})
                              .get("mappings", {})
                              .get("properties", {}))
    if not properties:
        print("  WARN: template has no mappings.properties; skipping mapping patch")
        return

    body = {"properties": properties}
    patched, failed = 0, 0
    for idx in indices:
        try:
            es_call("PUT", f"/{idx}/_mapping", body)
            patched += 1
        except HTTPError as e:
            failed += 1
            try:
                err_body = e.read().decode()
            except Exception:
                err_body = ""
            print(f"  WARN: PUT /{idx}/_mapping failed ({e.code}): {err_body[:200]}")
    print(f"  patched mappings on {patched} existing index(es)"
          + (f", {failed} failed" if failed else ""))


def main():
    if not wait_for_es():
        print(f"apply_index_template: Elasticsearch not reachable at {ES} "
              f"within {ES_READY_TIMEOUT}s.")
        return 1
    if not os.path.isfile(TEMPLATE_FILE):
        print(f"apply_index_template: template file missing: {TEMPLATE_FILE}")
        return 1
    try:
        template_body = load_template()
        put_template(template_body)
        patch_existing_indices(template_body)
    except (HTTPError, URLError) as e:
        print(f"apply_index_template: failed: {e}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
