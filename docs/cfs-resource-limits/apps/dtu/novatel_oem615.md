# novatel_oem615 - DTU custom

- **Source:** `nos3/components/novatel_oem615/fsw/`, `nos3/components/novatel_oem615/sim/`
- **Loaded by:** `cfe_es_startup.scr` line 37 (priority 70, stack 32768)
- **Pipe depth:** 10
- **MIDs:** CMD `0x1870`, HK TLM `0x0870`

## Role

Real-world NovAtel OEM615 GPS receiver model. Provides the GPS chain that the [generic_gnss.md](generic_gnss.md) FSW component consumes; the sim layer translates 42's position-vector socket into NovAtel's UART output format.

`nos3/components/novatel_oem615/fsw/shared/novatel_oem615_device.c` is currently dirty in the working tree (parser tweaks).

## Silent-failure modes

- Device-side parsing is what the bracket-strip protects against on the sim side. If the sim feeds malformed records, the FSW publishes zero lat/lon and HK still ticks.

## Attack-surface notes

- See [generic_gnss.md](generic_gnss.md) for the position-spoofing attack surface; this app is the heritage receiver model that paired with generic_gnss closes the position chain.
