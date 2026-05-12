#!/bin/bash
#
# NOS3 Simulator Log Capture
#
# Captures per-simulator Docker container stdout into omni_logs/ so that
# Logstash can parse and index sensor telemetry (GPS, EPS, IMU, MAG, FSS,
# Star Tracker, Reaction Wheels) into Kibana for false-telemetry analysis.
#
# Each simulator container writes its own log file:
#   nos3-gps.log       - GPS position/velocity (OEM615)
#   nos3-gnss.log      - Multi-constellation GNSS (GPS/GLONASS/Galileo/BeiDou)
#   nos3-eps.log       - EPS power/battery
#   nos3-imu.log       - IMU accelerations + gyro rates
#   nos3-mag.log       - Magnetometer field vector
#   nos3-fss.log       - Fine Sun Sensor alpha/beta angles
#   nos3-startrk.log   - Star Tracker attitude quaternion
#   nos3-rw0/1/2.log   - Reaction Wheel angular momenta
#   nos3-ttc.log       - TT&C downlink link-budget telemetry
#
# Launch: ./scripts/sim_logs_capture.sh &
# Stop:   pkill -f sim_logs_capture.sh
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="${SCRIPT_DIR}/../omni_logs"

# Container name -> log file stem mapping
# Names must match what ci_launch.sh creates: ${SC_NUM}-${sim}
declare -A SIMS=(
    # Navigation
    ["sc01-gps"]="nos3-gps"
    ["sc01-generic-gnss-sim"]="nos3-gnss"
    # Power
    ["sc01-generic-eps-sim"]="nos3-eps"
    # ADCS sensors
    ["sc01-generic-imu-sim"]="nos3-imu"
    ["sc01-generic-mag-sim"]="nos3-mag"
    ["sc01-generic-fss-sim"]="nos3-fss"
    ["sc01-generic-css-sim"]="nos3-css"
    ["sc01-generic-star-tracker-sim"]="nos3-startrk"
    # ADCS actuators
    ["sc01-generic-reactionwheel-sim0"]="nos3-rw0"
    ["sc01-generic-reactionwheel-sim1"]="nos3-rw1"
    ["sc01-generic-reactionwheel-sim2"]="nos3-rw2"
    ["sc01-generic-torquer-sim"]="nos3-torquer"
    ["sc01-generic-thruster-sim"]="nos3-thruster"
    # Comms
    ["sc01-generic-radio-sim"]="nos3-radio"
    ["sc01-generic-tt_c-sim"]="nos3-ttc"
    # Dynamics truth
    ["sc01-truth42sim"]="nos3-truth42"
    # Blackboard
    ["sc01-blackboard-sim"]="nos3-blackboard"
    # Sample component
    ["sc01-sample-sim"]="nos3-sample"
    # Camera
    ["sc01-camsim"]="nos3-cam"
)

CHILD_PIDS=()
SHUTDOWN_SENTINEL="$(mktemp -t sim_logs_capture.XXXXXX)"

cleanup() {
    echo "[sim_logs_capture] Shutting down." >&2
    rm -f "$SHUTDOWN_SENTINEL"
    for pid in "${CHILD_PIDS[@]}"; do
        kill "$pid" 2>/dev/null
    done
    exit 0
}
trap cleanup SIGTERM SIGINT SIGHUP

# Per-container reattach worker: respawns `docker logs -f` whenever it
# exits (e.g. after `docker restart sc01-...-sim`) so the omni_logs file
# resumes appending without a manual restart of this script. Item 5b in
# debug/FOLLOWUPS.md.
reattach_worker() {
    local container="$1" log_file="$2" docker_pid=
    # Track StartedAt so we only re-attach `docker logs -f` when the
    # container has actually been restarted. Without this guard, an
    # early-exiting container (e.g. a sim disabled in mission config)
    # gets its full historical log re-dumped on every loop iteration,
    # multiplying file size by ~one cycle per 2 seconds.
    local last_started_at=""
    _signal_handler() {
        [ -n "$docker_pid" ] && kill "$docker_pid" 2>/dev/null
        exit 0
    }
    trap _signal_handler SIGTERM SIGINT SIGHUP

    while [ -f "$SHUTDOWN_SENTINEL" ]; do
        local started_at
        started_at=$(docker inspect -f '{{.State.StartedAt}}' "$container" 2>/dev/null)
        if [ -n "$started_at" ] && [ "$started_at" != "$last_started_at" ]; then
            last_started_at="$started_at"
            docker logs -f "$container" >> "$log_file" 2>&1 &
            docker_pid=$!
            wait "$docker_pid" 2>/dev/null
        fi
        [ -f "$SHUTDOWN_SENTINEL" ] || break
        sleep 2
    done
}

mkdir -p "$LOG_DIR"

echo "[sim_logs_capture] Waiting for NOS3 simulator containers..."

# Wait until the NOS Engine server is running before attaching to component sims
while true; do
    if docker inspect -f '{{.State.Running}}' sc01-nos-engine-server 2>/dev/null | grep -q "true"; then
        break
    fi
    sleep 2
done
# Extra grace period for component sims to start
sleep 6

echo "[sim_logs_capture] Attaching to ${#SIMS[@]} simulator containers:"
for container in "${!SIMS[@]}"; do
    log_file="${LOG_DIR}/${SIMS[$container]}.log"
    echo "  ${container} -> ${log_file}"
    reattach_worker "$container" "$log_file" &
    CHILD_PIDS+=($!)
done
echo ""

# Stay alive until the NOS Engine server stops (NOS3 shut down)
while true; do
    running=$(docker inspect -f '{{.State.Running}}' sc01-nos-engine-server 2>/dev/null)
    if [ "$running" != "true" ]; then
        echo "[sim_logs_capture] NOS Engine stopped - exiting."
        break
    fi
    sleep 5
done

cleanup
