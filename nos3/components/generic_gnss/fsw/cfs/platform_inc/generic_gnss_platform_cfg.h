/************************************************************************
** File: generic_gnss_platform_cfg.h
**
** Purpose:
**   GENERIC_GNSS platform configuration. The FSW reads ASCII position
**   samples (lat, lon, alt) off a NOS Engine UART bus that the
**   generic_gnss simulator publishes on each tick. Spoofing this bus
**   diverges the FSW HK lat/lon from the simulator's truth log; the
**   ELK GNSS-to-GS Validation dashboard joins both for comparison.
**
**   `usart_29` was chosen as the next free UART slot after the heritage
**   GPS / cam / etc. allocations. If `make config` reports a clash, bump
**   the trailing digit and update `nos3-simulator.xml` to match.
*************************************************************************/
#ifndef _GENERIC_GNSS_PLATFORM_CFG_H_
#define _GENERIC_GNSS_PLATFORM_CFG_H_

#define GENERIC_GNSS_CFG_STRING       "usart_29"
#define GENERIC_GNSS_CFG_HANDLE       29
#define GENERIC_GNSS_CFG_BAUDRATE_HZ  115200
#define GENERIC_GNSS_CFG_MS_TIMEOUT   50
#define GENERIC_GNSS_CFG_POLL_PERIOD_MS 200

#endif
