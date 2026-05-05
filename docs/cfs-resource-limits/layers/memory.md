# Memory Resource Limits

Source: `nos3/cfg/nos3_defs/cpu1_platform_cfg.h`.

| Constant | Purpose | Size |
|---|---|---|
| `CFE_PLATFORM_SB_BUF_MEMORY_BYTES` | SB message buffer pool | 512 KB |
| `CFE_PLATFORM_TBL_BUF_MEMORY_BYTES` | Table services buffer | 512 KB |
| `CFE_PLATFORM_ES_CDS_SIZE` | Critical Data Store | 128 KB |
| `CFE_PLATFORM_ES_USER_RESERVED_SIZE` | User reserved memory | 1 MB |
| `CFE_PLATFORM_ES_RESET_AREA_SIZE` | Reset area | 170 KB |
| `CFE_PLATFORM_ES_MAX_BLOCK_SIZE` | Max single ES pool block | 80 KB |
| `CFE_PLATFORM_ES_PERF_DATA_BUFFER_SIZE` | cFS performance log entries | 10 000 |

## Implications

- **SB pool exhaustion** is observable at allocation time as `CFE_SB_GET_BUF_ERR_EID`, but the underlying message is dropped silently from the publisher's perspective.
- **TBL buffer** caps the size of any single table image; the LC / SC / DS tables are well under this. Exceeding it returns `CFE_TBL_ERR_FILE_SIZE_INCONSISTENT` at table load.
- **CDS** is small (128 KB). Apps that persist state across resets must budget within this; the EPS battery state and the boot counter both use CDS.
- **Reset area** stores the system log readable after a processor reset. Its 170 KB cap is the upper bound on post-mortem context.

## Attack surface

- **CDS poisoning:** an attacker with `MM` / `CFE_TBL_LOAD` privilege can write into CDS and affect post-reset behaviour (e.g. tamper boot counter, battery SOC seed).
- **Pool fragmentation:** sustained allocation of small buffers without timely return can fragment the 512 KB SB pool until a larger request fails, even when total free bytes look sufficient.
- **Performance log overflow:** at 10 000 entries the perf log wraps; an attacker who can trigger a burst of perf events can push their own activity off the buffer's tail.
