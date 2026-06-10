/**
 * @file
 *   RTS 004 -- Critical Health Response (force SAFE + comms downgrade).
 *
 * DTU parity with the Draco baseline so both testbeds react to a spoofed low
 * battery identically. Triggered by LC AP0 (BATTERY_LOW, WP0 < 14800 mV) or by
 * ground uplink. Sequence (this fork's RTS idiom: TimeTag-paced entries):
 *   wakeup 0: MGR SET_MODE SAFE   (drop to SAFE; ADCS slaves pointing to sun)
 *   wakeup 1: DS SET_DEST_STATE   (disable DS file index 3 = /data/inst, so
 *             instrument recording stops, mirroring RTS 002's enable)
 *   wakeup 2: TO_LAB SET_SAFE_TLM (DTU: downgrade the downlink to a low-rate
 *             housekeeping beacon - the high-rate component device/science
 *             streams are dropped, but the link and the CI_LAB uplink stay
 *             fully alive so the ground can recover with SET_NOMINAL_TLM)
 *
 * This is what the EPS-spoof attack weaponises: a forged low battery
 * (noisy_app OPCODE_EPS_OVERRIDE) trips AP0 -> this RTS -> a false SAFE that
 * denies full-rate telemetry. See docs/security/eps-spoof-comms-downgrade.md
 * and docs/security/00-dtu-secured-fork-notes.md.
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

/* DTU: SAFE-mode comms downgrade target (downlink throughput reduction).
   to_lab_msgids.h is on the def-table include path (to_lab_sub.c uses it). The
   SET_SAFE_TLM command code lives in to_lab/fsw/src/to_lab_msg.h, which may not
   be on this table's path - mirror it locally (kept in sync with to_lab_msg.h)
   so this table never depends on TO_LAB's private src include dir. */
#include "to_lab_msgids.h"
#ifndef TO_LAB_SET_SAFE_TLM_CC
#define TO_LAB_SET_SAFE_TLM_CC 7
#endif

typedef struct
{
    SC_RtsEntryHeader_t     hdr1;
    MGR_U8_cmd_t            cmd1;
    SC_RtsEntryHeader_t     hdr2;
    DS_DestStateCmd_t       cmd2;
    SC_RtsEntryHeader_t     hdr3;
    CFE_MSG_CommandHeader_t cmd3;  /* TO_LAB SET_SAFE_TLM (no-args command) */
} SC_RtsStruct004_t;

typedef union
{
    SC_RtsStruct004_t rts;
    uint16            buf[SC_RTS_BUFF_SIZE];
} SC_RtsTable004_t;

#define SC_MEMBER_SIZE(member) (sizeof(((SC_RtsStruct004_t *)0)->member))

SC_RtsTable004_t SC_Rts004 = {
    .rts = {
        /* MGR SET_MODE SAFE at wakeup 0 */
        .hdr1.TimeTag   = 1,
        .cmd1.CmdHeader = CFE_MSG_CMD_HDR_INIT(MGR_CMD_MID, SC_MEMBER_SIZE(cmd1), MGR_SET_MODE_CC, 0x00),
        .cmd1.U8        = MGR_SAFE_MODE,

        /* DS SET_DEST_STATE: disable file index 3 (/data/inst) to stop instrument recording */
        .hdr2.TimeTag                = 1,
        .cmd2.CommandHeader          = CFE_MSG_CMD_HDR_INIT(DS_CMD_MID, SC_MEMBER_SIZE(cmd2), DS_SET_DEST_STATE_CC, 0x00),
        .cmd2.Payload.FileTableIndex = 3,
        .cmd2.Payload.EnableState    = DS_DISABLED,

        /* DTU SAFE-mode comms downgrade: drop the high-rate component streams,
           keep the low-rate HK beacon. Uplink stays alive (recover with
           TO_LAB SET_NOMINAL_TLM). */
        .hdr3.TimeTag = 1,
        .cmd3         = CFE_MSG_CMD_HDR_INIT(TO_LAB_CMD_MID, SC_MEMBER_SIZE(cmd3), TO_LAB_SET_SAFE_TLM_CC, 0x00),
    }
};

CFE_TBL_FILEDEF(SC_Rts004, SC.RTS_TBL004, SC RTS004 Critical Health Rsp, sc_rts004.tbl)
