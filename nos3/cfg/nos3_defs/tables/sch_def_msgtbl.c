/*
** $Id: sch_def_msgtbl.c 1.3 2017/06/21 15:28:56EDT mdeschu Exp  $
**
**  Copyright (c) 2007-2014 United States Government as represented by the
**  Administrator of the National Aeronautics and Space Administration.
**  All Other Rights Reserved.
**
**  This software was created at NASA's Goddard Space Flight Center.
**  This software is governed by the NASA Open Source Agreement and may be
**  used, distributed and modified only pursuant to the terms of that
**  agreement.
**
** Purpose: Scheduler (SCH) default message definition table data
**
** Author:
**
** Notes:
**
*/

/*************************************************************************
** Include section
**************************************************************************/

#include "cfe.h"
#include "cfe_endian.h"
#include "cfe_tbl_filedef.h"
#include "sch_platform_cfg.h"
#include "sch_tbldefs.h"

#include "cfe_msgids.h"
#include "cf_msgids.h"
#include "ci_msgids.h"
#include "cs_msgids.h"
#include "ds_msgids.h"
#include "fm_msgids.h"
#include "hk_msgids.h"
#include "hs_msgids.h"
#include "lc_msgids.h"
#include "lc_msgdefs.h"
#include "md_msgids.h"
#include "mm_msgids.h"
#include "sc_msgids.h"
#include "sch_msgids.h"
#include "to_msgids.h"
#include "to_lab_msgids.h"

/* Component Include Files */
#include "cam_msgids.h"
#include "generic_adcs_msgids.h"
#include "generic_css_msgids.h"
#include "generic_eps_msgids.h"
#include "generic_fss_msgids.h"
#include "generic_gnss_msgids.h"
#include "generic_imu_msgids.h"
#include "generic_mag_msgids.h"
#include "generic_radio_msgids.h"
#include "generic_reaction_wheel_msgids.h"
#include "generic_star_tracker_msgids.h"
#include "generic_thruster_msgids.h"
#include "generic_torquer_msgids.h"
#include "generic_tt_c_msgids.h"
#include "mgr_msgids.h"
#include "novatel_oem615_msgids.h"
#include "sample_msgids.h"
#include "syn_msgids.h"

/*
** Default command definition table data
*/
SCH_MessageEntry_t SCH_DefaultMessageTable[SCH_MAX_MESSAGES] = {
    /* command ID #0 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}},

    /* cFE housekeeping request messages */
    {{CFE_MAKE_BIG16(CFE_ES_SEND_HK_MID), CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}},
    {{CFE_MAKE_BIG16(CFE_EVS_SEND_HK_MID), CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}},
    {{CFE_MAKE_BIG16(CFE_SB_SEND_HK_MID), CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}},
    {{CFE_MAKE_BIG16(CFE_TIME_SEND_HK_MID), CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}},
    {{CFE_MAKE_BIG16(CFE_TBL_SEND_HK_MID), CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}},

    /* CFS housekeeping request messages */
    {{CFE_MAKE_BIG16(CS_SEND_HK_MID), CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}}, /* 6 CS */
    {{CFE_MAKE_BIG16(DS_SEND_HK_MID), CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}}, /* 7 DS */
    {{CFE_MAKE_BIG16(FM_SEND_HK_MID), CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}}, /* 8 FM */

    /* command ID #9 - Housekeeping HK Request */
    {{CFE_MAKE_BIG16(HK_SEND_HK_MID), CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}},

    /* command ID #10 - Health & Safety HK Request */
    {{CFE_MAKE_BIG16(HS_SEND_HK_MID), CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}},

    {{CFE_MAKE_BIG16(LC_SEND_HK_MID), CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}},  /* 11 LC */
    {{CFE_MAKE_BIG16(MD_SEND_HK_MID), CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}},  /* 12 MD */
    {{CFE_MAKE_BIG16(MM_SEND_HK_MID), CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}},  /* 13 MM */
    {{CFE_MAKE_BIG16(SC_SEND_HK_MID), CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}},  /* 14 SC */
    {{CFE_MAKE_BIG16(SCH_SEND_HK_MID), CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}}, /* 15 SCH */

    /* CFS routine messages 16 - 127 */
    {{CFE_MAKE_BIG16(CF_SEND_HK_MID), CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}},  /* 16 CF HK */
    {{CFE_MAKE_BIG16(CF_WAKE_UP_MID), CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}},  /* 17 CF WAKEUP */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 18 was SBN HK - SBN not loaded */
    {{CFE_MAKE_BIG16(CS_BACKGROUND_CYCLE_MID), CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}}, /* 19 CS Cycle */
    /* 20 LC Wakeup: full 16-byte LC_SampleAPCmd_t so LC's length-verify accepts it.
       CCSDS length field = packet_bytes - 7 = 9. Payload: Start=0xFFFF, End=0xFFFF (LC heritage
       "sample all" wildcard - LC_ALL_ACTIONPOINTS), UpdateAge=1, Pad=0. Previously Start=0x0000
       which fails LC's range check ('Sample AP error: invalid AP number, start=0, end=65535'). */
    {{CFE_MAKE_BIG16(LC_SAMPLE_AP_MID), CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0009), CFE_MAKE_BIG16(0x0000),
      CFE_MAKE_BIG16(0xFFFF), CFE_MAKE_BIG16(0xFFFF), CFE_MAKE_BIG16(0x0001), CFE_MAKE_BIG16(0x0000)}},
    {{CFE_MAKE_BIG16(SC_1HZ_WAKEUP_MID), CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}}, /* 21 SC Wakeup */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 22 SCH Unused */
    {{CFE_MAKE_BIG16(MD_WAKEUP_MID), CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}}, /* 23 MD Wakeup */
    {{CFE_MAKE_BIG16(HS_WAKEUP_MID), CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}}, /* 24 HS Wakeup */
    /* Component housekeeping request messages 25-37 */
    {{CFE_MAKE_BIG16(GENERIC_EPS_REQ_HK_MID),          CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}}, /* 25 EPS */
    {{CFE_MAKE_BIG16(NOVATEL_OEM615_REQ_HK_MID),        CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}}, /* 26 GPS */
    {{CFE_MAKE_BIG16(GENERIC_ADCS_REQ_HK_MID),          CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}}, /* 27 ADCS */
    {{CFE_MAKE_BIG16(GENERIC_IMU_REQ_HK_MID),           CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}}, /* 28 IMU */
    {{CFE_MAKE_BIG16(GENERIC_CSS_REQ_HK_MID),           CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}}, /* 29 CSS */
    {{CFE_MAKE_BIG16(GENERIC_FSS_REQ_HK_MID),           CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}}, /* 30 FSS */
    {{CFE_MAKE_BIG16(GENERIC_MAG_REQ_HK_MID),           CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}}, /* 31 MAG */
    /* 32 RW: issue GENERIC_RW_APP_CMD_MID with FC=GENERIC_RW_APP_REQ_DATA_CC (2) so
       the app actually queries each wheel's momentum over UART and populates
       HkTlm.Payload.data.momentum[] before publishing HK. The prior slot content
       (GENERIC_RW_APP_SEND_HK_MID) only triggered ReportHousekeeping, which
       publishes whatever is already in the HK struct (zeros at init) and never
       talks to the sim, so the Kibana rw_momentum panel stayed empty.
       REQ_DATA calls ReportHousekeeping() itself, so one slot covers both.
       Secondary-header word is (FunctionCode<<8)|Checksum = 0x0200. */
    {{CFE_MAKE_BIG16(GENERIC_RW_APP_CMD_MID),           CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), CFE_MAKE_BIG16(0x0200)}}, /* 32 RW REQ_DATA */
    {{CFE_MAKE_BIG16(GENERIC_RADIO_REQ_HK_MID),         CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}}, /* 33 Radio */
    {{CFE_MAKE_BIG16(GENERIC_STAR_TRACKER_REQ_HK_MID),  CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}}, /* 34 StarTracker */
    {{CFE_MAKE_BIG16(GENERIC_TORQUER_REQ_HK_MID),       CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}}, /* 35 Torquer */
    {{CFE_MAKE_BIG16(MGR_REQ_HK_MID),                   CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}}, /* 36 MGR */
    {{CFE_MAKE_BIG16(SAMPLE_REQ_HK_MID),                CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}}, /* 37 Sample */
    {{CFE_MAKE_BIG16(TO_LAB_SEND_HK_MID),               CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}}, /* 38 TO_LAB */
    /* 39 ST device-data request: same MID as slot 34 (REQ_HK_MID) but with
       FunctionCode = GENERIC_STAR_TRACKER_REQ_DATA_TLM (1). This calls
       ReportDeviceTelemetry which sends UART code 2 to the sim, producing the
       "is_valid=..., data_point=q0, q1, q2, q3" debug line that Logstash
       parses into st_q0..st_q3 for the dashboard. Secondary word
       = (FunctionCode<<8)|Checksum = 0x0100. */
    {{CFE_MAKE_BIG16(GENERIC_STAR_TRACKER_REQ_HK_MID),   CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), CFE_MAKE_BIG16(0x0100)}}, /* 39 ST REQ_DATA_TLM */
    /* 40 ADCS ADAC wakeup: drives Generic_ADCS_execute_attitude_determination_and_attitude_control()
       so the controller actually computes Tcmd and emits SET_TORQUE messages to the RW. Without
       this slot, ADCS only ever publishes HK and never runs the control loop, so the wheels stay
       at zero momentum even with ADCS in BDOT/SUNSAFE/INERTIAL mode. */
    {{CFE_MAKE_BIG16(GENERIC_ADCS_ADAC_UPDATE_MID),     CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}}, /* 40 ADCS ADAC wakeup */
    /* 41-44 IMU/MAG/FSS/CSS device-data requests: same MID as their HK request slots
       (28-31) but with FunctionCode = REQ_DATA_TLM (1) so each app's
       ReportDeviceTelemetry runs and publishes GENERIC_*_DEVICE_TLM_MID. The ADCS
       app subscribes to those device-tlm MIDs to populate the DI packet (wbn,
       bvb, svb). Without these slots, IMU/MAG/FSS/CSS only emit HK (which doesn't
       carry sensor data), so ADCS sees DI all-zero, and SUNSAFE/INERTIAL controllers
       compute Tcmd = 0 even when the body is rotating. Mirrors the existing slot
       39 trick used to drive star-tracker device telemetry. Secondary header word
       = (1<<8)|0 = 0x0100. */
    {{CFE_MAKE_BIG16(GENERIC_IMU_REQ_HK_MID),           CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), CFE_MAKE_BIG16(0x0100)}}, /* 41 IMU REQ_DATA_TLM */
    {{CFE_MAKE_BIG16(GENERIC_MAG_REQ_HK_MID),           CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), CFE_MAKE_BIG16(0x0100)}}, /* 42 MAG REQ_DATA_TLM */
    {{CFE_MAKE_BIG16(GENERIC_FSS_REQ_HK_MID),           CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), CFE_MAKE_BIG16(0x0100)}}, /* 43 FSS REQ_DATA_TLM */
    {{CFE_MAKE_BIG16(GENERIC_CSS_REQ_HK_MID),           CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), CFE_MAKE_BIG16(0x0100)}}, /* 44 CSS REQ_DATA_TLM */
    /* 45-46: GENERIC_GNSS / GENERIC_TT_C HK requests. Without these, the apps
       initialise but never publish HK_TLM, so god_view_capture cannot decode
       gnss_lat/lon/in_denmark_box/in_science_mode/last_ping_seq/last_ping_time/
       ping_count and the GNSS-to-GS Validation dashboard panels are blank. */
    {{CFE_MAKE_BIG16(GENERIC_GNSS_REQ_HK_MID),          CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}}, /* 45 GNSS */
    {{CFE_MAKE_BIG16(GENERIC_TT_C_REQ_HK_MID),          CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}}, /* 46 TT&C */
    /* 47 SAMPLE device-data request: SAMPLE_REQ_HK_MID with FC=SAMPLE_REQ_DATA_TLM
       (1) so SAMPLE_ProcessGroundCommand publishes SAMPLE_DEVICE_TLM_MID with the
       PassNumber + RegionStatus that god_view_capture decodes into sample_pass_num.
       Mirrors the slot-44 CSS REQ_DATA_TLM trick. Without this slot the device
       telemetry is never emitted, so the Kibana panel stays at 0. */
    {{CFE_MAKE_BIG16(SAMPLE_REQ_HK_MID),                CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), CFE_MAKE_BIG16(0x0100)}}, /* 47 SAMPLE REQ_DATA_TLM */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 48 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 45 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 46 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 47 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 48 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 49 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 50 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 51 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 52 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 53 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 54 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 55 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 56 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 57 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 58 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 59 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 60 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 61 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 62 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 63 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 64 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 65 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 66 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 67 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 68 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 69 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 70 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 71 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 72 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 73 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 74 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 75 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 76 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 77 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 78 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 79 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 80 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 81 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 82 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 83 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 84 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 85 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 86 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 87 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 88 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 89 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 90 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 91 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 92 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 93 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 94 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 95 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 96 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 97 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 98 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 99 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 100 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 101 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 102 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 103 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 104 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 105 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 106 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 107 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 108 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 109 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 110 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 111 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 112 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 113 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 114 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 115 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 116 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 117 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 118 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 119 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 120 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 121 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}, /* 122 */
    {{CFE_MAKE_BIG16(SCH_UNUSED_MID)}}  /* 123 */
    /* slots 124-127 still removed: array exceeded SCH_MAX_MESSAGES (128) when
       slots 40-44 were added without truncating. Slots 45-46 (GNSS, TT_C) are
       inserted in-place over previously-unused indices, so the 128-slot total
       is preserved. */
};

/*
** Table file header
*/
CFE_TBL_FILEDEF(SCH_DefaultMessageTable, SCH.MSG_DEFS, SCH message definitions table, sch_def_msgtbl.tbl)

/*************************************************************************
**
** File data
**
**************************************************************************/

/*
** (none)
*/

/*************************************************************************
**
** Local function prototypes
**
**************************************************************************/

/*
** (none)
*/

/************************/
/*  End of File Comment */
/************************/
