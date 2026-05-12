#include "generic_gnss_device.h"
#include "cfe.h"
#include <stdlib.h>
#include <string.h>

int32 GENERIC_GNSS_ParseBusLine(const char *line, double *out_lat,
                                double *out_lon, double *out_alt)
{
    if (line == NULL || out_lat == NULL || out_lon == NULL || out_alt == NULL)
    {
        return OS_ERROR;
    }
    /* Require the "GNSS," prefix so we don't misparse stray bytes. */
    const size_t prefix_len = strlen(GENERIC_GNSS_BUS_PREFIX);
    if (strncmp(line, GENERIC_GNSS_BUS_PREFIX, prefix_len) != 0)
    {
        return OS_ERROR;
    }
    const char *cursor = line + prefix_len;
    char *endp = NULL;

    double lat = strtod(cursor, &endp);
    if (endp == cursor || *endp != ',') return OS_ERROR;
    cursor = endp + 1;

    double lon = strtod(cursor, &endp);
    if (endp == cursor || *endp != ',') return OS_ERROR;
    cursor = endp + 1;

    double alt = strtod(cursor, &endp);
    if (endp == cursor) return OS_ERROR;

    *out_lat = lat;
    *out_lon = lon;
    *out_alt = alt;
    return OS_SUCCESS;
}
