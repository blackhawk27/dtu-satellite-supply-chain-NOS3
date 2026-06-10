# Piggyback PoC - manual operator runbook

> **DTU secured-fork note.** Ported from the Draco baseline (commit `dd1d2cbb`). In this repo the LC battery autonomy is WP #27 (`< 24240` mV) -> AP #27 -> RTS 27 (not Draco's WP0/AP0 SAFE chain), and the DEBUG-path injection means CryptoLib does not gate the attack. Scenarios `0x02/0x04/0x06/0x08` are in scope; IMU (`0x0A`) and CDS persistence are Draco-only. See [`00-dtu-secured-fork-notes.md`](00-dtu-secured-fork-notes.md).


A step-by-step, click-by-click walkthrough of the covert-opcode piggyback PoC for
a human operator: launch with the COSMOS GUI, send the commands by hand, find the
evidence in Kibana (and build a dashboard), capture and analyse the pcap, then
archive the run.

This is the manual companion to:
- `nos3/poc/piggyback_noisy/run_poc.md` - quick reference + headless driver.
- `docs/security/threat-analysis.md` - the STRIDE/SPARTA write-up (Appendix A is
  the captured evidence from a live run done exactly this way).

Concrete values used below (from `nos3/Makefile` and `nos3/elk/.env`):

| Thing | Value |
|---|---|
| ES (Elasticsearch) | `http://localhost:9202` |
| Kibana | `http://localhost:5603` |
| ELK index | `nos3-telemetry-YYYY.MM.dd` (index pattern `nos3-telemetry*`) |
| CI_LAB uplink (commands in) | UDP `5012` on the FSW container (`sc01-nos-fsw`, IP `172.20.0.4`) |
| TO_LAB downlink (telemetry out) | UDP `5013` |
| Carrier MID (piggyback target) | `0x18E0` (CI_LAB_CMD_MID), function code `0` (NOOP) |
| noisy_app command MID | `0x18F2`; spoofed EPS HK MID `0x091A` |
| pcap capture dir | `nos3/attack_logs/tcpdump/` |

---

## 0. Prerequisites

```bash
cd nos3
make config && make            # noisy_app must build (it is ENABLED in targets.cmake)
grep noisy cfg/build/nos3_defs/cpu1_cfe_es_startup.scr   # expect: CFE_APP, noisy_app
```

For the GUI you need X11 working in the devcontainer. Verify BEFORE launching
(do not edit devcontainer/X11 config - see the repo guard rails):

```bash
echo "$DISPLAY"          # expect :0
xeyes &                  # a window should appear on your desktop; close it
```

If `xeyes` fails, fix X11 first; the COSMOS Launcher and 42 windows will not show
otherwise. You can still do the entire PoC headless with `make launch` + the
`drive_poc.py` driver (see run_poc.md) - the GUI is only for the manual workflow.

---

## 1. Launch with the COSMOS GUI

```bash
cd nos3 && make cosmos-gui
```

This brings up the full stack (ELK + 42 + all sims + FSW), then replaces the
headless COSMOS with the **COSMOS Launcher** window. What you should see:
- The NOS3 / COSMOS Launcher window (buttons: Command Sender, Telemetry Grapher,
  Script Runner, etc.).
- 42 dynamics windows (Cam / Map / Unit Sphere) if X11 is healthy.

Confirm `noisy_app` actually loaded (the malicious co-resident app):

```bash
grep "NOISY_APP: Initialized" omni_logs/cfs_evs.log
# 42/1/NOISY_APP 1: NOISY_APP: Initialized. CMD MID 0x18F2, sniffing carrier 0x18E0.
```

---

## 2. Send the commands by hand (COSMOS)

### 2a. Enable the telemetry downlink

COSMOS owns the TO_LAB output-enable. In the Launcher, open **Command Sender**,
choose target `CFS`, command `TO_ENABLE_OUTPUT`, and send it with the downlink
destination (the nos3-sc01 gateway, reachable from the FSW container):

```
CFS TO_ENABLE_OUTPUT with DEST_IP '172.20.0.1', DEST_PORT 5013
```

Telemetry should now flow; confirm with `tail -f omni_logs/tlm_hk_decoded.log`.

### 2b. The control (negative test)

Command Sender -> target `CI_DEBUG`, command `CI_DEBUG_NOOP_CC` -> Send.
A normal NOOP: noisy_app stays silent, the carrier's CmdCounter advances. This is
your baseline.

### 2c. The piggyback commands

Command Sender -> target `CI_DEBUG`, command `CI_DEBUG_PIGGYBACK_NOOP_CC`. Set the
`OPCODE` dropdown and send:
- `DORMANT` (0x00) - over-length but harmless (stealth); also clears a running override.
- `EPS_SPOOF` (0x02) - forges one low-battery EPS HK (one-shot).
- `SB_BURST` (0x04) - capped 64-packet SB burst (sim stays alive).
- `EPS_OVERRIDE` (0x06) - PERSISTENT: shadows every real EPS HK so the spoof always
  wins LC. With AP0 armed (section 2d) this forces SAFE and holds it. `0x00` clears it.
- `SB_FLOOD` (0x08) - DESTRUCTIVE: locks the whole SB pool (total blackout). The sim
  needs `make stop` + relaunch afterwards. Run `save-run` first if you want the evidence.

Or from **Script Runner** / the ruby console:

```ruby
cmd("CI_DEBUG CI_DEBUG_NOOP_CC")                          # control
cmd("CI_DEBUG CI_DEBUG_PIGGYBACK_NOOP_CC with OPCODE 2")  # EPS_SPOOF
cmd("CI_DEBUG CI_DEBUG_PIGGYBACK_NOOP_CC with OPCODE 4")  # SB_BURST
```

Watch the events live: `tail -f omni_logs/cfs_evs.log`. You should see, on the
EPS_SPOOF, the dual path on one packet:
```
42/1/CI_LAB_APP 16: Invalid msg length: ID = 0x18E0, CC = 0, Len = 9, Expected = 8
42/1/NOISY_APP  2: NOISY_APP: piggyback opcode 0x02 on MID 0x18E0 (len 9, count N).
42/1/NOISY_APP  3: NOISY_APP: EPS HK SPOOF sent on 0x091A (BatteryVoltage=10000 mV).
```

### 2d. (Optional) Drive the autonomous SAFE RTS from the spoof

LC fires the SAFE RTS once at cold start on the real low battery, then latches
the actionpoint (AP0) PASSIVE, so a single later spoof only logs
`AP failed while passive`. To re-fire RTS 1 **from the spoof**, re-arm AP0 and
send the EPS_SPOOF rapidly (faster than the ~1 Hz real EPS HK) so 3 consecutive
LC samples see the low value:

```ruby
# Re-arm the battery actionpoint
cmd("LC SET_AP_STATE with APNUMBER 0, NEWAPSTATE 1")   # AP0 -> ACTIVE
# Then send EPS_SPOOF ~8x/sec for ~4 seconds:
20.times { cmd("CI_DEBUG CI_DEBUG_PIGGYBACK_NOOP_CC with OPCODE 2"); wait(0.12) }
```

Expected:
```
42/1/LC 1000: Batt volt critical: SAFE : AP = 0, FailCount = ..., RTS = 1
42/1/SC   73: RTS Number 001 Started
```

(If the `LC SET_AP_STATE` target/param names differ in your COSMOS build, the
equivalent raw command is MID `0x18A4`, FC `3`, payload `APNumber=0,
NewAPState=1` - see `drive_poc.py` for the exact bytes.)

---

## 3. Find the messages in Kibana

Open `http://localhost:5603`.

### 3a. Index pattern (one-time)

`make launch` auto-creates the `nos3-telemetry*` data view. If it is missing:
Stack Management -> Data Views -> Create data view -> name `nos3-telemetry*`,
time field `@timestamp` -> Save.

### 3b. Discover (find the events)

Left menu -> **Discover** -> select data view `nos3-telemetry*` -> set the time
picker to "Last 1 hour". Paste each KQL into the search bar:

| What you are looking for | KQL |
|---|---|
| Covert opcode read by the sniffer | `message:"piggyback opcode"` |
| EPS HK spoof emitted | `message:"EPS HK SPOOF sent"` |
| Capped SB burst | `message:"SB BURST sent"` |
| Endpoint length rejection (the piggyback tell-tale) | `message:"Invalid msg length" and message:"0x18E0"` |
| Spoofed low battery in decoded HK | `battery_mv < 14800` |
| Autonomous SAFE fired | `message:"Batt volt critical: SAFE"` |
| SAFE RTS executed | `message:"RTS Number 001 Started"` |

Add columns (e.g. `evs_app_name`, `evs_message`, `battery_mv`) from the field
list on the left to make the table readable.

### 3c. The ready-made dashboard

A dedicated **NOS3 Security - Piggyback PoC** dashboard is now built
automatically by `elk/build_kibana_dashboards.py` (id `nos3-piggyback-poc`) on
every `make launch`. Open it directly:

```
http://localhost:5603/app/dashboards#/view/nos3-piggyback-poc
```

Panels: 4 metric tiles (piggyback opcodes read, CI_LAB length rejections,
spoofed EPS HK packets, autonomous SAFE fires), an EPS-battery-min line (the dip
to 10000 below the 14800 WP0 threshold), an attack-chain EVS-events-by-app bar
(NOISY_APP / CI_LAB / LC / SC), and a "Detection signals (KQL)" filter bar that
turns each threat-analysis detection into one column. To (re)build just this one
without a full launch:

```bash
KIBANA_URL=http://localhost:5603 ES_URL=http://localhost:9202 \
  python3 elk/build_kibana_dashboards.py --only nos3-piggyback-poc
```

### 3d. Build your own dashboard (manual)

If you prefer to assemble one by hand:

1. Left menu -> **Dashboard** -> **Create dashboard**.
2. **Create visualization** -> Lens. Build these panels (Save each to the
   dashboard):
   - **Metric: piggyback detections** - Lens, metric = Count, filter
     `message:"piggyback opcode"`.
   - **Metric: length rejections** - Count, filter
     `message:"Invalid msg length" and message:"0x18E0"`.
   - **Line: battery_mv over time** - X = `@timestamp`, Y = Min of `battery_mv`.
     The spoof shows as a dip to 10000 against the ~25000 baseline. Add a
     reference line at 14800 (the WP0 threshold).
   - **Bar over time: EVS events by app** - X = `@timestamp` (date histogram),
     break down by `evs_app_name`, filter `evs_app_name: ("NOISY_APP" or
     "CI_LAB_APP" or "LC" or "SC")`.
   - **Saved search panel** - add a Discover saved search with KQL
     `message:"piggyback" or message:"EPS HK SPOOF" or message:"Batt volt
     critical"` so the raw event lines are on the board.
3. Set the dashboard time range, then **Save** as `Piggyback PoC`.

The MCP `kibana` tools (if wired to port 5603) can script saved-object
create/update instead of clicking; the repo's
`elk/build_kibana_dashboards.py` is the programmatic path the build uses.

---

## 4. pcap capture

### 4a. How it is captured

`make launch` starts a **passive tcpdump sidecar** container
(`nos3-tcpdump-sc01`, via `scripts/start_tcpdump_sidecar.sh`) that shares the FSW
container's network namespace and sniffs UDP 5012 (commands) and 5013
(telemetry). Files land in `nos3/attack_logs/tcpdump/`:

```
sc01_5012_<timestamp>.pcap00   # uplink (your piggyback datagrams)
sc01_5013_<timestamp>.pcap00   # downlink (telemetry)
```

It is passive - it does not inject anything. Start/stop manually if needed:

```bash
./scripts/start_tcpdump_sidecar.sh start
./scripts/start_tcpdump_sidecar.sh stop
```

### 4b. Analyse the pcap

The sidecar mounts the capture dir at `/caps`. Read it with the sidecar's
tcpdump (no host install needed):

```bash
PC=/caps/$(ls -t attack_logs/tcpdump/sc01_5012_*.pcap* | head -1 | xargs basename)

# Payload-length histogram: 8 = control NOOP, 9 = piggyback, 28 = TO enable
docker exec nos3-tcpdump-sc01 sh -c "tcpdump -nr $PC" | \
  grep -oE 'UDP, length [0-9]+' | sort | uniq -c | sort -rn

# Hex of the 9-byte piggyback datagrams - the trailing byte is the covert opcode
docker exec nos3-tcpdump-sc01 sh -c "tcpdump -nXr $PC" | grep -A3 'UDP, length 9' | head
# tail bytes ...0002 0000 02  => CCSDS len=2, FC=0, checksum=0, opcode 0x02 (EPS_SPOOF)
# tail bytes ...0002 0000 04  => opcode 0x04 (SB_BURST)
```

To inspect in Wireshark on your desktop, copy the `.pcap00` out and open it;
decode UDP port 5012/5013 payloads as data, or apply a CCSDS dissector. Filter
`udp.length == 17` (8-byte UDP header + 9-byte payload) to isolate piggybacks.

---

## 5. Save and analyse the run

### 5a. Archive

```bash
cd nos3
make save-run RUN_LABEL=piggyback_poc
```

This copies `omni_logs/*.log`, `attack_logs/cfs_god_view.json`, and
`attack_logs/tcpdump/` to `~/nos3_runs/piggyback_poc/` (with a `metadata.txt`
recording the git commit), then stops the stack while **preserving** the ELK
indices (`KEEP_TLM=1`). `make stop` leaves no orphan containers.

### 5b. Re-load a saved run into Kibana later (no sim)

```bash
make load-run RUN=piggyback_poc      # restores logs + restarts ELK only
# open http://localhost:5603 and use the same KQL / dashboard from section 3
```

### 5c. Offline analysis (no stack running)

```bash
D=~/nos3_runs/piggyback_poc

# All noisy_app + endpoint + SAFE events, in order
grep -E "NOISY_APP|Invalid msg length|Batt volt critical|RTS Number 001" $D/omni_logs/cfs_evs.log

# Every spoofed low-battery HK record
grep '"battery_mv": 10000' $D/omni_logs/tlm_hk_decoded.log

# Software-bus god view - count messages per MID (0x091A = EPS HK)
grep -oE '"msg_id": "0x[0-9A-Fa-f]+"' $D/cfs_god_view.json | sort | uniq -c | sort -rn | head

# The captured piggyback datagrams (host tcpdump or any pcap tool)
tcpdump -nXr $D/tcpdump/sc01_5012_*.pcap00 2>/dev/null | grep -A3 'UDP, length 9'
```

---

## 6. Revert to mission-truth

To prove the fixture is cleanly removable (not a permanent backdoor):

```bash
# In cfg/nos3_defs/targets.cmake: re-comment the `noisy_app` applist line.
# In cfg/nos3_defs/cpu1_cfe_es_startup.scr: change `CFE_APP, noisy_app` -> `OFF_APP, noisy_app`.
# Optionally delete gsw/cosmos/config/targets/CI_DEBUG/cmd_tlm/PIGGYBACK_POC.txt
make config
grep noisy_app cfg/build/nos3_defs/cpu1_cfe_es_startup.scr   # now OFF_APP
make                                                          # noisy_app.so no longer loaded
```

After this, `noisy_app` is absent at boot and the PoC command is a no-op.
