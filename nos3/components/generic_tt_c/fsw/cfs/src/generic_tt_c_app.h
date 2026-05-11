/*******************************************************************************
** File: generic_tt_c_app.h
**
** Purpose:
**   Header for the generic TT&C cFS application. Holds the link-budget
**   housekeeping packet that the simulator populates on each HK request.
*******************************************************************************/
#ifndef _GENERIC_TT_C_APP_H_
#define _GENERIC_TT_C_APP_H_

#include "cfe.h"
#include "generic_tt_c_events.h"
#include "generic_tt_c_platform_cfg.h"
#include "generic_tt_c_perfids.h"
#include "generic_tt_c_msg.h"
#include "generic_tt_c_msgids.h"
#include "generic_tt_c_version.h"

#define GENERIC_TT_C_PIPE_DEPTH 32

#define GENERIC_TT_C_DEVICE_DISABLED 0
#define GENERIC_TT_C_DEVICE_ENABLED  1

typedef struct
{
    GENERIC_TT_C_Hk_tlm_t HkTelemetryPkt;

    CFE_MSG_Message_t *MsgPtr;
    CFE_SB_PipeId_t    CmdPipe;
    uint32             RunStatus;

    uint32                    DeviceID;
    GENERIC_TT_C_Device_tlm_t DevicePkt;

} GENERIC_TT_C_AppData_t;

extern GENERIC_TT_C_AppData_t GENERIC_TT_C_AppData;

void  TT_C_AppMain(void);
int32 GENERIC_TT_C_AppInit(void);
void  GENERIC_TT_C_ProcessCommandPacket(void);
void  GENERIC_TT_C_ProcessGroundCommand(void);
void  GENERIC_TT_C_ProcessTelemetryRequest(void);
void  GENERIC_TT_C_ReportHousekeeping(void);
void  GENERIC_TT_C_ReportDeviceTelemetry(void);
void  GENERIC_TT_C_ResetCounters(void);
void  GENERIC_TT_C_Enable(void);
void  GENERIC_TT_C_Disable(void);
int32 GENERIC_TT_C_VerifyCmdLength(CFE_MSG_Message_t *msg, uint16 expected_length);

#endif
