/**
 * @file md_msgids.h
 *
 * Hardcoded MID shim for non-EDS (legacy cFE) builds.
 * Values match DEFAULT_CFE_MISSION_MD_*_TOPICID from md_topicids.h
 * plus the standard CCSDS command (0x1800) / telemetry (0x0800) base.
 */
#ifndef MD_MSGIDS_H
#define MD_MSGIDS_H

#define MD_CMD_MID              0x1890  /**< MD ground command MID          */
#define MD_SEND_HK_MID          0x1891  /**< MD send housekeeping request   */
#define MD_WAKEUP_MID           0x1892  /**< MD wakeup (1 Hz dwell tick)    */

#define MD_HK_TLM_MID           0x0890  /**< MD housekeeping telemetry      */
#define MD_DWELL_PKT_MID_BASE   0x0891  /**< MD dwell packet telemetry base */

#endif /* MD_MSGIDS_H */
