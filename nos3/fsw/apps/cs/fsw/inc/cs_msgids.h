/**
 * @file cs_msgids.h
 *
 * Hardcoded MID shim for non-EDS (legacy cFE) builds.
 * Values match DEFAULT_CFE_MISSION_CS_*_TOPICID from cs_topicids.h
 * plus the standard CCSDS command (0x1800) / telemetry (0x0800) base.
 */
#ifndef CS_MSGIDS_H
#define CS_MSGIDS_H

#define CS_CMD_MID              0x189F  /**< CS ground command MID            */
#define CS_SEND_HK_MID          0x18A0  /**< CS send housekeeping request     */
#define CS_BACKGROUND_CYCLE_MID 0x18A1  /**< CS background cycle wakeup       */

#define CS_HK_TLM_MID           0x08A4  /**< CS housekeeping telemetry        */

#endif /* CS_MSGIDS_H */
