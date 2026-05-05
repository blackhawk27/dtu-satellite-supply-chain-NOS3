# mm (Memory Manager) - Draco-era

- **Source:** `nos3/fsw/apps/mm/`
- **Loaded by:** `cfe_es_startup.scr` line 24 (priority 61, stack 16384)
- **Pipe depth:** 12 (typical Draco default; check `mm_internal_cfg.h`)
- **MIDs (shim):** CMD `0x1888`, HK TLM `0x0888`
- **Tables:** none beyond HK; commanded operations carry their own data

## Draco-era integration note

Same shim pattern as CS / MD; see [../../ADDING_CFS_APPS.md](../../ADDING_CFS_APPS.md).

## Role

Provides peek/poke and load/dump primitives for arbitrary memory and EEPROM regions on commanded request from the ground. Loads are byte/word/dword/file-based; dumps are file-based.

## Silent-failure modes

- A poke into a write-protected region reports an error event but the operation is otherwise silent.
- Address-bounds checks are minimal; an attacker who can set `MM_MAX_LOAD_FILE_DATA_DWORD`/equivalent in tables widens the addressable range.

## Attack-surface notes

- **Highest-risk single primitive on the spacecraft.** With auth-bypassed `mm` access, an attacker has arbitrary read/write to the FSW process memory: hot-patch any function pointer, plant a backdoor, neutralise CS / HS / LC.
- The CS app is the primary defence; see [cs.md](cs.md) for how it can be silenced first.
