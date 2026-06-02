#!/bin/bash
#
# start_tcpdump_sidecar.sh - Passive wire-truth capture for NOS3 (View A).
#
# Spawns a tcpdump container that SHARES the FSW container's network namespace
# (--net container:<fsw>) so it sees every datagram on the FSW interfaces:
#   - UDP 5012 : commands arriving at CI_LAB   (command-side ground truth)
#   - UDP 5013 : telemetry leaving TO_LAB       (downlink ground truth)
# Capturing on the wire means the record is independent of who currently owns
# the TO_LAB destination, so it survives a real ground station stealing it.
#
# Files are written to attack_logs/tcpdump/sc01_<port>_<timestamp>.pcap and
# rotated by SIZE (-C megabytes, -W file count), not by time.
#
# Usage:
#   start_tcpdump_sidecar.sh [start]   # start the sidecar (default)
#   start_tcpdump_sidecar.sh stop      # stop and remove the sidecar
#
# Env overrides:
#   TCPDUMP_IMAGE   container image with tcpdump (default nicolaka/netshoot)
#   TCPDUMP_ROTATE_MB   rotate each pcap at this many MB (default 50)
#   TCPDUMP_KEEP_FILES  keep at most this many rotated files per port (default 20)

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

SC_NUM="${SC_NUM:-sc01}"
CONTAINER_NAME="nos3-tcpdump-${SC_NUM}"
CAP_DIR="$BASE_DIR/attack_logs/tcpdump"
TCPDUMP_IMAGE="${TCPDUMP_IMAGE:-nicolaka/netshoot}"
ROTATE_MB="${TCPDUMP_ROTATE_MB:-50}"
KEEP_FILES="${TCPDUMP_KEEP_FILES:-20}"

# Discover the FSW container the same way god_view/passive_listener resolve it.
_discover_fsw() {
    local name
    for name in "${SC_NUM}-nos-fsw" "sc01-nos-fsw" "nos3-${SC_NUM}-nos-fsw"; do
        if docker inspect "$name" >/dev/null 2>&1; then
            echo "$name"
            return 0
        fi
    done
    return 1
}

stop_sidecar() {
    docker rm -f "$CONTAINER_NAME" >/dev/null 2>&1 || true
    echo "[tcpdump_sidecar] stopped $CONTAINER_NAME"
}

case "${1:-start}" in
  stop)
    stop_sidecar
    exit 0
    ;;
  start) ;;
  *)
    echo "[tcpdump_sidecar] usage: $0 [start|stop]" >&2
    exit 2
    ;;
esac

FSW_CONTAINER="$(_discover_fsw)"
if [ -z "${FSW_CONTAINER:-}" ]; then
    echo "[tcpdump_sidecar] WARN: FSW container not found (${SC_NUM}-nos-fsw); skipping pcap capture." >&2
    exit 0
fi

mkdir -p "$CAP_DIR"

# Idempotent: drop any stale sidecar before starting a fresh one.
stop_sidecar

# One tcpdump per port so the two views stay in separate files. -U flushes each
# packet so files are readable mid-run; -C rotates by MB; -W caps file count
# (the rotation index is appended to the name as 00, 01, ...).
# NOTE: tcpdump only expands strftime (%Y%m%d...) in -w when TIME rotation (-G)
# is used. With SIZE rotation (-C) the % codes are taken literally, so we stamp
# the launch time here on the host and pass a concrete prefix to the container.
RUN_TS="$(date +%Y%m%d-%H%M%S)"
INNER_CMD="tcpdump -i any -U -C ${ROTATE_MB} -W ${KEEP_FILES} \
  -w /caps/${SC_NUM}_5012_${RUN_TS}.pcap 'udp port 5012' & \
tcpdump -i any -U -C ${ROTATE_MB} -W ${KEEP_FILES} \
  -w /caps/${SC_NUM}_5013_${RUN_TS}.pcap 'udp port 5013' & \
wait"

if docker run -d --name "$CONTAINER_NAME" \
        --net "container:${FSW_CONTAINER}" \
        -v "$CAP_DIR:/caps" \
        --entrypoint /bin/sh \
        "$TCPDUMP_IMAGE" -c "$INNER_CMD" >/dev/null 2>&1; then
    echo "[tcpdump_sidecar] started $CONTAINER_NAME (netns of $FSW_CONTAINER), pcaps -> $CAP_DIR"
else
    echo "[tcpdump_sidecar] WARN: failed to start sidecar (image '$TCPDUMP_IMAGE' missing or docker error); continuing without pcap capture." >&2
    exit 0
fi
