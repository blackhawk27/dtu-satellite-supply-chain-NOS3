/*******************************************************************************
** File: generic_gnss_device.h
**
** Purpose:
**   Device protocol for the generic_gnss UART bus. The sim writes one ASCII
**   line per tick of the form:
**       GNSS,<lat>,<lon>,<alt>\n
**   where lat/lon are decimal degrees (WGS84) and alt is meters above the
**   WGS84 ellipsoid. The FSW child task reads bytes off the UART, splits on
**   newlines, and calls GENERIC_GNSS_ParseBusLine to extract the three
**   numbers. Keeping the protocol ASCII / line-oriented makes it trivial
**   to spoof in attack scripts and to inspect with tcpdump-style tools.
*******************************************************************************/
#ifndef _GENERIC_GNSS_DEVICE_H_
#define _GENERIC_GNSS_DEVICE_H_

#include "common_types.h"

#define GENERIC_GNSS_BUS_PREFIX "GNSS,"

/* Parse one line written by the sim. Returns OS_SUCCESS on a well-formed
 * "GNSS,<lat>,<lon>,<alt>" string, OS_ERROR otherwise. Output doubles are
 * untouched on failure so callers can keep a previous-good cache. */
int32 GENERIC_GNSS_ParseBusLine(const char *line, double *out_lat,
                                double *out_lon, double *out_alt);

#endif /* _GENERIC_GNSS_DEVICE_H_ */
