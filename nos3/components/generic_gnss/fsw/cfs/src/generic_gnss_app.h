/*******************************************************************************
** File: generic_gnss_app.h
**
** Purpose:
**   Header for the generic GNSS cFS application. Holds the GNSS housekeeping
**   packet (multi-constellation visibility + DOP) the simulator populates.
*******************************************************************************/
#ifndef _GENERIC_GNSS_APP_H_
#define _GENERIC_GNSS_APP_H_

#include "cfe.h"
#include "hwlib.h"
#include "generic_gnss_events.h"
#include "generic_gnss_platform_cfg.h"
#include "generic_gnss_perfids.h"
#include "generic_gnss_msg.h"
#include "generic_gnss_msgids.h"
#include "generic_gnss_version.h"

#define GENERIC_GNSS_PIPE_DEPTH 32

#define GENERIC_GNSS_DEVICE_DISABLED 0
#define GENERIC_GNSS_DEVICE_ENABLED  1

#define GENERIC_GNSS_HK_MUTEX_NAME       "GNSS_HK_MUT"
#define GENERIC_GNSS_CHILD_TASK_NAME     "GNSS_BUS_RX"
#define GENERIC_GNSS_CHILD_TASK_STACK_SIZE 4096
#define GENERIC_GNSS_CHILD_TASK_PRIORITY  118
#define GENERIC_GNSS_BUS_LINE_MAX 256

typedef struct
{
    GENERIC_GNSS_Hk_tlm_t HkTelemetryPkt;

    CFE_MSG_Message_t *MsgPtr;
    CFE_SB_PipeId_t    CmdPipe;
    uint32             RunStatus;

    uint32                    DeviceID;
    GENERIC_GNSS_Device_tlm_t DevicePkt;

    /* SAT_HELLO beacon packet. Filled and transmitted from ReportHousekeeping
     * when InDenmarkBox == 1 so the GS responder can auto-arm at FOV entry
     * and disarm 10 s after the spacecraft drops out of the geofence. */
    GENERIC_GNSS_SatHello_tlm_t SatHelloPkt;
    uint32                       SatHelloSeq;

    /* Mirror of MGR.SpacecraftMode, refreshed each MGR HK arrival. */
    uint8 CachedMgrMode;

    /* GNSS sim bus interface. Mirrors the novatel_oem615 UART pattern: a
     * libuart device handle, a child task that drains the line-oriented
     * UART output, and a mutex protecting the lat/lon cache the HK report
     * pulls from. The sim writes one ASCII line per tick (lat, lon, alt). */
    uart_info_t  GnssUart;
    osal_id_t    HkDataMutex;
    uint32       BusRxCount;     /* incremented on each successful parse */
    uint32       BusRxErrorCount;
    double       LastBusLat;     /* deg, WGS84 */
    double       LastBusLon;     /* deg, WGS84 */
    double       LastBusAlt;     /* m, WGS84 ellipsoid */

} GENERIC_GNSS_AppData_t;

extern GENERIC_GNSS_AppData_t GENERIC_GNSS_AppData;

void  GNSS_AppMain(void);
int32 GENERIC_GNSS_AppInit(void);
void  GENERIC_GNSS_ProcessCommandPacket(void);
void  GENERIC_GNSS_ProcessGroundCommand(void);
void  GENERIC_GNSS_ProcessTelemetryRequest(void);
void  GENERIC_GNSS_ProcessMgrHk(void);
void  GENERIC_GNSS_ProcessGsPing(void);
void  GENERIC_GNSS_UpdatePositionAndFlags(void);
void  GENERIC_GNSS_ReportHousekeeping(void);
void  GENERIC_GNSS_ReportDeviceTelemetry(void);
void  GENERIC_GNSS_EmitSatHelloIfInBox(void);
void  GENERIC_GNSS_ResetCounters(void);
void  GENERIC_GNSS_Enable(void);
void  GENERIC_GNSS_Disable(void);
void  GENERIC_GNSS_BusRxChildTask(void);
int32 GENERIC_GNSS_VerifyCmdLength(CFE_MSG_Message_t *msg, uint16 expected_length);

#endif
