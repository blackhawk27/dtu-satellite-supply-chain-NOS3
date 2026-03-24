#!/bin/bash
#
# NOS3 Simulator Log Capture
#
# Captures per-simulator Docker container stdout into omni_logs/ so that
# Logstash can parse and index sensor telemetry (GPS, EPS, IMU, MAG, FSS,
# Star Tracker, Reaction Wheels) into Kibana for false-telemetry analysis.
#
# Each simulator container writes its own log file:
#   nos3-gps.log       — GPS position/velocity (OEM615)
#   nos3-eps.log       — EPS power/battery
#   nos3-imu.log       — IMU accelerations + gyro rates
#   nos3-mag.log       — Magnetometer field vector
#   nos3-fss.log       — Fine Sun Sensor alpha/beta angles
#   nos3-startrk.log   — Star Tracker attitude quaternion
#   nos3-rw0/1/2.log   — Reaction Wheel angular momenta
#
# Launch: ./scripts/sim_logs_capture.sh &
# Stop:   pkill -f sim_logs_capture.sh
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="${SCRIPT_DIR}/../omni_logs"

# Container name -> log file stem mapping
declare -A SIMS=(
    ["sc01-gps-sim"]="nos3-gps"
    ["sc01-eps-sim"]="nos3-eps"
    ["sc01-imu-sim"]="nos3-imu"
    ["sc01-mag-sim"]="nos3-mag"
    ["sc01-fss-sim"]="nos3-fss"
    ["sc01-startrk-sim"]="nos3-startrk"
    ["sc01-rw-sim0"]="nos3-rw0"
    ["sc01-rw-sim1"]="nos3-rw1"
    ["sc01-rw-sim2"]="nos3-rw2"
)

CHILD_PIDS=()

cleanup() {
    echo "[sim_logs_capture] Shutting down." >&2
    for pid in "${CHILD_PIDS[@]}"; do
        kill "$pid" 2>/dev/null
    done
    exit 0
}
trap cleanup SIGTERM SIGINT SIGHUP

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
    docker logs -f "${container}" >> "${log_file}" 2>&1 &
    CHILD_PIDS+=($!)
done
echo ""

# Stay alive until the NOS Engine server stops (NOS3 shut down)
while true; do
    running=$(docker inspect -f '{{.State.Running}}' sc01-nos-engine-server 2>/dev/null)
    if [ "$running" != "true" ]; then
        echo "[sim_logs_capture] NOS Engine stopped — exiting."
        break
    fi
    sleep 5
done

cleanup
