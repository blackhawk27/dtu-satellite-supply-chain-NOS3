/*******************************************************************************
** File: generic_gnss_msg.h
**
** Purpose:
**   Define generic_gnss application commands and telemetry messages. The HK
**   packet carries multi-constellation visibility and DOP. Distinct from the
**   GPS app (NMEA / position-fix-only) so users can filter subsystem:GNSS vs
**   subsystem:GPS in Kibana. The simulator owns the actual values; the FSW
**   transports them over the cFE Software Bus.
*******************************************************************************/
#ifndef _GENERIC_GNSS_MSG_H_
#define _GENERIC_GNSS_MSG_H_

#include "cfe.h"

#define GENERIC_GNSS_NOOP_CC           0
#define GENERIC_GNSS_RESET_COUNTERS_CC 1
#define GENERIC_GNSS_ENABLE_CC         2
#define GENERIC_GNSS_DISABLE_CC        3
#define GENERIC_GNSS_GS_PING_CC        4

#define GENERIC_GNSS_REQ_HK_TLM   0
#define GENERIC_GNSS_REQ_DATA_TLM 1

#define GENERIC_GNSS_SOLN_NONE   0
#define GENERIC_GNSS_SOLN_FLOAT  1
#define GENERIC_GNSS_SOLN_FIX_3D 2

typedef struct
{
    CFE_MSG_CommandHeader_t CmdHeader;

} GENERIC_GNSS_NoArgs_cmd_t;

typedef struct
{
    CFE_MSG_CommandHeader_t CmdHeader;
    uint32                  Seq;
} GENERIC_GNSS_GsPing_cmd_t;

typedef struct
{
    uint8  SolutionType;
    uint8  NumSatsTotal;
    uint8  NumSatsGps;
    uint8  NumSatsGlonass;
    uint8  NumSatsGalileo;
    uint8  NumSatsBeidou;
    float  Pdop;
    float  Hdop;
    float  Vdop;
    /* Append-only: keeps offsets 0..17 stable for existing consumers. */
    double GnssLat;        /* deg, WGS84 */
    double GnssLon;        /* deg, WGS84 */
    uint8  InDenmarkBox;   /* 1 if 55-58 N and 8-16 E */
    uint8  InScienceMode;  /* mirror of MGR.SpacecraftMode == SCIENCE */
    uint32 LastPingSeq;    /* sequence echoed from last GS_PING_CC */
    uint64 LastPingTime;   /* CFE_TIME microseconds at last ping */
    uint32 PingCount;      /* monotonic count of GS_PING_CC handled */
} __attribute__((packed)) GENERIC_GNSS_Device_HK_tlm_t;
#define GENERIC_GNSS_DEVICE_HK_LNGTH sizeof(GENERIC_GNSS_Device_HK_tlm_t)

typedef struct
{
    CFE_MSG_TelemetryHeader_t    TlmHeader;
    GENERIC_GNSS_Device_HK_tlm_t DeviceHK;

} __attribute__((packed)) GENERIC_GNSS_Device_tlm_t;
#define GENERIC_GNSS_DEVICE_TLM_LNGTH sizeof(GENERIC_GNSS_Device_tlm_t)

typedef struct
{
    CFE_MSG_TelemetryHeader_t    TlmHeader;
    uint8                        CommandErrorCount;
    uint8                        CommandCount;
    uint8                        DeviceErrorCount;
    uint8                        DeviceCount;
    uint8                        DeviceEnabled;
    GENERIC_GNSS_Device_HK_tlm_t DeviceHK;

} __attribute__((packed)) GENERIC_GNSS_Hk_tlm_t;
#define GENERIC_GNSS_HK_TLM_LNGTH sizeof(GENERIC_GNSS_Hk_tlm_t)

/* SAT_HELLO beacon: emitted at HK cadence (1 Hz) only when InDenmarkBox == 1.
 * The ground responder task uses presence of this packet as its trigger to
 * start uplinking GS_PING_CC, and >10 s of absence as its loss-of-signal
 * cue. Distinct MID from HK so the GS can subscribe to the beacon without
 * having to inspect every HK packet. */
typedef struct
{
    CFE_MSG_TelemetryHeader_t TlmHeader;
    uint32                    HelloSeq;     /* monotonic per-app-lifetime counter */
    uint64                    GnssTimeUs;   /* CFE_TIME microseconds at emit */
    double                    GnssLat;      /* echo of cached lat, deg WGS84 */
    double                    GnssLon;      /* echo of cached lon, deg WGS84 */
    uint8                     InDenmarkBox; /* always 1 when this packet is emitted */
} __attribute__((packed)) GENERIC_GNSS_SatHello_tlm_t;
#define GENERIC_GNSS_SAT_HELLO_TLM_LNGTH sizeof(GENERIC_GNSS_SatHello_tlm_t)

#endif
