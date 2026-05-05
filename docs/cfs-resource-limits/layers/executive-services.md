# Executive Services (ES) Limits

Source: `nos3/cfg/nos3_defs/cpu1_platform_cfg.h`.

```c
/* Milliseconds between ES health scans */
#define CFE_PLATFORM_ES_APP_SCAN_RATE      1000

/* Scan cycles before forceful kill of a non-responsive app */
#define CFE_PLATFORM_ES_APP_KILL_TIMEOUT   5

/* Maximum concurrently loaded applications */
#define CFE_PLATFORM_ES_MAX_APPLICATIONS   32
```

With a 1-second scan rate and a 5-cycle kill timeout, an application that stops calling `CFE_ES_RunLoop()` will be terminated after **5 seconds**.

## Exception action

The last field on each line of `nos3/fsw/build/exe/cpu1/cf/cfe_es_startup.scr` controls what ES does when an app crashes:

```
CFE_APP, noisy_app, NOISY_APP_Main, NOISY_APP, 160, 16384, 0x0, 0;
#                                                                 ^
#                             0 = restart the app on exception
#                             Non-zero = full processor reset
```

Every application in this build uses `0`, so a crashing app is silently restarted rather than triggering a processor reset.

## App slots

`CFE_PLATFORM_ES_MAX_APPLICATIONS = 32`. This build's startup script loads ~31 apps (including the `hs` slot, which is enabled in the build copy at `nos3/fsw/build/exe/cpu1/cf/cfe_es_startup.scr` line 13). Adding new apps without a budget can push the limit; the failure is `CFE_ES_ERR_LOAD_LIB` / `CFE_ES_ERR_APP_CREATE` at startup.

## Health & Safety (HS) status

The `hs` app is **present and loaded** in this build (cfe_es_startup.scr line 13, priority 85). Its watchdog tables and CPU-utilisation enforcement are documented separately in [`../apps/heritage/hs.md`](../apps/heritage/hs.md). This is a divergence from upstream NOS3, which historically shipped without HS; the cfs-resource-limits doc was rewritten on 2026-05-03 to reflect the current loaded set.

## Attack surface

- **App-kill window:** a 5-second hang grace period means a malicious app can stall the scheduler for up to ~5 s without being terminated. Useful in combination with SCH lag (see [scheduler.md](scheduler.md)).
- **Silent restart:** non-zero exception action would force a processor reset on crash, but the project standard is `0`. A repeatedly crashing app is a silent restart loop; only EVS exception events expose it, and EVS itself is rate-limited (see [event-services.md](event-services.md)).
- **No load-time auth:** ES will accept any `.so` placed in the cf filesystem and listed in the startup script. The integration step where new apps are dropped in is documented at [../ADDING_CFS_APPS.md](../ADDING_CFS_APPS.md).
