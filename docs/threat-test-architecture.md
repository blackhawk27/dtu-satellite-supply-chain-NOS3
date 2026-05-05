# NOS3/cFS Threat-Test Architecture

**Document scope:** Technical reference for the DTU satellite supply-chain research environment.
Covers the full Docker topology, internal cFS subsystem layout, the ELK telemetry pipeline, and
three active attack scenarios with detailed data-flow diagrams.

---

## Table of Contents

1. [Environment Overview](#1-environment-overview)
2. [Diagram 1 — Macro Architecture: Docker Networks and Data Pipelines](#2-diagram-1--macro-architecture)
3. [Diagram 2 — cFS and NOS3 Internal System Flow](#3-diagram-2--cfs-and-nos3-internal-system-flow)
4. [Diagram 3 — Telemetry, Log, and Metric Pipeline](#4-diagram-3--telemetry-log-and-metric-pipeline)
5. [Diagram 4 — Attack Scenario 1: Telemetry Spoofing / Command Injection](#5-diagram-4--attack-scenario-1-telemetry-spoofing--command-injection)
6. [Diagram 5 — Attack Scenario 2: Software Bus Resource Exhaustion (DoS)](#6-diagram-5--attack-scenario-2-software-bus-resource-exhaustion-dos)
7. [Diagram 6 — Attack Scenario 3: Log Tampering and Observability Evasion](#7-diagram-6--attack-scenario-3-log-tampering-and-observability-evasion)
8. [CCSDS Message ID Quick Reference](#8-ccsds-message-id-quick-reference)
9. [Vulnerability Summary](#9-vulnerability-summary)

---

## 1. Environment Overview

The test environment is fully containerized and runs on a single Linux host (WSL2/Docker Desktop).
Four logical subsystems interact over two Docker bridge networks:

| Subsystem | Container Name | Image | Exposed Ports |
|---|---|---|---|
| Flight Software (cFS) | `nos3-fsw` | `ivvitc/nos3-64:20251107` | 5011/udp (CI), 5010/udp (TO) |
| NOS3 Hardware Sims | `nos3-sim-*` | same build image | NOS Engine IPC |
| Ground Software (COSMOS) | `nos3-cosmos` | Ruby/COSMOS | 7779/tcp (API), 2900/tcp (TLM) |
| Elasticsearch | `nos3-elasticsearch` | `elasticsearch:7.17.10` | 9200/tcp, 9300/tcp |
| Logstash | `nos3-logstash` | `logstash:7.17.10` | 5044/tcp (Beats), 9600/tcp (monitoring) |
| Kibana | `nos3-kibana` | `kibana:7.17.10` | 5601/tcp |

Two Docker bridge networks partition the traffic:

- **`nos3-core`** — shared services plane: NOS Engine time driver, ground software, all ELK containers.
- **`nos3_sc_1`** — spacecraft-local plane: FSW container to each per-component hardware simulator.

Logstash reaches log files via Docker bind-mounts, not over the network:
- `../attack_logs/` bind-mounted read-only to `/attack_logs/` inside `nos3-logstash`
- `../omni_logs/` bind-mounted read-only to `/omni_logs/` inside `nos3-logstash`

---

## 2. Diagram 1 — Macro Architecture

```mermaid
graph TD
    subgraph HOST["Host (WSL2 Linux)"]
        direction TB
        subgraph VOLUMES["Shared Host Volumes (bind-mount)"]
            VOL_ATTACK[("attack_logs/\ncfs_god_view.json")]
            VOL_OMNI[("omni_logs/\n*.log")]
        end

        subgraph NOS3_NET["Docker Network: nos3_sc_1 (172.20.0.0/16)"]
            direction TB
            subgraph FSW_CTR["Container: nos3-fsw\n(ivvitc/nos3-64:20251107)"]
                CFS["cFS Runtime\n(cfe-core + apps)"]
                HWLIB["HWLIB\n(Hardware Abstraction)"]
            end
            subgraph SIM_CTR["Containers: nos3-sim-*"]
                SIM_EPS["EPS Simulator\nNOS Engine I2C"]
                SIM_GPS["GPS Simulator\nNOS Engine UART"]
                SIM_RW["Reaction Wheel\nNOS Engine SPI"]
                SIM_RADIO["Radio Simulator\nNOS Engine CAN"]
                SIM_ADCS["42 Dynamics\n(ADCS/Attitude)"]
            end
            NOSENG["NOS Engine\nTime Driver\n(nos3-engine)"]
        end

        subgraph CORE_NET["Docker Network: nos3-core (172.19.0.0/16)"]
            direction TB
            subgraph GSW_CTR["Container: nos3-cosmos\nCOSMOS Ground Software"]
                COSMOS_CMD["Command Sender\nUDP :5011"]
                COSMOS_TLM["Telemetry Receiver\nUDP :5010"]
                COSMOS_SCRPT["God-View Script\n(cfs_god_view.json writer)"]
            end
            subgraph ELK["ELK Stack"]
                subgraph LS_CTR["Container: nos3-logstash"]
                    LS_INPUT["Logstash Input\n(file plugin)"]
                    LS_FILTER["Logstash Filter\n(translate + grok)"]
                    LS_OUTPUT["Logstash Output\nHTTP :9200"]
                end
                subgraph ES_CTR["Container: nos3-elasticsearch"]
                    ES[("Elasticsearch\n7.17.10\nTCP :9200\nTCP :9300")]
                    IDX["Index: nos3-telemetry-\nYYYY.MM.dd"]
                end
                subgraph KIB_CTR["Container: nos3-kibana"]
                    KIB["Kibana\n7.17.10\nHTTP :5601"]
                end
            end
        end

        ANALYST["Analyst / Attacker\n(Host Browser / Scripts)"]
    end

    %% Spacecraft-local comms
    CFS <-->|"NOS Engine IPC\nI2C / UART / SPI / CAN"| HWLIB
    HWLIB <-->|"nos3_sc_1 bridge"| SIM_EPS & SIM_GPS & SIM_RW & SIM_RADIO
    SIM_ADCS <-->|"Attitude/Ephemeris UDP"| HWLIB
    NOSENG -->|"1 Hz tick"| CFS
    NOSENG -->|"1 Hz tick"| SIM_EPS & SIM_GPS & SIM_RW

    %% Ground comms across core network
    CFS -->|"TO_APP UDP :5010\ncFS Software Bus TLM out"| COSMOS_TLM
    COSMOS_CMD -->|"CI_LAB UDP :5011\nCCSDS uplink"| CFS
    COSMOS_SCRPT -->|"writes NDJSON"| VOL_ATTACK

    %% Simulator logs
    SIM_EPS & SIM_GPS & SIM_RW & SIM_RADIO & SIM_ADCS -->|"stdout redirect"| VOL_OMNI

    %% Logstash ingestion
    VOL_ATTACK -->|"file plugin (ro mount)"| LS_INPUT
    VOL_OMNI   -->|"file plugin (ro mount)"| LS_INPUT
    LS_INPUT --> LS_FILTER --> LS_OUTPUT
    LS_OUTPUT -->|"HTTP POST :9200"| ES
    ES --> IDX

    %% Kibana
    KIB -->|"REST :9200"| ES
    ANALYST -->|"HTTP :5601"| KIB
    ANALYST -->|"Attack scripts\nPython/C"| CFS

    style HOST fill:#1a1a2e,stroke:#444,color:#eee
    style NOS3_NET fill:#162032,stroke:#3a6ea5,color:#cce
    style CORE_NET fill:#1e1e2e,stroke:#5a3a9a,color:#ddf
    style FSW_CTR fill:#0d2137,stroke:#2a7ec8,color:#9cf
    style SIM_CTR fill:#0d2137,stroke:#2a7ec8,color:#9cf
    style ELK fill:#1a0d37,stroke:#6a2ac8,color:#ccf
    style GSW_CTR fill:#0d2137,stroke:#2a7ec8,color:#9cf
    style VOLUMES fill:#1a2d1a,stroke:#3a8a3a,color:#cfc
```

### Technical Explanation

**Network isolation.** The `nos3_sc_1` bridge carries all spacecraft-internal traffic. NOS Engine
delivers hardware bus transactions (I2C, UART, SPI, CAN) to FSW via TCP sockets, emulating real
embedded bus timings. The HWLIB layer inside cFS translates OS calls into NOS Engine protocol
messages so the same FSW binary runs against emulated or real hardware without recompilation.

**Telemetry uplink/downlink.** The Telemetry Output app (`TO_APP`) opens a UDP socket and
streams packetized CCSDS telemetry to the COSMOS ground station on port 5010. Commands flow in
reverse via `CI_LAB`, which listens on UDP port 5011 and pushes received CCSDS command packets
onto the Software Bus as if they originated from a legitimate commanding source. This inbound
path is the primary attack surface for command injection.

**God-View logging.** A COSMOS background script subscribes to every telemetry definition and
writes one NDJSON line per Software Bus message received at the ground station to
`attack_logs/cfs_god_view.json`. This gives the ELK pipeline a near-complete view of all
Software Bus traffic without requiring an agent inside the FSW container.

**ELK ingestion.** Logstash uses the `file` input plugin with `start_position => beginning` and
`sincedb_path => /dev/null`, which means the pipeline re-indexes the entire log set on every
container restart. This is intentional for research replays. Logstash reaches the log files via
read-only Docker bind-mounts, not over the network. Security implication: tampering with log
files on the host volume is sufficient to poison the ELK index.

---

## 3. Diagram 2 — cFS and NOS3 Internal System Flow

```mermaid
graph LR
    subgraph CFE_CORE["cFE Core Services (FSW_CORE layer)"]
        direction TB
        ES["CFE_ES\nExecutive Services\n0x1806 / 0x0800"]
        EVS["CFE_EVS\nEvent Services\n0x1801 / 0x0808"]
        SB["CFE_SB\nSoftware Bus\n0x1803 / 0x0803\nPool: 512 KB static"]
        TBL["CFE_TBL\nTable Services\n0x1804 / 0x0804"]
        TIME["CFE_TIME\nTime Services\n0x1805 / 0x0805\n1 Hz tone: 0x1811"]
    end

    subgraph SCHED["Scheduler (FSW_HERITAGE)"]
        SCH["SCH_LAB / SCH_APP\n0x1895\nSends wakeup MIDs\nat configured rates"]
    end

    subgraph HERITAGE["Heritage Apps (FSW_HERITAGE layer)"]
        direction TB
        TO["TO_APP\nTelemetry Output\n0x1880\nUDP :5010 egress"]
        CI["CI_LAB\nCommand Ingest\n0x18E0\nUDP :5011 ingress"]
        DS["DS (Data Storage)\n0x18BB\nFile recorder"]
        FM["FM (File Manager)\n0x188C"]
        CF["CF (CFDP)\n0x18B3"]
        LC["LC (Limit Checker)\n0x18A4\nWP/AP eval"]
        SC["SC (Stored Cmd)\n0x18A9\nRTS/ATS execution"]
        MD["MD (Memory Dwell)\n0x1890\nPeriodic mem reads"]
        HK["HK (Housekeeping)\n0x189A"]
        HS["HS (Health Monitor)\n0x18AE"]
        CS["CS (Checksum)\n0x189F"]
        MM["MM (Memory Mgr)\n0x1888\nPeek/Poke/Load/Dump"]
        BEACON["BEACON_APP (RESEARCH)\n0x18F0"]
    end

    subgraph COMPONENT["Component Apps (COMPONENT layer)"]
        direction TB
        EPS_APP["GENERIC_EPS\n0x191A / 0x091A\nI2C via HWLIB"]
        GPS_APP["NOVATEL_GPS\n0x1870 / 0x0870\nUART via HWLIB"]
        RW_APP["GENERIC_RW\n0x1992 / 0x0993\nSPI via HWLIB"]
        RADIO_APP["GENERIC_RADIO\n0x1930 / 0x0930\nCAN via HWLIB"]
        ADCS_APP["GENERIC_ADCS\n0x1940 / 0x0940\nUDP attitude feed"]
        CSS_APP["GENERIC_CSS\n0x1910"]
        IMU_APP["GENERIC_IMU\n0x1925"]
        MAG_APP["GENERIC_MAG\n0x192A"]
        ST_APP["GENERIC_STAR_TRACKER\n0x1935"]
        THR_APP["GENERIC_THRUSTER\n0x18EA"]
        TRQ_APP["GENERIC_TORQUER\n0x193A"]
        CAM_APP["ARDUCAM\n0x18C8"]
    end

    subgraph HWLIB_LAYER["HWLIB / PSP Abstraction"]
        HWLIB["HWLIB\n(cfe_psp_port.c)"]
        PSP["PSP / OSAL\n(OS_TranslatePath,\nOS_OpenCreate,\nOS_PortRead/Write)"]
    end

    subgraph NOS_ENG["NOS Engine (nos3_sc_1 network)"]
        NE_I2C["NOS Engine I2C\nTCP bus emulation"]
        NE_UART["NOS Engine UART\nTCP bus emulation"]
        NE_SPI["NOS Engine SPI\nTCP bus emulation"]
        NE_CAN["NOS Engine CAN\nTCP bus emulation"]
    end

    subgraph SIM_HW["Hardware Simulators"]
        S_EPS["EPS Sim\n(solar, battery, power)"]
        S_GPS["GPS Sim\n(Novatel OEM6)"]
        S_RW["Reaction Wheel Sim"]
        S_RADIO["Radio Sim"]
        S_42["42 Dynamics\n(attitude/ephemeris)"]
    end

    %% Scheduler drives wakeups
    TIME -->|"1 Hz tone MID 0x1811"| SCH
    SCH -->|"wakeup MIDs"| EPS_APP & GPS_APP & RW_APP & RADIO_APP & ADCS_APP & LC & SC & MD & HK & HS & CS

    %% SB at center
    EPS_APP & GPS_APP & RW_APP & RADIO_APP & ADCS_APP -->|"HK TLM packets\n(publish to SB)"| SB
    CI -->|"CCSDS CMD uplink\n(publish to SB)"| SB
    SB -->|"route by MID\n(subscribe/dispatch)"| TO & DS & LC & SC & MD & HS & CS & HK & MM & EPS_APP & GPS_APP & RW_APP & RADIO_APP & ADCS_APP
    SB -->|"EVS events\n0x0808"| EVS
    EVS -->|"filtered events"| DS & TO

    %% EVS -> cfs_evs.log
    EVS -->|"text output\n/cf/cfs_evs.log"| EVLOG[("cfs_evs.log\nomni_logs/")]

    %% Component apps to HWLIB
    EPS_APP & GPS_APP & RW_APP & RADIO_APP & ADCS_APP & CSS_APP & IMU_APP & MAG_APP & ST_APP & THR_APP & TRQ_APP & CAM_APP -->|"OS_Read/Write\nPortRead/Write"| HWLIB
    HWLIB --> PSP
    PSP -->|"I2C"| NE_I2C
    PSP -->|"UART"| NE_UART
    PSP -->|"SPI"| NE_SPI
    PSP -->|"CAN"| NE_CAN
    NE_I2C --- S_EPS
    NE_UART --- S_GPS
    NE_SPI --- S_RW
    NE_CAN --- S_RADIO
    S_42 -->|"UDP attitude feed"| ADCS_APP

    %% TO egress
    TO -->|"CCSDS TLM frames\nUDP :5010"| COSMOS_GND[("COSMOS\nGround Station")]

    style CFE_CORE fill:#0a1628,stroke:#1a5fa8,color:#9cf
    style SCHED fill:#1a1a0a,stroke:#a8a81a,color:#ffc
    style HERITAGE fill:#1a0a28,stroke:#8a1aa8,color:#daf
    style COMPONENT fill:#0a2814,stroke:#1aa84a,color:#afc
    style HWLIB_LAYER fill:#281a0a,stroke:#a85a1a,color:#fca
    style NOS_ENG fill:#1a0a0a,stroke:#a81a1a,color:#faa
    style SIM_HW fill:#0a1a1a,stroke:#1a8a8a,color:#aff
```

### Technical Explanation

**Software Bus routing model.** The cFS Software Bus (`CFE_SB`) implements a publish-subscribe
message router. Every message is identified by a 16-bit Message ID (MID) encoded in the CCSDS
Primary Header. Commands carry MIDs in the range `0x1xxx`; telemetry MIDs are in `0x0xxx`. An
app calls `CFE_SB_TransmitMsg()` to publish; subscriber apps call `CFE_SB_ReceiveBuffer()` on
named pipe handles. There is no sender-identity field, no ownership model, and no
authorization check on which app may publish a given MID. Any app can publish any MID.

**Scheduler-driven execution.** `SCH_LAB` sends wakeup message IDs at configured rates (1 Hz, 4 Hz,
etc.) derived from its schedule table loaded at startup via `CFE_TBL`. Component apps block on
`CFE_SB_ReceiveBuffer()` and run only when the scheduler delivers their wakeup MID. This fixed
cadence means that any MID injected between two scheduler ticks can be the last packet a
subscriber sees before it evaluates that MID's data.

**HWLIB and PSP abstraction.** Component FSW apps call `HWLIB_*` functions which delegate to the
Platform Support Package (`PSP`). In the NOS3 environment, the PSP routes I/O through NOS Engine
TCP sockets to hardware simulators. The same call path is used for both simulation and
hardware-in-the-loop testing. Critically, `cfe_psp_port_direct.c` casts all port addresses to
non-`volatile` pointers, meaning compiler optimizations (e.g., LICM) can hoist reads out of
loops, creating stale-cache hardware-blinding conditions under aggressive optimization.

**EVS event logging.** `CFE_EVS` accepts formatted event messages from all apps and can route
them to multiple output ports: the Software Bus (`CFE_EVS_LONG_EVENT_MSG_MID 0x0808`), a local
binary log file, and stdout. The `cfs_evs.log` file in `omni_logs/` is the primary source of
application-level narrative events in the ELK pipeline.

---

## 4. Diagram 3 — Telemetry, Log, and Metric Pipeline

```mermaid
sequenceDiagram
    autonumber
    box NOS3/cFS Container
        participant SCH as SCH_APP<br/>(Scheduler)
        participant EPS as GENERIC_EPS<br/>(Component App)
        participant SB as CFE_SB<br/>(Software Bus)
        participant EVS as CFE_EVS<br/>(Event Services)
        participant TO as TO_APP<br/>(Telemetry Output)
        participant DS as DS<br/>(Data Storage)
    end
    box Shared Host Volumes
        participant GODVIEW as attack_logs/<br/>cfs_god_view.json
        participant EVLOG as omni_logs/<br/>cfs_evs.log
        participant SIMLOG as omni_logs/<br/>eps_sim.log
    end
    box nos3-logstash Container
        participant LS as Logstash<br/>(Pipeline)
    end
    box nos3-elasticsearch Container
        participant ES as Elasticsearch<br/>:9200
    end
    box nos3-kibana Container
        participant KIB as Kibana<br/>:5601
    end
    box COSMOS Ground Station
        participant COSMOS as COSMOS<br/>(god-view script)
    end

    Note over SCH,TO: Normal 1 Hz cycle
    SCH->>EPS: GENERIC_EPS_REQ_HK_MID (0x191B) wakeup
    EPS->>EPS: Poll I2C via HWLIB (NOS Engine TCP)
    EPS->>SB: TransmitMsg(GENERIC_EPS_HK_TLM_MID 0x091A)<br/>payload: solar_power_w, battery_wh, battery_mv, etc.
    SB->>TO: Route 0x091A to TO subscription pipe
    SB->>DS: Route 0x091A to DS subscription pipe
    SB->>EVS: Publish event if HK anomaly (0x0808)
    TO->>TO: Pack CCSDS frame
    TO-->>COSMOS: UDP :5010 — CCSDS TLM packet
    COSMOS->>GODVIEW: Append NDJSON line<br/>{"log_type":"cfs_sb","msg_id":"0x091A","length":N}
    EVS->>EVLOG: Write text event line<br/>TIMESTAMP [INFO] - EPS::HK: battery_mv=12450

    Note over GODVIEW,LS: Logstash file-tail loop (continuous, no sincedb)
    LS->>GODVIEW: file plugin tail /attack_logs/cfs_god_view.json
    LS->>LS: codec => json (parse NDJSON)
    LS->>LS: translate: msg_id -> msg_name<br/>"0x091A" -> "GENERIC_EPS_HK_TLM_MID"
    LS->>LS: translate: msg_id -> layer = "COMPONENT"
    LS->>LS: translate: msg_id -> subsystem = "EPS"
    LS->>LS: translate: msg_id -> msg_direction = "TLM"
    LS->>LS: mutate: convert sequence to integer
    LS->>ES: HTTP POST /nos3-telemetry-YYYY.MM.dd/_doc<br/>Content-Type: application/json

    LS->>EVLOG: file plugin tail /omni_logs/cfs_evs.log
    LS->>SIMLOG: file plugin tail /omni_logs/eps_sim.log
    LS->>LS: grok: parse TIMESTAMP [LEVEL] - Component::Method: message
    LS->>LS: mutate: extract eps_battery_mv, eps_solar_power_w (regex)
    LS->>ES: HTTP POST /nos3-telemetry-YYYY.MM.dd/_doc

    Note over ES,KIB: Analyst query cycle
    KIB->>ES: GET /nos3-telemetry-*/_search<br/>query: subsystem=EPS, range: last 5m
    ES-->>KIB: JSON hits (eps_battery_mv time series)
    KIB->>KIB: Render Lens visualization<br/>time-series chart
```

### Technical Explanation

**Two distinct data planes.** The pipeline ingests from two structurally different sources that
together give a complete picture:

- `cfs_god_view.json` (type=`software_bus`): produced by the COSMOS god-view script, which
  subscribes to every known TLM definition via the COSMOS API and serializes each received packet
  as one NDJSON line. The key fields are `msg_id` (hex string), `sequence` (CCSDS Primary
  Sequence Count), `length` (bytes), and `timestamp`. The MID-to-name translation table in
  Logstash covers every MID used by this mission, mapping them into human-readable `msg_name`,
  `layer`, `subsystem`, and `msg_direction` fields.

- `omni_logs/*.log` (type=`system_log`): one file per NOS3 simulator component plus auxiliary
  files (`cpu_monitor.log`, `cfs_evs.log`). These are plain-text files with the format
  `TIMESTAMP [LEVEL] - Component::Method:  message`. Logstash parses these with grok patterns
  and uses regex `mutate` filters to extract numeric telemetry values (e.g., `eps_battery_mv`,
  `rw_momentum`, `gps_lat`) as typed Elasticsearch fields suitable for Kibana visualizations.

**Index time-series model.** Elasticsearch receives documents via HTTP POST to a date-rolled index
(`nos3-telemetry-YYYY.MM.dd`). No Beats agents are deployed; Logstash acts as both shipper and
transformer. The absence of an in-container Beats agent is architecturally significant: there is
no process inside the FSW container that could be killed to interrupt data flow. Instead, an
attacker must tamper with the shared host volume files or exhaust the Software Bus to prevent
packets from reaching the god-view script at the ground station.

**Sequence counter forensics.** The `sequence` field (CCSDS Primary Sequence Count, 14-bit
auto-incrementing per source app) is indexed as an integer. A spoofed packet injected by a
foreign app resets to PSC=0, producing a non-monotonic jump mid-stream. This jump is the primary
forensic artifact of an in-process spoof attack and is queryable in Elasticsearch, though no
automated Kibana alert currently fires on it.

---

## 5. Diagram 4 — Attack Scenario 1: Telemetry Spoofing / Command Injection

**Threat model:** An attacker has code execution inside the NOS3 FSW container (e.g., via a
compromised cFS app loaded as a shared library, or a research app like `noisy_app` deliberately
introduced for testing). The attacker publishes a forged EPS housekeeping telemetry packet with
a false battery voltage designed to suppress LC watchpoint #0 and mask a real low-battery
condition from the health monitoring system.

```mermaid
graph TD
    subgraph ATTACKER_APP["Malicious App: noisy_app (inside FSW container)"]
        direction LR
        A1["CFE_MSG_Init()\nMsgId=GENERIC_EPS_HK_TLM_MID 0x091A\nPSC reset to 0"]
        A2["Set BatteryVoltage = 0x3000\n(12288 mV — falsely healthy)"]
        A3["CFE_SB_TransmitMsg()\nNo authorization check\nSB accepts any publisher"]
    end

    subgraph SB_CORE["CFE_SB Software Bus"]
        SB_ROUTE["Route by MID 0x091A\nto all subscribers"]
        SB_LEGIT["Legitimate EPS packet\nPSC=N, battery_mv=9800\n(real low-battery)"]
        SB_SPOOF["Spoofed EPS packet\nPSC=0, battery_mv=12288\n(false healthy)"]
    end

    subgraph LC_EVAL["LC (Limit Checker) — Watchpoint Evaluation"]
        WP0["WP#0: battery_mv < 10000\nResult on REAL packet: LC_WATCH_TRUE (trip)"]
        WP0_SPOOF["WP#0: battery_mv < 10000\nResult on SPOOFED packet: LC_WATCH_FALSE (pass)"]
        AP0["AP#0: WP#0 true for 3 consecutive cycles\nFire RTS#10 -> safe mode"]
        AP0_SUPPRESS["AP#0: WP#0 = FALSE\nSafe mode NOT triggered\nBattery drains silently"]
    end

    subgraph COSMOS_GS["COSMOS Ground Station"]
        COSMOS_DISP["COSMOS TLM display\nShows LAST received packet\n-> Displays spoofed 12288 mV"]
    end

    subgraph ELK_DETECT["ELK Observability Layer"]
        GOD_LOG["attack_logs/cfs_god_view.json\nBOTH packets logged:\n...seq=500... seq=0... seq=501..."]
        LS_PARSE["Logstash: extracts sequence\nas integer field"]
        ES_IDX["Elasticsearch index:\nTwo docs with msg_id=0x091A\nsequence jump: 501 -> 0 -> 502"]
        KIB_VIZ["Kibana: eps_battery_mv time-series\nSpike visible if analyst queries\nNO automated alert configured"]
        KIB_ALERT["[DETECTION GAP]\nNo Kibana alert on\nnon-monotonic PSC\nManual query required:\nsequence:0 AND msg_id:0x091A"]
    end

    subgraph REAL_EPS["Real EPS App (victim)"]
        EPS_REAL["GENERIC_EPS publishes\nauthentic HK at 1 Hz\nPSC auto-increment by cFE"]
    end

    EPS_REAL -->|"PSC=N, battery_mv=9800\n(genuine low battery)"| SB_LEGIT
    SB_LEGIT -->|"arrives at LC first in SCH frame"| WP0

    A1 --> A2 --> A3
    A3 -->|"noisy_app runs at higher SCH priority\nPublishes AFTER real EPS in same tick"| SB_SPOOF
    SB_SPOOF -->|"LC evaluates LAST packet before wakeup"| WP0_SPOOF

    WP0 -->|"if no spoof: trip after 3 cycles"| AP0
    AP0 -->|"RTS#10 command sequence\nto SC_APP"| SAFE_MODE[/"Safe Mode Activated"/]
    WP0_SPOOF -->|"spoof present: always PASS"| AP0_SUPPRESS

    SB_LEGIT & SB_SPOOF -->|"TO_APP forwards both to COSMOS"| COSMOS_DISP
    SB_LEGIT & SB_SPOOF -->|"COSMOS writes both to god-view"| GOD_LOG

    GOD_LOG --> LS_PARSE --> ES_IDX --> KIB_VIZ --> KIB_ALERT

    style ATTACKER_APP fill:#3a0a0a,stroke:#ff3333,color:#faa
    style LC_EVAL fill:#1a1a3a,stroke:#5555ff,color:#bbf
    style ELK_DETECT fill:#1a3a1a,stroke:#33ff33,color:#afa
    style COSMOS_GS fill:#1a2a3a,stroke:#3399ff,color:#acf
    style KIB_ALERT fill:#3a2a0a,stroke:#ffaa33,color:#fca
```

### Technical Explanation

**Root cause — no publisher authorization.** `CFE_SB_TransmitMsg()` in cFE Draco 7.0.0-rc4
validates only: message pointer non-NULL, message size meets minimum header size, and MsgId
within configured space (`CFE_MISSION_SB_MAX_PIPE_DEPTH`). There is no ownership registry
associating a MsgId with a specific app. Any app can call `CFE_MSG_Init()` with
`GENERIC_EPS_HK_TLM_MID` and `CFE_SB_TransmitMsg()` immediately after. The bus accepts and
routes the packet identically to a legitimate EPS HK packet.

**Temporal exploitation.** The `noisy_app` research app is assigned scheduler priority 20 —
higher than the EPS component app. Within a single 1 Hz SCH frame, the EPS app publishes its
authentic HK packet first, then `noisy_app` publishes the spoofed packet. LC evaluates the
last packet received on its subscription pipe before the LC wakeup MID fires. By placing the
spoof after the authentic packet in the same tick, the spoofed value wins every evaluation.

**LC watchpoint suppression mechanics.** LC watchpoint #0 compares
`GENERIC_EPS_HK_TLM_MID.Payload.BatteryVoltage` against threshold 10000 mV. With
`BatteryVoltage=0x3000` (12288), the result is `LC_WATCH_FALSE` (no trip). After the configured
`ResultsAgeWhenStale` (4 cycles), a stale result is treated as `LC_WATCH_PASS`, so even a flood
that drops all EPS packets also suppresses the alert.

**CCSDS sequence counter artifact.** `CFE_MSG_Init()` resets the CCSDS Primary Sequence Count
to 0. The real EPS app's sequence counter auto-increments continuously. The god-view log will
contain: `...sequence=500, sequence=501, sequence=0, sequence=502...` — the reset to 0
mid-stream is the canonical forensic signature of this attack and is queryable in Elasticsearch
but is not alerted by any configured Kibana rule.

---

## 6. Diagram 5 — Attack Scenario 2: Software Bus Resource Exhaustion (DoS)

**Threat model:** A malicious or compromised cFS app floods the Software Bus with oversized
packets across all valid MIDs, exhausting the single static 512 KB shared buffer pool.
All legitimate sensor HK packets fail to allocate; the LC health monitor goes blind.

```mermaid
graph TD
    subgraph FLOOD_APP["Attack App: noisy_app (MID Flood)"]
        F1["Loop: MID 0x0000 to 0x1FFF\n(8,192 MIDs)"]
        F2["Per MID: 4 TransmitMsg calls\n= 32,768 total attempts"]
        F3["Payload: 4,096 bytes\n+ 8-byte CCSDS header\n= 4,104 bytes per packet"]
    end

    subgraph SB_POOL["CFE_SB Global Buffer Pool\n(512 KB static arena)"]
        POOL_MATH["Pool math:\n524,288 / (4,104 + ~8 alloc header)\n= ~127 packets to exhaustion"]
        POOL_EXHAUST["Pool EXHAUSTED\nat ~MID 0x0020\n(8,160 remaining MIDs\nget CFE_SB_BUF_ALOC_ERR)"]
        PIPE_LIMIT["CFE_PLATFORM_SB_DEFAULT_MSG_LIMIT=4\n(irrelevant: pool is the bottleneck)"]
    end

    subgraph VICTIMS["Legitimate App Transmission (Starvation)"]
        EPS_TX["GENERIC_EPS TransmitMsg\nReturn: CFE_SB_BUF_ALOC_ERR\nPacket silently DROPPED\n(return code unchecked)"]
        GPS_TX["NOVATEL_GPS TransmitMsg\nReturn: CFE_SB_BUF_ALOC_ERR\nPacket silently DROPPED"]
        RW_TX["GENERIC_RW TransmitMsg\nReturn: CFE_SB_BUF_ALOC_ERR\nPacket silently DROPPED"]
    end

    subgraph LC_BLIND["LC Health Monitor (Blinded)"]
        STALE["After 4 LC wakeup cycles\n(ResultsAgeWhenStale=4):\nAll WPs -> LC_WATCH_STALE"]
        STALE_PASS["LC_WATCH_STALE treated as\nLC_WATCH_PASS\nAll APs: no action"]
        MONITOR_BLIND["Health monitoring BLIND\nduring entire flood duration"]
    end

    subgraph EVS_SILENCE["EVS / Alerting (Silenced)"]
        EVS_FILTER["CFE_SB_Q_FULL_ERR_EID\nfiltered at cpu1_platform_cfg.h:261\n-> ZERO EVS events emitted\nduring pool exhaustion"]
        NO_EVS["No events in cfs_evs.log\nNo events in ELK\nFlood is invisible to EVS"]
    end

    subgraph REBOUND["Post-Flood Rebound (Hazard)"]
        POOL_DRAIN["Subscribers consume queued messages\nPool gradually reclaims space"]
        BURST["Pent-up HK burst:\nEPS + RW + GPS publish simultaneously"]
        MULTI_WP["Multiple WPs fire in same LC cycle\nif hardware degraded during flood"]
        SC_RACE["SC RTS engine single-threaded\nInterleaved contradictory commands\nNon-deterministic execution"]
    end

    subgraph ELK_OBS["ELK Observability"]
        CPU_LOG["cpu_monitor.log:\nTOTAL_CPU_PCT spike\nNUM_PIDS unchanged (no new process)"]
        GOD_GAP["cfs_god_view.json GAP:\nEPS/GPS/RW MIDs absent from log\nduring flood window"]
        KIB_CPU["Kibana: TOTAL_CPU_PCT\ntime-series shows spike\n(indirect indicator)"]
        KIB_GAP["Kibana: msg_id count-over-time\nEPS/GPS/RW drop to 0\nduring flood window\n(strongest indicator)"]
    end

    F1 --> F2 --> F3
    F3 -->|"First ~128 calls succeed\nfill pool"| POOL_MATH
    POOL_MATH --> POOL_EXHAUST
    POOL_EXHAUST --> PIPE_LIMIT

    POOL_EXHAUST -->|"SB rejects new allocs"| EPS_TX & GPS_TX & RW_TX
    EPS_TX & GPS_TX & RW_TX -->|"HK packets never reach LC pipe"| STALE
    STALE --> STALE_PASS --> MONITOR_BLIND

    POOL_EXHAUST -->|"CFE_SB_BUF_ALOC_ERR\nnot reportable via EVS\n(filtered by platform cfg)"| EVS_FILTER
    EVS_FILTER --> NO_EVS

    POOL_DRAIN --> BURST --> MULTI_WP --> SC_RACE

    EPS_TX & GPS_TX & RW_TX -->|"Telemetry gap visible in god-view"| GOD_GAP
    F3 -->|"CPU load from tight loop"| CPU_LOG
    CPU_LOG --> KIB_CPU
    GOD_GAP --> KIB_GAP

    style FLOOD_APP fill:#3a0a0a,stroke:#ff3333,color:#faa
    style SB_POOL fill:#1a0a2a,stroke:#aa33ff,color:#daf
    style VICTIMS fill:#0a1a2a,stroke:#3366ff,color:#acf
    style LC_BLIND fill:#1a1a0a,stroke:#aaaa00,color:#ffc
    style EVS_SILENCE fill:#2a0a0a,stroke:#ff6633,color:#fca
    style REBOUND fill:#2a1a0a,stroke:#ff9933,color:#fda
    style ELK_OBS fill:#0a2a0a,stroke:#33cc33,color:#afa
```

### Technical Explanation

**Pool architecture.** cFE SB uses a single static memory arena of
`CFE_PLATFORM_SB_BUF_MEMORY_BYTES = 524,288` bytes (512 KB) allocated at startup. All in-flight
messages from all apps, across all pipes and all MID subscriptions, draw from this single pool.
There is no per-app or per-MID quota enforcement at the pool level.

**Flood packet sizing.** The `noisy_app` attack allocates packets of `sizeof(NOISY_APP_Pkt_t)` =
4,096-byte payload + 8-byte CCSDS header = 4,104 bytes per packet. Including an approximately
8-byte internal allocation header, each allocation claims ~4,112 bytes. Pool exhaustion
occurs after approximately 127 packets, which is reached before the MID loop increments past
`0x0020` — leaving 8,160 MIDs worth of `TransmitMsg()` calls returning
`CFE_SB_BUF_ALOC_ERR` silently.

**Per-pipe limit bypass.** `CFE_PLATFORM_SB_DEFAULT_MSG_LIMIT = 4` limits how many messages of a
single MID can queue on a single pipe. This limit is never the binding constraint here because
the global pool is exhausted first. The per-pipe limit is effectively bypassed as a side-effect
of the pool flood geometry.

**EVS silence.** The critical evasion property of this attack is that
`CFE_SB_Q_FULL_ERR_EID` is filtered at `cpu1_platform_cfg.h` line 261. No EVS events are
emitted during pool exhaustion. The `cfs_evs.log` file and therefore the ELK event timeline
show no anomaly during the flood. The only observable artifacts are: (1) a CPU usage spike in
`cpu_monitor.log` from the tight transmission loop, and (2) a gap in `cfs_god_view.json` where
EPS/GPS/RW MIDs stop appearing, because TO_APP itself cannot allocate buffer to forward their
packets to the ground station.

**Post-flood rebound hazard.** When the flood app exits and queued messages are consumed,
the pool recovers. Pent-up sensor HK publications arrive simultaneously. If real hardware
degradation occurred during the blind period (e.g., actual low battery), multiple LC
watchpoints trip in the same cycle, firing multiple Actionpoints. The SC stored-command RTS
engine is single-threaded; interleaved contradictory RTS commands (e.g., safe-mode entry
and a science mode resume) produce non-deterministic execution order.

---

## 7. Diagram 6 — Attack Scenario 3: Log Tampering and Observability Evasion

**Threat model:** An attacker with host-level write access to the Docker bind-mount volumes
(i.e., having escaped the FSW container or compromised the build host) tampers with log files to
poison or blind the ELK index while executing a payload. The attack leverages
CVE-2025-25371 (OSAL path traversal, confirmed VULNERABLE) to read or write arbitrary host files
via authenticated CCSDS commands to the MM (Memory Manager) app.

```mermaid
graph TD
    subgraph ATTACKER["Attacker (Host or Container Escape)"]
        ATK_ENTRY["Entry vector options:\n1. CVE-2025-25371 — OSAL path traversal\n   via MM_LOAD_MEM_FROM_FILE_CC\n   FileName=/mnt/../../attack_logs/x\n2. Direct host volume write\n   (container escape / bind-mount access)\n3. Compromised cFS app at startup"]
        ATK_GOAL["Goal: Blind ELK while\nexecuting payload undetected"]
    end

    subgraph CVE_DETAIL["CVE-2025-25371: OSAL Path Traversal (VULNERABLE)"]
        direction TB
        MM_CMD["Attacker sends CCSDS CMD:\nMM_LOAD_MEM_FROM_FILE_CC (0x1888)\nFileName = /mnt/../../attack_logs/poison.json"]
        OSAL_TRANS["OS_TranslatePath(path):\nChecks: length OK, starts with /, has /, in mount table\nDoes NOT strip ../ sequences\nTranslates to: <sysmountpt>/../../attack_logs/poison.json"]
        MM_READ["MM reads attacker-controlled file\ninto flight RAM — arbitrary memory write\nvia authenticated uplink"]
        ALT_DUMP["Alt: MM_DUMP_MEM_TO_FILE_CC\nDump flight RAM to arbitrary host path\n-> Overwrite omni_logs/ or attack_logs/"]
    end

    subgraph LOG_TAMPER["Log Tampering Operations"]
        direction TB
        TRUNC["Option A: Truncate cfs_god_view.json\n-> ELK re-reads from beginning\n   but sincedb_path=/dev/null means\n   Logstash re-ingests entire file\n   Truncation causes gap in timeline"]
        INJECT["Option B: Inject false NDJSON lines\n{'msg_id':'0x191A','sequence':999,'length':8}\n-> Pollutes ES index with phantom commands\n   confuses forensic timeline"]
        CORRUPT["Option C: Corrupt logstash.conf structure\nor inject logstash filter bypass\n(requires host logstash config write)"]
        EVLOG_WIPE["Option D: Overwrite cfs_evs.log\nwith whitespace / benign events\n-> EVS event history erased from ELK"]
    end

    subgraph PAYLOAD["Simultaneous Payload Execution"]
        direction TB
        MM_POKE["MM_POKE_MEM_CC: write arbitrary value\nto in-memory cFS table or app state\n(post-CVE-2025-25371 read-then-write)"]
        SC_RTS["Inject SC RTS sequence via SB\n(no authorization on SC_CMD 0x18A9)\nExecute arbitrary stored command block"]
        OSAL_TRAV2["MM_SYM_TBL_TO_FILE_CC:\nExfiltrate symbol table\n-> Enumerate all app load addresses\n   for further ROP/exploitation"]
    end

    subgraph ELK_BLIND["ELK Observability (Blinded)"]
        ES_POISON["Elasticsearch index contains:\n- Injected phantom events\n- Gaps from truncated logs\n- No reliable forensic baseline"]
        KIB_CONFUSED["Kibana dashboards:\n- msg_id counts misleading\n- Timeline has artificial gaps\n- Analyst cannot distinguish\n  real from injected events"]
        DETECTION["Residual detection:\n- File mtime on host changes\n  (not monitored by current ELK)\n- Logstash file plugin restarts\n  on inode change\n- No file-integrity monitoring (FIM)\n  deployed in current config"]
    end

    subgraph MITIGATIONS["Defense Gaps (Current Configuration)"]
        M1["[GAP] No file-integrity monitoring\non bind-mounted volumes"]
        M2["[GAP] Logstash sincedb_path=/dev/null\nmeans tampered files are re-indexed\nwithout error on every restart"]
        M3["[GAP] xpack.security.enabled=false\nin Elasticsearch — no auth on :9200\nAny container on nos3-core can\ndelete/modify the index directly"]
        M4["[GAP] CVE-2025-25371 unpatched —\nOSAL OS_TranslatePath does not\nstrip ../ sequences"]
        M5["[GAP] No Kibana alerts on\nlog volume anomalies (drops/spikes)"]
    end

    ATK_ENTRY --> ATK_GOAL
    ATK_GOAL --> MM_CMD & LOG_TAMPER

    MM_CMD --> OSAL_TRANS --> MM_READ --> MM_POKE
    MM_CMD --> OSAL_TRANS --> ALT_DUMP --> TRUNC & INJECT & EVLOG_WIPE

    ATK_GOAL --> CORRUPT
    ATK_GOAL --> INJECT

    MM_POKE --> SC_RTS
    MM_READ --> OSAL_TRAV2

    TRUNC & INJECT & CORRUPT & EVLOG_WIPE --> ES_POISON --> KIB_CONFUSED
    KIB_CONFUSED --> DETECTION

    M1 & M2 & M3 & M4 & M5 --> DETECTION

    style ATTACKER fill:#3a0a0a,stroke:#ff3333,color:#faa
    style CVE_DETAIL fill:#2a0a2a,stroke:#cc33cc,color:#eae
    style LOG_TAMPER fill:#2a1a0a,stroke:#ff9933,color:#fda
    style PAYLOAD fill:#1a0a3a,stroke:#6633ff,color:#ccf
    style ELK_BLIND fill:#0a0a2a,stroke:#3333ff,color:#aaf
    style MITIGATIONS fill:#3a2a0a,stroke:#ffcc33,color:#ffc
```

### Technical Explanation

**CVE-2025-25371 — OSAL path traversal.** `OS_TranslatePath()` in
`osal/src/os/posix/os-impl-filesys.c` performs four checks: path length within `OS_MAX_PATH_LEN`
(64), path starts with `/`, path contains `/`, and path prefix is in the virtual-to-physical
mount table. It does NOT canonicalize the path or strip `../` sequences. An attacker with
authenticated CCSDS uplink access (i.e., access to the CI_LAB UDP port 5011 or the COSMOS
command interface) can craft `FileName = /mnt/../../attack_logs/evil.json` which passes all four
checks and resolves to an arbitrary host path reachable from the FSW container.

Three MM command codes exploit this surface:
- `MM_LOAD_MEM_FROM_FILE_CC` (`0x1888`+CC=2): reads attacker-controlled file content into
  arbitrary flight RAM — equivalent to an arbitrary memory write primitive.
- `MM_DUMP_MEM_TO_FILE_CC` (`0x1888`+CC=3): writes a range of flight RAM to an attacker-
  specified file path — arbitrary host file write.
- `MM_SYM_TBL_TO_FILE_CC` (`0x1888`+CC=5): dumps the cFS symbol table (all app load addresses,
  global variables) to a file — information disclosure enabling further exploitation.

**Log poisoning mechanics.** The Logstash pipeline uses `sincedb_path => /dev/null`, which
disables position tracking. On every container restart, Logstash re-reads both log files from
byte 0. This is intentional for research replay but creates a vulnerability: an attacker who
truncates `cfs_god_view.json` and replaces it with a crafted version will have their fabricated
events fully indexed on the next Logstash restart. Injected NDJSON lines with plausible MIDs
and sequences are indistinguishable from authentic records in Kibana.

**Elasticsearch authentication gap.** The ELK stack is deployed with `xpack.security.enabled=false`
(docker-compose.yml line 8). Any container on the `nos3-core` Docker network can issue
unauthenticated HTTP DELETE or POST requests to Elasticsearch port 9200. A compromised COSMOS
container could delete the entire `nos3-telemetry-*` index family, permanently destroying the
forensic record, without touching any host filesystem.

**No file-integrity monitoring.** There is no Auditd, AIDE, or Filebeat with FIM module
monitoring the bind-mount volumes. Changes to `attack_logs/` or `omni_logs/` on the host are
not detected by any component of the current observability stack.

---

## 8. CCSDS Message ID Quick Reference

Commands use MID range `0x1xxx`; telemetry uses `0x0xxx`.

### cFE Core Services

| App | CMD MID | TLM HK MID | Notes |
|---|---|---|---|
| CFE_ES | 0x1806 | 0x0800 | App lifecycle, memory stats |
| CFE_EVS | 0x1801 | 0x0801 | Event services; long event: 0x0808 |
| CFE_SB | 0x1803 | 0x0803 | Pipe stats: 0x080A |
| CFE_TBL | 0x1804 | 0x0804 | Table registry: 0x080C |
| CFE_TIME | 0x1805 | 0x0805 | 1 Hz tone: 0x1811 |

### Heritage Apps

| App | CMD MID | HK TLM | Notes |
|---|---|---|---|
| SCH_APP | 0x1895 | 0x0897 | Schedule table driver |
| TO_APP | 0x1880 | 0x0880 | UDP egress :5010 |
| CI_LAB | 0x18E0 | 0x08E0 | UDP ingress :5011 |
| DS | 0x18BB | 0x08B8 | File data storage |
| FM | 0x188C | 0x088A | File manager |
| MM | 0x1888 | 0x0887 | Memory manager (CVE-2025-25371 surface) |
| LC | 0x18A4 | 0x08A7 | Limit checker / watchpoints |
| SC | 0x18A9 | 0x08AA | Stored commands / RTS |
| MD | 0x1890 | 0x0890 | Memory dwell |
| HS | 0x18AE | 0x08AD | Health monitor |

### Component Apps

| App | CMD MID | HK TLM | Bus |
|---|---|---|---|
| GENERIC_EPS | 0x191A | 0x091A | I2C |
| NOVATEL_GPS | 0x1870 | 0x0870 | UART |
| GENERIC_RW | 0x1992 | 0x0993 | SPI |
| GENERIC_RADIO | 0x1930 | 0x0930 | CAN |
| GENERIC_ADCS | 0x1940 | 0x0940 | UDP (42 dynamics) |
| GENERIC_CSS | 0x1910 | 0x0910 | — |
| GENERIC_IMU | 0x1925 | 0x0925 | — |
| GENERIC_MAG | 0x192A | 0x092A | — |
| GENERIC_STAR_TRACKER | 0x1935 | 0x0935 | — |
| GENERIC_THRUSTER | 0x18EA | 0x08EA | — |
| GENERIC_TORQUER | 0x193A | 0x093A | — |
| ARDUCAM | 0x18C8 | 0x08C8 | — |
| BEACON_APP | 0x18F0 | 0x08F0 | RESEARCH layer |
| SAMPLE_APP | 0x18FA | 0x08FA | COMPONENT layer |

---

## 9. Vulnerability Summary

| ID | Category | Severity | Status | Attack Scenario |
|---|---|---|---|---|
| CVE-2025-25371 | OSAL path traversal | High | VULNERABLE | Scenario 3 (log tamper, exfil) |
| CVE-2025-25372 | MM DoS/segfault | Medium | PATCHED | — |
| CVE-2025-25373 | MM RCE | Critical | PATCHED | — |
| H-SB-AUTH | No SB publisher auth | High | Architectural (no fix) | Scenario 1 (telemetry spoof) |
| H-SB-POOL | SB pool exhaustion | High | Architectural (no fix) | Scenario 2 (DoS) |
| H-ES-NOAUTH | ES unauthenticated | High | Config gap | Scenario 3 (index wipe) |
| H-ELK-FIM | No file-integrity monitoring | Medium | Config gap | Scenario 3 |
| H-EVS-FILTER | EVS alloc errors filtered | Medium | Config gap | Scenario 2 |
| H-SEQ-NOALERT | No monotonic PSC alert | Low | Detection gap | Scenario 1 |
| H-PSC-LICM | PSP non-volatile port access | Medium | Code defect | Scenario 4 (ADCS blinding) |

### Detection Coverage Matrix

| Attack | CPU Spike (Kibana) | MID Gap (Kibana) | PSC Jump (ES query) | EVS Event | FIM Alert |
|---|---|---|---|---|---|
| Scenario 1 — Spoof | No | No | Yes (manual) | No | No |
| Scenario 2 — DoS | Yes | Yes | No | No | No |
| Scenario 3 — Log Tamper | No | Partial | No | No | No |

All three scenarios demonstrate that the current ELK pipeline provides reactive, analyst-driven
detection only. No automated alerting rules are configured for any of the attack-specific
indicators identified in this document.
