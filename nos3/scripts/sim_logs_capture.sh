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
    # Re-attach `docker logs -f` whenever the stream drops, not only on a
    # container restart. The previous version reattached ONLY when StartedAt
    # changed; if the follow stream broke for any other reason (daemon hiccup
    # under heavy CPU load, EOF, broken pipe) while the container kept running,
    # `wait` returned, StartedAt was unchanged, and the file froze PERMANENTLY
    # even though the sim was alive and still emitting (e.g. nos3-gnss.log
    # "truth stops" in Kibana - see docs/debug/telemetry-and-elk-pipeline.md).
    #
    # `--since` keeps the re-dump guard the old StartedAt check was protecting:
    #   * container (re)started  -> --since <StartedAt>: capture the whole new run
    #   * same run, stream dropped -> --since <now>: resume without re-dumping
    #     history (loses at most the ~2 s reconnect gap; never duplicates lines,
    #     which would double-count telemetry in Logstash).
    # The "container not Running" branch is skipped entirely, so an early-exiting
    # disabled sim is never re-dumped on every loop iteration.
    local last_started_at=""
    _signal_handler() {
        [ -n "$docker_pid" ] && kill "$docker_pid" 2>/dev/null
        exit 0
    }
    trap _signal_handler SIGTERM SIGINT SIGHUP

    while [ -f "$SHUTDOWN_SENTINEL" ]; do
        local started_at running since
        running=$(docker inspect -f '{{.State.Running}}' "$container" 2>/dev/null)
        if [ "$running" = "true" ]; then
            started_at=$(docker inspect -f '{{.State.StartedAt}}' "$container" 2>/dev/null)
            if [ -n "$started_at" ] && [ "$started_at" != "$last_started_at" ]; then
                last_started_at="$started_at"
                since="$started_at"
            else
                since="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
            fi
            docker logs -f --since "$since" "$container" >> "$log_file" 2>&1 &
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
