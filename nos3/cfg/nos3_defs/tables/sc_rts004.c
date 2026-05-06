/************************************************************************
 * NASA Docket No. GSC-19,200-1, and identified as "cFS Draco"
 *
 * Copyright (c) 2023 United States Government as represented by the
 * Administrator of the National Aeronautics and Space Administration.
 * All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ************************************************************************/

/**
 * @file
 *   RTS 004 -- Critical Health Response (force SAFE)
 *
 * Triggered by LC AP5 (HS_APP_FAILURE: HS ResetsPerformed > 0) when HS
 * detects an app failure and resets it, or by ground uplink. Sequence:
 *   wakeup 0: MGR SET_MODE SAFE     (drops spacecraft to SAFE; ADCS
 *             slaves its pointing back to sun)
 *   wakeup 1: DS SET_DEST_STATE     (disable DS file index 3 = /data/inst
 *             so instrument recording stops, conserving flash and power
 *             during recovery)
 *
 * The DS-disable mirrors RTS 002's enable, so SCIENCE/SAFE transitions
 * are symmetric. RTS 005 (orbit-low) and RTS 006 (solar-array) emit the
 * same MGR SET_MODE SAFE but with distinct EVS event tags.
 */

#include "cfe.h"
#include "cfe_tbl_filedef.h"

#include "sc_tbldefs.h"
#include "sc_platform_cfg.h"
#include "sc_msgdefs.h"
#include "sc_msgids.h"
#include "sc_msg.h"

#include "mgr_msgids.h"
#include "mgr_msg.h"
#include "mgr_app.h"

#include "ds_msgids.h"
#include "ds_msg.h"
#include "ds_tbldefs.h"

typedef struct
{
    SC_RtsEntryHeader_t  hdr1;
    MGR_U8_cmd_t         cmd1;
    SC_RtsEntryHeader_t  hdr2;
    DS_SetDestStateCmd_t cmd2;
} SC_RtsStruct004_t;

typedef union
{
    SC_RtsStruct004_t rts;
    uint16            buf[SC_RTS_BUFF_SIZE];
} SC_RtsTable004_t;

#define SC_MEMBER_SIZE(member) (sizeof(((SC_RtsStruct004_t *)0)->member))

SC_RtsTable004_t SC_Rts004 = {
    .rts = {
        /* Force MGR SET_MODE SAFE at wakeup 0 */
        .hdr1.WakeupCount = 0,
        .cmd1.CmdHeader   = CFE_MSG_CMD_HDR_INIT(MGR_CMD_MID, SC_MEMBER_SIZE(cmd1), MGR_SET_MODE_CC, 0x00),
        .cmd1.U8          = MGR_SAFE_MODE,

        /* Stop DS instrument recording one tick later */
        .hdr2.WakeupCount             = 1,
        .cmd2.CommandHeader           = CFE_MSG_CMD_HDR_INIT(DS_CMD_MID, SC_MEMBER_SIZE(cmd2), DS_SET_DEST_STATE_CC, 0x00),
        .cmd2.Payload.FileTableIndex  = 3,
        .cmd2.Payload.EnableState     = DS_DISABLED,
    }
};

CFE_TBL_FILEDEF(SC_Rts004, SC.RTS_TBL004, SC RTS004 Critical Health Rsp, sc_rts004.tbl)
