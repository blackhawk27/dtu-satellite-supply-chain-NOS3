#!/usr/bin/env bash
#
# elk_doctor.sh - one-shot diagnostic for the NOS3 ELK telemetry chain.
#
# Walks every layer that has to be alive for Kibana to show data and
# prints PASS / FAIL + the next-step fix when something is missing.
# Intended for new clones where `make launch` finished but Kibana
# shows "could not find the index pattern" or an empty Discover view.
#
# Chain: capture scripts -> log files -> Logstash -> Elasticsearch ->
# Kibana saved-object (data view) -> dashboards.
#
# Exits 0 if every layer is healthy, non-zero otherwise (count of
# failing layers). Safe to run any time, side-effect free.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NOS3_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ES_URL="${ES_URL:-http://localhost:9200}"
KIBANA_URL="${KIBANA_URL:-http://localhost:5601}"
DV_ID="5b3163a0-3ea7-11f1-adf4-55f5fc5a104a"
TEMPLATE_NAME="nos3-telemetry"

fails=0
section() { printf '\n==== %s ====\n' "$1"; }
pass()    { printf '  PASS  %s\n' "$1"; }
fail()    { printf '  FAIL  %s\n' "$1"; fails=$((fails + 1)); }
note()    { printf '        %s\n' "$1"; }

container_state() {
    # Echo one of: running, stopped, missing
    if ! docker inspect -f '{{.State.Running}}' "$1" >/tmp/.elk_doctor_state 2>/dev/null; then
        echo missing
        return
    fi
    if grep -qx true /tmp/.elk_doctor_state; then echo running; else echo stopped; fi
}

# ---- 0. Build-time prerequisites ------------------------------------------
section "0/7 Build-time prerequisites (cfg/build/elk/*.yml)"
# Logstash's pipeline does dictionary lookups via `translate { dictionary_path
# => "/etc/logstash/mid/mid_to_name.yml" }`, fed by a bind mount of
# cfg/build/elk/. If those YAMLs are missing, Logstash crashes on startup
# with "File does not exist or cannot be opened ..." and never indexes
# anything, which masquerades as a downstream "no nos3-telemetry-* index"
# symptom three layers later.
for f in mid_to_name.yml mid_to_layer.yml mid_to_subsystem.yml; do
    p="$NOS3_DIR/cfg/build/elk/$f"
    if [ -s "$p" ]; then
        pass "$p"
    else
        fail "$p missing or empty"
        note "fix: re-run 'make launch' (start-elk auto-copies elk/seed_mid/ as a fallback),"
        note "     or manually: cp $NOS3_DIR/elk/seed_mid/*.yml $NOS3_DIR/cfg/build/elk/"
    fi
done

# ---- 1. Containers --------------------------------------------------------
section "1/7 Containers"
for c in nos3-elasticsearch nos3-logstash nos3-kibana; do
    state=$(container_state "$c")
    if [ "$state" = "running" ]; then
        pass "$c is running"
    else
        fail "$c is $state"
        note "fix: cd nos3/elk && docker compose up -d"
    fi
done
for c in sc01-nos-fsw cosmos-openc3-operator-1; do
    state=$(container_state "$c")
    if [ "$state" = "running" ]; then
        pass "$c is running"
    else
        fail "$c is $state"
        note "fix: re-run 'make launch' from $NOS3_DIR"
    fi
done
rm -f /tmp/.elk_doctor_state

# ---- 2. Capture processes -------------------------------------------------
section "2/7 Capture processes (populate omni_logs/ + attack_logs/)"
for name in passive_listener.py cfs_evs_capture.sh sim_logs_capture.sh cpu_monitor.sh; do
    # Self-exclude this pgrep's own argv by hiding the first character in a
    # bracket class. Escape the literal '.' so it stays an exact-name match.
    esc=${name//./\\.}
    pat="[${esc:0:1}]${esc:1}"
    pid=$(pgrep -f "$pat" 2>/dev/null | head -1)
    if [ -n "$pid" ]; then
        pass "$name running (pid $pid)"
    else
        fail "$name not running"
        note "fix: re-run 'make launch' (capture scripts are backgrounded by the launch target)"
    fi
done

# ---- 3. Log file sizes ----------------------------------------------------
section "3/7 Log files (Logstash inputs)"
check_log() {
    local p="$1"
    if [ ! -f "$p" ]; then
        fail "missing: $p"
        return
    fi
    local sz
    sz=$(stat -c %s "$p" 2>/dev/null || echo 0)
    if [ "$sz" -gt 0 ]; then
        pass "$(printf '%-45s %s bytes' "$p" "$sz")"
    else
        fail "$(printf '%-45s 0 bytes (capture has not written anything)' "$p")"
        note "fix: check that sc01-nos-fsw + cosmos-openc3-operator-1 are up, then wait ~30s"
    fi
}
check_log "$NOS3_DIR/attack_logs/cfs_god_view.json"
check_log "$NOS3_DIR/omni_logs/cfs_evs.log"
check_log "$NOS3_DIR/omni_logs/cpu_monitor.log"
shopt -s nullglob
sim_logs=( "$NOS3_DIR"/omni_logs/nos3-*.log )
shopt -u nullglob
if [ ${#sim_logs[@]} -gt 0 ]; then
    pass "found ${#sim_logs[@]} omni_logs/nos3-*.log files"
else
    fail "no omni_logs/nos3-*.log files yet (sim_logs_capture.sh has not produced any)"
fi

# ---- 4. Elasticsearch -----------------------------------------------------
section "4/7 Elasticsearch"
if curl -fsS "$ES_URL/_cluster/health" > /dev/null 2>&1; then
    health=$(curl -fsS "$ES_URL/_cluster/health" | python3 -c 'import sys,json; print(json.load(sys.stdin)["status"])')
    pass "ES reachable at $ES_URL (cluster: $health)"
else
    fail "ES not reachable at $ES_URL"
    note "fix: cd nos3/elk && docker compose up -d elasticsearch"
fi
idx=$(curl -fsS "$ES_URL/_cat/indices/nos3-telemetry-*?h=index,docs.count" 2>/dev/null || true)
if [ -n "$idx" ]; then
    pass "nos3-telemetry-* indices:"
    echo "$idx" | sed 's/^/        /'
else
    fail "no nos3-telemetry-* index in ES yet"
    note "fix: Logstash has not produced any docs. Check 'docker logs nos3-logstash --tail 80'"
    note "     and confirm the log files above are non-empty."
fi

# ---- 4b. Index template ---------------------------------------------------
# The composable index template declares spacecraft_mode and every other
# fixed-schema field. Without it, ES uses dynamic mapping and a field is
# absent from the mapping (and thus from _fields_for_wildcard, and thus
# from the Kibana data view's cached fields) until a real doc carrying
# that field is indexed. That manifests in Kibana panels as
# "Field spacecraft_mode was not found".
section "4b/7 Elasticsearch index template ($TEMPLATE_NAME)"
tmpl=$(curl -fsS "$ES_URL/_index_template/$TEMPLATE_NAME" 2>/dev/null || true)
if [ -n "$tmpl" ]; then
    has_mode=$(printf '%s' "$tmpl" | python3 -c 'import sys,json; d=json.load(sys.stdin);
ts=d.get("index_templates") or []
for t in ts:
    props=(((t.get("index_template") or {}).get("template") or {}).get("mappings") or {}).get("properties") or {}
    if "spacecraft_mode" in props: print("yes"); break
else:
    print("no")' 2>/dev/null || echo no)
    if [ "$has_mode" = "yes" ]; then
        pass "template $TEMPLATE_NAME registered (declares spacecraft_mode)"
    else
        fail "template $TEMPLATE_NAME registered but missing spacecraft_mode"
        note "fix: cd $NOS3_DIR && make apply-index-template"
    fi
else
    fail "template $TEMPLATE_NAME not registered"
    note "fix: cd $NOS3_DIR && make apply-index-template"
fi

# ---- 5. Kibana data view --------------------------------------------------
section "5/7 Kibana data view (saved object)"
code=$(curl -sS -o /dev/null -w '%{http_code}' \
        "$KIBANA_URL/api/saved_objects/index-pattern/$DV_ID" \
        -H 'kbn-xsrf: true' 2>/dev/null)
code=${code:-000}
case "$code" in
  200) pass "data view $DV_ID exists" ;;
  404) fail "data view $DV_ID missing"
       note "fix: 'make build-kibana-dashboards' (now auto-creates the data view)" ;;
  *)   fail "Kibana returned HTTP $code for the data view lookup"
       note "fix: cd nos3/elk && docker compose up -d kibana; wait for /api/status to return 200" ;;
esac

# ---- 6. Dashboards --------------------------------------------------------
section "6/7 Kibana dashboards"
count=$(curl -fsS "$KIBANA_URL/api/saved_objects/_find?type=dashboard&per_page=100" \
        -H 'kbn-xsrf: true' 2>/dev/null \
        | python3 -c 'import sys,json; print(json.load(sys.stdin).get("total",0))' 2>/dev/null \
        || echo 0)
if [ "$count" -ge 1 ]; then
    pass "found $count dashboard(s) in Kibana"
else
    fail "no dashboards in Kibana"
    note "fix: 'make build-kibana-dashboards'"
fi

# ---- Summary --------------------------------------------------------------
section "Summary"
if [ "$fails" -eq 0 ]; then
    echo "  All layers healthy. Open $KIBANA_URL and pick a dashboard."
    exit 0
else
    echo "  $fails check(s) failed. Fix the earliest failing layer first;"
    echo "  later layers depend on it."
    exit 1
fi
