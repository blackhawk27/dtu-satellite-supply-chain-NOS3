/*******************************************************************************
** File: generic_imu_app.h
**
** Purpose:
**   This is the main header file for the GENERIC_IMU application.
**
*******************************************************************************/
#ifndef _GENERIC_IMU_APP_H_
#define _GENERIC_IMU_APP_H_

/*
** Include Files
*/
#include "cfe.h"
#include "generic_imu_device.h"
#include "generic_imu_events.h"
#include "generic_imu_platform_cfg.h"
#include "generic_imu_perfids.h"
#include "generic_imu_msg.h"
#include "generic_imu_msgids.h"
#include "generic_imu_version.h"
#include "hwlib.h"

/*
** Specified pipe depth - how many messages will be queued in the pipe
*/
#define GENERIC_IMU_PIPE_DEPTH 32

/*
** Enabled and Disabled Definitions
*/
#define GENERIC_IMU_DEVICE_DISABLED 0
#define GENERIC_IMU_DEVICE_ENABLED  1

/* ---- Covert IMU-bias channel (consumer side of noisy_app OPCODE_IMU_BIAS) -- *
 * This is the backdoor that latches the file dead-drop written by noisy_app's
 * NOISY_WriteImuDeadDrop(). The channel is the FILE on the RAM disk - it never
 * touches the Software Bus, so cfs_god_view.json / EVS never see the cause of
 * the resulting telemetry drift. IMU_CovertDrop_t MUST stay byte-identical to
 * NOISY_ImuDrop_t in nos3/fsw/apps/noisy_app/fsw/src/noisy_app.c (mirrored
 * out-of-band, not a shared build artifact - same contract the noisy_app
 * comment names). */
#define GENERIC_IMU_DROP_PATH  "/ram/.imu_cal" /* dotfile: looks like cal data */
#define GENERIC_IMU_DROP_MAGIC 0x494D5542u     /* 'IMUB' */

typedef struct
{
    uint32 magic;
    uint8  version;
    uint8  axis_mask;  /* X|Y|Z = bit0|bit1|bit2 */
    uint8  profile;    /* 1 = slow drift, 0 = constant */
    uint8  flags;      /* 0x01 = consume (remove) file on latch */
    float  gyro_step;  /* angular bias added per cycle */
    float  accel_step; /* linear bias added per cycle */
    float  gyro_cap;   /* angular bias clamp */
    float  accel_cap;  /* linear bias clamp (0 = none) */
} __attribute__((packed)) IMU_CovertDrop_t;

/*
** GENERIC_IMU global data structure
** The cFE convention is to put all global app data in a single struct.
** This struct is defined in the `generic_imu_app.h` file with one global instance
** in the `.c` file.
*/
typedef struct
{
    /*
    ** Housekeeping telemetry packet
    ** Each app defines its own packet which contains its OWN telemetry
    */
    GENERIC_IMU_Hk_tlm_t HkTelemetryPkt; /* GENERIC_IMU Housekeeping Telemetry Packet */

    /*
    ** Operational data  - not reported in housekeeping
    */
    CFE_MSG_Message_t *MsgPtr;    /* Pointer to msg received on software bus */
    CFE_SB_PipeId_t    CmdPipe;   /* Pipe Id for HK command pipe */
    uint32             RunStatus; /* App run status for controlling the application state */

    /*
     ** Device data
     */
    uint32                   DeviceID;  /* Device ID provided by CFS on initialization */
    GENERIC_IMU_Device_tlm_t DevicePkt; /* Device specific data packet */

    /*
    ** Device protocol: CAN
    */
    can_info_t Generic_imuCan; /* Hardware protocol definition */

    /*
    ** Covert IMU-bias channel state (latched from GENERIC_IMU_DROP_PATH).
    ** Bias is applied to the published device TLM only; the sim/42 truth is
    ** never touched, producing the truth-vs-bus divergence used for detection.
    */
    uint8 BiasLatched;   /* nonzero once a valid dead-drop has been latched */
    uint8 BiasAxisMask;  /* which axes to bias (X|Y|Z) */
    uint8 BiasProfile;   /* 1 = slow drift, 0 = constant */
    float BiasGyroStep;  /* angular bias added per cycle */
    float BiasGyroCap;   /* angular bias clamp */
    float BiasGyro;      /* accumulated angular bias applied to AngularAcc */
    float BiasAccelStep; /* linear bias added per cycle */
    float BiasAccelCap;  /* linear bias clamp */
    float BiasAccel;     /* accumulated linear bias applied to LinearAcc */

} GENERIC_IMU_AppData_t;

/*
** Exported Data
** Extern the global struct in the header for the Unit Test Framework (UTF).
*/
extern GENERIC_IMU_AppData_t GENERIC_IMU_AppData; /* GENERIC_IMU App Data */

/*
**
** Local function prototypes.
**
** Note: Except for the entry point (IMU_AppMain), these
**       functions are not called from any other source module.
*/
void  IMU_AppMain(void);
int32 GENERIC_IMU_AppInit(void);
void  GENERIC_IMU_ProcessCommandPacket(void);
void  GENERIC_IMU_ProcessGroundCommand(void);
void  GENERIC_IMU_ProcessTelemetryRequest(void);
void  GENERIC_IMU_ReportHousekeeping(void);
void  GENERIC_IMU_ReportDeviceTelemetry(void);
void  GENERIC_IMU_PollCovertDrop(void);
void  GENERIC_IMU_ResetCounters(void);
void  GENERIC_IMU_Enable(void);
void  GENERIC_IMU_Disable(void);
int32 GENERIC_IMU_VerifyCmdLength(CFE_MSG_Message_t *msg, uint16 expected_length);

#endif /* _GENERIC_IMU_APP_H_ */
