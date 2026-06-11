#!/usr/bin/env bash
# run_gnss_drift_compare.sh - reproducible GNSS drift PoC scenario for cross-testbed
# comparison (cFS/Linux vs RTEMS). Runs the SAME sequence on either testbed and logs
# bus-vs-truth divergence against SIM TIME (sc_timestamp), so the curves can be overlaid
# independent of NOS Engine wall-clock dilation (which differs by host load).
#
# Sequence: baseline window -> arm gnss_drift (0x0E) -> sample -> clear (0x00) -> sample.
#
# Output: omni_logs/gnss_drift_compare_<label>.csv with columns
#   wall_iso,phase,bus_sc_timestamp,bus_lat,bus_lon,truth_lat,truth_lon,gap_lat
# Plot gap_lat vs bus_sc_timestamp to compare drift slope (expect ~0.02 deg/sim-s,
# plateau at 3.0 deg) between the two testbeds.
#
# Prerequisites: NOS3 stack running (make launch) with GENERIC_GNSS + truth42 + captures.
#
# Usage:
#   ./scripts/attack/run_gnss_drift_compare.sh [--label cfs] [--opcode gnss_drift]
#                                              [--samples 24] [--interval 15]
#                                              [--direct] [--clear-after N]

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NOS3_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
GNSS_LOG="$NOS3_DIR/omni_logs/nos3-gnss.log"
HK_LOG="$NOS3_DIR/omni_logs/tlm_hk_decoded.log"

LABEL="cfs"; OPCODE="gnss_drift"; SAMPLES=24; INTERVAL=15; DIRECT="--direct"; CLEAR_AFTER=18
while [ $# -gt 0 ]; do
    case "$1" in
        --label)       LABEL="$2"; shift 2;;
        --opcode)      OPCODE="$2"; shift 2;;
        --samples)     SAMPLES="$2"; shift 2;;
        --interval)    INTERVAL="$2"; shift 2;;
        --clear-after) CLEAR_AFTER="$2"; shift 2;;
        --no-direct)   DIRECT=""; shift;;
        *) echo "unknown arg: $1"; exit 2;;
    esac
done

OUT="$NOS3_DIR/omni_logs/gnss_drift_compare_${LABEL}.csv"
echo "wall_iso,phase,bus_sc_timestamp,bus_lat,bus_lon,truth_lat,truth_lon,gap_lat" > "$OUT"

sample() {  # $1 = phase label
    local phase="$1"
    local tl truth_lat truth_lon
    tl=$(grep -h 'GNSS_TRUTH\] lat=[0-9-]' "$GNSS_LOG" 2>/dev/null | tail -1)
    truth_lat=$(echo "$tl" | grep -oE 'lat=[0-9.-]+' | head -1 | cut -d= -f2)
    truth_lon=$(echo "$tl" | grep -oE 'lon=[0-9.-]+' | head -1 | cut -d= -f2)
    # bus position from the last GENERIC_GNSS HK; join with the latest truth sample
    python3 - "$phase" "${truth_lat:-nan}" "${truth_lon:-nan}" "$HK_LOG" "$OUT" <<'PY'
import sys, json, datetime
phase, tlat, tlon, hklog, out = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5]
try:
    d = [json.loads(l) for l in open(hklog) if '"app": "GENERIC_GNSS"' in l]
except Exception:
    d = []
now = datetime.datetime.utcnow().isoformat()
if d:
    b = d[-1]
    try:
        gap = b['gnss_lat'] - float(tlat)
    except Exception:
        gap = float('nan')
    line = f"{now},{phase},{b.get('sc_timestamp','')},{b['gnss_lat']},{b['gnss_lon']},{tlat},{tlon},{gap:.4f}"
else:
    line = f"{now},{phase},,,,{tlat},{tlon},"
open(out, 'a').write(line + "\n")
print(line)
PY
}

echo "[compare] label=$LABEL opcode=$OPCODE samples=$SAMPLES interval=${INTERVAL}s clear_after=$CLEAR_AFTER"
echo "[compare] baseline sample"
sample baseline

echo "[compare] arming $OPCODE"
python3 "$SCRIPT_DIR/attack_eps_piggyback.py" --opcode "$OPCODE" $DIRECT 2>&1 | grep -iE 'opcode|Sent' || true

for i in $(seq 1 "$SAMPLES"); do
    if [ "$i" -eq "$CLEAR_AFTER" ]; then
        echo "[compare] clearing (opcode 0x00)"
        python3 "$SCRIPT_DIR/attack_eps_piggyback.py" --opcode off $DIRECT 2>&1 | grep -iE 'opcode|Sent' || true
    fi
    phase="drift"; [ "$i" -ge "$CLEAR_AFTER" ] && phase="cleared"
    sample "$phase"
    sleep "$INTERVAL"
done

echo "[compare] done -> $OUT"
echo "[compare] plot gap_lat vs bus_sc_timestamp; expect ~0.02 deg/sim-s rise, plateau 3.0, snap-back on clear"
