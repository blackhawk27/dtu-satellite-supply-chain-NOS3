/************************************************************************
** File: generic_gnss_events.h
**
** Purpose:
**   Define GENERIC_GNSS application event IDs.
*************************************************************************/
#ifndef _GENERIC_GNSS_EVENTS_H_
#define _GENERIC_GNSS_EVENTS_H_

#define GENERIC_GNSS_RESERVED_EID        0
#define GENERIC_GNSS_STARTUP_INF_EID     1
#define GENERIC_GNSS_LEN_ERR_EID         2
#define GENERIC_GNSS_PIPE_ERR_EID        3
#define GENERIC_GNSS_SUB_CMD_ERR_EID     4
#define GENERIC_GNSS_SUB_REQ_HK_ERR_EID  5
#define GENERIC_GNSS_PROCESS_CMD_ERR_EID 6

#define GENERIC_GNSS_CMD_ERR_EID         10
#define GENERIC_GNSS_CMD_NOOP_INF_EID    11
#define GENERIC_GNSS_CMD_RESET_INF_EID   12
#define GENERIC_GNSS_CMD_ENABLE_INF_EID  13
#define GENERIC_GNSS_ENABLE_INF_EID      14
#define GENERIC_GNSS_ENABLE_ERR_EID      15
#define GENERIC_GNSS_CMD_DISABLE_INF_EID 16
#define GENERIC_GNSS_DISABLE_INF_EID     17
#define GENERIC_GNSS_DISABLE_ERR_EID     18

#define GENERIC_GNSS_DEVICE_TLM_ERR_EID 30
#define GENERIC_GNSS_REQ_HK_ERR_EID     31
#define GENERIC_GNSS_REQ_DATA_ERR_EID   32

#define GENERIC_GNSS_GS_PING_INF_EID    40
#define GENERIC_GNSS_GS_PING_ERR_EID    41
#define GENERIC_GNSS_SUB_MGR_HK_ERR_EID 42

#endif
