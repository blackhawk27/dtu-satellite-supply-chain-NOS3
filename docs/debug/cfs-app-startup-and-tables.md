# cFS app startup, load order, and definition tables

cFE parses `cfg/nos3_defs/cpu1_cfe_es_startup.scr` top to bottom: each entry
is dlopen'd and its task spawned in order. Task priorities only affect
scheduling after load, so a load-order bug cannot be fixed with priorities.
The build copy is `cfg/build/nos3_defs/cpu1_cfe_es_startup.scr`; edit the
source and re-run `make config`, or update both.

## SCH schedule table: Remainder must be strictly less than Frequency

- Symptom: `SCH 30: Schedule tbl verify error - idx[54] ena[1] typ[1] fre[4]
  rem[4] msg[23]` then `SCH 9: Error (RC=0xFFFFFFFD) Loading SDT`. SCH never
  schedules any messages, so all app telemetry (HK/HS/LC and the rest) is
  absent from Kibana.
- Root cause: `cfg/nos3_defs/tables/sch_def_schtbl.c:111` had Remainder 4
  equal to Frequency 4. SCH validation requires `Remainder < Frequency`.
- Fix: changed Remainder from 4 to 3 at line 111 (`4, 4, 23` to `4, 3, 23`).
  Slot 10 sub-entries already use Remainders 0, 1, 2, so 3 is the correct
  next offset.

## Startup load order: MD/MM must come after LC/SC

- Symptom: at about T+1.47 s, `MD_APP: Error 0xC0000003 received loading
  tbl#1`, then `CFE_ES_ExitApp: Application MD called CFE_ES_ExitApp`. OSAL
  logs `Error: LC_AppData: ./cf/*.so: undefined symbol: LC_AppData` (and the
  same for `SC_OperData`) on all loaded `.so` files, followed by
  `CFE_ES_Main: Startup Sync failed`.
- Root cause: the startup script listed `md` and `mm` before `lc` and `sc`.
  When `MD_AppMain` runs its init (loading its default dwell table and
  resolving `LC_AppData`/`SC_OperData` via `OS_SymbolLookup`), LC and SC are
  not yet dlopen'd, so MD rejects the dwell table and exits.
- Fix: move `md` and `mm` below `lc` and `sc` in
  `cfg/nos3_defs/cpu1_cfe_es_startup.scr`, and regenerate the build copy
  (`make config`, or edit `cfg/build/nos3_defs/cpu1_cfe_es_startup.scr`).
- Note: an earlier note called the `SC_OperData` dlerror benign. It is not
  benign on its own; it was a symptom of this real load-order bug, surfaced
  once SCH started loading cleanly. The dlerror artifact is described next.

## SC_OperData dlerror artifact (benign once load order is correct)

- Symptom: `OS_GenericSymbolLookup_Impl():114:Error: SC_OperData:
  ./cf/sch.so: undefined symbol: SC_OperData`.
- Root cause: a stale `dlerror()` state artifact. When SCH fails to load
  (for example the Remainder bug above), OSAL's `dlerror()` retains the last
  failed-dlopen context pointing at `sch.so`; a later
  `OS_SymbolLookup("SC_OperData")` from the MD dwell-table loader surfaces
  the stale message. `SC_OperData` is properly defined in `sc_app.c:52` and
  SC loads before MD (priority 54 vs 60).
- Fix: resolves automatically once the SCH table and the MD/MM load order
  are correct. No separate fix needed.

## HS app: iodriver missing from the PSP module list

- Symptom: `OS_ModuleLoad_Impl():107:Error loading shared library:
  ./cf/hs.so: undefined symbol: CFE_PSP_IODriver_Command`.
- Root cause: `nos3/fsw/psp/fsw/nos-linux/psp_module_list.cmake` did not
  include `iodriver`. `targets.cmake` adds iodriver to
  `MISSION_GLOBAL_APPLIST` so it compiles, but PSP modules must also be in
  the PSP module list to be initialized at startup. Building the binary is
  not sufficient; without the list entry `iodriver_Init()` is never called.
  The pc-linux (amd64-posix/unit-test) PSP already includes iodriver via its
  own `psp_module_list.cmake`.
- Fix: added `iodriver` to `nos3/fsw/psp/fsw/nos-linux/psp_module_list.cmake`.

## CS app SIGSEGV: 64-bit text-segment pointer truncated to 32 bits

This is load-bearing on amd64.

- Symptom: `sc01_nos_fsw` exits 139 (SIGSEGV) about 33 seconds after start,
  right after `RTS Number 001 Started`. dmesg shows
  `CS[...]: segfault at 85e7c000 ip 00005c1c85e7fbb6 ...` in `core-cpu1`.
  The fault address is exactly the low 32 bits of the executable mapping
  base, a smoking gun for a 64-bit pointer truncated to 32 bits.
- Root cause: `addr2line` resolves the crashing IP to `CFE_ES_CalculateCRC`
  (`cfe/modules/es/fsw/src/cfe_es_api.c:1628`, the `ByteValue = *BufPtr;`
  load in the CRC-16 loop). `CS_Res_EepromMemory_Table_Entry_t::StartAddress`
  is `CFE_ES_MemAddress_t`, typedef'd to `uint32`. `cs_init.c:254` assigns
  the 64-bit cpuaddr from `CFE_PSP_GetCFETextSegmentInfo` into that 32-bit
  field; on a PIE-loaded cFS process the cFE text segment lives at a high
  canonical address (~0x5c1c85e7c000), so the upper 32 bits are silently
  dropped. `CFE_ES_MEMADDRESS_TO_PTR` then zero-extends the truncated value
  back to 64-bit, producing a pointer into unmapped low memory; the first
  CRC byte-load page-faults. The same truncation hits the four runtime
  tables populated from cFS-level pointers (CFE text segment, OS text
  segment, app code segments, tables). Ground-loaded EEPROM/Memory tables
  are unaffected because their `StartAddress` values fit in 32 bits.
- Fix: in `nos3/fsw/apps/cs/fsw/inc/cs_internal_cfg.h`, change the four
  `DEFAULT_CS_INTERNAL_*_STATE` macros from `CS_ChecksumState_ENABLED` to
  `CS_ChecksumState_DISABLED` so the CS background loop does not walk those
  four tables on power-on. CS still loads, publishes housekeeping, and
  accepts ground commands; only the auto-checksum path that dereferences
  truncated pointers is suppressed. Each define carries a comment explaining
  the truncation. The clean fix (widening `CFE_ES_MemAddress_t` to 64-bit,
  or splitting the runtime field from the telemetry field) touches the cFS
  upstream wire format and ground tooling and is deferred to an upstream PR.
- Load-bearing: re-enabling any of the four states without first widening
  the `StartAddress` field reproduces the SIGSEGV within about 30 seconds of
  boot.

## NOS-time bogus startup tick stalls OS_TaskDelay and the 1 Hz timer

A single bad first tick from NOS Engine can freeze a task delay and an
armed timer for the rest of the run. This produced two distinct symptoms.

- Symptom A (TO_LAB stuck): after a clean boot, TO_LAB logs "Awaiting enable
  command" and is then silent. `attack_logs/cfs_god_view.json` stays 0
  bytes. `CFE_SB 17: Msg Limit Err, MsgId 0x18e8, pipe TO_LAB_CMD_PIPE`
  fires about every 30 s, TO_LAB's queue sits at its BufLimit, `downlink_on`
  never flips true, and `CommandCounter` stays 0. Other apps are fine.
- Symptom B (HK silent, after fixing only one side): TO_LAB unsticks and
  emits "TO telemetry output enabled" every 30 s, but `cfs_god_view.json`
  contains only 0x0808 EVS events; no HK MIDs ever arrive.
  `TIME_ONEHZ_TASK` shows about 10 voluntary context switches over minutes
  (check `/proc/<pid>/task/<tid>/status` in the FSW container). cFE TIME's
  1 Hz timer never fires, so SCH never wakes and no HK is requested.
- Root cause: `CFE_PSP_NosTickCallback`
  (`nos3/fsw/psp/fsw/nos-linux/src/cfe_psp_start.c`) occasionally delivers a
  bogus huge first tick (observed 4.5e10 to 5.6e10 ticks, about 14-17
  sim-years). Subsequent ticks are sane, but two places in
  `nos3/fsw/osal/src/os/nos/src/NOS-time.c` latch the bogus value and never
  recover: `NOS_clock_nanosleep` captures `end_time = req_ticks + sim_time`
  once and the inner loop `while (sim_time < end_time)` then spins forever;
  `NOS_timer_settime` writes `expire_time += sim_time` for relative arming,
  freezing the 1 Hz tick about 14 sim-years out so `NOS_timer_fire` never
  fires it.
- Fix (applied): add rebase guards at BOTH poison points. In
  `NOS_clock_nanosleep`, on each loop iteration, if
  `flags==0 && (end_time - sim_time) > max_residual` (with
  `max_residual = req_ticks * 1000`, or `ticks_per_second * 10` for
  zero-tick requests), rebase `end_time = sim_time + req_ticks` and continue
  the loop normally (do not break, which would return the delay early and
  starve apps). In `NOS_timer_fire`, for each armed entry compute
  `interval_ticks` from `it_interval` and `max_future = interval_ticks *
  1000`; if `expire_time > time + max_future`, rebase
  `expire_time = time + interval_ticks` and run the normal
  `expire_time <= time` check in the same iteration so a stuck timer fires
  immediately on the next tick. A remaining root-cause cleanup is to have
  `CFE_PSP_NosTickCallback` reject ticks that sit more than N seconds ahead
  of the previous tick or wall-clock.
- Verification: with both guards in place, `TIME_ONEHZ_TASK` voluntary
  context switches climb about once per second, SCH fires 1 Hz HK requests,
  `cfs_god_view.json` grows with all expected heritage HK MIDs, and
  `omni_logs/tlm_hk_decoded.log` populates with numeric HK fields per app.
- Live workaround (recovers a running sim without rebuild): attaching strace
  to TO_LAB's host thread for even one second produces a ptrace-driven
  `EINTR` on the inner `clock_nanosleep`, which propagates up to
  `OS_TaskDelay_Impl`'s `do ... while (status == EINTR)`, re-enters
  `NOS_clock_nanosleep`, and recomputes `end_time` from the now-sane time.
