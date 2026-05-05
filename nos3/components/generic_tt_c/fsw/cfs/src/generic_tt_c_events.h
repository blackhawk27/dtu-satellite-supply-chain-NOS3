/************************************************************************
** File: generic_tt_c_events.h
**
** Purpose:
**   Define GENERIC_TT_C application event IDs.
*************************************************************************/
#ifndef _GENERIC_TT_C_EVENTS_H_
#define _GENERIC_TT_C_EVENTS_H_

#define GENERIC_TT_C_RESERVED_EID        0
#define GENERIC_TT_C_STARTUP_INF_EID     1
#define GENERIC_TT_C_LEN_ERR_EID         2
#define GENERIC_TT_C_PIPE_ERR_EID        3
#define GENERIC_TT_C_SUB_CMD_ERR_EID     4
#define GENERIC_TT_C_SUB_REQ_HK_ERR_EID  5
#define GENERIC_TT_C_PROCESS_CMD_ERR_EID 6

#define GENERIC_TT_C_CMD_ERR_EID         10
#define GENERIC_TT_C_CMD_NOOP_INF_EID    11
#define GENERIC_TT_C_CMD_RESET_INF_EID   12
#define GENERIC_TT_C_CMD_ENABLE_INF_EID  13
#define GENERIC_TT_C_ENABLE_INF_EID      14
#define GENERIC_TT_C_ENABLE_ERR_EID      15
#define GENERIC_TT_C_CMD_DISABLE_INF_EID 16
#define GENERIC_TT_C_DISABLE_INF_EID     17
#define GENERIC_TT_C_DISABLE_ERR_EID     18

#define GENERIC_TT_C_DEVICE_TLM_ERR_EID 30
#define GENERIC_TT_C_REQ_HK_ERR_EID     31
#define GENERIC_TT_C_REQ_DATA_ERR_EID   32

#endif
