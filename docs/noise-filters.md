# Logstash Noise Filters

This document describes every noise-suppression rule in `nos3/elk/logstash.conf`. Each entry explains what the message is, why it appears, and why it is safe to suppress from Kibana.

The pipeline has two suppression strategies:

- **Drop** (`drop {}`): the event is discarded before reaching Elasticsearch. Used when the line is high-frequency, has no diagnostic content, and would never be queried after the fact.
- **Tag-strip** (`add_tag => ["noise_filtered"]` plus `replace => { "evs_severity" => "INFORMATION" }`): the event is kept in the index but downgraded to INFORMATION severity and tagged so that ERROR/WARNING dashboards stop showing it. Used when the line is benign-by-design but may still be useful during forensic queries.

Filters are listed in the order they execute in `logstash.conf`.

---

## Drop rules

### 1. XML config dumps and blank lines

**Pattern:** `message` starts with `<` or is whitespace-only
**Section:** system_log (all sources), near top of filter
**Root cause:** Some simulator containers emit their full XML configuration block to stdout on startup, and various log lines contain empty content. These events contain no telemetry or operational data.
**Why safe to drop:** No signal value; they increase index size without contributing to any dashboard or alert.

---

### 2. cFE startup sync / boot messages

**Pattern:** `evs_raw` matches `Startup Sync failed`, `cFE startup complete`, `attempting to start`, or `Startup.*timeout`
**Source file:** `nos3/fsw/cfe/fsw/cfe-core/src/es/cfe_es_start.c`
**Root cause:** cFE logs a fixed set of progress messages during every cold-start boot sequence. They appear on every launch without exception.
**Why safe to drop:** These are informational milestones, not errors. Their presence is guaranteed and their absence would indicate a hard FSW crash that would be caught by other means (FSW container going down).

---

### 3. SBN single-FSW peer-connect noise

**Pattern:** `evs_app_name == "SBN"` and `evs_message` matches `^(connecting to peer|unable to connect to peer|Processing all subscriptions|configuring peer)`
**Source:** cFS Software Bus Network app
**Root cause:** SBN is a multi-FSW feature that tries to connect to peer flight software instances. This fork runs single-FSW only, so SBN repeatedly attempts to connect to a peer that does not exist and logs both the attempt and the failure on every retry cycle.
**Why safe to drop:** No peer exists by design. There is no operational consumer for the connection-attempt records. (A second SBN class, `unable to connect to peer`, also appears later as a tag-strip rule because it also leaks through other paths; both are addressing the same underlying single-FSW topology.)

---

### 4. LC startup-transient float-NaN watchpoint events

**Pattern:** `evs_app_name == "LC"` and `evs_message` matches `WP data value is a float NAN`
**Source:** Limit Checker (LC) cFS app evaluating watchpoints
**Root cause:** All 17 NaN-producing watchpoints (3, 6-9, 30-33, 35-38, 40-43) read from `NOVATEL_OEM615_DEVICE_TLM_MSG` (GPS lat/lon/alt). The GPS sim publishes those fields as NaN until its first 42 frame parse succeeds. The events cluster at boot and fire 38 to 40 times per launch; LC degrades to PASS immediately once the GPS frame populates.
**Why safe to drop:** The condition is transient and self-resolves on the first valid GPS frame. To surface a real persistent NaN (e.g. GPS sim wedged), remove this filter and inspect the GPS sim init path.

---

### 5. CFE_SB "No subscribers for MsgId" events

**Pattern:** `evs_app_name == "CFE_SB"` and `evs_message` matches `^No subscribers for MsgId`
**Source:** cFE Software Bus, when an app publishes a message that no other app has subscribed to.
**Root cause:** cFE startup publishes a few status messages from CFE_ES that no app subscribes to. SB logs the unsubscribed publish as informational on every occurrence.
**Why safe to drop:** No actionable consumer exists in this build, and the event is documented as benign in the cFE manual. (A narrower variant for MID `0x808`, the boot race against TO_LAB's subscribe, is handled separately as a tag-strip rule below.)

---

### 6. OSAL undefined-symbol probe errors

**Pattern:** `message` matches `OS_GenericSymbolLookup_Impl.*undefined symbol`
**Source file:** `nos3/fsw/osal/src/os/portable/os-impl-posix-dl-symtab.c:114`
**Root cause:** OSAL walks every dynamically-loaded `.so` at boot and tries to resolve all known symbols via `dlsym()`. Apps such as LC and SC export their primary data structures (`LC_AppData`, `SC_OperData`) from the statically-linked `core-cpu1` image, not from their individual `.so` files. The probe therefore fails with `undefined symbol`. The apps themselves load and execute correctly: only the symbol-table indexing step fails.
**Why safe to drop:** If LC or SC fail to initialise for a real reason, they emit a cFE EVS event (`LC APP INIT FAILED`, etc.) which is retained. The OSAL symbol-probe error has no operational meaning beyond the normal startup sequence.

---

### 7. MD app table-staging failures

**Patterns (three related log lines from the same root cause):**
- `message` matches `MD_APP.*Error.*0xC0000003.*loading tbl`
- `message` matches `MD.*Application Init Failed`
- `evs_raw` matches `MD.*Table entry.*0xC0000003`

**Source file:** `nos3/fsw/apps/md/fsw/src/md_app.c:403`
**Error code:** `0xC0000003 = CFE_TBL_ERR_INVALID_NAME`
**Root cause:** The Memory Dwell (MD) app expects compiled table binary files (`/cf/md_dw01.tbl`, `/cf/md_dw02.tbl`, etc.) to be present in the FSW runtime filesystem. These files are not staged to `/cf/` in the current research configuration, so MD fails to load its dwell table on every boot. All three log lines trace the same single failure: MD cannot find its table, logs an error, then logs its init failure, which cFE EVS also echoes.
**Why safe to drop:** MD is not used by the current mission profile (EO1). No watchpoints or dwell samples are configured against it. Retaining these three lines produces duplicate noise for a single known-missing artefact. If MD is activated in a future configuration, the table files must be built and deployed to `/cf/` to silence this error at source.

---

### 8. MGR anomalous-reboot / SAFE MODE entry on first boot

**Pattern:** `message` matches `MGR.*Restore Hk Packet error.*Anomalous reboot`
**Source file:** `nos3/components/mgr/fsw/cfs/src/mgr_app.c:576`
**Root cause:** On the very first boot after a clean container start, the MGR persistence file (`mgr_hk.bin`) does not exist. `MGR_RestoreHkFile()` reads uninitialised bytes; the `SpacecraftMode` field is neither `MGR_SAFE_REBOOT_MODE` (0) nor `MGR_SCIENCE_REBOOT_MODE` (1), so MGR treats this as an anomalous reboot and enters SAFE MODE. This is the correct and intended fallback behaviour for an unknown prior state. The second boot onwards reads a valid persistence file and does not trigger this path.
**Why safe to drop:** SAFE MODE entry is the correct default for a first boot. Genuine anomalous-reboot conditions (e.g. hardware watchdog, power cycle mid-operation) would be accompanied by additional MGR events and EPS / HS anomaly markers that are retained. This single first-boot instance is not actionable.

---

### 9. Radio simulator cryptolib hostname failure

**Pattern:** `subsystem` matches `nos3-radio` and `sim_message` matches `Failed to resolve IP for cryptolib` or `cryptolib`
**Source file:** `nos3/components/generic_radio/sim/src/generic_radio_hardware_model.cpp:507`
**Root cause:** The generic-radio simulator is configured (`nos3/cfg/sims/nos3-simulator.xml`) to forward encrypted TC/TM frames to a service named `cryptolib` on TCP ports 8010/8011. This service provides CryptoLib-based AES/GCM encryption for the CCSDS Space Data Link protocol. It is not deployed in the current research/development configuration, so DNS resolution of the `cryptolib` hostname fails continuously throughout the run.
**Why safe to drop:** The radio simulator operates normally on its unencrypted path (UDP relay between FSW and the ground software). The cryptolib TCP path is an optional encryption layer. Dropping this removes high-frequency repeated noise without masking any radio connectivity event that would affect mission operations.

---

### 10. 42-dynamics socket retries (five related variants)

**Patterns (five related log lines from the same root cause):**
- `sim_message` matches `failed to connect host fortytwo`
- `sim_message` matches `could not connect socket for host fortytwo`
- `sim_message` matches `Maximum number of connection attempts reached.*Host fortytwo`
- `sim_message` matches `Unable to connect TELEMETRY host fortytwo`
- `sim_message` matches `Unable to connect COMMAND host fortytwo`

**Source file:** `nos3/sims/sim_common/src/sim_data_42socket_provider.cpp:161` (and the retry-loop terminus paths)
**Root cause:** Component simulators (GPS, IMU, magnetometer, star tracker, thruster, etc.) start simultaneously with the 42 orbital-mechanics and attitude-dynamics engine. Two distinct sub-cases produce these messages:

1. **Transient race:** the 42 container typically takes 2 to 10 seconds longer to reach its listen state on its sockets. During this window each dependent simulator logs a WARNING per retry and eventually succeeds.
2. **Expected-by-design unreachable port:** under the current static config, several Inp_IPC.txt entries are deliberately set to `OFF` so 42 never opens those ports. The star tracker (port 4282) and thruster (ports 4280 / 4242) hit the connect-refused path and the retry-loop terminus messages persistently because they will never connect. See the project root notes for the load-bearing IPC OFF entries.

**Why safe to drop:** Case (1) is a transient that self-resolves; case (2) is a design constraint of this profile. If 42 fails to start entirely (container crash, port conflict) the symptom is loss of all GPS/attitude telemetry fields in Kibana, which is more visible and actionable than repeated socket-retry warnings. Likewise, missing star-tracker telemetry is visible from the dashboard via empty `st_q0` / no fresh ST HK without needing the retry warnings.

---

### 11. RW data-point parsing race during 42 handshake

**Pattern:** `subsystem` matches `^nos3-rw[0-9]+$` and `sim_message == "Parsing exception stod"`
**Source:** `Generic_RWDataPoint::do_parsing` in the generic reaction-wheel sim
**Root cause:** Each generic-reactionwheel-sim instance constructs its first `GenericRWDataPoint` from whatever 42 frame is available the moment the 42 socket connects. If the frame has not yet been populated, the value for `SC[0].Whl[N].H` is empty and `std::stod` throws `"stod"` with no numeric content. Within ~1 second the next 42 frame arrives and parsing succeeds; `rw_momentum` then populates correctly. The 25-burst per RW is purely a startup-race log.
**Why safe to drop:** Adds no diagnostic value because downstream `rw_momentum` populates regardless. Dropping prevents the dashboard ERROR count from being dominated by this transient.

---

### 12. CSS data-point parsing race during 42 handshake

**Pattern:** `subsystem == "nos3-css"` and `sim_message == "Parsing exception stof"`
**Source file:** `generic_css_data_point.cpp:96-106` (`Generic_cssDataPoint::do_parsing`)
**Root cause:** Identical class to the RW stod race above. `do_parsing` reads `SC[0].CSS[N].Illum` / `Valid` keys from the first 42 frame; on the very first parse the keys are empty and `std::stof` throws `"stof"`. The catch handler in `do_parsing` zeros the data vector and the next 42 frame parses normally.
**Why safe to drop:** One-shot at startup; downstream `css_*` fields populate within ~1 sim second.

---

### 13. "No COMMAND port requested" DEBUG narrative line

**Pattern:** `sim_message` matches `No COMMAND port \([0-9]+\) to (42|N) requested, none connected`
**Source:** `SimData42SocketProvider`
**Root cause:** Printed for every sim that operates with no 42 command path (CSS, EPS, GPS, IMU, MAG, Truth42). It is a confirmation that the configuration was honoured, not an error. The line is DEBUG-level but the words "none connected" hit broad ops-grep patterns.
**Why safe to drop:** Configuration confirmation, not a fault. Dropped to keep dashboard error counters clean.

---

### 14. Inactive-simulator startup warning

**Pattern:** `sim_message` matches `Simulator ".+" is not active in "nos3-simulator.xml"`
**Source:** Per-simulator main() in the sim launcher
**Root cause:** Sim containers launch every component from a single image and each reads `nos3-simulator.xml` to find its own `<active>` flag. Components marked `<active>false</active>` (camsim, generic-thruster-sim in this research profile) log a WARNING at startup and then exit cleanly.
**Why safe to drop:** Expected-by-design configuration output, not a fault. Dropped to keep the dashboard's WARNING count clean.

---

## Tag-strip rules

These events stay in the index (queryable forensically) but are tagged `noise_filtered` and have their `evs_severity` rewritten to `INFORMATION` so they no longer fan out to ERROR / WARNING dashboards.

---

### T1. HS_SysMonInit: no system monitor on Linux PSP

**Pattern:** `evs_raw` matches `HS_SysMonInit\(\): No system monitor device available`
**Source file:** `hs_sysmon.c:53`
**Root cause:** Single boot-time syslog write. The message is informational-by-design under the NOS3 Linux PSP (no `/proc`-based CPU monitor); container-level CPU comes from the `cpu_monitor` sidecar instead.
**Why tag-strip rather than drop:** Useful forensically when investigating CPU-monitoring path issues, so kept in the index but demoted from ERROR.

---

### T2. CS "No valid entries in the table"

**Pattern:** `evs_app_name == "CS"` and `evs_message` matches `No valid entries in the table`
**Source file:** `cs_table_processing.c` (events 107 / 108 / 109)
**Root cause:** Emitted once per CS default table at boot whenever every slot is in `CS_ChecksumState_EMPTY` (the upstream NOS3 default). The proper FSW-side fix is to populate at least one slot with `State=DISABLED`, but the nos-linux PSP rejects `addr=0` in `CFE_PSP_MemValidateRange`, so a safe placeholder cannot be supplied without picking a real EEPROM/RAM range.
**Why tag-strip rather than drop:** CS events remain forensically interesting (any future checksum-failure investigation wants the full CS event stream), so demoted rather than dropped.

---

### T3. CFE_SB 14 "No subscribers for MsgId 0x808" boot race

**Pattern:** `evs_app_name == "CFE_SB"` and `evs_event_id == 14` and `evs_message` matches `MsgId 0x808\b`
**Source:** `0x0808 = CFE_EVS_LONG_EVENT_MSG_MID`. TO_LAB subscribes it (see `cfg/nos3_defs/tables/to_lab_sub.c:102`) but CFE_ES emits its first EVS LONG events before TO_LAB completes `CFE_SB_Subscribe`.
**Root cause:** A bounded handful of events publish with no subscriber and CFE_SB logs SB14 once per orphan packet. Stops as soon as TO_LAB initialises.
**Why tag-strip rather than drop:** Narrower than the full "No subscribers for MsgId" drop above; this specific MID + event-id combination is the boot-race instance and is kept queryable for FSW-init investigations.

---

### T4. LC 60 "AP failed while passive" recurring evaluations

**Pattern:** `evs_app_name == "LC"` and `evs_event_id == 60`
**Source file:** `lc_action.c` (`LC_AP_PASSIVE_FAIL_INF_EID`)
**Root cause:** Fired every time an AP currently in `LC_APSTATE_PASSIVE` state evaluates to FAIL. AP3 (SCIENCE_OVERFLY) and AP4 (SCIENCE_EXIT) are inverses of each other over the Denmark FOV box, so once their RTSes have fired and the APs are passivated, every subsequent LC eval produces one of these per AP.
**Why tag-strip rather than drop:** Operationally meaningful AP-state transitions are still visible via LC55 (state change) and LC56 (RTS request); LC60 is the recurring diagnostic, not the alert, and is kept for forensic analysis of mission-event timing.

---

### T5. SBN peer-connect race during cold-start

**Pattern:** `evs_app_name == "SBN"` and `evs_message` matches `unable to connect to peer`
**Source:** SBN (Software Bus Network) peer connect
**Root cause:** SBN tries to connect to peer FSWs that may not yet exist on the network. cFE prints these as INFORMATION events but the message contains "unable" so a generic keyword sweep flags them.
**Why tag-strip rather than drop:** No mission impact (SBN is a multi-FSW feature and this fork runs single-FSW). The narrower drop rule above catches the high-frequency variants; this tag-strip catches any leak-through. (See entry 3 for the related drop rule.)

---

### T6. Generic component "already enabled" startup events

**Pattern:** `evs_message` matches `Device enable failed, already enabled`
**Source:** Component cFS apps (GENERIC_CSS / FSS / IMU / MAG / SAMPLE)
**Root cause:** These apps emit an ERROR-severity EVS event on cold-start when the boot SC table issues an Enable command for a device that the sim already brings up enabled. One event per component per boot, never repeated.
**Why tag-strip rather than drop:** Classified as known-noise so the dashboard ERROR sweep stays clean, but kept in the index so changes in boot SC table behaviour remain observable.
