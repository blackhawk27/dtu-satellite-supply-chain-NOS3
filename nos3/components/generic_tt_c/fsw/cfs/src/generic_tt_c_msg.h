/*******************************************************************************
** File: generic_tt_c_msg.h
**
** Purpose:
**   Define generic_tt_c application commands and telemetry messages. The
**   housekeeping packet carries link-budget telemetry: link state, the active
**   ground station label, current pass geometry, and downlink throughput
**   counters. The simulator owns the actual values; the FSW transports them
**   over the cFE Software Bus.
*******************************************************************************/
#ifndef _GENERIC_TT_C_MSG_H_
#define _GENERIC_TT_C_MSG_H_

#include "cfe.h"

#define GENERIC_TT_C_NOOP_CC           0
#define GENERIC_TT_C_RESET_COUNTERS_CC 1
#define GENERIC_TT_C_ENABLE_CC         2
#define GENERIC_TT_C_DISABLE_CC        3

#define GENERIC_TT_C_REQ_HK_TLM   0
#define GENERIC_TT_C_REQ_DATA_TLM 1

#define GENERIC_TT_C_LINK_STATE_IDLE     0
#define GENERIC_TT_C_LINK_STATE_ACQUIRED 1

#define GENERIC_TT_C_GS_NAME_LEN 24

typedef struct
{
    CFE_MSG_CommandHeader_t CmdHeader;

} GENERIC_TT_C_NoArgs_cmd_t;

typedef struct
{
    uint8    LinkState;
    char     GroundStation[GENERIC_TT_C_GS_NAME_LEN];
    float    ElevationDeg;
    float    SlantRangeKm;
    float    DopplerHz;
    uint32   PassNumber;
    uint32   PassCount;
    uint32   PacketsDownlinked;
    uint32   BytesDownlinked;
    uint32   PacketsDropped;
} __attribute__((packed)) GENERIC_TT_C_Device_HK_tlm_t;
#define GENERIC_TT_C_DEVICE_HK_LNGTH sizeof(GENERIC_TT_C_Device_HK_tlm_t)

typedef struct
{
    CFE_MSG_TelemetryHeader_t    TlmHeader;
    GENERIC_TT_C_Device_HK_tlm_t DeviceHK;

} __attribute__((packed)) GENERIC_TT_C_Device_tlm_t;
#define GENERIC_TT_C_DEVICE_TLM_LNGTH sizeof(GENERIC_TT_C_Device_tlm_t)

typedef struct
{
    CFE_MSG_TelemetryHeader_t    TlmHeader;
    uint8                        CommandErrorCount;
    uint8                        CommandCount;
    uint8                        DeviceErrorCount;
    uint8                        DeviceCount;
    uint8                        DeviceEnabled;
    GENERIC_TT_C_Device_HK_tlm_t DeviceHK;

} __attribute__((packed)) GENERIC_TT_C_Hk_tlm_t;
#define GENERIC_TT_C_HK_TLM_LNGTH sizeof(GENERIC_TT_C_Hk_tlm_t)

#endif
