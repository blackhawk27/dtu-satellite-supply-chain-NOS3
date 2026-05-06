#!/bin/bash
#
# fsw_respawn.sh - Launch cFS FSW inside the nos-fsw container.
#
# Called by ci_launch.sh as the container entrypoint.  Waits for the NOS Engine
# time server to be ready (port 12000) before each FSW start, then re-executes
# on unexpected exit so transient crashes don't leave the spacecraft without FSW.
#
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FSW_BIN="${1:-./core-cpu1}"
NOS_ENGINE_HOST="${NOS_ENGINE_HOST:-nos-engine-server}"
NOS_ENGINE_PORT="${NOS_ENGINE_PORT:-12000}"

wait_for_nos_engine() {
    echo "[fsw_respawn] Waiting for NOS Engine at ${NOS_ENGINE_HOST}:${NOS_ENGINE_PORT}..."
    local attempts=0
    while ! bash -c "echo > /dev/tcp/${NOS_ENGINE_HOST}/${NOS_ENGINE_PORT}" 2>/dev/null; do
        attempts=$((attempts + 1))
        if [ $attempts -ge 60 ]; then
            echo "[fsw_respawn] WARNING: NOS Engine not reachable after 60s; starting FSW anyway"
            return
        fi
        sleep 1
    done
    echo "[fsw_respawn] NOS Engine ready (${attempts}s)"
    # Extra settling time so the engine's routing table is fully initialized
    sleep 2
}

echo "[fsw_respawn] Starting FSW: $FSW_BIN"

# DS destination directories must exist before core-cpu1 opens files on
# OSAL /data/<dest>.  ci_launch.sh already creates these on the host, but
# this in-container mkdir closes the cold-boot race where DS tries to
# create its first file before the host-side sudo mkdir has landed.
mkdir -p ./data/cam ./data/evs ./data/hk ./data/inst

if [ "${FSW_NO_RESPAWN:-0}" = "1" ]; then
    wait_for_nos_engine
    LOG=/tmp/fsw_gdb_$(date +%s).log
    echo "[fsw_respawn] FSW_NO_RESPAWN=1 set; running $FSW_BIN under gdb (log -> $LOG)"
    gdb -batch \
        -ex "set pagination off" \
        -ex "set print elements 0" \
        -ex "handle SIGSEGV stop print" \
        -ex run \
        -ex "thread apply all bt full" \
        -ex "info registers" \
        -ex "info threads" \
        --args $FSW_BIN 2>&1 | tee "$LOG"
    EXIT_CODE=${PIPESTATUS[0]}
    echo "[fsw_respawn] gdb exited $EXIT_CODE; trace at $LOG. Sleeping 30s before container exit so logs can be pulled."
    sleep 30
    exit $EXIT_CODE
fi

while true; do
    wait_for_nos_engine
    $FSW_BIN
    EXIT_CODE=$?
    echo "[fsw_respawn] FSW exited with code $EXIT_CODE. Restarting in 3s..."
    sleep 3
done
