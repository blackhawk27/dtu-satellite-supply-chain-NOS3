/**
 * @file
 *   RTS 002 -- Enter SCIENCE Mode (Denmark FOV overfly).
 *
 * Fired by LC AP3 (SCIENCE_OVERFLY) when GENERIC_GNSS InDenmarkBox flips to 1,
 * or by ground uplink. Sequence:
 *   wakeup 0: MGR SET_MODE SCIENCE (ADCS slaves to MGR mode internally).
 *   wakeup 1: MGR SCI_PASS_INC (increment SciPassCount for the Kibana gauge).
 *   wakeup 2: DS SET_DEST_STATE (enable DS file index 3 = /data/inst, so
 *             instrument packets are recorded for this pass).
 *   wakeup 3: CF PLAYBACK_DIR (queue /data/inst contents for CFDP class-2
 *             downlink to ground entity 23 over channel 0).
 *   wakeup 4: LC SET_AP_STATE AP4 (re-arm SCIENCE_EXIT so the next FOV exit
 *             can fire RTS 005; AP4 went PASSIVE on the previous fire).
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
#include "ds_msgdefs.h"
#include "ds_msg.h"
#include "ds_extern_typedefs.h"

#include "cf_msgids.h"
#include "cf_msg.h"

#include "lc_msgids.h"
#include "lc_msgdefs.h"
#include "lc_msg.h"

typedef struct
{
    SC_RtsEntryHeader_t  hdr1;
    MGR_U8_cmd_t         cmd1;
    SC_RtsEntryHeader_t  hdr2;
    MGR_NoArgs_cmd_t     cmd2;
    SC_RtsEntryHeader_t  hdr3;
    DS_DestStateCmd_t    cmd3;
    SC_RtsEntryHeader_t  hdr4;
    CF_PlaybackDirCmd_t  cmd4;
    SC_RtsEntryHeader_t  hdr5;
    LC_SetAPState_t      cmd5;
} SC_RtsStruct002_t;

typedef union
{
    SC_RtsStruct002_t rts;
    uint16            buf[SC_RTS_BUFF_SIZE];
} SC_RtsTable002_t;

#define SC_MEMBER_SIZE(member) (sizeof(((SC_RtsStruct002_t *)0)->member))

SC_RtsTable002_t SC_Rts002 = {
    .rts = {
        /* MGR SET_MODE SCIENCE at wakeup 0 */
        .hdr1.TimeTag   = 1,
        .cmd1.CmdHeader = CFE_MSG_CMD_HDR_INIT(MGR_CMD_MID, SC_MEMBER_SIZE(cmd1), MGR_SET_MODE_CC, 0x00),
        .cmd1.U8        = MGR_SCIENCE_MODE,

        /* MGR SCI_PASS_INC at wakeup 1 (Kibana SCIENCE-pass gauge) */
        .hdr2.TimeTag = 1,
        .cmd2         = {CFE_MSG_CMD_HDR_INIT(MGR_CMD_MID, SC_MEMBER_SIZE(cmd2), MGR_SCI_PASS_INC_CC, 0x00)},

        /* DS SET_DEST_STATE: enable file index 3 (/data/inst) for this pass */
        .hdr3.TimeTag                = 1,
        .cmd3.CommandHeader          = CFE_MSG_CMD_HDR_INIT(DS_CMD_MID, SC_MEMBER_SIZE(cmd3), DS_SET_DEST_STATE_CC, 0x00),
        .cmd3.Payload.FileTableIndex = 3,
        .cmd3.Payload.EnableState    = DS_ENABLED,

        /* CF PLAYBACK_DIR: queue /data/inst for CFDP class-2 to entity 23 on channel 0.
           keep=0 so files are deleted after successful downlink to free flash. */
        .hdr4.TimeTag      = 1,
        .cmd4.cmd_header   = CFE_MSG_CMD_HDR_INIT(CF_CMD_MID, SC_MEMBER_SIZE(cmd4), CF_PLAYBACK_DIR_CC, 0x00),
        .cmd4.cfdp_class   = 1,
        .cmd4.keep         = 0,
        .cmd4.chan_num     = 0,
        .cmd4.priority     = 0,
        .cmd4.dest_id      = 23,
        .cmd4.src_filename = "/data/inst",
        .cmd4.dst_filename = "inst/",

        /* LC SET_AP_STATE: re-arm AP4 (SCIENCE_EXIT) so the next FOV exit fires RTS 5 */
        .hdr5.TimeTag    = 1,
        .cmd5.CmdHeader  = CFE_MSG_CMD_HDR_INIT(LC_CMD_MID, SC_MEMBER_SIZE(cmd5), LC_SET_AP_STATE_CC, 0x00),
        .cmd5.APNumber   = 4,
        .cmd5.NewAPState = LC_APSTATE_ACTIVE,
    }
};

CFE_TBL_FILEDEF(SC_Rts002, SC.RTS_TBL002, SC RTS002 Enter SCIENCE Mode, sc_rts002.tbl)
