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
 *   RTS 006 -- Solar-Array Recovery
 *
 * Triggered by LC AP7 (SOLAR_OFF_RECOVERY) when WP5 latches: EPS solar
 * array voltage has fallen below 100 mV (array off or fully shadowed).
 * Forces SAFE mode so the ADCS controller switches to sun-pointing,
 * giving the array a chance to recover. Distinct from RTS 4 only in the
 * EVS event tag.
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

typedef struct
{
    SC_RtsEntryHeader_t hdr1;
    MGR_U8_cmd_t        cmd1;
} SC_RtsStruct006_t;

typedef union
{
    SC_RtsStruct006_t rts;
    uint16            buf[SC_RTS_BUFF_SIZE];
} SC_RtsTable006_t;

#define SC_MEMBER_SIZE(member) (sizeof(((SC_RtsStruct006_t *)0)->member))

SC_RtsTable006_t SC_Rts006 = {
    .rts = {
        /* Force MGR SET_MODE SAFE at wakeup 0 */
        .hdr1.WakeupCount = 0,
        .cmd1.CmdHeader   = CFE_MSG_CMD_HDR_INIT(MGR_CMD_MID, SC_MEMBER_SIZE(cmd1), MGR_SET_MODE_CC, 0x00),
        .cmd1.U8          = MGR_SAFE_MODE,
    }
};

CFE_TBL_FILEDEF(SC_Rts006, SC.RTS_TBL006, SC RTS006 Solar-Array Recovery, sc_rts006.tbl)
