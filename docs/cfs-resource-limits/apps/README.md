# Per-App Resource Limits

One file per loaded application or component. Used as input for security-testing scope decisions: each page documents the app's surface area (msg IDs, pipe depth, telemetry rate, storage), known silent-failure modes, and what an attacker can do with control over its inputs.

Pages are grouped by provenance:

| Group | Source | Pages |
|---|---|---|
| **heritage/** | NASA cFS upstream apps shipped with NOS3 | [ci_lab](heritage/ci_lab.md), [to_lab](heritage/to_lab.md), [ci](heritage/ci.md), [to](heritage/to.md), [sch](heritage/sch.md), [hk](heritage/hk.md), [ds](heritage/ds.md), [fm](heritage/fm.md), [lc](heritage/lc.md), [sc](heritage/sc.md), [sbn](heritage/sbn.md), [cf](heritage/cf.md), [hs](heritage/hs.md) |
| **draco-era/** | NASA cFS Draco-era apps requiring the EDS MID shim (see [../ADDING_CFS_APPS.md](../ADDING_CFS_APPS.md)) | [cs](draco-era/cs.md), [md](draco-era/md.md), [mm](draco-era/mm.md) |
| **dtu/** | DTU custom apps + generic component apps | [mgr](dtu/mgr.md), [beacon_app](dtu/beacon_app.md), [noisy_app](dtu/noisy_app.md), [generic_gnss](dtu/generic_gnss.md), [generic_tt_c](dtu/generic_tt_c.md), [generic_eps](dtu/generic_eps.md), [generic_adcs](dtu/generic_adcs.md), [generic_radio](dtu/generic_radio.md), [generic_reaction_wheel](dtu/generic_reaction_wheel.md), [generic_imu](dtu/generic_imu.md), [generic_mag](dtu/generic_mag.md), [generic_css](dtu/generic_css.md), [generic_fss](dtu/generic_fss.md), [generic_torquer](dtu/generic_torquer.md), [generic_thruster](dtu/generic_thruster.md), [generic_star_tracker](dtu/generic_star_tracker.md), [novatel_oem615](dtu/novatel_oem615.md), [arducam](dtu/arducam.md), [blackboard](dtu/blackboard.md), [onair](dtu/onair.md), [syn](dtu/syn.md), [sample](dtu/sample.md) |

## Resource matrix (loaded apps only)

Loaded set determined by `nos3/fsw/build/exe/cpu1/cf/cfe_es_startup.scr` (`CFE_APP` lines before the `!` EOF marker).

| App | Prio | Pipe depth | CMD MID | HK TLM MID | Notes |
|---|---|---|---|---|---|
| sch | 40 | 12 | 0x1895 | 0x0897 | 100 Hz tick |
| ci | 41 | 10 | 0x18B0 | 0x08B0 | UDP cmd ingest |
| to | 42 | 10 - 32 | 0x1880 | 0x0880 | UDP tlm out |
| cf | 50 | 32 | 0x18B3 | 0x08B0 | CFDP file xfer |
| ds | 51 | 45 | 0x18BB | 0x08B8 | Data storage |
| fm | 52 | 10 | 0x188C | 0x088C | File manager |
| lc | 53 | 12 | 0x18A4 | 0x08A4 | Limit checker |
| sc | 54 | 12 | 0x18A9 | 0x08AA | Stored cmd |
| cs | 55 | 12 | 0x189F | 0x08A4 | Checksum (Draco shim) |
| md | 60 | 50 | 0x1890 | 0x0890 | Memory dwell (Draco shim) |
| mm | 61 | 12 | 0x1888 | 0x0888 | Memory manager (Draco shim) |
| sbn | 63 | 32 | n/a | n/a | Bus network |
| hs | 85 | 12 / 32 / 1 | 0x18AE | 0x08AD | Health & Safety |
| hk | 90 | 40 | 0x189B | 0x089B | Combined HK |
| ci_lab | 80 | 10 | 0x18E0 | 0x08E0 | UDP cmd lab |
| to_lab | 81 | 10 | 0x1880 | n/a | UDP tlm lab |
| generic_adcs | 60 | 10 | 0x195E | 0x095E | DTU ADCS app |
| generic_css | 62 | 10 | 0x1960 | 0x0960 | Coarse sun sensor |
| generic_eps | 63 | 10 | 0x191A | 0x091A | EPS controller |
| generic_fss | 64 | 10 | 0x1962 | 0x0962 | Fine sun sensor |
| generic_imu | 65 | 10 | 0x1942 | 0x0942 | IMU |
| generic_mag | 66 | 10 | 0x1944 | 0x0944 | Magnetometer |
| generic_radio | 67 | 10 | 0x1956 | 0x0956 | Radio |
| generic_rw | 68 | 10 | 0x1992 | 0x0992 | Reaction wheel |
| generic_torquer | 69 | 10 | 0x1948 | 0x0948 | Magnetorquer |
| novatel_oem615 | 70 | 10 | 0x1870 | 0x0870 | NovAtel GPS RX |
| sample | 71 | 10 | 0x18FA | 0x08FA | Reference app |
| generic_st | 72 | 10 | 0x196A | 0x096A | Star tracker |
| mgr | 75 | 10 | 0x19F8 | 0x09F8 | Mission manager |
| generic_tt_c | 76 | 10 | 0x1950 | 0x0950 | TT&C radio |
| generic_gnss | 77 | 10 | 0x1952 | 0x0952 | GNSS receiver |

CMD MIDs are `0x18XX` (cFS heritage) or `0x19XX` (DTU components, via `0x1900 | topic_id` macro in `nos3/fsw/cfs/cfg/.../cfe_msgid*.h`). The MID values shown above are computed; verify against `nos3/cfg/build/InOut/cpu1/components_msgid_table.txt` after `make config`.

## Apps that are *not* loaded by default

| App | Status | Why |
|---|---|---|
| beacon_app | `OFF_APP` in startup script | DTU custom; enabled only for the GS Hello-Flow loop tests |
| noisy_app | `OFF_APP` | Test fixture for ELK pipeline noise floor |
| cpu_killer | `OFF_APP` | Security test fixture; never enabled in normal build |
| arducam, blackboard, onair, syn, generic_thruster | not in active CFE_APP set | sc-research-config.xml additions; sc-mission-config.xml leaves them out |
