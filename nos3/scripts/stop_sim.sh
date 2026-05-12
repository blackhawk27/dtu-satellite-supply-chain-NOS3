#!/bin/bash -i
#
# Stop only the running NOS3 simulation (sims, FSW, GSW, 42, NOS Engine,
# CryptoLib, capture scripts) without disturbing:
#   - in-progress build containers (nos_build_sim, nos_build_fsw,
#     nos_build_cryptolib) - they share the $DBOX image with sim containers,
#     so a blanket "stop containers by ancestor=$DBOX" (as stop.sh does)
#     would kill them too. This script targets sim containers by name.
#   - the ELK stack (nos3-elasticsearch, nos3-logstash, nos3-kibana) so the
#     telemetry from the run remains analyzable in Kibana.
#   - omni_logs/, attack_logs/, and nos3-telemetry-* indices so the run
#     stays inspectable post-mortem.
#
# Use 'make stop' for a full teardown.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
source "$SCRIPT_DIR/env.sh"

# Warn (but do not abort) if a build is in progress so the user knows the
# script is intentionally leaving it alone.
BUILD_RUNNING=$($DCALL ps --filter=name="nos_build_" --format '{{.Names}}' 2>/dev/null)
if [ -n "$BUILD_RUNNING" ]; then
    echo "Build container(s) currently running - they will NOT be stopped:"
    echo "$BUILD_RUNNING" | sed 's/^/  /'
    echo ""
fi

# Kill auxiliary capture scripts (sim runtime helpers, not the build).
pkill -f "cpu_monitor.sh"      2>/dev/null || true
pkill -f "cfs_evs_capture.sh"  2>/dev/null || true
pkill -f "sim_logs_capture.sh" 2>/dev/null || true
pkill -f "god_view_capture.py" 2>/dev/null || true

# Sim/FSW/GSW container name patterns from ci_launch.sh. Each pattern is a
# substring match; we filter out anything beginning with nos_build_ as a
# belt-and-braces guard against accidentally hitting a build container.
SIM_PATTERNS=(
    "sc0"                       # per-spacecraft: sc01-fortytwo, sc01-nos-fsw, sc01-* sims
    "nos-terminal"
    "nos-udp-terminal"
    "nos-sim-bridge"
    "nos-time-driver"
    "cosmos-openc3-operator-1"
    "ait"
    "influxdb"
    "ttc-command"
    "openc3"
)

TO_STOP=$(
    for pat in "${SIM_PATTERNS[@]}"; do
        $DCALL ps --filter=name="$pat" --format '{{.Names}}'
    done | sort -u | grep -v '^nos_build_' || true
)

if [ -n "$TO_STOP" ]; then
    echo "Stopping sim containers:"
    echo "$TO_STOP" | sed 's/^/  /'
    echo "$TO_STOP" | xargs $DCALL stop > /dev/null 2>&1 &
    wait
else
    echo "No running sim containers found."
fi

# Tear down the ait compose stack (auxiliary GSW). ELK stays up.
(cd "$BASE_DIR/gsw/ait" 2>/dev/null && ${DOCKER_COMPOSE_COMMAND} down > /dev/null 2>&1) || true

# Remove per-spacecraft networks (recreated by next ci_launch.sh). Leave
# nos3-core alone - ELK is still attached to it.
$DNETWORK ls --filter=name="nos3-sc" -q 2>/dev/null | xargs -r $DNETWORK rm > /dev/null 2>&1 || true

# 42 IO directory (root-owned files written from inside the 42 container).
sudo rm -rf "$USER_NOS3_DIR/42/NOS3InOut" 2>/dev/null || true

# Ephemeral runtime state - safe to clear, regenerated on next launch.
rm -f /dev/shm/Blackboard 2>/dev/null || true
rm -rf /tmp/gpio_fake /tmp/gpio* 2>/dev/null || true
sudo rm -rf "$BASE_DIR/fsw/build/exe/cpu1/scratch/"* 2>/dev/null || true

# COSMOS Gemfiles are emitted on launch.
yes | rm "$GSW_DIR/Gemfile"      > /dev/null 2>&1 || true
yes | rm "$GSW_DIR/Gemfile.lock" > /dev/null 2>&1 || true

echo ""
echo "Sim stopped. Preserved:"
echo "  - build containers (if any were running)"
echo "  - ELK stack (Kibana at http://localhost:5601)"
echo "  - omni_logs/, attack_logs/, and nos3-telemetry-* indices"
echo "Run 'make stop' for a full teardown including ELK and indices."
exit 0
