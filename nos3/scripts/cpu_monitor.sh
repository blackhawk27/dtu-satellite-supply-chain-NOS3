#!/bin/bash
#
# NOS3 CPU Performance Monitor
#
# Samples the cFS Docker container CPU usage every N seconds and writes
# key=value formatted log lines to omni_logs/ so Logstash/Kibana can
# graph real-time CPU load across the satellite flight software stack.
#
# Usage:
#   ./scripts/cpu_monitor.sh [interval_seconds]
#   Default interval: 2 seconds
#
# Run this in a separate terminal while NOS3 is active.
# Stop with Ctrl+C.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_FILE="${SCRIPT_DIR}/../omni_logs/cpu_monitor.log"
INTERVAL="${1:-2}"
FSW_CONTAINER="sc01-nos-fsw"

# Exit cleanly when make stop sends SIGTERM or the terminal is closed
trap 'echo "CPU_MONITOR STATUS=TERMINATED" >> "$LOG_FILE"; exit 0' SIGTERM SIGINT SIGHUP

echo "NOS3 CPU Monitor starting..."
echo "  Container : ${FSW_CONTAINER}"
echo "  Interval  : ${INTERVAL}s"
echo "  Log file  : ${LOG_FILE}"
echo ""

STARTED=false

while true; do

    # --- Check container is alive -------------------------------------------
    RUNNING=$(docker inspect -f '{{.State.Running}}' "$FSW_CONTAINER" 2>/dev/null)

    if [ "$RUNNING" != "true" ]; then
        if [ "$STARTED" = "true" ]; then
            # Container was running but has now stopped — NOS3 shut down
            echo "CPU_MONITOR STATUS=CONTAINER_STOPPED" >> "$LOG_FILE"
            echo "NOS3 CPU Monitor: container stopped, exiting."
            exit 0
        fi
        echo "CPU_MONITOR STATUS=WAITING_FOR_CONTAINER CONTAINER=${FSW_CONTAINER}" >> "$LOG_FILE"
        sleep "$INTERVAL"
        continue
    fi

    STARTED=true

    # --- Container-level CPU and memory (single sample, no stream) ----------
    # timeout 5 prevents hanging if the container dies mid-call
    READ=$(timeout 5 docker stats --no-stream \
        --format "{{.CPUPerc}} {{.MemUsage}} {{.PIDs}}" \
        "$FSW_CONTAINER" 2>/dev/null)

    CPU_PCT=$(echo "$READ" | awk '{gsub(/%/,""); print $1}')
    MEM_USED=$(echo "$READ" | awk '{print $2}')
    MEM_LIMIT=$(echo "$READ" | awk '{print $4}')
    NUM_PIDS=$(echo "$READ" | awk '{print $5}')

    echo "CPU_MONITOR TOTAL_CPU_PCT=${CPU_PCT} MEM_USED=${MEM_USED} MEM_LIMIT=${MEM_LIMIT} NUM_PIDS=${NUM_PIDS}" \
        >> "$LOG_FILE"

    # --- Per-thread breakdown (only threads using >0% CPU) ------------------
    # Uses ps inside the container. Thread names match cFS app task names.
    timeout 5 docker exec "$FSW_CONTAINER" \
        ps -eLo comm,pcpu --no-headers 2>/dev/null | \
        awk '$2 > 0 {
            name = $1
            gsub(/[^a-zA-Z0-9_]/, "_", name)
            printf "CPU_THREAD THREAD_NAME=%s CPU_PCT=%s\n", name, $2
        }' >> "$LOG_FILE"

    sleep "$INTERVAL"
done
