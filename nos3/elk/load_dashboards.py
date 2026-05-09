#!/usr/bin/env python3
"""
Load pre-built Kibana dashboards from a checked-in .ndjson into a running
Kibana instance via the Saved Objects Import API.

Why this exists:
  build_kibana_dashboards.py creates dashboards programmatically against a
  live Kibana, but those saved objects vanish when the Kibana volume is
  wiped (e.g. `docker compose down -v`). To make the EO1 dashboards
  reproducible across rebuilds, we keep an ndjson export under
  nos3/elk/dashboards/ and re-import it on each launch with this loader.

Usage:
  python3 nos3/elk/load_dashboards.py
  python3 nos3/elk/load_dashboards.py path/to/custom.ndjson

Exit codes:
  0 - all objects imported (or already present), 1 - import failed
"""
import json
import os
import subprocess
import sys
import time
from pathlib import Path
from urllib.parse import quote

KIBANA = os.environ.get("KIBANA_URL", "http://localhost:5601")
DEFAULT_NDJSON = Path(__file__).parent / "dashboards" / "nos3-eo1-dashboards.ndjson"
WAIT_SECONDS = int(os.environ.get("KIBANA_WAIT_SECS", "60"))

# Saved objects that lived in older versions of the ndjson and must be purged
# from any Kibana that imported them. Add an entry whenever a dashboard,
# search, or visualization is renamed or deleted from the ndjson:
# _import?overwrite=true never deletes orphans on its own.
OBSOLETE_OBJECTS = [
    ("dashboard", "nos3-eo1-ttnc-downlink-validation"),
]

# Title-substring sweeps for orphan child panels (lens visualizations,
# legacy visualizations) left behind when a parent dashboard is deleted.
# Kibana's saved-objects DELETE on a dashboard does NOT cascade to its
# panels, and the prior build script generated lens ids via random
# suffixes, so there is no stable id list. Title-substring search is
# the reliable way to clean these up.
OBSOLETE_TITLE_SWEEPS = [
    ("lens", "TTNC"),
    ("visualization", "TTNC"),
]


def wait_for_kibana(url: str, timeout: int) -> None:
    """Block until /api/status returns level=available, or raise on timeout."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        r = subprocess.run(
            ["curl", "-sS", "-o", "/dev/null", "-w", "%{http_code}",
             f"{url}/api/status"],
            capture_output=True, text=True)
        if r.stdout.strip() == "200":
            return
        time.sleep(2)
    print(f"Kibana at {url} did not become healthy within {timeout}s",
          file=sys.stderr)
    sys.exit(1)


def delete_obsolete(url: str, items: list) -> None:
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
            print(f"[load_dashboards] purged obsolete {so_type}/{so_id} ({code})")
        else:
            print(f"[load_dashboards] WARN purge {so_type}/{so_id} -> {code}",
                  file=sys.stderr)


def delete_by_search(url: str, so_type: str, query: str) -> None:
    """Find every saved object of `so_type` whose title matches `query`
    and DELETE it. Used to sweep orphan child panels left behind after a
    parent dashboard was removed."""
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
        print(f"[load_dashboards] WARN _find {so_type} '{query}' returned non-JSON",
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
            print(f"[load_dashboards] WARN sweep {so_type}/{so_id} -> {code}",
                  file=sys.stderr)
    print(f"[load_dashboards] swept {deleted} orphan {so_type}(s) "
          f"matching \"{query}\"")


def import_ndjson(url: str, ndjson_path: Path) -> dict:
    """POST the ndjson to /api/saved_objects/_import?overwrite=true."""
    if not ndjson_path.is_file():
        print(f"ndjson file not found: {ndjson_path}", file=sys.stderr)
        sys.exit(1)
    r = subprocess.run(
        ["curl", "-sS", "-X", "POST",
         f"{url}/api/saved_objects/_import?overwrite=true",
         "-H", "kbn-xsrf: true",
         "--form", f"file=@{ndjson_path}"],
        capture_output=True, text=True)
    try:
        resp = json.loads(r.stdout)
    except json.JSONDecodeError:
        print("Unexpected response from Kibana:", r.stdout[:500], file=sys.stderr)
        sys.exit(1)
    return resp


def main(argv: list[str]) -> int:
    ndjson = Path(argv[1]) if len(argv) > 1 else DEFAULT_NDJSON
    print(f"[load_dashboards] target Kibana: {KIBANA}")
    print(f"[load_dashboards] importing:     {ndjson}")
    wait_for_kibana(KIBANA, WAIT_SECONDS)
    delete_obsolete(KIBANA, OBSOLETE_OBJECTS)
    for so_type, query in OBSOLETE_TITLE_SWEEPS:
        delete_by_search(KIBANA, so_type, query)
    resp = import_ndjson(KIBANA, ndjson)
    if not resp.get("success", False):
        errors = resp.get("errors", [])
        print(f"Import reported {len(errors)} error(s):", file=sys.stderr)
        for e in errors[:10]:
            print(f"  - {e.get('type')}/{e.get('id')}: {e.get('error', {}).get('type')}",
                  file=sys.stderr)
        return 1
    print(f"[load_dashboards] imported {resp.get('successCount', 0)} saved object(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
