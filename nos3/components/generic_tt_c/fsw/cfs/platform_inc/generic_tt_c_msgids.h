/************************************************************************
** File:
**   generic_tt_c_msgids.h
**
** Purpose:
**   Define GENERIC_TT_C Message IDs.
**
**   Hardcoded legacy MID style (this repo); MID values match the Draco
**   topic-id-derived values (CFE_PLATFORM_COMPONENT_*_TOPICID_TO_MIDV
**   with topic ids 0x50 / 0x51) so the wire format stays identical
**   between the two testbeds.
*************************************************************************/
#ifndef _GENERIC_TT_C_MSGIDS_H_
#define _GENERIC_TT_C_MSGIDS_H_

#define GENERIC_TT_C_CMD_MID         0x1950
#define GENERIC_TT_C_REQ_HK_MID      0x1951
#define GENERIC_TT_C_HK_TLM_MID      0x0950
#define GENERIC_TT_C_DEVICE_TLM_MID  0x0951

#endif /* _GENERIC_TT_C_MSGIDS_H_ */
