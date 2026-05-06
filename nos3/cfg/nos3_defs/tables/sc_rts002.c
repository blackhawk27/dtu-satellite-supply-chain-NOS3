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
 *   RTS 002 -- Enter SCIENCE Mode (full activation)
 *
 * Fired by LC AP3 (SCIENCE_OVERFLY) on entry to the Denmark FOV box, or
 * by ground uplink. Sequence:
 *   wakeup 0: MGR SET_MODE SCIENCE  (flips spacecraft mode; ADCS slaves
 *             its pointing to MGR mode internally so no separate ADCS
 *             command is needed)
 *   wakeup 1: MGR SCI_PASS_INC      (increment science-pass counter for
 *             the Kibana gauge)
 *   wakeup 2: DS SET_DEST_STATE     (enable DS file index 3 = /data/inst,
 *             so instrument packets are recorded for this pass)
 *   wakeup 3: CF PLAYBACK_DIR       (queue /data/inst contents for CFDP
 *             class-2 downlink to ground entity 23 over channel 0)
 *   wakeup 4: LC SET_AP_STATE AP4   (re-arm AP4 SCIENCE_EXIT so the next
 *             FOV exit can fire RTS 005. Without this, AP4 stays passive
 *             after its first fire and the spacecraft cannot return to
 *             SAFE on subsequent FOV exits. AP4 RPN passes inside FOV,
 *             so re-arming here is safe; AP4 just sits armed waiting
 *             for the exit.)
 *
 * RTS 004 (SAFE entry) mirrors steps 0 and 2 in reverse so DS recording
 * stops when the spacecraft drops out of SCIENCE.
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

#include "cf_msgids.h"
#include "cf_msg.h"

#include "lc_msgids.h"
#include "lc_msgdefs.h"
#include "lc_msgstruct.h"
#include "lc_fcncodes.h"

typedef struct
{
    SC_RtsEntryHeader_t  hdr1;
    MGR_U8_cmd_t         cmd1;
    SC_RtsEntryHeader_t  hdr2;
    MGR_NoArgs_cmd_t     cmd2;
    SC_RtsEntryHeader_t  hdr3;
    DS_SetDestStateCmd_t cmd3;
    SC_RtsEntryHeader_t  hdr4;
    CF_PlaybackDirCmd_t  cmd4;
    SC_RtsEntryHeader_t  hdr5;
    LC_SetAPStateCmd_t   cmd5;
} SC_RtsStruct002_t;

typedef union
{
    SC_RtsStruct002_t rts;
    uint16            buf[SC_RTS_BUFF_SIZE];
} SC_RtsTable002_t;

#define SC_MEMBER_SIZE(member) (sizeof(((SC_RtsStruct002_t *)0)->member))

SC_RtsTable002_t SC_Rts002 = {
    .rts = {
        /* Send MGR SET_MODE SCIENCE at wakeup 0 */
        .hdr1.WakeupCount = 0,
        .cmd1.CmdHeader   = CFE_MSG_CMD_HDR_INIT(MGR_CMD_MID, SC_MEMBER_SIZE(cmd1), MGR_SET_MODE_CC, 0x00),
        .cmd1.U8          = MGR_SCIENCE_MODE,

        /* Increment MGR SciPassCount one tick later so the Kibana SCIENCE-pass
           gauge reflects each Denmark overfly. */
        .hdr2.WakeupCount = 1,
        .cmd2             = {CFE_MSG_CMD_HDR_INIT(MGR_CMD_MID, SC_MEMBER_SIZE(cmd2), MGR_SCI_PASS_INC_CC, 0x00)},

        /* Enable DS destination index 3 (/data/inst) so instrument packets
           are recorded during the SCIENCE pass. (RTS 004 disables it on SAFE
           entry, so this is what actually gates instrument data capture.) */
        .hdr3.WakeupCount             = 2,
        .cmd3.CommandHeader           = CFE_MSG_CMD_HDR_INIT(DS_CMD_MID, SC_MEMBER_SIZE(cmd3), DS_SET_DEST_STATE_CC, 0x00),
        .cmd3.Payload.FileTableIndex  = 3,
        .cmd3.Payload.EnableState     = DS_ENABLED,

        /* Queue /data/inst for CFDP class-2 downlink to ground (entity 23)
           over channel 0. keep=0 means files are deleted after successful
           downlink to free flash. */
        .hdr4.WakeupCount    = 3,
        .cmd4.cmd_header     = CFE_MSG_CMD_HDR_INIT(CF_CMD_MID, SC_MEMBER_SIZE(cmd4), CF_PLAYBACK_DIR_CC, 0x00),
        .cmd4.cfdp_class     = 1,
        .cmd4.keep           = 0,
        .cmd4.chan_num       = 0,
        .cmd4.priority       = 0,
        .cmd4.dest_id        = 23,
        .cmd4.src_filename   = "/data/inst",
        .cmd4.dst_filename   = "inst/",

        /* Re-arm AP4 (SCIENCE_EXIT). AP4 went PASSIVE on its previous fire
           and would otherwise never re-fire. AP3 (SCIENCE_OVERFLY) just
           fired this RTS, so AP3 is now PASSIVE and AP4 must take over
           detecting the next FOV exit. */
        .hdr5.WakeupCount             = 4,
        .cmd5.CommandHeader           = CFE_MSG_CMD_HDR_INIT(LC_CMD_MID, SC_MEMBER_SIZE(cmd5), LC_SET_AP_STATE_CC, 0x00),
        .cmd5.Payload.APNumber        = 4,
        .cmd5.Payload.NewAPState      = LC_APSTATE_ACTIVE,
    }
};

CFE_TBL_FILEDEF(SC_Rts002, SC.RTS_TBL002, SC RTS002 Enter SCIENCE Mode, sc_rts002.tbl)
