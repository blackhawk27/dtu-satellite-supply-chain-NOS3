/**
 * @file
 *   RTS 005 -- FOV-Exit Recovery (SAFE entry + AP3 rearm).
 *
 * Triggered by LC AP4 (SCIENCE_EXIT) once the spacecraft has been outside
 * the Denmark FOV box for 10 consecutive SCH cycles. Sequence:
 *   wakeup 0: MGR SET_MODE SAFE (MGR drives ADCS to SUNSAFE pointing).
 *   wakeup 1: DS SET_DEST_STATE (disable DS file index 3 = /data/inst, so
 *             no instrument packets are recorded out of FOV).
 *   wakeup 2: LC SET_AP_STATE AP3 (re-arm SCIENCE_OVERFLY so the next FOV
 *             entry can fire RTS 002; AP3 went PASSIVE on its previous
 *             fire).
 *
 * Distinct from RTS 1 (battery-low SAFE entry): re-arming AP3 must happen
 * only on a true geographic FOV exit, not on a battery-low recovery that
 * may still be inside the FOV.
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

#include "lc_msgids.h"
#include "lc_msgdefs.h"
#include "lc_msg.h"

typedef struct
{
    SC_RtsEntryHeader_t hdr1;
    MGR_U8_cmd_t        cmd1; /* MGR SET_MODE SAFE */
    SC_RtsEntryHeader_t hdr2;
    DS_DestStateCmd_t   cmd2; /* DS disable instrument recording */
    SC_RtsEntryHeader_t hdr3;
    LC_SetAPState_t     cmd3; /* LC re-arm AP3 */
} SC_RtsStruct005_t;

typedef union
{
    SC_RtsStruct005_t rts;
    uint16            buf[SC_RTS_BUFF_SIZE];
} SC_RtsTable005_t;

#define SC_MEMBER_SIZE(member) (sizeof(((SC_RtsStruct005_t *)0)->member))

SC_RtsTable005_t SC_Rts005 = {
    .rts = {
        /* MGR SET_MODE SAFE at wakeup 0 */
        .hdr1.TimeTag   = 1,
        .cmd1.CmdHeader = CFE_MSG_CMD_HDR_INIT(MGR_CMD_MID, SC_MEMBER_SIZE(cmd1), MGR_SET_MODE_CC, 0x00),
        .cmd1.U8        = MGR_SAFE_MODE,

        /* DS SET_DEST_STATE: disable file index 3 (mirror of RTS 002 wakeup 2) */
        .hdr2.TimeTag                = 1,
        .cmd2.CommandHeader          = CFE_MSG_CMD_HDR_INIT(DS_CMD_MID, SC_MEMBER_SIZE(cmd2), DS_SET_DEST_STATE_CC, 0x00),
        .cmd2.Payload.FileTableIndex = 3,
        .cmd2.Payload.EnableState    = DS_DISABLED,

        /* LC SET_AP_STATE: re-arm AP3 (SCIENCE_OVERFLY) for the next FOV entry */
        .hdr3.TimeTag    = 1,
        .cmd3.CmdHeader  = CFE_MSG_CMD_HDR_INIT(LC_CMD_MID, SC_MEMBER_SIZE(cmd3), LC_SET_AP_STATE_CC, 0x00),
        .cmd3.APNumber   = 3,
        .cmd3.NewAPState = LC_APSTATE_ACTIVE,
    }
};

CFE_TBL_FILEDEF(SC_Rts005, SC.RTS_TBL005, SC RTS005 FOV-Exit Recovery, sc_rts005.tbl)
