#!/usr/bin/env python3
"""
passive_listener.py - Passive CCSDS Software Bus packet logger for NOS3.

Listens on the TO_LAB downlink UDP port, parses each packet's CCSDS primary
header (and, for telemetry packets, the cFE telemetry secondary-header
spacecraft time), and appends a JSON line to attack_logs/cfs_god_view.json
for every packet received. Decoded HK/DEVICE payloads are written to
omni_logs/tlm_hk_decoded.log.

This is the passive replacement for god_view_capture.py. It does NOT inject
any commands: it never enables TO_LAB output and never enables/polls sensors.
The ground software (COSMOS) owns the TO_LAB output-enable, and the boot RTS
plus the SCH table drive the sensors, so the FSW behaves exactly as it would
without any logger attached. The only side effect is an optional UDP mirror so
COSMOS (and any extra downstream consumer) still receive the same stream.

Two complementary time fields are written on every record:
  - host_timestamp : wall-clock Unix time when this host received the packet
  - timestamp      : kept identical to host_timestamp for backward
                     compatibility with the existing Logstash pipeline and
                     Kibana dashboards (documented as host_timestamp).
  - sc_timestamp   : spacecraft time decoded from the cFE telemetry secondary
                     header (TLM packets only), as a Unix epoch float. The
                     raw header counts Mission Elapsed Time from the J2000
                     epoch, which under the fake-tone sim clock would land in
                     the year 2000. We REBASE it so the first TLM packet of
                     the run anchors to host wall-clock now; later packets
                     then advance by the spacecraft's own elapsed time. The
                     stored value stays true UTC epoch (Danish local-time
                     display is a Kibana dateFormat:tz setting, not a shift
                     baked into the data).

Usage (background):
    python3 scripts/passive_listener.py &

Stop:
    pkill -f passive_listener.py
"""

import fcntl
import json
import math
import os
import signal
import socket
import struct
import sys
import threading
import time

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
LISTEN_HOST        = "0.0.0.0"      # Listen on all interfaces
LISTEN_PORT        = 5013            # TO_LAB downlink port (cfgTLM_PORT in to_lab_app.h)
LOCKFILE           = "/tmp/nos3_passive_listener.lock"

# Subseconds resolution for the cFE telemetry secondary header. It carries
# 4 bytes of seconds and 2 bytes of subseconds, big-endian
# (default_cfe_msg_sechdr.h: "Time, big endian: 4 byte seconds, 2 byte
# subseconds"). 16-bit subseconds -> fraction = subsec / 65536 s. The seconds
# field counts Mission Elapsed Time (MET) from the J2000 epoch
# (CFE_MISSION_TIME_EPOCH_YEAR=2000, DAY=1 per cfg/nos3_defs/cfe_mission_cfg.h).
CFE_SUBSEC_PER_SEC = 65536.0

# Spacecraft-time rebasing. Under the fake-tone sim clock the spacecraft boots
# near MET 0, so the raw J2000-based value would land in the year 2000. We
# rebase so the FIRST telemetry packet of the run maps to the host's current
# wall-clock time (UTC epoch); every later packet then advances by the
# spacecraft's own elapsed time from that anchor. This keeps the run "starting
# now" on the Kibana / Wireshark time axis while preserving SC-relative timing
# (MET drift, packet gaps). Danish local-time display is a Kibana setting
# (dateFormat:tz = Europe/Copenhagen, applied by
# elk/refresh_index_pattern_fields.py), NOT an offset baked into the stored
# UTC value. Computed once, on the first TLM packet of the process.
_SC_TIME_OFFSET = None

# The listener stays the sole UDP 5013 sink (COSMOS points TO_LAB here via its
# own TO_DEBUG_ENABLE_OUTPUT_CC background task). COSMOS still needs SAT_HELLO
# samples for the GNSS GS Responder background task, so we mirror each packet to
# a side channel. ci_launch.sh maps cosmos UDP 5013 -> host UDP 5113, so sending
# to 127.0.0.1:5113 lands in cosmos's DEBUG interface read socket.
#   NOS3_TLM_MIRROR_DEST="host:port" or "off"  -> primary (cosmos) mirror
#   NOS3_TLM_MIRROR_EXTRA="host:port" or unset -> optional second downstream
def _parse_dest(raw: str, default_port: int):
    raw = (raw or "").strip()
    if not raw or raw.lower() == "off":
        return None
    host, _, port_s = raw.partition(":")
    try:
        return host or "127.0.0.1", int(port_s) if port_s else default_port
    except ValueError:
        return None


def _resolve_mirror_dests() -> list:
    dests = []
    primary = _parse_dest(os.environ.get("NOS3_TLM_MIRROR_DEST", "127.0.0.1:5113"), 5113)
    if primary is not None:
        dests.append(primary)
    extra = _parse_dest(os.environ.get("NOS3_TLM_MIRROR_EXTRA", ""), 5013)
    if extra is not None:
        dests.append(extra)
    return dests


MIRROR_DESTS = _resolve_mirror_dests()

SCRIPT_DIR    = os.path.dirname(os.path.abspath(__file__))
OUTPUT_FILE   = os.path.join(SCRIPT_DIR, "..", "attack_logs", "cfs_god_view.json")
DECODED_FILE  = os.path.join(SCRIPT_DIR, "..", "omni_logs",   "tlm_hk_decoded.log")
MID_REGISTRY  = os.path.join(SCRIPT_DIR, "..", "cfg", "build", "mid_registry.json")

# ---------------------------------------------------------------------------
# HK payload decode table
#
# Keyed by the symbolic MID name (e.g. "GENERIC_EPS_HK_TLM_MID"). The actual
# integer MID and subsystem label are resolved at startup by looking the name
# up in cfg/build/mid_registry.json, which is regenerated by `make config`
# (see scripts/mid/gen_mid_registry.py). If a topic ID changes in an
# FSW header, re-running `make config` is all that is needed here.
#
# Value: (app_name, [(field, byte_offset, struct_fmt), ...])
# Offsets are from the start of the CCSDS packet (primary header at 0).
# CFE_MSG_TelemetryHeader_t is 16 bytes in this Draco build:
# CCSDS Primary (6) + TelemetrySecondaryHeader (6 bytes Time) + Spare[4].
# Verified empirically by hex-dumping a live MGR HK packet (byte 18 == 0x03
# after MGR set SpacecraftMode = 3) and an EPS HK packet (uint16 LE at
# byte 20 == 25199 mV, matching the sim's Battery Voltage debug print).
# ---------------------------------------------------------------------------
_DECODE_BY_NAME: dict = {
    "HS_HK_TLM_MID": ("HS", [
        ("cmd_count",         16, "B"),
        ("cmd_err_count",     17, "B"),
        ("resets_performed",  24, "<H"),  # ResetsPerformed (uint16 LE). Payload
                                          # has 8 bytes of u8 state before this
                                          # (cmd ctrs + 6 mon-state bytes + Spare).
        ("cpu_avg_pct",       44, "<I"),  # UtilCpuAvg stored as pct*100 uint32
        ("cpu_peak_pct",      48, "<I"),  # UtilCpuPeak stored as pct*100 uint32
    ]),
    "LC_HK_TLM_MID": ("LC", [
        ("cmd_count",      16, "B"),
        ("cmd_err_count",  17, "B"),
        ("lc_state",       18, "B"),
    ]),
    "SC_HK_TLM_MID": ("SC", [
        ("curr_ats_id",    16, "B"),
        ("num_rts_active", 24, "<H"),
        ("rts_active_ctr", 28, "<H"),
    ]),
    "HK_HK_TLM_MID": ("HK", [
        ("cmd_count",      16, "B"),
        ("cmd_err_count",  17, "B"),
    ]),
    "TO_LAB_HK_TLM_MID": ("TO_LAB", [
        ("cmd_count",      16, "B"),   # CommandCounter
        ("cmd_err_count",  17, "B"),   # CommandErrorCounter
    ]),
    "GENERIC_EPS_HK_TLM_MID": ("EPS", [
        ("battery_mv",     20, "<H"),   # BatteryVoltage in DeviceHK payload
        ("solar_array_mv", 32, "<H"),   # SolarArrayVoltage in DeviceHK payload
    ]),
    "MGR_HK_TLM_MID": ("MGR", [
        ("spacecraft_mode", 18, "B"),   # SpacecraftMode (uint8)
        ("sci_pass_count",  32, "<H"),  # SciPassCount (uint16 LE)
    ]),
    # GENERIC_GNSS_HK_TLM_MID payload layout (after the 16-byte
    # CFE_MSG_TelemetryHeader_t):
    #   16: CommandErrorCount (B)
    #   17: CommandCount (B)
    #   18: DeviceErrorCount (B)
    #   19: DeviceCount (B)
    #   20: DeviceEnabled (B)
    #   21: SolutionType (B)        --  start of DeviceHK
    #   22: NumSatsTotal (B)
    #   23: NumSatsGps (B)
    #   24: NumSatsGlonass (B)
    #   25: NumSatsGalileo (B)
    #   26: NumSatsBeidou (B)
    #   27: Pdop (<f)
    #   31: Hdop (<f)
    #   35: Vdop (<f)
    #   39: GnssLat (<d)            --  appended for GNSS-to-GS validation
    #   47: GnssLon (<d)
    #   55: InDenmarkBox (B)
    #   56: InScienceMode (B)
    #   57: LastPingSeq (<I)
    #   61: LastPingTime (<Q)
    #   69: PingCount (<I)
    "GENERIC_GNSS_HK_TLM_MID": ("GENERIC_GNSS", [
        ("cmd_count",        17, "B"),
        ("gnss_lat",         39, "<d"),
        ("gnss_lon",         47, "<d"),
        ("in_denmark_box",   55, "B"),
        ("in_science_mode",  56, "B"),
        ("last_ping_seq",    57, "<I"),
        ("last_ping_time",   61, "<Q"),
        ("ping_count",       69, "<I"),
    ]),
    "CFE_ES_HK_TLM_MID":    ("CFE_ES",   [("cmd_count", 16, "B")]),
    "CFE_SB_HK_TLM_MID":    ("CFE_SB",   [("cmd_count", 16, "B")]),
    "CFE_TIME_HK_TLM_MID":  ("CFE_TIME", [("cmd_count", 16, "B")]),
    "NOVATEL_OEM615_HK_TLM_MID": ("GPS", [("cmd_count", 16, "B")]),
    "SCH_HK_TLM_MID":       ("SCH",      [("cmd_count", 16, "B")]),

    # Sensor DEVICE_TLM payloads. Each payload starts at offset 16 (after the
    # 16-byte CFE_MSG_TelemetryHeader_t). Field offsets verified against the
    # packed Device_Data_tlm_t structs in components/<sensor>/fsw/shared/*_device.h
    # at this Draco snapshot. Names match the existing TSVB visualisations
    # (mag_x..., imu_acc_x..., fss_alpha, st_q0..., rw_momentum_*, ...).
    "GENERIC_MAG_DEVICE_TLM_MID": ("MAG", [
        ("mag_x",          16, "<i"),   # int32 MagneticIntensityX
        ("mag_y",          20, "<i"),
        ("mag_z",          24, "<i"),
    ]),
    "GENERIC_IMU_DEVICE_TLM_MID": ("IMU", [
        ("imu_acc_x",      16, "<f"),   # X.LinearAcc
        ("imu_gyro_x",     20, "<f"),   # X.AngularAcc
        ("imu_acc_y",      24, "<f"),
        ("imu_gyro_y",     28, "<f"),
        ("imu_acc_z",      32, "<f"),
        ("imu_gyro_z",     36, "<f"),
    ]),
    "GENERIC_FSS_DEVICE_TLM_MID": ("FSS", [
        ("fss_alpha",      16, "<f"),
        ("fss_beta",       20, "<f"),
        ("fss_error_code", 24, "B"),
    ]),
    "GENERIC_CSS_DEVICE_TLM_MID": ("CSS", [
        ("css_v0",         16, "<H"),
        ("css_v1",         18, "<H"),
        ("css_v2",         20, "<H"),
        ("css_v3",         22, "<H"),
        ("css_v4",         24, "<H"),
        ("css_v5",         26, "<H"),
    ]),
    "SAMPLE_DEVICE_TLM_MID": ("SAMPLE", [
        ("sample_counter",   16, "<I"),
        ("sample_x",         20, "<H"),
        ("sample_y",         22, "<H"),
        ("sample_z",         24, "<H"),
        ("sample_pass_num",  26, "<H"),
        ("sample_region",    28, "B"),
    ]),
    "GENERIC_STAR_TRACKER_DEVICE_TLM_MID": ("STAR_TRACKER", [
        ("st_q0",          16, "<d"),
        ("st_q1",          24, "<d"),
        ("st_q2",          32, "<d"),
        ("st_q3",          40, "<d"),
        ("st_valid",       48, "B"),
    ]),
    # RW HK payload (no separate DEVICE TLM): 11 uint8 counters + double[3] momenta.
    # Fields at fixed offsets after the 16-byte TLM header. The packed outer
    # struct does not realign the inner GENERIC_RW_Data, so the doubles start
    # right after the 11 counter bytes (offset 27).
    "GENERIC_RW_APP_HK_TLM_MID": ("RW", [
        ("rw_cmd_err_count",   16, "B"),
        ("rw_cmd_count",       17, "B"),
        ("rw0_dev_err_count",  18, "B"),
        ("rw1_dev_err_count",  19, "B"),
        ("rw2_dev_err_count",  20, "B"),
        ("rw0_dev_count",      21, "B"),
        ("rw1_dev_count",      22, "B"),
        ("rw2_dev_count",      23, "B"),
        ("rw0_enabled",        24, "B"),
        ("rw1_enabled",        25, "B"),
        ("rw2_enabled",        26, "B"),
        ("rw0_momentum",       27, "<d"),
        ("rw1_momentum",       35, "<d"),
        ("rw2_momentum",       43, "<d"),
    ]),
}


def _load_decode_table() -> dict:
    """Resolve symbolic MID names -> integer MIDs via mid_registry.json."""
    try:
        with open(MID_REGISTRY) as fh:
            registry = json.load(fh)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"[passive_listener] WARN: cannot read {MID_REGISTRY}: {exc}",
              file=sys.stderr)
        return {}
    name_to_mid = {entry["name"]: (int(hex_key, 16), entry["subsystem"])
                   for hex_key, entry in registry.items()}
    out: dict = {}
    for name, (app, fields) in _DECODE_BY_NAME.items():
        resolved = name_to_mid.get(name)
        if resolved is None:
            print(f"[passive_listener] WARN: {name} not in MID registry; "
                  f"decode skipped", file=sys.stderr)
            continue
        mid_int, subsystem = resolved
        out[mid_int] = (app, subsystem, fields)
    return out


DECODE_TABLE: dict = _load_decode_table()

_CPU_PCT_FIELDS = {"cpu_avg_pct", "cpu_peak_pct"}


def _scrub_nan(value):
    # NaN / +-Inf float fields appear when FSW publishes HK before the bus
    # mirror has cached a real sample (GnssLat/GnssLon default to NaN until the
    # GNSS sim's first UART line is parsed). Python's json.dumps emits these as
    # the literal token "NaN" which is rejected by Logstash's json codec, so
    # the entire HK record is dropped with _jsonparsefailure. Replace with
    # None so the line stays valid JSON and downstream still reflects "no
    # value yet" via a null field. Walk dicts and lists recursively so a future
    # nested HK shape (or a list of payload samples) cannot reintroduce the
    # regression.
    if isinstance(value, float):
        return None if not math.isfinite(value) else value
    if isinstance(value, dict):
        return {k: _scrub_nan(v) for k, v in value.items()}
    if isinstance(value, list):
        return [_scrub_nan(v) for v in value]
    return value


def decode_sc_time(data: bytes) -> float | None:
    """Decode spacecraft time from the cFE telemetry secondary header,
    rebased so the first packet of the run anchors to host wall-clock now.

    The 6-byte telemetry secondary header (immediately after the 6-byte CCSDS
    primary header) is 4 bytes of seconds + 2 bytes of subseconds, big-endian,
    counting Mission Elapsed Time (MET) since the J2000 epoch. Rather than
    return the raw J2000-based value (which lands in the year 2000 under the
    fake-tone sim clock), we add a fixed offset so the FIRST TLM packet maps to
    the current wall-clock time and later packets advance by SC elapsed time.
    Returns a Unix epoch float (UTC), or None if the packet is too short.
    """
    global _SC_TIME_OFFSET
    if len(data) < 12:
        return None
    seconds = struct.unpack_from(">I", data, 6)[0]
    subsecs = struct.unpack_from(">H", data, 10)[0]
    met = seconds + subsecs / CFE_SUBSEC_PER_SEC
    if _SC_TIME_OFFSET is None:
        # Anchor: make the first telemetry packet read as "now" (UTC epoch).
        _SC_TIME_OFFSET = time.time() - met
    return met + _SC_TIME_OFFSET


def decode_payload(data: bytes, header: dict) -> dict | None:
    """Decode payload fields from a CCSDS packet using DECODE_TABLE.

    Returns a record dict ready for JSON serialisation, or None if the MID
    has no decode entry or the packet is too short.
    """
    mid_int = int(header["msg_id"], 16)
    entry = DECODE_TABLE.get(mid_int)
    if entry is None or len(data) < 14:
        return None
    app_name, subsystem, fields = entry
    record: dict = {
        "type":           "hk_decoded",
        "app":            app_name,
        "subsystem":      subsystem,
        "msg_id":         header["msg_id"],
        "timestamp":      header["timestamp"],
        "host_timestamp": header["host_timestamp"],
        "sequence":       header["sequence"],
    }
    if header.get("sc_timestamp") is not None:
        record["sc_timestamp"] = header["sc_timestamp"]
    for field_name, offset, fmt in fields:
        size = struct.calcsize(fmt)
        if len(data) >= offset + size:
            (val,) = struct.unpack_from(fmt, data, offset)
            if field_name in _CPU_PCT_FIELDS:
                if val == 0xFFFFFFFF:
                    continue
                val = round(val / 100.0, 2)
            record[field_name] = val
    return record


def parse_primary_header(data: bytes) -> dict | None:
    """
    Parse a CCSDS primary header (6 bytes).
    Returns a dict with msg_id, sequence, length, timestamp/host_timestamp and
    (for TLM packets) sc_timestamp fields, or None if the packet is too short.
    """
    if len(data) < 6:
        return None
    stream_id, seq_ctrl, data_len = struct.unpack_from(">HHH", data, 0)
    apid      = stream_id & 0x07FF
    pkt_type  = (stream_id >> 12) & 0x01   # 1=command, 0=telemetry
    seq_count = seq_ctrl & 0x3FFF
    total_len = data_len + 7               # CCSDS: data_length = total - 7

    now = time.time()
    mid = stream_id
    record = {
        "msg_id":         f"0x{mid:04X}",
        "apid":           apid,
        "pkt_type":       "CMD" if pkt_type else "TLM",
        "sequence":       seq_count,
        "length":         total_len,
        # host_timestamp is wall-clock receive time. `timestamp` is kept equal
        # to it for backward compatibility with the existing Logstash pipeline.
        "timestamp":      now,
        "host_timestamp": now,
    }
    if pkt_type:
        # cFS commands carry the function code at byte offset 6 (just after the
        # 6-byte CCSDS primary header). Capture it on CMD packets so logstash
        # can tag specific function codes. CMD packets have no telemetry
        # secondary-header time, so sc_timestamp is left absent.
        if len(data) >= 7:
            record["function_code"] = data[6]
    else:
        # TLM packets carry the spacecraft time in the secondary header.
        sc_ts = decode_sc_time(data)
        if sc_ts is not None:
            record["sc_timestamp"] = sc_ts
    return record


def _acquire_singleton_lock():
    """Acquire an exclusive advisory lock so only one capture runs at a time.

    Returns the open file object (must be kept alive for the lifetime of the
    process to hold the lock). If another instance is already running, prints a
    clear message and exits 0 so the Makefile background spawn stays quiet.
    """
    lock_fh = open(LOCKFILE, "w")
    try:
        fcntl.flock(lock_fh.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        print(f"[passive_listener] Another instance already holds {LOCKFILE}; exiting.",
              file=sys.stderr)
        lock_fh.close()
        sys.exit(0)
    lock_fh.write(f"{os.getpid()}\n")
    lock_fh.flush()
    return lock_fh


def main() -> None:
    lock_fh = _acquire_singleton_lock()

    os.makedirs(os.path.dirname(OUTPUT_FILE), exist_ok=True)
    os.makedirs(os.path.dirname(DECODED_FILE), exist_ok=True)

    print(f"[passive_listener] Listening on {LISTEN_HOST}:{LISTEN_PORT} (passive, no injection)", flush=True)
    print(f"[passive_listener] Output: {OUTPUT_FILE}", flush=True)
    out_sz = os.path.getsize(OUTPUT_FILE) if os.path.exists(OUTPUT_FILE) else 0
    dec_sz = os.path.getsize(DECODED_FILE) if os.path.exists(DECODED_FILE) else 0
    print(f"[passive_listener] OUTPUT_FILE size at start  = {out_sz} bytes", flush=True)
    print(f"[passive_listener] DECODED_FILE size at start = {dec_sz} bytes", flush=True)
    for dest in MIRROR_DESTS:
        print(f"[passive_listener] Mirroring downlink to {dest[0]}:{dest[1]}", flush=True)

    stop_event = threading.Event()

    def _sighandler(sig, frame):   # noqa: ANN001
        print("[passive_listener] Shutting down.", file=sys.stderr)
        stop_event.set()

    signal.signal(signal.SIGTERM, _sighandler)
    signal.signal(signal.SIGINT,  _sighandler)

    mirror_sock = None
    mirror_warned = False
    if MIRROR_DESTS:
        mirror_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    # Open output files in append mode so previous runs are not erased.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock, \
         open(OUTPUT_FILE,  "a", buffering=1) as out_fh, \
         open(DECODED_FILE, "a", buffering=1) as decoded_fh:

        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((LISTEN_HOST, LISTEN_PORT))
        sock.settimeout(1.0)

        while not stop_event.is_set():
            try:
                data, _addr = sock.recvfrom(65535)
            except socket.timeout:
                continue
            except OSError as exc:
                print(f"[passive_listener] recv error: {exc}", file=sys.stderr)
                break

            if mirror_sock is not None:
                for dest in MIRROR_DESTS:
                    try:
                        mirror_sock.sendto(data, dest)
                    except OSError as exc:
                        if not mirror_warned:
                            print(f"[passive_listener] mirror send error to {dest}: {exc}",
                                  file=sys.stderr)
                            mirror_warned = True

            record = parse_primary_header(data)
            if record is None:
                continue

            out_fh.write(json.dumps(_scrub_nan(record)) + "\n")

            decoded = decode_payload(data, record)
            if decoded is not None:
                decoded_fh.write(json.dumps(_scrub_nan(decoded)) + "\n")

    if mirror_sock is not None:
        mirror_sock.close()

    stop_event.set()
    try:
        fcntl.flock(lock_fh.fileno(), fcntl.LOCK_UN)
        lock_fh.close()
        os.unlink(LOCKFILE)
    except OSError:
        pass
    print("[passive_listener] Done.")


if __name__ == "__main__":
    main()
