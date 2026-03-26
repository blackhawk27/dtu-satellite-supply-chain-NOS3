/**
 * @file mm_msgids.h
 *
 * Hardcoded MID shim for non-EDS (legacy cFE) builds.
 * Values match DEFAULT_MM_MISSION_*_TOPICID from mm_topicids.h
 * plus the standard CCSDS command (0x1800) / telemetry (0x0800) base.
 */
#ifndef MM_MSGIDS_H
#define MM_MSGIDS_H

#define MM_CMD_MID      0x1888  /**< MM ground command MID        */
#define MM_SEND_HK_MID  0x1889  /**< MM send housekeeping request */

#define MM_HK_TLM_MID   0x0887  /**< MM housekeeping telemetry    */

#endif /* MM_MSGIDS_H */
