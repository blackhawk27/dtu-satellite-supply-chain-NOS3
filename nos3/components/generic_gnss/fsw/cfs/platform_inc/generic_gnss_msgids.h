/************************************************************************
** File:
**   generic_gnss_msgids.h
**
** Purpose:
**   Define GENERIC_GNSS Message IDs.
**
**   Hardcoded legacy MID style (this repo); MID values match the Draco
**   topic-id-derived values (CFE_PLATFORM_COMPONENT_*_TOPICID_TO_MIDV
**   with topic ids 0x52 / 0x53 / 0x54) so the wire format stays
**   identical between the two testbeds.
*************************************************************************/
#ifndef _GENERIC_GNSS_MSGIDS_H_
#define _GENERIC_GNSS_MSGIDS_H_

#define GENERIC_GNSS_CMD_MID            0x1952
#define GENERIC_GNSS_REQ_HK_MID         0x1953
#define GENERIC_GNSS_HK_TLM_MID         0x0952
#define GENERIC_GNSS_DEVICE_TLM_MID     0x0953
#define GENERIC_GNSS_SAT_HELLO_TLM_MID  0x0954

#endif /* _GENERIC_GNSS_MSGIDS_H_ */
