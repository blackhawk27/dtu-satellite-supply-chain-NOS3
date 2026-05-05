# 04. syn - SYNOPSIS Prioritisation Library

This document describes the SYN cFS app, a port of NASA JPL's
SYNOPSIS science-data prioritisation library to the cFS
environment. The thesis-relevant property of this app is the
heap-leak in its `RESET_CC` command handler at `syn_app.c:367`,
which appears in Phase 4 of the supply-chain attack research.

## 1. Real-world role

SYNOPSIS is an autonomous science-data prioritisation library
used by mission operations to decide which observations to
downlink first when the link budget is constrained. The library
ranks observations against a weighted policy and returns an
ordered queue. On a flight target, this is the kind of
on-board autonomy that dictates downlink cost.

## 2. App overview

The component lives at `nos3/components/syn/`:

- `fsw/cfs/src/syn_app.c`: cFS app entry point and command
  handlers.
- `fsw/cfs/platform_inc/`: MID values, topic IDs, platform
  config.
- `synopsis/`: the upstream SYNOPSIS library port.

MIDs (component-base, `0x1900 / 0x0900`):

- Topic ID per `syn_topicids.h`.
- Command MID through `SYN_CMD_MID`.
- Telemetry MID through `SYN_HK_TLM_MID`.

Command codes include `SYN_NOOP_CC`, `SYN_RESET_COUNTERS_CC`,
the configuration commands `SYN_CONFIG_ALPHA_CC` /
`SYN_CONFIG_BETA_CC`, and the demo-reset command `SYN_RESET_CC`.

## 3. Headline function: the RESET_CC heap-leak

`syn_app.c:355` is the case body for `SYN_RESET_CC`. The
relevant lines (355-388):

```
case SYN_RESET_CC:
    if (SYN_VerifyCmdLength(SYN_AppData.MsgPtr, sizeof(SYN_NoArgs_cmd_t)) == OS_SUCCESS)
    {
        ...
        itc_app_deinit(memory);
        memory = malloc(mem_req_bytes);    // <-- line 367
        ...
        reset_status = OS_remove(SYN_DATABASE_PATH);
        if (reset_status != OS_SUCCESS) { ... }
        else
        {
            reset_status = OS_cp(SYN_DATABASE_CLEAN_PATH, SYN_DATABASE_PATH);
            if (reset_status != OS_SUCCESS) { ... }
            else
            {
                itc_app_init(mem_req_bytes, memory);
            }
        }
    }
    break;
```

The pattern is: deinit the SYNOPSIS in-process memory,
`malloc` a fresh buffer, copy a clean database in, re-init.
The leak is that the freshly malloc-ed buffer at line 367 is
not freed if either of the file operations (`OS_remove` or
`OS_cp`) fails: the function returns through the `break` with
the new `memory` reachable but `itc_app_init` not called, so
the SYNOPSIS library's bookkeeping pointer is stale and the
new buffer is orphaned.

Repeated `SYN_RESET_CC` issuance in the failure path therefore
leaks `mem_req_bytes` on each call. With a sufficiently large
`mem_req_bytes` (the SYNOPSIS database is non-trivial), a few
hundred replays exhaust the cFE-process heap and crash the
FSW.

This is the surface that Phase 4 of the supply-chain attack
research uses to demonstrate a resource-exhaustion attack
delivered through a benign-looking ground command. The attack
script `nos3/scripts/attack/attack_syn_leak.py` reproduces the
sequence.

## 4. ELK extraction

Logstash extracts the SYN HK fields:

- `syn_dl_size`, `syn_num_files_downlinked`, plus standard
  CmdCounter / ErrCounter.

EVS lines from the demo-reset path land in
`omni_logs/cfs_evs.log`.

Kibana panels:

- "SYNOPSIS Prioritisation State" on
  `nos3-std-telemetry-overview`.
- "FSW Memory Footprint" on `nos3-std-mission-validation`
  (used to visualise the resource-exhaustion attack).

## 5. Attack-surface relevance

The SYN app appears explicitly in the thesis attack-chain
research:

- **Resource exhaustion**: the heap-leak in
  `SYN_RESET_CC` is exploitable by an attacker with the
  ability to issue ground commands. The attack shows how a
  small leak per benign command can be amplified into an FSW
  crash. The Kibana panel "FSW Memory Footprint" is the
  detection surface.
- **Ground-station compromise**: an attacker with COSMOS
  access can chain `SYN_RESET_CC` issuance with a tampered
  `SYN_DATABASE_PATH` removal so the failure path is
  deterministic.

## 6. DTU divergence vs upstream

The component port (SYNOPSIS into cFS) was added before the
import baseline; the heap-leak is in the port itself, not
introduced by DTU. The DTU treatment here is to characterise
and exploit the leak in research, not to fix it. The thesis
documents the leak as a found vulnerability in the testbed.

## 7. Verification

- `SYN_RESET_CC` issuable from COSMOS; the EVS log carries
  `SYN_CMD_RESET_DEMO_EID` per call.
- Force the failure path by removing the
  `SYN_DATABASE_CLEAN_PATH` file before issuing the command;
  observe per-issuance growth in the FSW process's resident
  set size.
- `nos3/scripts/attack/attack_syn_leak.py` reproduces the
  end-to-end attack and shows the ramp on the "FSW Memory
  Footprint" panel.
