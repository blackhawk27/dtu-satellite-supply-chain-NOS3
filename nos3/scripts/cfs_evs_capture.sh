#!/bin/bash
#
# NOS3 cFS EVS Event Capture
#
# Tails the cFS FSW container stdout into omni_logs/cfs_evs.log so that
# Logstash can parse and index cFS Event Services (EVS) messages into Kibana.
#
# cFS writes EVS events to stdout in the format:
#   YYYY-DDD-HH:MM:SS.sssss AppName EventID/EventType: Message text
#
# Docker adds a timestamp prefix when using docker logs --timestamps:
#   2024-01-15T10:30:45.123Z  YYYY-DDD-...
#
# Run this in a separate terminal while NOS3 is active:
#   ./scripts/cfs_evs_capture.sh
#
# Stop with Ctrl+C. The script exits automatically when the container stops.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_FILE="${SCRIPT_DIR}/../omni_logs/cfs_evs.log"
FSW_CONTAINER="sc01-nos-fsw"

trap 'echo "[cfs_evs_capture] SIGTERM received, exiting." >> "$LOG_FILE"; exit 0' SIGTERM SIGINT SIGHUP

mkdir -p "$(dirname "$LOG_FILE")"

echo "[cfs_evs_capture] Starting EVS capture."
echo "  Container : ${FSW_CONTAINER}"
echo "  Log file  : ${LOG_FILE}"
echo ""

STARTED=false

while true; do
    RUNNING=$(docker inspect -f '{{.State.Running}}' "$FSW_CONTAINER" 2>/dev/null)

    if [ "$RUNNING" != "true" ]; then
        if [ "$STARTED" = "true" ]; then
            echo "[cfs_evs_capture] Container stopped, exiting."
            exit 0
        fi
        sleep 2
        continue
    fi

    STARTED=true

    # --timestamps adds an ISO8601 prefix to each line, which Logstash can parse.
    # stderr is where cFS often writes startup noise - redirect to same log.
    docker logs -f --timestamps "$FSW_CONTAINER" 2>&1 | while IFS= read -r line; do
        echo "$line" >> "$LOG_FILE"
    done

    # docker logs exited (container restarted?) - loop and reconnect
    sleep 1
done
