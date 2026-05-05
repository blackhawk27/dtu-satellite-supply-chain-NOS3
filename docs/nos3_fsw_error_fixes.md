# NOS3 FSW Error Fixes

> **Canonical mission description:** [`missions/NOS3-DTU-EO1-mission-implementation.md`](missions/NOS3-DTU-EO1-mission-implementation.md). This file is a runtime-error troubleshooting catalogue, not an architecture description.

Catalog of concrete FSW-runtime and toolchain issues encountered while running the DTU NOS3 simulation, each with the observable symptom, root cause, and applied fix. Use this as a first-stop checklist when "telemetry looks wrong" or "apps won't start."

---

## 1. `Msg Limit Err, MsgId 0x18e8, pipe TO_LAB_CMD_PIPE` flood on TO_LAB

### Symptom

`nos3/omni_logs/cfs_evs.log` repeats every 2-22 s (and eventually saturates CFE_EVS's filter quota):

```
EVS Port1 1970-001-00:00:37.349 42/1/CFE_SB 17: Msg Limit Err,MsgId 0x18e8,pipe TO_LAB_CMD_PIPE,sender CI_LAB_APP
```

Downstream effects:

- `nos3/attack_logs/cfs_god_view.json` stays 0 bytes - no UDP telemetry is ever received on port 5013.
- `nos3/omni_logs/tlm_hk_decoded.log` stays 0 bytes.
- Kibana shows no HK/HS documents for the run.

### Root cause

Every `make launch` spawns `scripts/god_view_capture.py` in the background. `make stop` calls `docker stop` on the containers but never `pkill`s the host-side Python capture scripts. After two or three run cycles there are 2-3 concurrent god_view instances, each sending `TO_LAB_OUTPUT_ENABLE_CC` (MID 0x18e8) to CI_LAB every 30 s. `TO_LAB_CMD_PIPE` is created with a modest MsgLimit, so the concurrent enable-command burst at startup repeatedly overflows the pipe. CFE_SB emits the `Msg Limit Err` event each time. Because TO_LAB keeps dropping the flood, the enable command is intermittently lost and TO_LAB may never latch a valid destination IP - hence the empty `cfs_god_view.json`.

Confirmation:

```bash
ps aux | grep god_view_capture.py | grep -v grep | wc -l   # frequently 2-3
ss -ulnp | grep 5013                                       # multiple PIDs bound via SO_REUSEADDR
```

### Fix

Make `nos3/Makefile` clean up its own background scripts in both `stop` and `launch`:

```makefile
stop:
    @pkill -f god_view_capture.py 2>/dev/null || true
    @pkill -f cfs_evs_capture.sh  2>/dev/null || true
    @pkill -f sim_logs_capture.sh 2>/dev/null || true
    @pkill -f cpu_monitor.sh      2>/dev/null || true
    ./scripts/stop.sh
    ...

launch:
    @pkill -f god_view_capture.py 2>/dev/null || true
    @pkill -f cfs_evs_capture.sh  2>/dev/null || true
    @pkill -f sim_logs_capture.sh 2>/dev/null || true
    @pkill -f cpu_monitor.sh      2>/dev/null || true
    ./scripts/ci_launch.sh
    ...
```

`god_view_capture.py` already holds an `fcntl` singleton lock on `/tmp/nos3_god_view.lock`, but the lock is no help against instances spawned before that mechanism was introduced. The Makefile pkill is the belt to the lock's suspenders.

Follow-up ergonomics fix: in `nos3/scripts/god_view_capture.py`, add `flush=True` to the three startup `print(...)` calls - otherwise Python block-buffers stdout when it's redirected to a file and `god_view.log` looks empty for several minutes during debugging.

---

## 2. `Msg Limit Err, MsgId 0x80d, pipe TO_TLM_PIPE_0, sender CFE_SB` flood on TO app

### Symptom

`nos3/omni_logs/cfs_evs.log` contains, typically during the first ~30 s of every run:

```
EVS Port1 1970-001-00:11:09.599 42/1/CFE_SB 17: Msg Limit Err,MsgId 0x80d,pipe TO_TLM_PIPE_0,sender CFE_SB
```

The pipe name (`TO_TLM_PIPE_0`) tells you this is the `to` app - the generic TO with configurable routes - not the simpler `to_lab` app. It frequently coexists with Issue 1 in a fresh log.

### Root cause

MID `0x080D` is `CFE_SB_ALLSUBS_TLM_MID` - the "all subscriptions report" message that CFE_SB publishes when anyone issues `CFE_SB_SEND_ALL_SUBSCRIPTIONS_CC`, and which SBN also republishes on every subscription event. In `nos3/cfg/nos3_defs/tables/to_config.c` the TO app subscribes to this MID with MsgLimit=1:

```c
{CFE_SB_MSGID_WRAP_VALUE(CFE_SB_ALLSUBS_TLM_MID), {0,0}, 1, 0xffff, TO_GROUP_CFE | TO_MGROUP_ONE, 0,1},
...
{CFE_SB_MSGID_WRAP_VALUE(CFE_SB_ONESUB_TLM_MID),  {0,0}, 1, 0xffff, TO_GROUP_CFE | TO_MGROUP_ONE, 0,1},
```

During startup every cFS app does dozens of `CFE_SB_Subscribe(...)` calls in rapid succession. SBN is loaded (line for `sbn` in `cpu1_cfe_es_startup.scr`) and publishes ALLSUBS reports for those events. TO's pipe has one-slot capacity for 0x080D - the second concurrent report overflows and CFE_SB emits the `Msg Limit Err` event. Once the startup burst clears, the error naturally subsides, but it's noisy in the log and masks real problems.

### Fix

Raise the MsgLimit for `CFE_SB_ALLSUBS_TLM_MID` and `CFE_SB_ONESUB_TLM_MID` in `nos3/cfg/nos3_defs/tables/to_config.c`:

```c
{CFE_SB_MSGID_WRAP_VALUE(CFE_SB_ALLSUBS_TLM_MID), {0,0}, 64, 0xffff, TO_GROUP_CFE | TO_MGROUP_ONE, 0,1},
...
{CFE_SB_MSGID_WRAP_VALUE(CFE_SB_ONESUB_TLM_MID),  {0,0}, 64, 0xffff, TO_GROUP_CFE | TO_MGROUP_ONE, 0,1},
```

64 comfortably absorbs the startup burst for the typical ~50-app NOS3 bring-up and is in line with what the component HK subscriptions already use (32-64). Requires a full FSW rebuild to regenerate `to_config.tbl`.

---

## 3. `CFE_ES_Main: Startup Sync failed` + MD `CFE_ES_ExitApp` at T+1.5 s

### Symptom

Shortly after startup in `nos3/omni_logs/cfs_evs.log`:

```
MD_APP: Error 0xC0000003 received loading tbl#1
CFE_ES_ExitApp: Application MD called CFE_ES_ExitApp
OS_GenericSymbolLookup_Impl(): Error: LC_AppData: ./cf/*.so: undefined symbol: LC_AppData
OS_GenericSymbolLookup_Impl(): Error: SC_OperData: ./cf/*.so: undefined symbol: SC_OperData
CFE_ES_Main: Startup Sync failed - Applications may not have all started
```

Once MD exits, every downstream dependency (stored command sequences, dwell monitors) degrades silently.

### Root cause

`nos3/cfg/nos3_defs/cpu1_cfe_es_startup.scr` lists `md` and `mm` **before** `lc` and `sc`. cFE parses the startup script top-to-bottom; each line dlopens the app and kicks its task. `MD_AppMain`'s init loads its default dwell table, which calls `OS_SymbolLookup("LC_AppData")` and `OS_SymbolLookup("SC_OperData")`. At that moment `lc.so` and `sc.so` have not been dlopened yet, so both lookups fail and MD bails out with `CFE_ES_ExitApp`. Task priority is only consulted **after** load - it cannot rescue a dlopen-order bug.

An earlier 2026-04-09 note that classified this as "benign dlerror residue" was wrong; it is a real load-order bug that surfaces once SCH starts loading cleanly.

### Fix

In `nos3/cfg/nos3_defs/cpu1_cfe_es_startup.scr`, move `md` and `mm` **below** `lc` and `sc`:

```
CFE_APP, cs,   CS_AppMain,   CS,   55, ...
CFE_APP, cf,   CF_AppMain,   CF,   50, ...
CFE_APP, ds,   DS_AppMain,   DS,   51, ...
CFE_APP, fm,   FM_AppMain,   FM,   52, ...
CFE_APP, lc,   LC_AppMain,   LC,   53, ...
CFE_APP, sc,   SC_AppMain,   SC,   54, ...
CFE_APP, md,   MD_AppMain,   MD,   60, ...
CFE_APP, mm,   MM_AppMain,   MM,   61, ...
CFE_APP, sbn,  SBN_AppMain,  SBN,  63, ...
```

`make config` regenerates `cfg/build/nos3_defs/cpu1_cfe_es_startup.scr` from the source file. Verify with:

```bash
grep -n "^CFE_APP, md\|^CFE_APP, mm\|^CFE_APP, lc\|^CFE_APP, sc" \
  nos3/cfg/build/nos3_defs/cpu1_cfe_es_startup.scr
# Expect lc and sc lines to have lower line numbers than md and mm
```

---

## 4. NOS Engine bogus startup tick poisons both `NOS_clock_nanosleep` and `NOS_timer_settime` - TO_LAB stuck AND HK never flows

This is the most important fix in this document. Two distinct places in `nos3/fsw/osal/src/os/nos/src/NOS-time.c` capture `CFE_PSP_sim_time` and write it into a local state that never gets corrected when the sim clock rebases. The bogus-tick only poisons the value for a brief window at startup, but whichever call happens inside that window produces a permanent stall.

### Symptom pattern A - TO_LAB stuck, `cfs_god_view.json` stays 0 bytes

After a clean boot, `cfs_evs.log` shows `TO Lab Initialized, Awaiting enable command` and then nothing from TO_LAB ever again. Other apps (SCH, HS, CS, SBN) are fine. `attack_logs/cfs_god_view.json` stays 0 bytes. TO_LAB's `Tlm_pipe` gradually backs up to 25/50, `downlink_on` never flips true, `HkTlm.Payload.CommandCounter` stays 0. A gdb attach on TO_LAB's thread shows it stuck inside `NOS_clock_nanosleep`'s `while (sim_time < end_time)` loop, with `end_time` a value consistent with ~14 years of NOS Engine ticks.

### Symptom pattern B - TO_LAB works, `cfs_god_view.json` grows, but only 0x0808 (EVS event) packets; no HK telemetry ever

This is the state you land in once you fix pattern A but miss the same bug in the timer path. You'll see:

- `TO telemetry output enabled for IP 172.19.0.1` in EVS every 30 s (god_view's enable retry is landing).
- `cfs_god_view.json` grows but contains only MID `0x0808` (EVS events forwarded by TO_LAB's subscription to them).
- No `0x0800` / `0x0803` / `0x0805` / `0x091A` / `0x08AD` / any HK TLM MID.
- Inside the FSW container, `TIME_ONEHZ_TASK` has only ~10 voluntary context switches over minutes of runtime (should be one per second → hundreds), and SCH has very few wakeups. cFE TIME's 1 Hz tick never fires, so SCH never wakes, so no HK requests ever go out, so no app ever responds with HK telemetry.

### Root cause

Both symptoms trace to the same NOS Engine startup anomaly: `CFE_PSP_NosTickCallback` occasionally delivers a bogus huge first tick (observed values ~4.5e10 to 5.6e10 ticks ≈ 14-17 sim-years). Whatever OSAL state captures `CFE_PSP_sim_time` during that window freezes to a far-future value.

**Path 1 - `NOS_clock_nanosleep`:** Captures `end_time = req_ticks + sim_time` once on entry. If caught at a bogus tick, `end_time` is frozen 14 sim-years out. Subsequent ticks restore sim_time to sane values, but the stack-local `end_time` is stuck, so the poll loop `while (sim_time < end_time)` spins forever. Any task doing `OS_TaskDelay` inside its main loop (TO_LAB does) stays blocked forever.

**Path 2 - `NOS_timer_settime`:** Captures `expire_time += sim_time` when `flags == 0` (relative arming). If caught at a bogus tick, `expire_time` is also frozen 14 sim-years out. `NOS_timer_fire(time)` then never sees `expire_time <= time` for that entry and the timer never fires. This is what happens to the `TIME_ONEHZ_TASK` 1 Hz timer cFE TIME installs early in boot - which cascades into SCH, HK, and every app that depends on scheduled telemetry.

### Fix (both paths)

Edit `nos3/fsw/osal/src/os/nos/src/NOS-time.c`.

**Path 1 - `NOS_clock_nanosleep`:** Detect an implausibly large residual on each iteration and rebase `end_time` from the current sim_time. Continue the loop normally (not `break`) so the task actually sleeps for the requested duration:

```c
NE_SimTime req_ticks = req->tv_sec * CFE_PSP_ticks_per_second
                     + req->tv_nsec * CFE_PSP_ticks_per_second / NOS_NANO;
NE_SimTime end_time = req_ticks;
if (flags == 0) end_time += sim_time;

/* Sanity clamp: NOS Engine can deliver a bogus huge startup tick. If
 * end_time was captured then, the loop spins for ~14 sim-years. When
 * the residual exceeds 1000x the requested delay (min 10s of ticks),
 * assume end_time is stale and rebase to current sim_time. Only applies
 * to relative mode (flags==0); absolute mode trusts the caller. */
NE_SimTime max_residual = req_ticks > 0 ? req_ticks * 1000 : CFE_PSP_ticks_per_second * 10;
while ((sim_time < end_time) && !interrupt) {
    if (flags == 0 && (end_time - sim_time) > max_residual) {
        end_time = sim_time + req_ticks;  /* rebase, continue loop normally */
    }
    interrupt = clock_nanosleep(clock_id, flags, &delay, &real_rem);
    ...update sim_time...
}
```

Note: an earlier version of this fix used `break` inside the clamp, which caused tasks to return early without actually sleeping. That works for TO_LAB (which only cares that the loop terminates) but starves other subsystems of delay semantics. Use `rebase + continue` so each task genuinely sleeps for its requested duration.

**Path 2 - `NOS_timer_fire`:** Check each armed timer's `expire_time` against the tick time; if it's implausibly far in the future, rebase by one interval:

```c
for (int i = 0; i < OS_MAX_TIMEBASES; i++) {
    if (NOS_timer_table[i].in_use && NOS_timer_table[i].armed) {
        /* Sanity clamp: if a bogus NOS Engine startup tick poisoned this
         * timer's expire_time (observed ~14 sim-years out), it will never
         * fire. When expire_time is absurdly far beyond current time
         * (>1000x the interval, min 10s), rebase to the next interval
         * relative to current sim_time. Fixes cFE TIME 1Hz tick stall. */
        NE_SimTime interval_ticks =
            NOS_timer_table[i].it_interval.tv_sec * CFE_PSP_ticks_per_second +
            NOS_timer_table[i].it_interval.tv_nsec * CFE_PSP_ticks_per_second / NOS_NANO;
        NE_SimTime max_future = interval_ticks > 0 ? interval_ticks * 1000
                                                   : CFE_PSP_ticks_per_second * 10;
        if (NOS_timer_table[i].expire_time > time + max_future) {
            NOS_timer_table[i].expire_time = time + (interval_ticks > 0 ? interval_ticks : 0);
        }
        if (NOS_timer_table[i].expire_time <= time) {
            ...original fire logic...
        }
    }
}
```

This is the critical fix - without it, every downstream HK/LC/SC/HS schedule-driven telemetry stays silent.

Root-cause side: `CFE_PSP_NosTickCallback` in `fsw/psp/fsw/nos-linux/src/cfe_psp_start.c` should validate incoming ticks from NOS Engine and reject obvious garbage. Left for a separate change once we confirm the two OSAL rebase guards above resolve the stall (they do - verified with a full fresh boot showing cfs_god_view.json growing through 0x0800 / 0x0803 / 0x0805 / 0x091A / 0x08AD / 0x08AA / 0x08A7 / 0x089B HK MIDs at ~1 Hz each, and the decoded HK log populating with real numeric fields).

This turns a 14-year hang into at most 1000 × requested sleep (usually ≤ 100 s for OS_TaskDelay(100 ms)). The lossy edge case - one taskdelay returns slightly early if it genuinely caught a bogus tick - is a trade we're happy to make because the alternative is an app that never runs.

Root-cause side: `CFE_PSP_NosTickCallback` should validate incoming ticks from NOS Engine and reject obvious garbage. Left for a separate change once we confirm the fix above resolves the stall.

### Live workaround for Path 1 only (no rebuild)

Attaching strace to TO_LAB's host thread for one second forces `EINTR` into the inner `clock_nanosleep`, which returns up to `OS_TaskDelay_Impl`'s `do ... while (status == EINTR)` loop - which re-enters `NOS_clock_nanosleep` and recomputes `end_time` from the now-sane `sim_time`:

```bash
# Find TO_LAB's host TID (requires CAP_SYS_PTRACE or sudo)
sudo gdb -batch -p $(pgrep -f core-cpu1) -ex 'thread find TO_LAB_APP' -ex 'info threads'
# Nudge it awake
sudo strace -p <TID> -e trace=clock_nanosleep &
STRACE_PID=$!
sleep 1
kill $STRACE_PID
```

Useful if you need to recover a running sim without rebuilding FSW. Not a replacement for the source fix.

---

## 5. FSW build fails with `CFE_PLATFORM_CMD_TOPICID_TO_MIDV` redefined

### Symptom

`make fsw` (or any build that pulls in a component `*_msgid_values.h`) fails with `-Werror`:

```
error: "CFE_PLATFORM_CMD_TOPICID_TO_MIDV" redefined
  cfg/build/nos3_defs/cfe_msgid_api.h:22
note: this is the location of the previous definition
  fsw/cfe/modules/core_api/config/default_cfe_core_api_msgid_mapping.h:49
```

### Root cause

`cfg/nos3_defs/cfe_msgid_api.h` includes `cfe_core_api_base_msgids.h` (which already defines the two `..._TOPICID_TO_MIDV(topic)` macros) and then redefines them. The values are numerically equivalent - both resolve to `0x1800 | topic` and `0x0800 | topic` respectively - but the preprocessor still flags the redefinition and `arch_build_custom.cmake`'s `-Werror` converts it to fatal.

Detail in `docs/fsw-build-errors-msgid-redefinition.md`.

### Fix

Guard both definitions in `cfg/nos3_defs/cfe_msgid_api.h`:

```c
#ifndef CFE_PLATFORM_CMD_TOPICID_TO_MIDV
#define CFE_PLATFORM_CMD_TOPICID_TO_MIDV(topic)  (0x1800 | (topic))
#endif
#ifndef CFE_PLATFORM_TLM_TOPICID_TO_MIDV
#define CFE_PLATFORM_TLM_TOPICID_TO_MIDV(topic)  (0x0800 | (topic))
#endif
```

Safe because whichever definition wins produces the same MID values.

---

## 6. `Msg Limit Err, MsgId 0x18f9, pipe MGR_CMD_PIPE, sender SCH` (MGR init stall)

### Symptom

`nos3/omni_logs/cfs_evs.log` logs this once per sim-second, starting at about T+8 s and never clearing for the rest of the run:

```
EVS Port1 1970-001-00:00:08.37998 42/1/CFE_SB 17: Msg Limit Err,MsgId 0x18f9,pipe MGR_CMD_PIPE,sender SCH
```

Other MGR symptoms confirm the app is dead:

- Only two MGR lines ever appear in `cfs_evs.log`: the `Loading file: /cf/mgr.so` line and `MGR: Restore Hk Packet error: Anomalous reboot, returning to SAFE MODE`.
- No `MGR App Initialized` EVS event is ever emitted (`mgr_app.c:168`).
- No `MGR: Calling OS_SetLocalTime with ticks: ...` print is ever emitted (end of `MGR_RestoreHkFile`).
- `grep -c '"msg_id": *"0x08F8"' attack_logs/cfs_god_view.json` is 0: MGR never publishes a single HK packet.

### Root cause

Two stacked problems:

1. `MGR_RestoreHkFile()` at `mgr_app.c:534-590` calls `OS_GetLocalTime` / `OS_SetLocalTime` unconditionally. During the startup window, NOS Engine can deliver a bogus tick (see fix #4), so `OS_GetLocalTime` returns a huge value. When MGR then calls `OS_SetLocalTime` with that huge value plus the persisted `TimeTics` (which is already suspect on an anomalous reboot), OSAL time gets poisoned and MGR's init task appears to never return from the call. `MGR_AppInit` never reaches `CFE_EVS_SendEvent(MGR_STARTUP_INF_EID, ...)` and the main loop never starts.
2. MGR subscribes to both `MGR_CMD_MID` and `MGR_REQ_HK_MID` via plain `CFE_SB_Subscribe` at `mgr_app.c:124` and `:135`. That defaults the per-MID MsgLimit to `CFE_PLATFORM_SB_DEFAULT_MSG_LIMIT = 4` even though the pipe depth is `MGR_PIPE_DEPTH = 32`. With MGR's main loop dead, SCH's 1 Hz HK request (`sch_def_schtbl.c:275` + `sch_def_msgtbl.c:119`) fills the 4 per-MID slots in under 4 s, and every subsequent request at 1 Hz trips the `Msg Limit Err`.

### Fix

Two edits in `nos3/components/mgr/fsw/cfs/src/mgr_app.c`.

**a) Skip the OS clock round-trip when nothing would be added.** On the anomalous-reboot path the persisted file is already untrusted; folding its `TimeTics` into OS time is unsafe. Zero `TimeTics` on that branch and then guard the time-setting block itself, so when both the offset and `TimeTics` are 0 the whole `OS_GetLocalTime` / `OS_SetLocalTime` call is skipped:

```c
if ((MGR_AppData.HkTelemetryPkt.SpacecraftMode != MGR_SAFE_REBOOT_MODE) &&
    (MGR_AppData.HkTelemetryPkt.SpacecraftMode != MGR_SCIENCE_REBOOT_MODE))
{
    MGR_AppData.HkTelemetryPkt.SpacecraftMode = MGR_SAFE_REBOOT_MODE;
    MGR_AppData.HkTelemetryPkt.AnomRebootCtr++;
    /* Persistence file is untrusted here; a corrupt TimeTics would be folded into
     * OS_SetLocalTime below and can stall MGR_AppInit. */
    MGR_AppData.HkTelemetryPkt.TimeTics = 0;
    OS_printf("MGR: Restore Hk Packet error: Anomalous reboot, returning to SAFE MODE\n");
}
else
{
    MGR_AppData.HkTelemetryPkt.BootCounter++;
}

/* Skip the OS clock round-trip when there's no delta to add. A stray NOS Engine
 * bogus startup tick read via OS_GetLocalTime here can otherwise poison OS time
 * and stall MGR_AppInit (see §4 above for the tick anomaly). */
if (MGR_CFG_REBOOT_TIME_TIC_OFFSET != 0 || MGR_AppData.HkTelemetryPkt.TimeTics != 0)
{
    OS_GetLocalTime(&temp_time);
    temp_time.ticks = temp_time.ticks + MGR_CFG_REBOOT_TIME_TIC_OFFSET + MGR_AppData.HkTelemetryPkt.TimeTics;
    OS_SetLocalTime(&temp_time);
}
OS_printf("MGR: Calling OS_SetLocalTime with ticks: %ld\n", MGR_AppData.HkTelemetryPkt.TimeTics);
```

**b) Raise the per-MID subscribe limit to match the pipe depth.** In `MGR_AppInit` at `mgr_app.c:124` and `:135`, replace `CFE_SB_Subscribe` with `CFE_SB_SubscribeEx`:

```c
status = CFE_SB_SubscribeEx(CFE_SB_ValueToMsgId(MGR_CMD_MID), MGR_AppData.CmdPipe,
                            CFE_SB_DEFAULT_QOS, MGR_PIPE_DEPTH);
...
status = CFE_SB_SubscribeEx(CFE_SB_ValueToMsgId(MGR_REQ_HK_MID), MGR_AppData.CmdPipe,
                            CFE_SB_DEFAULT_QOS, MGR_PIPE_DEPTH);
```

With MsgLimit raised from 4 to 32 (`MGR_PIPE_DEPTH`), a future transient stall along the same path buffers up to 32 requests before EVS emits a single overflow event, instead of producing one per second for the rest of the run.

### Verification

After `make fsw` and relaunch, after ~30 s of sim time:

```bash
grep -c "Msg Limit Err,MsgId 0x18f9" omni_logs/cfs_evs.log   # 0
grep   "MGR App Initialized"         omni_logs/cfs_evs.log   # >= 1 line
grep -c "MGR: TimeTics"              omni_logs/cfs_evs.log   # growing (one per HK cycle)
grep -oE '"msg_id": *"0x08F8"' attack_logs/cfs_god_view.json | wc -l   # growing
```

Confirmed end-to-end on the fix: 0 overflow errors, `MGR App Initialized` present at T+1.64 s, 76 HK packets on MID `0x08F8` within the first 90 s, and `MGR: TimeTics = ...` prints at 1 Hz as MGR's HK loop runs.

Note the TimeTics value reported by MGR HK will still be large early in a boot that caught a bogus NOS Engine tick (this fix does not repair `OS_GetLocalTime` itself, it just stops MGR from feeding the bogus value back into `OS_SetLocalTime`). That is a read-only symptom and does not cascade once the write-back is avoided.

---

## 7. `Invalid AP sample msg length, ID = 0x000018A6, Len = 8, Expected = 16` (LC sample-AP truncation by SCH)

### Symptom

`nos3/omni_logs/cfs_evs.log` logs this once per sim-second, from the first LC wakeup onward and never clearing:

```
EVS Port1 1970-001-00:00:01.12345 LC 42/4: Invalid AP sample msg length: ID = 0x000018A6, CC = 0, Len = 8, Expected = 16
```

LC is up (loaded, subscribed) but cannot actually evaluate actionpoints:

- Zero `LC_APSAMPLE_INF_EID` (`1003`) events in `cfs_evs.log`: LC never runs a successful sample.
- `LC_HkPacket.CurrentLCState` and AP result counters never advance.
- Any downstream logic that keys on LC actionpoint transitions (e.g. science-overfly tagging in `logstash.conf`) never fires.

### Root cause

SCH's default message-definition table at `nos3/cfg/nos3_defs/tables/sch_def_msgtbl.c:102` packs only the CCSDS command header for the LC wakeup entry:

```c
{{CFE_MAKE_BIG16(LC_SAMPLE_AP_MID), CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}}, /* 20 LC Wakeup */
```

That is 4 uint16 = 8 bytes, with the CCSDS length field set to `0x0001` (encoding `total_bytes - 7 = 1`). `SCH_ProcessNextEntry` copies exactly those 8 bytes onto the SB each time schedule slot 20 fires.

LC's dispatcher requires the full 16-byte `LC_SampleAPCmd_t` (primary+secondary header plus the 8-byte `LC_SampleAP_Payload_t` of `StartIndex`, `EndIndex`, `UpdateAge`, `Padding`). See:

- `nos3/fsw/apps/lc/config/default_lc_msgstruct.h:206-225` for the struct definition.
- `nos3/fsw/apps/lc/fsw/src/lc_dispatch.c:112-123` sets `ExpectedLength = sizeof(LC_SampleAPCmd_t)` (= 16) and calls `LC_VerifyMsgLength`.
- `nos3/fsw/apps/lc/fsw/src/lc_dispatch.c:79-88` fires `LC_APSAMPLE_LEN_ERR_EID` (= `42`, per `lc_eventids.h:586`) when the received length differs from 16.

The other stock SCH entries (HK requests, SC wakeup, MD wakeup, HS wakeup) legitimately ride on 8-byte messages because their dispatchers only need a `CFE_MSG_CommandHeader_t`. LC is the only stock consumer of SCH's wakeup stream that carries a payload.

### Fix

Edit `nos3/cfg/nos3_defs/tables/sch_def_msgtbl.c:102` so the LC wakeup packs a full 16-byte `LC_SampleAPCmd_t`. The CCSDS primary-header length field follows `size_bytes - 7`, so for a 16-byte packet it is `9` (= `0x0009`). `SCH_MAX_MSG_WORDS = 64` (`nos3/fsw/apps/sch/fsw/platform_inc/sch_platform_cfg.h:113`), so 8 uint16 fits easily.

Before:

```c
{{CFE_MAKE_BIG16(LC_SAMPLE_AP_MID), CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0001), 0x0000}}, /* 20 LC Wakeup */
```

After:

```c
/* 20 LC Wakeup: full 16-byte LC_SampleAPCmd_t so LC's length-verify accepts it.
   CCSDS length field = packet_bytes - 7 = 9. Payload: Start=0, End=0xFFFF, UpdateAge=1, Pad=0. */
{{CFE_MAKE_BIG16(LC_SAMPLE_AP_MID), CFE_MAKE_BIG16(0xC000), CFE_MAKE_BIG16(0x0009), CFE_MAKE_BIG16(0x0000),
  CFE_MAKE_BIG16(0x0000), CFE_MAKE_BIG16(0xFFFF), CFE_MAKE_BIG16(0x0001), CFE_MAKE_BIG16(0x0000)}},
```

Payload choices (`StartIndex = 0`, `EndIndex = 0xFFFF`, `UpdateAge = 1`, `Padding = 0`) match the semantics of a periodic "sample every actionpoint, update result age" wakeup: LC clamps `StartIndex`/`EndIndex` to the configured AP range in `LC_SampleAPs`, so `0xFFFF` safely resolves to `LC_MAX_ACTIONPOINTS - 1`.

### Verification

After `make config && make fsw && make launch`, at ~30 s of sim time:

```bash
grep -c "LC_APSAMPLE_LEN_ERR"  omni_logs/cfs_evs.log   # 0 (or 1 from a boot-time race)
grep -c " LC 42/4:"            omni_logs/cfs_evs.log   # 0
grep -c " LC 1003/3:"          omni_logs/cfs_evs.log   # growing (one per sampled AP transition)
```

In Kibana, `evs_app_name:LC AND evs_event_id:1003` should return documents; `lc_actionpoint_fired` tags start appearing as AP state transitions are detected.

---

## 8. `DS 15: FILE CREATE error: result = -1, dest = 0, name = '/data/evs/...'` (DS cold-boot race on destination dir)

### Symptom

`nos3/omni_logs/cfs_evs.log` logs one (occasionally a handful) of these shortly after a cold boot:

```
EVS Port1 1970-001-00:11:04.09999 42/1/DS 15: FILE CREATE error: result = -1, dest = 0, name = '/data/evs/evs1970001001104.ds'
```

EID 15 is `DS_CREATE_FILE_ERR_EID` (`fsw/apps/ds/fsw/src/ds_file.c:502`). `result = -1` maps to OSAL reporting the underlying directory does not exist when `OS_creat` is called. After this error, DS silently drops packets destined for that file index (`dest = 0` is event packets) until a later retry succeeds.

### Root cause

DS destination 0 is configured with `Pathname = "/data/evs"` in `nos3/cfg/nos3_defs/tables/ds_file_tbl.c:66-80`. OSAL maps that to `./data/evs` relative to the FSW container's working directory via `OS_FileSysAddFixedMap("./data", "/data")` in PSP startup (`nos3/fsw/psp/fsw/nos-linux/src/cfe_psp_start.c`).

`nos3/scripts/ci_launch.sh:47` runs `sudo mkdir -p $FSW_DIR/data/{cam,evs,hk,inst}` on the host before `docker run` for `sc01-nos-fsw`. On a warm boot this is fine - the directories are already present from a previous run. On a **cold** boot (empty `fsw/build/exe/cpu1/data/`) the host-side `sudo mkdir` and the FSW container's first DS file create can race: if `core-cpu1` starts fast enough that `DS_AppMain` runs before the mkdir settles in the bind-mounted tree, the first `OS_creat` returns -1.

The error is self-healing on subsequent retries - once the directory is visible to the container, DS resumes writing. But the single error still appears in Kibana and masks real DS problems.

### Fix

Add one line to `nos3/scripts/fsw/fsw_respawn.sh` immediately before the respawn loop, so the DS destination directories are unconditionally present inside the container just before `core-cpu1` is invoked:

```bash
# DS destination directories must exist before core-cpu1 opens files on
# OSAL /data/<dest>.  ci_launch.sh already creates these on the host, but
# this in-container mkdir closes the cold-boot race where DS tries to
# create its first file before the host-side sudo mkdir has landed.
mkdir -p ./data/cam ./data/evs ./data/hk ./data/inst
```

Relative path is correct: `fsw_respawn.sh` runs inside `sc01-nos-fsw` with CWD = `$FSW_DIR`, and PSP maps `./data` to OSAL `/data`. The FSW container runs as root (`docker exec sc01-nos-fsw id` reports `uid=0`), so no `sudo` is needed.

This change is belt-and-suspenders: `ci_launch.sh` still creates the directories on the host for the initial bind mount, but `fsw_respawn.sh` also creates them on every FSW respawn, including mid-sim restarts after crashes where the host-side mkdir is not re-run.

### Verification

After applying the edit and relaunching:

```bash
cd nos3
rm -rf fsw/build/exe/cpu1/data/evs     # simulate cold boot
make launch                             # triggers ci_launch.sh which triggers fsw_respawn.sh
sleep 30
grep -c "FILE CREATE error" omni_logs/cfs_evs.log        # 0
ls -la fsw/build/exe/cpu1/data/evs/                      # evs*.ds files present
```

In Kibana: `evs_raw : *FILE CREATE error*` returns 0 documents across the run.

---

## Quick verification matrix

After applying fixes and rebuilding FSW:

```bash
# 1. Kill any stale host-side capture scripts (belt for the Makefile's suspenders)
pkill -f god_view_capture.py 2>/dev/null; pkill -f cfs_evs_capture.sh 2>/dev/null

# 2. Rebuild and relaunch
cd nos3 && make config && make fsw && make launch

# 3. Verify, ~30 s after launch
ps aux | grep -c god_view_capture.py              # exactly 2 (process + grep)
grep "TO Lab Output Enabled"   omni_logs/cfs_evs.log   # >= 1 line
grep -c "Msg Limit Err,MsgId 0x18e8" omni_logs/cfs_evs.log  # 0
grep -c "Msg Limit Err,MsgId 0x80d"  omni_logs/cfs_evs.log  # 0
grep -c "Msg Limit Err,MsgId 0x18f9" omni_logs/cfs_evs.log  # 0
grep -c "LC_APSAMPLE_LEN_ERR"        omni_logs/cfs_evs.log  # 0 (or 1 from a boot-time race)
grep -c "FILE CREATE error"          omni_logs/cfs_evs.log  # 0
grep -c "CFE_ES_ExitApp: Application MD" omni_logs/cfs_evs.log  # 0
grep "MGR App Initialized" omni_logs/cfs_evs.log            # >= 1 line
wc -l attack_logs/cfs_god_view.json              # growing every second
wc -l omni_logs/tlm_hk_decoded.log               # growing every second
```

In Kibana, the filter `type:hk_decoded AND app:HS` should now return documents with numeric `cpu_avg_pct`, `cpu_peak_pct`, `cmd_count`, `cmd_err_count` fields.
