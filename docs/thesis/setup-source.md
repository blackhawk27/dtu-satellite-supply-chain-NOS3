# Setup 1 source material: legacy cFS 6.7.99 on Ubuntu 22

Structured source material for the "Setup" chapter of the thesis. The
thesis writer is expected to draft prose from this file; nothing in
this document is itself thesis prose. Anchors are 7-char commit hashes
plus `path:line` references against the working tree at HEAD.

Truth ordering used throughout: code at HEAD > build / config that
actually runs > recent commits touching the file > `docs/thesis/` >
inline comments / README / commit messages. Claims sourced only from
the lower tiers are tagged in section 12.


## 1. Identity and framing

Setup 1 is the legacy-cFS baseline of the DTU supply-chain satellite
testbed. The repository is a fork of NASA's NOS3 (NASA Operational
Simulator for Space Systems) flattened at commit `55ad214` (subject
`chore: imported and flattened NOS3 environment with all submodules`,
date 2026-03-18). HEAD at the time of writing is `076dad6` (subject
`fix(elk): seed MID-registry YAMLs so Logstash starts on fresh
clones`, date 2026-05-12); 71 commits separate the two. The cFE
version macros in this fork report `CFE_MAJOR_VERSION=6`,
`CFE_MINOR_VERSION=7`, `CFE_REVISION=99`
(`nos3/fsw/cfe/modules/core_api/fsw/inc/cfe_version.h:33-35`); the
`CFE_BUILD_BASELINE` string is `v7.0.0-rc4` and the
`CFE_VERSION_STRING` macro names the codename as Draco
(`cfe_version.h:30`, `:63-64`). The thesis frames this as "legacy
cFS 6.7.99" on the strength of the major/minor/revision triple;
the build still pre-dates the Draco redesign at the macro level
(no `TOPICID_TO_MIDV` indirection, legacy MID macros, legacy CMake
helpers), even though the baseline tag is a Draco release
candidate. Verified against HEAD on 2026-05-14. The flight-software
image is cFS (not F-prime), the ground software is OpenC3 /
COSMOS 4.5 (selected in `nos3/cfg/nos3-mission.xml`), and the
spacecraft profile is `spacecraft/sc-mission-config.xml`.

The setup matters to the thesis because it is the unchanged-trust
baseline. The cFS Software Bus has no per-publisher authentication,
the FSW-to-GSW link has two independent paths (one CryptoLib-wrapped,
one direct UDP), and the build process registers any cFS app placed
under `fsw/apps/` into the boot image. Setup 2 (cFS Draco 7.0.0,
sibling repo) modernises the cFE; Setup 3 (Draco + ZTA on RTEMS) adds
mutual authentication. Setup 1 gives the reader the unhardened
reference against which the later setups' deltas can be measured.


## 2. Architectural overview

### Process inventory

Every runtime process lives inside a Docker container. There are no
host-resident services. Containers are split across two Docker
bridge networks:

- `nos3-legacy-core`: shared core network, named by the
  `$NETWORK_NAME` variable (default `nos3-legacy-core`, set in
  `elk/.env`; the `nos3-legacy-` prefix lets the RTEMS / Draco /
  Legacy testbeds coexist without a name collision). Created on
  demand by `scripts/ci_launch.sh:66-71` via `$NETWORK_NAME` with
  subnet `192.168.41.0/24`; `make start-elk` (`nos3/Makefile:362`)
  also ensures it exists, and it is declared `external: true` in
  `elk/docker-compose.yml`. Hosts the COSMOS operator container; the
  three NOS Engine helper containers (`nos-terminal`,
  `nos-udp-terminal`, `nos-sim-bridge`, `nos-time-driver`), started
  here and later dual-homed onto the spacecraft network; and the
  three ELK containers.
- `nos3-sc01`: per-spacecraft network. Created by
  `scripts/ci_launch.sh:184-188` as `SC_NET="nos3-${SC_NUM}"` where
  `SC_NUM=sc01`. Hosts the FSW container, the 42 dynamics container,
  CryptoLib, OnAIR, the NOS Engine server, the 42 truth-bus sim
  (`truth42sim`), and 17 hardware simulator containers
  (`ci_launch.sh:235-241`).

Container catalogue at steady state (verified against
`scripts/ci_launch.sh:78-269`):

- ELK stack on the shared `nos3-legacy-core` network
  (`elk/docker-compose.yml`):
  `nos3-legacy-elasticsearch` (image `docker.elastic.co/elasticsearch:7.17.10`,
  single-node, `xpack.security.enabled=false`, named volume
  `es_data_vol`), `nos3-legacy-logstash` (same registry, 7.17.10, bind
  mounts `attack_logs`, `omni_logs`, and `cfg/build/elk` for the
  `translate` filter dictionaries), and `nos3-legacy-kibana` (7.17.10).
- Ground software: `cosmos-openc3-operator-1` (image
  `ballaerospace/cosmos:4.5.0`, headless run loop started via inline
  Ruby in `ci_launch.sh:98-102`), `cryptolib` standalone built from
  `components/cryptolib/` and run as `sc01-cryptolib`.
- NOS Engine helpers on `nos3-legacy-core` (with `--alias` attachments
  onto `nos3-sc01`): `nos-time-driver`, `nos-terminal`,
  `nos-udp-terminal`, `nos-sim-bridge`.
- Spacecraft side on `nos3-sc01`: `sc01-fortytwo` (42 dynamics),
  `sc01-nos-fsw` (cFS image), `sc01-onair`, `sc01-cryptolib`,
  `sc01-nos-engine-server`, `sc01-truth42sim`, and 16 simulator
  containers named `sc01-<sim>` for `camsim`, `generic-css-sim`,
  `generic-eps-sim` (bind-mounts `attack_logs:ro` for the
  per-app-load EPS model), `generic-fss-sim`, `gps` (Novatel),
  `generic-gnss-sim`, `generic-imu-sim`, `generic-mag-sim`,
  `generic-reactionwheel-sim0`, `generic-reactionwheel-sim1`,
  `generic-reactionwheel-sim2`, `generic-radio-sim`,
  `generic-tt_c-sim`, `sample-sim`, `generic-star-tracker-sim`,
  `generic-thruster-sim`, `generic-torquer-sim`, `blackboard-sim`.

Note: `ci_launch.sh:235-241` starts all 17 sims unconditionally
regardless of `<enable>` in the spacecraft profile. Components
disabled in `cfg/spacecraft/sc-mission-config.xml` (`syn`, `thruster`,
`arducam`) either have no container started in the loop above
(`syn`, `arducam` not in the list) or exit early on read of their
mission XML (`generic-thruster-sim`).

### FSW app inventory

Inside the cFS image (`sc01-nos-fsw`), the app set is the union of
two startup-script blocks. The build copy at
`cfg/build/nos3_defs/cpu1_cfe_es_startup.scr` is what cFE actually
parses; the source copy at `cfg/nos3_defs/cpu1_cfe_es_startup.scr`
is what `make config` derives from.

Active `CFE_APP` entries (auto-loaded on boot, verified against the
build-time copy): `sch`, `ci`, `to`, `ci_lab`, `to_lab`,
`beacon_app`, `hs`, `hk`, `cs`, `generic_tt_c`, `generic_gnss`, `md`,
`mm`, `cf`, `ds`, `fm`, `lc`, `sbn`, `sc`, `generic_adcs` (twice),
`generic_css`, `generic_eps`, `generic_fss`, `novatel_oem615`,
`generic_imu`, `mgr`, `generic_mag`, `generic_radio`, `generic_rw`,
`sample`, `generic_st`, `generic_torquer`, `arducam`, plus the
`hwlib`, `io_lib`, `cryptolib`, `cfs_lib` libraries.

Startup-script row keyword (`cpu1_cfe_es_startup.scr`, both the
source copy under `cfg/nos3_defs/` and the build copy under
`cfg/build/nos3_defs/`): the keyword is a simple toggle, `CFE_APP`
means the app loads on boot and `OFF_APP` means it is present in the
image but not loaded. The preceding `OFF_APP, ===_DTU_THESIS_MENU_===`
row is only a visual separator/marker, not a conditional. As shipped,
the `noisy_app` row reads `CFE_APP`, so the malicious app loads on
every boot; flipping its row to `OFF_APP` disables it. The
`cpu_killer` row is `OFF_APP` (and its source does not exist in this
tree).

This matches the framing in
`docs/thesis/04-apps/noisy_app.md:29-32` and
`docs/thesis/02-architecture.md:145-149`, both of which state that
`noisy_app` is loaded on every launch.

### Trust boundaries

Four boundaries are distinguishable at the architectural level.

1. Host to Docker daemon. Trusted. The user must be in the `docker`
   group; the build container reads `/etc/passwd` and `/etc/group`
   read-only (`scripts/env.sh:53`).
2. Container to container, inside Docker bridges. No authentication
   on either bridge; the host's kernel iptables / bridge-netfilter
   path is silently destructive when
   `net.bridge.bridge-nf-call-iptables=1` and is reasserted to `0`
   on every launch (`scripts/ci_launch.sh:62-65`).
3. Flight software to ground software. Has two paths, one
   CryptoLib-wrapped and one direct UDP. Both are configured in
   `gsw/cosmos/config/tools/cmd_tlm_server/cmd_tlm_server.txt`. See
   section 3 and section 12 (b) for the difference between this and
   the docs claim that CryptoLib is the only path.
4. Inside the cFS Software Bus. No publisher identity, no
   per-message MAC, no sequence-counter validation, no rate limit.
   `CFE_SB_TransmitMsg` is callable from any in-process task. This
   is the boundary the supply-chain threat model attacks.

### Figure F1 candidate

The "system overview" figure should draw both Docker networks with
the dual-homed containers (COSMOS, NOS Engine helpers) straddling
the boundary, every simulator container as a separate box on the
`nos3-sc01` side, and the four host-side capture scripts feeding
the `nos3-legacy-logstash` bind-mount. The current docs draft for Figure
F1 (`docs/thesis/08-figures/figures.md:14-78`) draws cFS as the
dual-homed container; the correct dual-homing target is COSMOS, not
cFS (`scripts/ci_launch.sh:78-103` starts the COSMOS container with
`--network=$NETWORK_NAME` (the shared `nos3-legacy-core`) and
`ci_launch.sh:192` then connects it to
`nos3-sc01`; the FSW container at `ci_launch.sh:204-208` is only on
`nos3-sc01`).


## 3. Command-flow tour

A single representative end-to-end command: operator sends a
`BEACON_APP_PING_CC` (function code 2) on MID `0x18F0` from COSMOS
script-runner to the spacecraft, the FSW dispatches it through
`beacon_app`, `beacon_app` publishes a "pong" telemetry packet on
`BEACON_APP_HK_TLM_MID` (`0x08F0`), and the packet arrives in
Elasticsearch as a `software_bus` document. This is purely the
legitimate-command example; `noisy_app`'s covert arming is a
separate path that rides the CI_LAB carrier MID `0x18E0` (see
section 4.2), not this `beacon_app` MID.

Hops, in order, with the code path at each step:

1. **Operator browser -> COSMOS web UI**. COSMOS is reachable on
   `localhost:2900` (the Traefik fronted route published by the
   COSMOS compose). Authentication is COSMOS's own; the host-side
   port is loopback only.
2. **COSMOS Ruby script-runner -> cosmos-openc3-operator-1 cmd
   pipeline**. The `BEACON_APP CMD with PING_CC` packet is created
   inside the COSMOS server based on the target definitions in
   `nos3/gsw/cosmos/config/targets/BEACON_APP/cmd_tlm/`.
3. **COSMOS -> CI_LAB (direct UDP)**. The COSMOS DEBUG interface
   (`cmd_tlm_server.txt:6`,
   `INTERFACE DEBUG udp_interface.rb nos-fsw 5012 5013`) sends a
   CCSDS Space Packet over UDP to `nos-fsw:5012`. No CryptoLib in
   this path. The packet is plain CCSDS over UDP.
4. **(Parallel) COSMOS -> CryptoLib -> cFS (RADIO path)**. The
   COSMOS RADIO interface (`cmd_tlm_server.txt:27`,
   `INTERFACE RADIO udp_interface.rb cryptolib 6010 6011`) sends to
   `cryptolib:6010` (`CRYPTO_RX_GROUND_PORT`, declared in
   `scripts/env.sh:66`). CryptoLib wraps the packet in a CCSDS
   Space Data Link frame (TM/AOS, depending on configuration),
   applies the configured Security Association (SA), and forwards to
   `cryptolib:8010` (`CRYPTO_TX_RADIO_PORT`,
   `scripts/env.sh:68`), from where the cFS radio side decodes.
5. **`ci_lab` UDP listener**. `ci_lab` opens
   `DefaultListenPort = CI_LAB_BASE_UDP_PORT + CFE_PSP_GetProcessorId() - 1`
   (`fsw/apps/ci_lab/fsw/src/ci_lab_app.c:153`). With
   `CFE_PSP_GetProcessorId()` returning 1 on this build and
   `CI_LAB_BASE_UDP_PORT = 5012`
   (`fsw/apps/ci_lab/fsw/src/ci_lab_app.h:40`), the port is 5012.
6. **`ci_lab` republishes onto the Software Bus**. The decoded
   packet is fed to `CFE_SB_TransmitMsg` with its embedded MID
   intact. From the Software Bus's perspective, the publisher is
   `ci_lab` regardless of which ground station the packet
   originated from (and regardless of whether CryptoLib touched it
   on the wire).
7. **Software Bus routing**. The Software Bus indexes pipes by
   MID. `beacon_app` (`fsw/apps/beacon_app/fsw/src/beacon_app.c`)
   subscribes to MID `0x18F0` and cFE delivers the packet to its
   pipe. (`noisy_app` does NOT subscribe to `0x18F0`; it subscribes
   to its own `0x18F2` and to the CI_LAB carrier `0x18E0`.)
8. **`beacon_app` dispatch**. The handler in
   `beacon_app.c:57-92` reads the CCSDS function code, branches on
   it, increments counters, and calls `BEACON_APP_SendHk` which
   publishes the HK telemetry under MID `0x08F0`.
9. **`noisy_app` is not on this path**. This beacon ping does not
   reach `noisy_app`. `noisy_app` arms only when a covert opcode
   byte rides the CI_LAB carrier (`0x18E0`), dispatched in
   `NOISY_APP_RunScenario` (see section 4.2). It is listed here
   only to make explicit that the malicious app is decoupled from
   the legitimate beacon command.
10. **HK telemetry leaves the bus via `to_lab`**.
    `nos3/cfg/nos3_defs/tables/to_lab_sub.c:285-287` subscribes
    `to_lab` to `BEACON_APP_CMD_MID`, `BEACON_APP_SEND_HK_MID`, and
    `BEACON_APP_HK_TLM_MID`. `to_lab` writes the HK packet as CCSDS
    Space Packet over UDP to `host:5013` (`cfgTLM_PORT = 5013` in
    `fsw/apps/to_lab/fsw/src/to_lab_app.h:52`).
11. **`god_view_capture.py` reads UDP 5013**. The script binds
    `LISTEN_HOST = "0.0.0.0"` on `LISTEN_PORT = 5013`
    (`scripts/god_view_capture.py:59-60`) and writes one JSON line
    per packet to `attack_logs/cfs_god_view.json`. The same script
    also issues `TO_LAB_OUTPUT_ENABLE_CC` commands directly to
    `nos-fsw:5012` (`scripts/god_view_capture.py:58, :359`) so it
    is not a passive observer; it is the active configurator of
    the TO_LAB output target. It mirrors received packets to
    `127.0.0.1:5113` (default `NOS3_TLM_MIRROR_DEST`,
    `scripts/god_view_capture.py:71-81`), which `ci_launch.sh`
    publishes as the COSMOS DEBUG read socket so COSMOS still
    receives telemetry.
12. **Logstash tails the file**. The bind mount
    `../attack_logs:/attack_logs:ro` (`elk/docker-compose.yml`)
    makes the JSON file readable by the `nos3-legacy-logstash` container.
    The `file` input in `elk/logstash.conf` tails it with
    `sincedb_path => "/dev/null"` so a Logstash restart re-reads
    from the beginning.
13. **Filter chain**. The MID `0x18F0` document gets `mid_layer`
    and `mid_subsystem` enriched from the YAML dictionaries under
    `cfg/build/elk/`, the `beacon_cmd` tag added at
    `logstash.conf:159-161`, and either `attack_loaded`,
    `attack_armed`, or `attack_trigger_ping` depending on the EVS
    message regex (`logstash.conf:327-339`).
14. **Elasticsearch index**. Output writes to
    `nos3-telemetry-%{+YYYY.MM.dd}`; the daily index is created on
    first write.
15. **Kibana dashboard**. The "Cyber-Physical Attack Radar"
    dashboard (one of the twenty saved objects built by
    `elk/build_kibana_dashboards.py`) renders the document on the
    EVS-tagged timeline panel.

Interception / failure surfaces, by hop:

- Hop 3 (DEBUG path): plain UDP, no authentication. A host with
  reach to `nos-fsw:5012` injects arbitrary commands.
- Hop 4 (RADIO path): CryptoLib SDLP-wrapped. Replay protection is
  by sequence counter inside the SA; an attacker who reads a
  captured frame cannot replay it after the SA window closes.
- Hop 7 (Software Bus): no provenance. A second subscriber or
  publisher on any command MID (for example `noisy_app` sniffing
  the CI_LAB carrier `0x18E0`) is indistinguishable from the
  legitimate ground operator; the bus enforces no per-publisher
  identity.
- Hop 11 (UDP 5013): a host that races `god_view_capture.py` for
  the same port wins the bind and silently drops the
  `cfs_god_view.json` stream.
- Hop 12 (file bind mount): a writer with file access can append
  forged documents into `cfs_god_view.json`, which Logstash will
  ingest at face value.

### Figure F2 candidate (sequence)

A six-lane sequence figure (operator, COSMOS, CryptoLib, CI_LAB,
Software Bus, TO_LAB / god_view) ordered top-to-bottom. Each arrow
should be labelled with the wire format (CCSDS SPP, CCSDS SDLP,
plain UDP) and either "plaintext" or "authenticated". The split
between hop 3 and hop 4 is the visual point.


## 4. Component-by-component detail

Entries cover apps that are actually present in the FSW build (any
`CFE_APP` line in the generated startup script) or in the simulator
launcher (`scripts/ci_launch.sh:235-241`). Each entry records the
verified path, MID allocation, divergence status against `55ad214`,
and the load-bearing invariants the present code expects to remain
true.

Verification convention: every MID is grepped against the
component's `platform_inc/<name>_msgids.h` at HEAD; every commit
hash is `git log` against the file in question.


### 4.1 cFE core services and heritage cFS apps

cFE 6.7.99 services (`cfe_es`, `cfe_evs`, `cfe_sb`, `cfe_tbl`,
`cfe_time`) are byte-identical to the import baseline aside from one
DTU revert (`1ab8a90`, "refactor(cfe sb): remove inline JSON
god-view fopen from SB transmit path"). That commit removed an
earlier experiment in which `cfe_sb` itself wrote
`cfs_god_view.json` from the transmit path; the post-`1ab8a90`
arrangement keeps cFE clean and moves all capture to the host-side
`god_view_capture.py`. cFE startup script in source at
`cfg/nos3_defs/cpu1_cfe_es_startup.scr`; build copy at
`cfg/build/nos3_defs/cpu1_cfe_es_startup.scr`.

The cFS heritage suite present in this build, with status against
the baseline:

- `sch` (scheduler). Tables `sch_def_schtbl.c` and
  `sch_def_msgtbl.c` extended to drive HK, HS, CS, MM, MD, LC, DS,
  FM, SC, plus per-component HK requests. Commits `2111b90`,
  `b505580`, `6bba473`. Status: upstream-modified.
- `ci_lab` and `to_lab`. Upstream-as-is on the application side.
  Subscription table is mission-level
  (`nos3/cfg/nos3_defs/tables/to_lab_sub.c`, 380 lines, generated
  copy in `cfg/build/`); commit `8cad7cc` widened it to enumerate
  every component MID so the full bus surface is visible in
  `cfs_god_view.json`. Telemetry port is `cfgTLM_PORT = 5013`
  (`fsw/apps/to_lab/fsw/src/to_lab_app.h:52`). Listen port for
  commands is `CI_LAB_BASE_UDP_PORT = 5012`
  (`fsw/apps/ci_lab/fsw/src/ci_lab_app.h:40`).
- `ci`. Separate command-ingest app present in the build. The
  `CI_TRANSPORT` variable selects which `fsw/examples/<transport>/`
  sub-tree is compiled in
  (`fsw/apps/ci/CMakeLists.txt:17`,
  `aux_source_directory(fsw/examples/${CI_TRANSPORT} ...)`); for
  the `amd64-nos3` and `amd64-posix` toolchains the value is
  `udp_tf` (`cfg/build/nos3_defs/toolchain-amd64-nos3.cmake:33`,
  `cfg/nos3_defs/toolchain-amd64-nos3.cmake:33`). The selected
  header `fsw/apps/ci/fsw/examples/udp_tf/ci_platform_cfg.h:44`
  defines `CI_CUSTOM_UDP_PORT = 5010`, and the matching
  `fsw/examples/udp_tf/ci_custom.c:141` writes it into the
  socket. Verified at HEAD on 2026-05-14: `ci` binds UDP 5010 in
  the production build. The research attack scripts under
  `scripts/attack/` target the same port
  (`scripts/attack/attack_noisy_trigger.py:102`,
  `attack_md_jam.py:183`, `attack_syn_leak.py:102`).
- `hs` and `hk`. Source restored from upstream submodules in
  commit `3b45603`; SCH wiring corrected in `b505580`.
  Status: upstream-as-is now, but the restoration commit is the
  fork-specific marker because the import flattening left them as
  empty directories.
- `cs`, `mm`, `md`. Same restoration pattern (`7670998`). Three
  follow-on commits port them off Draco APIs back to legacy cFE:
  `67799bf99` and `b03227f60` substitute
  `generate_config_includefile` for the Draco-only
  `generate_configfile_set`; `b2947fe` ports `cs` off Draco APIs;
  `9309e3a` ports `md` and `mm` MID definitions off the Draco
  `TOPICID_TO_MIDV` indirection back to hardcoded MIDs;
  `449e904` replaces `OS_strnlen` with `strnlen` in `mm`;
  `1bb69bf` restores the missing `default_md_tbl.h` umbrella
  header; `af0fc6f` disables the on-power checksum tables in `cs`
  because the upstream defaults assumed 32-bit cFE and trigger
  alignment faults on the 64-bit `amd64-nos3` build.
- `cf`, `ds`, `fm`, `lc`, `sbn`, `sbn_client`, `sc`. Source
  restored from upstream submodules in `f7ac1730`. The `lc` and
  `sc` definition tables now drive an automated safe-mode response
  (commit `bdf010d`); the active watchpoint set is
  `cfg/nos3_defs/tables/lc_def_wdt.c` (2232 lines) and the
  stored-command tables are `cfg/nos3_defs/tables/sc_rts*.c` (17
  RTS files: `sc_rts001.c..003.c`, `005.c`, `025.c..037.c`). Chain
  shape, verified at HEAD on 2026-05-14: `lc_def_wdt.c` declares
  watchpoints over HK MIDs from `generic_eps`, `generic_gnss`,
  `novatel_oem615`, `mgr`, and `sample`
  (`lc_def_wdt.c:40-51`). `sc_rts001.c` is the Power-On Reset
  (POR) sequence run at boot: it enables RTS 2-64, starts RTS 3
  (Safe Mode), enables Science Transition RTS 26, enables
  Science_Reboot to Science Mode RTS 25, and starts RTS 37
  (`sc_rts001.c:33-160`). `sc_rts002.c` is the SCIENCE-entry RTS
  fired by an LC actionpoint on Denmark FOV entry. The
  `LC_AP -> SC_RTS` chain is real, not aspirational. Verified
  against HEAD on 2026-05-14.

### 4.2 `noisy_app` (DTU-added)

- Source: `nos3/fsw/apps/noisy_app/fsw/src/noisy_app.c`. The
  authoritative wire surface and per-opcode behaviour live in
  `docs/thesis/04-apps/noisy_app.md`; the attack analyses live in
  `docs/security/`. This entry is the setup-level summary.
- Purpose: model a compromised cFS payload that hides on a benign
  carrier command (the covert-opcode "piggyback" pattern). As
  shipped its startup-script row reads `CFE_APP`, so it loads on
  every boot (see section 1 of
  `cfg/nos3_defs/cpu1_cfe_es_startup.scr`); the row is a toggle,
  and flipping it to `OFF_APP` disables loading.
- MIDs consumed: its own command MID
  `NOISY_APP_CMD_MID = 0x18F2` (direct path), the CI_LAB carrier
  `CARRIER_CMD_MID = 0x18E0` (the sniffed piggyback channel), and
  `GENERIC_EPS_HK_TLM_MID = 0x091A` (read for the EPS-spoof
  scenario). It does NOT subscribe to `beacon_app`'s `0x18F0`.
- Trigger: it sniffs the carrier MID. A normal header-only CI_LAB
  NOOP is ignored; a NOOP that is LONGER than a header-only NOOP
  carries a trailing covert opcode byte, which dispatches a
  scenario in `NOISY_APP_RunScenario`. Because the legitimate
  carrier app still validates its own command length, the extra
  byte is invisible to it.
- Covert opcodes (the trailing byte): `0x00` clear, `0x02`
  EPS_SPOOF (forge one low-battery EPS HK packet, BatteryVoltage
  10000 mV), `0x04` SB_BURST, `0x06` EPS_OVERRIDE, `0x08` SB_FLOOD
  (gated pool-lock DoS), `0x0A` IMU_BIAS, `0x0C` GNSS teleport,
  `0x0E` GNSS drift. See `docs/thesis/04-apps/noisy_app.md` for the
  exact effects, EVS event ids, and the cross-app memory-write
  technique (`/proc/self/maps` + `dlopen`).
- Status: new-in-this-repo. The current implementation is the
  piggyback sniffer ported from the Draco baseline (see
  `docs/debug/attack-poc-integration.md`); it replaced an earlier
  legacy broadcast-storm version.
- Detection: the live Logstash signature tags for the current
  piggyback scenarios are defined in `elk/logstash.conf`:
  `piggyback_opcode` (EVS "piggyback opcode"), `eps_spoof` (EVS
  "EPS HK SPOOF" / "EPS override ENGAGED"), `sb_pool_lock` (EVS
  "SB POOL LOCK"), and `noisy_app_spam_target` (keyed on the seven
  pipe-saturation target MIDs). The conf also still carries the
  legacy lifecycle regexes (`attack_loaded`, `attack_armed`,
  `attack_trigger_ping`) from the earlier broadcast-storm app;
  the current noisy_app does not emit those EVS strings, so those
  tags no longer fire.

### 4.3 `beacon_app` (DTU-added)

- Source: `nos3/fsw/apps/beacon_app/fsw/src/beacon_app.c`,
  headers under `fsw/platform_inc/`.
- MIDs (`fsw/platform_inc/beacon_app_msgids.h:4-6`):
  - `BEACON_APP_CMD_MID = 0x18F0` (command input).
  - `BEACON_APP_SEND_HK_MID = 0x18F1` (HK request).
  - `BEACON_APP_HK_TLM_MID = 0x08F0` (HK telemetry).
- Function codes: `NOOP_CC = 0`, `RESET_COUNTERS_CC = 1`,
  `PING_CC = 2` (`beacon_app.c:8-10`).
- Purpose in the threat model: legitimate ground-commandable
  ping / pong. Shares its command MID with `noisy_app`'s trigger so
  a single unauthenticated ping uplink arms the attacker without
  needing a custom attacker-side command.
- Status: new-in-this-repo. Commit `90e024c`. The HK output emits
  `BEACON_APP PINGCOUNTER=... CMDCOUNTER=... ERRCOUNTER=...` as a
  key=value EVS string, which `nos3/elk/logstash.conf`'s `kv`
  filter (added in `c1fbebe`) indexes as Kibana fields.

### 4.4 `mgr` (mission manager; upstream component, DTU-modified)

- Source: `nos3/components/mgr/fsw/cfs/src/mgr_app.c` plus
  `mgr_msg.h`, `mgr_app.h`, `platform_inc/mgr_msgids.h`,
  `platform_inc/mgr_platform_cfg.h`.
- MIDs (`platform_inc/mgr_msgids.h`):
  - `MGR_CMD_MID = 0x18F8`.
  - `MGR_REQ_HK_MID = 0x18F9`.
  - `MGR_HK_TLM_MID = 0x08F8`.
- Mode constants (`mgr_app.h`): `MGR_SAFE_MODE = 1`,
  `MGR_SAFE_REBOOT_MODE = 2`, `MGR_SCIENCE_MODE = 3`,
  `MGR_SCIENCE_REBOOT_MODE = 4`. Verified via grep but file not
  re-listed here; the docs entry
  (`docs/thesis/04-apps/mgr.md:26-37`) is correct against code.
- Persistence: writes mode to `/data/mgr.bin`
  (`MGR_CFG_FILE_PATH` in `platform_inc/mgr_platform_cfg.h`).
  `make launch` wipes this file (`Makefile:324`) on every start.
- Status: upstream-modified. Single DTU commit `8161e16`:
  short-circuit SET_MODE when target == current to suppress
  duplicate EVS, and revert the reboot-resume policy to SAFE.
  Earlier SCH-table bundle `2111b907` also touched mgr's HK
  cadence.

### 4.5 `blackboard` (upstream component, DTU-modified)

- Source: `nos3/components/blackboard/sim/src/blackboard_data_point.cpp`
  (the changed file) and siblings.
- Wire surface: 42 IPC port 4285 (`Inp_IPC.txt:160-170`); single
  TX prefix `"SC[0]"`. The blackboard does not own a Software Bus
  MID range; it is a simulator-side data-exchange service.
- Status: upstream-modified. Commit `a0d796f`. Three changes to
  `blackboard_data_point.cpp`:
  1. File-static `stof_safe` helper that strips `,`, `[`, and `]`
     before calling `std::stof` and returns 0.0 on whitespace-only
     input (`blackboard_data_point.cpp:8`).
  2. Empty-frame deferral guard in `do_parsing` that probes
     `SC[N].svb`, returns early with a DEBUG log line if the
     frame is empty (`blackboard_data_point.cpp:27-44`). This is
     a load-bearing pattern; the inline guard at those lines is
     itself the canonical statement of the invariant (commit
     `a0d796f`).
  3. Vector fields go through `parse_double_vector`; scalar
     fields use `stof_safe` (`blackboard_data_point.cpp:49-160`).
- Load-bearing invariants (verified at HEAD): empty-frame
  deferral must remain an early `return`, not a `throw`
  (`blackboard_data_point.cpp:27-44`); `stof_safe` must strip
  all three of `,`, `[`, `]` (`blackboard_data_point.cpp:8`);
  vector fields must continue to use `parse_double_vector`
  (`blackboard_data_point.cpp:49-160`).

### 4.6 `generic_gnss` (DTU-added)

- Source: `nos3/components/generic_gnss/` (entire subtree).
- MIDs (`fsw/cfs/platform_inc/generic_gnss_msgids.h`):
  - `GENERIC_GNSS_CMD_MID = 0x1952`.
  - `GENERIC_GNSS_REQ_HK_MID = 0x1953`.
  - `GENERIC_GNSS_HK_TLM_MID = 0x0952`.
  - `GENERIC_GNSS_DEVICE_TLM_MID = 0x0953`.
  - `GENERIC_GNSS_SAT_HELLO_TLM_MID = 0x0954`.
- Simulator wire: 42 IPC port 4287 (`Inp_IPC.txt:186-198`); single
  TX prefix `"SC[0].GPS[0].PosW"`; count `1`.
- Parser pattern (`sim/src/generic_gnss_data_point.cpp:17-37`):
  computes the key, reads the value from the data point, defers
  on empty input with a DEBUG log, then strips `,`, `[`, `]` by
  in-place character substitution (`generic_gnss_data_point.cpp:28`)
  and parses with a `stringstream` (not `parse_double_vector`).
  See section 12 (b) for the docs claim vs. code reality.
- Status: new-in-this-repo. Initial `ee347c91`. FSW source
  restored on `main` after a merge mishap in `f766be0`.
- Load-bearing invariants: port 4287 prefix narrowed to a single
  position string so 42's non-blocking `write()` does not exit on
  `EAGAIN`; deferral on empty input must remain a return, not a
  throw; the five MIDs are referenced by `to_lab_sub.c:141-142`
  and `:371-373` and the Logstash `gps_*` extractors.

### 4.7 `generic_tt_c` (DTU-added)

- Source: `nos3/components/generic_tt_c/` (entire subtree).
- MIDs (`fsw/cfs/platform_inc/generic_tt_c_msgids.h`):
  - `GENERIC_TT_C_CMD_MID = 0x1950`.
  - `GENERIC_TT_C_REQ_HK_MID = 0x1951`.
  - `GENERIC_TT_C_HK_TLM_MID = 0x0950`.
  - `GENERIC_TT_C_DEVICE_TLM_MID = 0x0951`.
- Simulator wire: 42 IPC port 4286 (`Inp_IPC.txt:172-184`); two TX
  prefixes `"SC[0].GPS[0].PosW"` and `"SC[0].GPS[0].VelW"`;
  count `2`.
- Parser pattern
  (`sim/src/generic_tt_c_data_point.cpp:18-48`): same as the GNSS
  parser, with both keys read and both deferred-on-empty
  (`generic_tt_c_data_point.cpp:30`); manual bracket strip
  (`generic_tt_c_data_point.cpp:35-36`) into stringstreams.
- Status: new-in-this-repo. Initial `2b7dad4`. FSW source
  restored on `main` after a merge mishap in `db78b8b`.

### 4.8 `generic_eps` (upstream component, DTU-modified)

- Source: `nos3/components/generic_eps/`.
- MIDs (`generic_eps_msgids.h:25`):
  `GENERIC_EPS_HK_TLM_MID = 0x091A`. Command MIDs in `0x191A` /
  `0x191B`.
- HK struct (`generic_eps_msg.h:60-61`):
  `GENERIC_EPS_Hk_tlm_t` with the standard cFE telemetry header
  plus a `GENERIC_EPS_Device_HK_tlm_t DeviceHK` field. The
  `BatteryVoltage` member is what `noisy_app` overwrites with
  a forged low-battery reading of 10000 mV.
- Status: upstream-modified.
  - `02f08c6` (`feat(generic_eps): drive sim power state from
    per-app/per-MID load model`): EPS sim now draws its power
    model from per-app activity inferred from
    `attack_logs/cfs_god_view.json` bind-mounted read-only into
    the sim container (`ci_launch.sh:252-256`).
  - `59b1cbf` (`fix: resolve EPS sim race condition causing
    battery overflow on first tick`): defer first publish to the
    second tick.

### 4.9 `generic_adcs` (upstream component, unmodified)

- Source: `nos3/components/generic_adcs/`.
- MIDs (`fsw/cfs/platform_inc/generic_adcs_msgids.h`):
  - `GENERIC_ADCS_CMD_MID = 0x1940`.
  - `GENERIC_ADCS_REQ_HK_MID = 0x1941`.
  - `GENERIC_ADCS_ADAC_UPDATE_MID = 0x1942`.
  - `GENERIC_ADCS_HK_TLM_MID = 0x0940`.
  - `GENERIC_ADCS_DI_MID = 0x0941`.
  - `GENERIC_ADCS_AD_MID = 0x0942`.
  - `GENERIC_ADCS_GNC_MID = 0x0943`.
  - `GENERIC_ADCS_AC_MID = 0x0944`.
  - `GENERIC_ADCS_DO_MID = 0x0945`.
- Status: upstream-as-is at the source level. The LICM
  hardware-blinding hazard under `-O2` is a build-flag concern,
  not a code change; this fork builds at `-O0` (Makefile default
  `BUILDTYPE = debug`, `nos3/Makefile:6` and `:49-51`), so the
  hazard does not arise in the default build.

### 4.10 `generic_star_tracker` (upstream component, DTU-modified)

- Status: upstream-modified. Commit `c08b016`
  (`fix(generic_star_tracker): emit unit quaternion for synthetic
  ST telemetry`). With the `_42_PROVIDER` variant disabled, the
  sim emits a unit quaternion as its synthetic output instead of
  waiting for an IPC line that never arrives. The IPC mode for
  port 4282 is `OFF` in `cfg/InOut/Inp_IPC.txt:142`.

### 4.11 `generic_reaction_wheel` (upstream component, DTU-modified)

- Status: upstream-modified. Commit `19cebab`
  (`fix(generic_rw): defer parsing on empty 42 frame, normalize
  line endings`). Same defensive-parser pattern as the TT&C and
  GNSS parsers.

### 4.12 Other simulators

The remaining `components/` subtrees (`arducam`, `cryptolib`,
`generic_css`, `generic_fss`, `generic_imu`, `generic_mag`,
`generic_radio`, `generic_torquer`, `generic_thruster`,
`novatel_oem615`, `onair`, `sample`, `syn`) are upstream-as-is at
the source level. CryptoLib is built standalone with the flags
recorded in `Makefile:79`
(`-DSTANDALONE_TCP=1 -DSA_INTERNAL=1 -DMC_INTERNAL=1
-DKEY_INTERNAL=1 -DCRYPTO_LIBGCRYPT=1 -DSA_FILE=OFF`).
`syn` is disabled in `sc-mission-config.xml:72-74` and is not
loaded; the architecture doc claim that it is load-bearing is in
section 12 (b).

### 4.13 Ground software (COSMOS)

- Compose: `gsw/cosmos/config/tools/cmd_tlm_server/cmd_tlm_server.txt`
  declares two UDP interfaces (DEBUG and RADIO) plus the
  `SIM_42_TRUTH_INT` and `SIM_CMDBUS_BRIDGE` interfaces.
- DEBUG path: `udp_interface.rb nos-fsw 5012 5013`. This bypasses
  CryptoLib and gives COSMOS direct access to `ci_lab` and
  `to_lab`. Multiple COSMOS targets (CI_DEBUG, CFS, CMD_UTIL, and
  the 14 GENERIC_* targets plus MGR, NOVATEL_OEM615, SAMPLE,
  TO_DEBUG, GENERIC_ADCS) ride this interface.
- RADIO path: `udp_interface.rb cryptolib 6010 6011`. The
  corresponding `..._RADIO` targets ride this. CryptoLib's other
  end forwards to FSW on `8010`/`8011`.
- DTU additions in COSMOS config: GENERIC_GNSS target and
  ground-responder background task (commit `eb80ddd4`);
  SIM_CMDBUS_BRIDGE target (`b9786e9`, `68641ded`, `be5a6bb9`);
  CFDP and `gnss_gs_responder_task.rb` background tasks (declared
  at the top of `cmd_tlm_server.txt`).

### 4.14 ELK pipeline

- Compose: `elk/docker-compose.yml`. Three services pinned to
  `7.17.10`, attached to the shared external network
  `nos3-legacy-core` (= `$NETWORK_NAME`, created by
  `scripts/ci_launch.sh` or `make start-elk`), one named volume
  `es_data_vol`.
- Logstash pipeline: `elk/logstash.conf`, 1421 lines.
  - 4 input file globs: `cfs_god_view.json` (`software_bus`),
    `omni_logs/*.log` excluding `tlm_hk_decoded.log`
    (`system_log`), `tlm_hk_decoded.log` (`hk_decoded`),
    `gnss_uplinks.log` (`gs_uplink`).
  - Three `translate` filters resolve `msg_id` -> `msg_name`,
    `mid_layer`, `mid_subsystem` via dictionaries at
    `/etc/logstash/mid/`.
  - Numeric extractors for EPS, GPS, RW, CPU monitor (lines vary;
    docs/thesis/05-elk/01-logstash-filters.md is correct at the
    block-level shape).
  - Attack tags (line numbers verified against HEAD on
    2026-05-14): `noisy_app_spam_target` block at
    `logstash.conf:150-156` keyed on 7 MIDs (the `add_tag` is on
    line 155); `beacon_cmd` block at `:158-161`; the
    `attack_armed` / `attack_trigger_ping` / `attack_loaded`
    block at `:326-338` keyed on EVS regex (add_tag lines 330,
    333, 336); `sb_pipe_overflow` at `:347`.
- Dashboard builder: `elk/build_kibana_dashboards.py` plus
  `refresh_index_pattern_fields.py`. Run on every `make launch`
  (`Makefile:350-351`).
- Seed registry: `nos3/elk/seed_mid/` contains
  `mid_to_name.yml`, `mid_to_layer.yml`, `mid_to_subsystem.yml`
  (181 lines each, verified `wc -l`). The Makefile fallback in
  `start-elk` copies these into `cfg/build/elk/` if
  `scripts/mid/gen_mid_registry.py` fails (`Makefile:375-384`).
- Diagnostic: `make doctor` -> `scripts/elk_doctor.sh`, 7 layers
  starting from build-time prerequisites
  (`scripts/elk_doctor.sh:40-58`).
- Capture scripts: `scripts/god_view_capture.py` (619 lines),
  `scripts/cfs_evs_capture.sh`, `scripts/sim_logs_capture.sh`,
  `scripts/cpu_monitor.sh`.


## 5. Build and deploy

Reference host: Linux box (developed inside a VS Code Remote
Containers devcontainer on WSL2; native Linux works equivalently)
with Docker installed, the user in the `docker` group, ~60 GB free
disk, and ~8 GB RAM. X11 is needed only for the COSMOS GUI launch;
`NOS3_HEADLESS=1` and the default headless launch path skip it.

Clone-to-running, mapped to actual Makefile targets and the
backing scripts:

1. `git clone <remote-url>` (any user-chosen directory).
2. `cd nos3 && make prep`. Runs `scripts/cfg/prepare.sh`, which
   pulls the pinned build image (`$DCALL image pull $DBOX`,
   `ivvitc/nos3-64:20251107`, `scripts/env.sh:59`), populates the
   per-user `~/.nos3/` directories, and builds the 42 dynamics
   engine inline (the host-side 42 install directory).
3. `make config` (also auto-run by `make all` if
   `cfg/build/current_config_path.txt` is missing,
   `Makefile:66-72`). Reads `cfg/nos3-mission.xml` and the active
   `<sc-1-cfg>` (default `spacecraft/sc-mission-config.xml`),
   writes generated launch scripts and per-app definitions into
   `cfg/build/`.
4. `make fsw` -> `cfg/build/fsw_build.sh`. CMake against
   `fsw/cfe`; build target `mission-install`. Output in
   `fsw/build/exe/cpu1/`.
5. `make sim` -> `scripts/build_sim.sh`. CMake against the
   `sims/` tree; install into `sims/build/bin/`.
6. `make gsw` -> `scripts/gsw/build_cryptolib.sh` and then
   `cfg/build/gsw_build.sh`. Builds CryptoLib standalone with
   `STANDALONE_TCP=1`, `SA_INTERNAL=1`, `MC_INTERNAL=1`,
   `KEY_INTERNAL=1`, `CRYPTO_LIBGCRYPT=1` (`Makefile:79`), plus
   the COSMOS configuration assembly.
7. `make launch` (`Makefile:315-351`). Sequence:
   - Kill any prior capture processes (4 `pkill` calls).
   - Truncate `attack_logs/*` and `omni_logs/*`.
   - Delete `fsw/build/exe/cpu1/data/mgr.bin` so mode persistence
     starts fresh.
   - `make start-elk`: create `nos3-legacy-core` network, ensure the
     MID-registry YAMLs exist (seed-copy fallback if the
     generator fails), bring up the three ELK containers, wait
     up to 40 s for Elasticsearch to leave RED.
   - `curl DELETE /nos3-telemetry-*` so Kibana starts on a fresh
     dataset (unless `KEEP_TLM=1`).
   - `docker rm -f cosmos-openc3-operator-1` so the CI launch
     script can recreate it cleanly.
   - `scripts/ci_launch.sh`. Starts COSMOS, the NOS Engine
     helpers, the `nos3-sc01` network, 42, the cFS container,
     OnAIR, CryptoLib, and the 17 simulators in the loop at
     `ci_launch.sh:235-241`.
   - Start the four capture scripts in the background.
   - Restart Logstash (`docker restart nos3-legacy-logstash`) so it
     picks up the newly created log files; wait for port 9600.
   - `make build-kibana-dashboards`: twenty saved objects
     (nineteen dashboards and one saved search) via the Saved
     Objects API.
   - `make refresh-kibana-fields`: rewrite the index pattern's
     cached field list.

Two operator-side build practices are load-bearing for build
velocity on this devcontainer but are not enforced by the
Makefile: do not delete the `nos_build_fsw` container between
build attempts (its presence keeps CMake's dependency graph
intact, so incremental rebuilds remain incremental), and do not
hand-roll `docker run -i ...` to bypass `cfg/build/fsw_build.sh`
(the script's TTY is what make uses to pick parallel job count;
a TTY-less invocation serialises the install phase on the 9p
bind mount and stretches a 30-40 minute build to 4 hours).


## 6. Validation and smoke test

The system "flies" when:

1. Containers are running (verified by
   `docker ps --format '{{.Names}}'` showing
   `nos3-legacy-elasticsearch`, `nos3-legacy-logstash`, `nos3-legacy-kibana`, and at
   least one `sc01-*` container).
2. Elasticsearch is yellow or green (`curl
   localhost:9203/_cluster/health`).
3. Kibana is available (`curl localhost:5604/api/status`).
4. The `nos3-telemetry-*` index has both `software_bus` and
   `system_log` documents (`_count` queries with the
   corresponding `type` field).

The end-to-end diagnostic of record is `make doctor`
(`Makefile:419-420`, `scripts/elk_doctor.sh`). It walks seven
layers in order:

0. Build-time prerequisites (`cfg/build/elk/*.yml` present and
   non-empty).
1. Containers (the three ELK containers running).
2. Capture processes (four host-side scripts alive).
3. Log file sizes (four capture files non-empty).
4. Elasticsearch (cluster reachable, at least one
   `nos3-telemetry-*` index).
5. Kibana data view (`nos3-telemetry*` saved object present;
   hardcoded UUID `5b3163a0-3ea7-11f1-adf4-55f5fc5a104a` in
   `elk_doctor.sh:23`).
6. Dashboards (the twenty-saved-object catalogue, nineteen
   dashboards and one saved search, from
   `build_kibana_dashboards.py`).

The exit code is the number of failing layers. Layer 0 is
deliberately checked first because a missing MID-registry YAML
masquerades as "no nos3-telemetry-* index" at layer 4
(Logstash crashes on startup with "File does not exist or
cannot be opened").

Per-component smoke tests live in the COSMOS scripts and the
research-attack scripts under `nos3/scripts/attack/`
(`attack_noisy_trigger.py`, `attack_md_jam.py`,
`attack_syn_leak.py`, `run_scenario1.sh`,
`investigate_mm_traversal.sh`, `resolve_eps_address.sh`). The
attack scripts default to UDP port 5010 (the `ci` app) rather
than 5012 (`ci_lab`); the `ci` listener does bind 5010 in this
build (`CI_TRANSPORT=udp_tf` selects
`fsw/apps/ci/fsw/examples/udp_tf/ci_platform_cfg.h:44`).


## 7. Attack surfaces exposed

Concrete vulnerabilities readable in the code at HEAD. Grouped
by category. No mitigations are proposed; the section is the
bridge to the threat-model chapter.

### 7.1 cFS Software Bus has no provenance or capability

`cfe_sb` performs no per-task identity check. Any task that holds
a valid `CFE_SB_Buffer_t` can call `CFE_SB_TransmitMsg` under any
MID. The bus routing table maps MID to a list of subscriber pipes
and forwards by copy.

Concrete instances:

- `noisy_app.c:73-78` forges `GENERIC_EPS_HK_TLM_MID = 0x091A`
  with a hand-built payload. No checks; the legitimate EPS
  publisher cannot tell anything happened.
- `noisy_app.c:101-110` iterates `current_mid` from `0x0000` to
  `0x1FFF` and emits four copies of a 4-KB packet to each. No
  per-MID rate-limit gate exists; the SB pool
  (`CFE_PLATFORM_SB_BUF_MEMORY_BYTES = 524288`,
  `cfg/nos3_defs/cpu1_platform_cfg.h:153`) exhausts within ~128
  packets and subsequent `CFE_SB_TransmitMsg` calls fail with
  `SB_PIPE_OVERFLOW`. The exhaustion itself is observable as a
  log line; the publisher's identity is not.
- The CCSDS sequence counter on every packet is publisher-set and
  bus-unverified. A second publisher of an existing MID writes
  whatever counter value it pleases; downstream consumers
  (including `to_lab` and `god_view_capture.py`) accept it.

### 7.2 Subscription table is the only TO_LAB filter

`nos3/cfg/nos3_defs/tables/to_lab_sub.c` is the sole gate between
the Software Bus and the radio link. Commit `8cad7cc` widened it
to enumerate every component MID, so on this fork the gate is
maximally permissive: any MID an attacker publishes that already
appears in the table (which is "all of them") leaves the
spacecraft. Cost of the wide table:

- Anything `noisy_app` forges under an existing MID downlinks.
- Anything `noisy_app` bursts at (the seven `SB_BURST` spam
  targets) that overlaps an existing MID downlinks (well, up
  to the bus's overflow threshold).
- The legitimate vs. forged distinction has to be reconstructed
  by sequence-counter gap analysis on the ground.

### 7.3 The startup-script keyword is a build-trivial toggle

`cfg/nos3_defs/cpu1_cfe_es_startup.scr` (and the matching build
copy under `cfg/build/nos3_defs/`) define these rows:

```
OFF_APP, ===_DTU_THESIS_MENU_===,   CHANGE_CFE_TO_OFF,        TO_DISABLE,       0,  0,     0x0, 0;
CFE_APP, noisy_app,                 NOISY_APP_Main,           NOISY_APP,        20, 16384, 0x0, 0;
OFF_APP, cpu_killer,                CPU_KILLER_Main,          CPU_KILLER,       160, 16384, 0x0, 0;
```

The first row is only a visual separator/marker, not a
conditional. The keyword on each app row is a simple toggle:
`CFE_APP` loads the app on boot, `OFF_APP` keeps it in the image
but unloaded. As shipped the `noisy_app` row reads `CFE_APP`, so
the malicious app loads on every boot; flipping it to `OFF_APP`
plus a rebuild disables it. Nothing in the build path notices the
malicious app either way.

`cpu_killer` is referenced as `OFF_APP` but the source does not
exist. The line is therefore aspirational; an attacker who ships a
`cpu_killer.c` and a CMakeLists could flip the row to `CFE_APP`
without any further startup-script change.

### 7.4 Both cFS UDP listeners are unauthenticated

`ci_lab` listens on UDP 5012
(`fsw/apps/ci_lab/fsw/src/ci_lab_app.c:153`). The COSMOS DEBUG
interface
(`gsw/cosmos/config/tools/cmd_tlm_server/cmd_tlm_server.txt:6`)
sends here directly, with no CryptoLib in the path. Anything
inside the `nos3-sc01` Docker bridge that can reach `nos-fsw:5012`
can inject CCSDS command packets.

`ci` (the separate sample CI app) is also in the build, with
`CI_TRANSPORT=udp_tf` selecting
`fsw/apps/ci/fsw/examples/udp_tf/ci_platform_cfg.h:44`
(`CI_CUSTOM_UDP_PORT = 5010`); the attack scripts target it on
UDP 5010 (`scripts/attack/attack_noisy_trigger.py:102` and
siblings). The `ci` listener is therefore a second
unauthenticated UDP entry point on the spacecraft container,
parallel to `ci_lab`'s 5012.

### 7.5 CryptoLib applies only on the RADIO path

The DEBUG and RADIO paths in
`gsw/cosmos/config/tools/cmd_tlm_server/cmd_tlm_server.txt` are
parallel. CryptoLib mediates only the RADIO path. Anything
COSMOS routes through DEBUG goes plain UDP to `ci_lab`. An
operator-error misconfiguration (or a target-definition
misprint) that routes a sensitive command through DEBUG ships
it unauthenticated.

`god_view_capture.py` listens on TO_LAB's downlink port 5013
directly (`scripts/god_view_capture.py:60`), bypassing CryptoLib
entirely. From the analyst's perspective this is a feature
(captures the unencrypted CCSDS stream); from the attacker's
perspective it confirms that the FSW's downlink emerges in the
clear inside the Docker bridge before CryptoLib applies any
wrap.

### 7.6 42 dynamics IPC is fragile and unauthenticated

`cfg/InOut/Inp_IPC.txt` declares the TCP IPC sockets that 42
exposes to simulators. The 42 binary
(`/home/vscode/.nos3/42/Source/AutoCode/TxRxIPC.c`, around line
464) uses non-blocking writes and calls `exit(1)` on `EAGAIN`.
Any client that subscribes with a broad prefix saturates 42's
kernel send buffer at 42's 100 Hz emission rate and crashes the
dynamics engine inside seconds. This is denial-of-service
against the simulation but not a flight-side concern; the
relevance is that the simulator harness for the attack
experiments is itself susceptible to lateral disruption.

A separate issue: 42's `InitInterProcessComm`
(`/home/vscode/.nos3/42/Source/42ipc.c:43`) reads each IPC
entry's trailing comment with `fscanf("%[^\n]")` into a
`char junk[120]` buffer. Comments longer than ~100 characters
overflow into the next field, producing
`Bogus input <garbled>` and an exit. Classical unbounded
`scanf` into a fixed buffer.

### 7.7 Memory-management apps trust their inputs

`mm`, `md`, and `cs` are heritage cFS apps that operate on raw
memory addresses. `mm` provides `LOAD_MEM_FROM_FILE`,
`DUMP_TO_FILE`, `PEEK`, and `POKE` command codes. The
observation here is structural; the detailed chain analysis
belongs in the Threat Model chapter, not the Setup chapter:

- `mm`'s file-path argument is parsed by `OS_strnlen` (replaced
  by `strnlen` in commit `449e904`) without an OSAL containment
  check. This is the OSAL-traversal entry point.
- `cs` table format is upstream-defaulted to 32-bit alignment;
  the fix in `af0fc6f` disabled the on-power table set rather
  than rewriting it, so any operator who re-enables the table
  set hits an alignment fault. Class of issue: 32/64-bit
  porting hazard surfaced as a silent crash.
- `md` "memory dwell" can be programmed by ground command to
  periodically read arbitrary addresses and publish their
  values. A malicious ground (or any cFS app publishing
  `MD_CMD_MID` directly) configures `md` into an exfiltration
  channel.

### 7.8 Sim-side parsers are also unauthenticated wire-readers

Three component simulators
(`blackboard_data_point.cpp`,
`generic_tt_c_data_point.cpp`,
`generic_gnss_data_point.cpp`) read a textual data stream from
42 and parse it with bracket-stripping plus `stringstream` or
`stof`. None of them validate the source: any container on the
`nos3-sc01` bridge that can open a TCP connection to the
relevant 42 port can subscribe to truth state and feed
falsified values back to the FSW via the corresponding
simulator's HK MID. This is the "malicious component simulator"
extension of the supply-chain threat model.

### 7.9 Capture surface trust is one-way but not tamper-evident

`scripts/god_view_capture.py` writes `cfs_god_view.json` from
the host side. The Docker bridge does not let a compromised
cFS app touch the host filesystem. But:

- The file is written from a host process that any local user
  can SIGKILL. A run resumed after a kill loses the
  intervening window.
- Logstash bind-mounts the file read-only; nothing else does.
  A host-side actor with write access can append fabricated
  documents and Logstash will index them.
- Elasticsearch has `xpack.security.enabled=false`
  (`elk/docker-compose.yml:10`). Any host with reach to
  `localhost:9203` can rewrite, delete, or close indices.

The ELK pipeline is therefore forensic-grade against an
in-spacecraft attacker and integrity-soft against a host-side
attacker. The thesis framing already restricts the threat
model to the spacecraft side; the section is here so the
reader knows the boundary.


## 8. Code snippet candidates

1. **`noisy_app.c:73-79` (EPS HK spoof).** Five lines that show
   the entire spoof: `memset`, `CFE_MSG_Init` with the EPS HK
   MID and the EPS length symbol, `BatteryVoltage = 10000`
   (a forged low-battery reading, in mV),
   `CFE_SB_TimeStampMsg`, `CFE_SB_TransmitMsg`. The single
   point the snippet makes is that a publisher under one app
   can fake telemetry as another app with no bus-side
   pushback. Draft caption: "Listing X.1 - `noisy_app` forges
   an EPS housekeeping packet through cFE's standard SB
   publish API; the bus has no per-MID publisher ACL."

2. **`noisy_app.c` (resource-exhaustion DoS).** The
   SB pool-lock path (covert opcode `0x08`, "SB POOL LOCK
   engaged") and the pipe-flood path (covert opcode `0x04`,
   "PIPE FLOOD - N junk cmds delivered"). One point: an
   in-spacecraft publisher exhausts the
   `CFE_PLATFORM_SB_BUF_MEMORY_BYTES = 524288` pool, and the
   SB has no rate-limit gate. Caption: "Listing X.2 -
   `noisy_app`'s pool-lock DoS starves the cFE buffer pool;
   the SB has no per-app quota or rate-limit gate."
   (Historical note: the superseded broadcast-storm design
   instead iterated every MID in `0x0000..0x1FFF` with a
   nested 4-iteration inner loop. That design no longer
   runs.)

3. **`cmd_tlm_server.txt:6` and `:27` (the two FSW-to-GSW
   paths).** A two-line excerpt:
   `INTERFACE DEBUG udp_interface.rb nos-fsw 5012 5013 ...` and
   `INTERFACE RADIO udp_interface.rb cryptolib 6010 6011 ...`.
   One point: the boundary the threat-model chapter treats as
   "the encrypted ground link" is in fact one of two parallel
   paths; the other is plain UDP. Caption: "Listing X.3 -
   COSMOS configures two independent UDP interfaces to the
   spacecraft. Only the RADIO path passes through CryptoLib."

4. **`scripts/god_view_capture.py:58-60` (the privileged
   observer).** Three lines naming `CI_LAB_CMD_PORT = 5012`,
   `LISTEN_HOST = "0.0.0.0"`, `LISTEN_PORT = 5013`. One
   point: the analyst's privileged tap is a host-side UDP
   socket on the same port the GSW uses; there is no
   spacecraft-side cooperation needed to instrument the bus
   downlink. Caption: "Listing X.4 - `god_view_capture.py`
   binds the TO_LAB downlink port and treats every CCSDS
   packet on the wire as a JSON line."

5. **`cpu1_cfe_es_startup.scr` attack rows (3 lines).**
   The `===_DTU_THESIS_MENU_===` separator/marker plus the
   `CFE_APP` row for `noisy_app` (loaded on boot) and the
   `OFF_APP` row for `cpu_killer`. One point: the supply-chain
   attack surface is the shipped binary itself; the row keyword
   is a one-line build-time toggle. Caption: "Listing X.5 -
   The startup script's attack rows. `noisy_app` ships as
   `CFE_APP` and loads on boot; the keyword is a single-edit
   toggle (`CFE_APP` loaded, `OFF_APP` not loaded)."


## 9. Diagram candidates

Single dense figure is rejected. Each diagram is one idea.

### F1c Container layout (replacement for current F1)

One thing the reader leaves with: two Docker networks, the
shared core (`nos3-legacy-core`) hosting the shared services and
the ELK stack together, the per-spacecraft world on the other,
and COSMOS straddling both as the dual-homed container.

Boxes:

- Host frame (outer rounded rectangle).
- `nos3-legacy-core` rectangle (the shared core network):
  `cosmos-openc3-operator-1` (dual-home notch on the right
  side), `nos-time-driver`, `nos-terminal`,
  `nos-udp-terminal`, `nos-sim-bridge` (all four with notch
  showing dual-home onto sc01), and the three ELK containers
  `nos3-legacy-elasticsearch`, `nos3-legacy-logstash`,
  `nos3-legacy-kibana`.
- `nos3-sc01` rectangle (right or bottom): `sc01-fortytwo`
  (top), `sc01-nos-fsw`, `sc01-cryptolib`, `sc01-onair`,
  `sc01-nos-engine-server`, `sc01-truth42sim`, and the 16
  simulator containers.
- Host-side capture scripts on the outside of the Docker
  frame, with a bind-mount arrow pointing into
  `nos3-legacy-logstash`.

Arrows:

- Operator browser to `nos3-legacy-kibana` (port 5601) and to
  `cosmos-openc3-operator-1` (port 2900).
- `cosmos-openc3-operator-1` -> `sc01-nos-fsw` UDP
  5012/5013 (DEBUG, labelled "plaintext").
- `cosmos-openc3-operator-1` -> `sc01-cryptolib` UDP
  6010/6011 (RADIO, labelled "CCSDS SDLP wrap").
- `sc01-cryptolib` -> `sc01-nos-fsw` UDP 8010/8011.
- Every sim container -> `sc01-fortytwo` (one bundled
  arrow labelled "42xx TCP, non-blocking, 100 Hz").
- `god_view_capture.py` (host-side) -> bind-mount into
  `nos3-legacy-logstash`.

### F2 Six wire boundaries (keep as-is)

Already in `docs/thesis/08-figures/figures.md`. The six
boundaries described in `docs/thesis/03-communication/`
are correct in form; the only correction is the network
naming (`nos3-sc01`, not `nos3_sc01`).

### F3 Software Bus pub-sub topology (keep)

The diagram in `docs/thesis/08-figures/figures.md:115-154`
correctly shows the MID `0x091A` highlighted with both
`noisy_app` (red) and `generic_eps` (blue) as publishers.

### F4 Logstash dispatch tree

One thing: the four input types and how each one is filtered
into the same `nos3-telemetry-YYYY.MM.dd` index.

Boxes: capture script -> file -> Logstash input -> filter
branch -> Elasticsearch index. Annotate each filter branch
with one representative tag set.

### F5 Denmark overfly map (keep)

Already correct against `cfg/InOut/Orb_LEO.txt:16-19` (60°
inclination, 346° RAAN, 61.6° true anomaly, 400 km circular).

### F6 noisy_app attack timeline (keep)

The swim-lane diagram in `docs/thesis/08-figures/figures.md`
has been reframed to the current piggyback design: lane 2
shows a single covert opcode byte riding the CI_LAB carrier
MID `0x18E0`, not a "4 KB packets x 4 x 8192 MIDs" broadcast
storm. The SB-burst opcode (`0x04`) targets exactly the seven
MIDs that receive the Logstash `noisy_app_spam_target` tag, so
the tag now matches the burst targets directly. The legacy
8192-MID storm design no longer runs.

### F7 Trust-boundary overlay

One thing: where the four trust boundaries sit, drawn on
top of F1c. Dashed lines around the host frame ("trusted
host"), around the cFS container ("Software Bus: no internal
auth"), around the CryptoLib container ("only authenticated
wire boundary"), and around the ELK stack ("one-way
forensic").


## 10. Git history digest

Import baseline: `55ad214b0f52e5aac0a98cd4bb3b47ebe3e4410`,
`chore: imported and flattened NOS3 environment with all
submodules`, 2026-03-18.

HEAD: `076dad6c791912b93f417d0dc9deeeb26f71f91b`,
`fix(elk): seed MID-registry YAMLs so Logstash starts on fresh
clones`, 2026-05-12.

Divergence: 71 commits, 764 files changed,
+124922 / -5491 (cumulative diff stat, `git diff --stat
55ad214..HEAD | tail -1`).

### Top design-decision commits, chronological

- `7441371` 2026-03-18 `chore: added devcontainer, gitignore,
  and initial COSMOS configs`. Why this mattered: established
  the devcontainer baseline this fork depends on; everything
  that follows assumes the X11 plumbing and the
  `ivvitc/nos3-64` toolchain.
- `08fe63c` 2026-03-18 `feat: integrated ELK stack and cFS god
  view logging`. Why this mattered: the seed commit for the
  entire DTU observability stack; nothing prior existed.
- `dd75079` 2026-03-19 `feat: implemented and staged noisy_app
  broadcast storm attack`. Why this mattered: the
  centrepiece-malware seed; defines the threat-model
  realisation that the thesis is built around. (This commit's
  broadcast-storm design has since been superseded: the
  current `noisy_app` uses a covert-opcode piggyback trigger
  on the CI_LAB carrier MID `0x18E0` rather than an
  8192-MID storm.)
- `90e024c` 2026-03-23 `feat: add BEACON_APP ground-commandable
  ping/pong telemetry application`. Why this mattered: gives
  the threat model a passive-trigger surface (legitimate
  ping = attacker arming signal); makes the
  unauthenticated-bus story concrete.
- `8131144` 2026-03-23 `feat: add save-run and load-run
  targets for offline attack replay`. Why this mattered:
  established replay across machines, which is what makes
  attack experiments comparable.
- `7670998` 2026-03-26 `fix: replace empty gitlinks with full
  CS, MD, MM cFS app source`. Why this mattered: the
  flattening import had left half the heritage cFS apps as
  empty directories; the FSW build was broken until they were
  filled in.
- `0d529bc` 2026-05-09 `fix(42 ipc): tighten ports/prefixes to
  keep fortytwo alive`. Why this mattered: identified the
  100 Hz × broad-prefix crash class and pinned the IPC table
  to narrow prefixes. The invariant is now an inline comment
  on each affected entry in `cfg/InOut/Inp_IPC.txt`.
- `2a0bd47` 2026-05-09 `feat(cfg): enable generic_gnss/tt_c,
  switch ST to synthetic, bump epoch`. Why this mattered:
  finalised the spacecraft profile this fork actually runs;
  the Denmark overfly window is anchored on it.
- `bdf010d` 2026-04-02 `feat(lc/md/sc): implement
  health-monitoring and automated safe-mode response`. Why
  this mattered: filled in the LC/SC table chain that the
  thesis claims is the autonomy backbone; pre-this commit
  the autonomy was hand-driven.
- `8161e16` 2026-05-09 `fix(mgr): suppress duplicate SET_MODE
  EVS spam; revert reboot to SAFE mode`. Why this mattered:
  the mode-byte semantics propagate through the entire ELK
  pipeline (the `mgr_mode` field is the join key for
  `science_overfly` and `spacecraft_mode_change` tags); the
  reboot-resume policy change is operational-safety glue.

### Top 5 areas of churn by directory

(From `git diff --stat 55ad214..HEAD -- <dir>` at HEAD.)

1. `nos3/fsw/apps/` - 435 files, +95700 / -1301. Dominated by
   the heritage-cFS-app restoration commits (`7670998`,
   `f7ac1730`, `3b45603`) and the `noisy_app` /
   `beacon_app` adds.
2. `nos3/gsw/` - 173 files, +13281 / -1. Dominated by the
   COSMOS-targets generated for new components and the SIM
   bridge config.
3. `nos3/elk/` - 10 files, +5443. Entire pipeline is a DTU
   addition.
4. `nos3/components/` - 65 files, +4516 / -1491. Dominated by
   `generic_gnss` and `generic_tt_c` (whole-component adds),
   plus the parser hardening across `blackboard`,
   `generic_rw`, `generic_star_tracker`, and `generic_eps`.
5. `nos3/scripts/` - 49 files, +2922 / -977. Dominated by the
   four capture scripts (`god_view_capture.py` alone is
   619 lines), `elk_doctor.sh` (180 lines), `cpu_monitor.sh`,
   `cfs_evs_capture.sh`, `sim_logs_capture.sh`,
   `stop_sim.sh`, and the attack-research scripts under
   `scripts/attack/`.

### Commits documenting a failure that earned a paragraph

- `0d529bc` 2026-05-09 (42 IPC tightening). The 42
  non-blocking-write crash class; root cause is
  `/home/vscode/.nos3/42/Source/AutoCode/TxRxIPC.c:464-468`
  calling `write()` on a non-blocking socket and `exit(1)`-ing
  on `-1`, which the simulator-side reader at
  `nos3/sims/sim_common/src/sim_data_42socket_provider.cpp:218-230`
  cannot drain fast enough at broad-prefix traffic volumes.
- `a0d796f` 2026-05-11 (blackboard parser). The `stof` failure
  on bracketed vectors and the empty-frame startup transient;
  fix anchored in the file-static `stof_safe` helper and the
  empty-frame deferral guard at
  `nos3/components/blackboard/sim/src/blackboard_data_point.cpp:8`,
  `:27-44`.
- `db78b8b` 2026-05-11 and `f766be0` 2026-05-12 (`generic_tt_c`
  and `generic_gnss` source restored on `main`). A
  branch-management mishap removed the FSW source; the
  recovery is documented in the commit messages.
- `2595970` 2026-05-12 (`+x` on every Makefile-invoked shell
  script in git). Earlier clones intermittently lost the
  executable bit; commit fixes the index entries.


## 11. Open questions and gaps

Items resolved during pass 2 are recorded in section 13 with
the verification step that closed them. The list below holds
only the items that still require operator action.

- `cpu_killer` in the startup script's `OFF_APP` menu has no
  source file under `nos3/fsw/apps/` or `nos3/components/`. Is
  it a planted hook for a future attack variant, or a stale
  reference to a deleted experiment? Cannot be resolved from
  code alone; treat as menu-only in the thesis.
- The `syn` (SYNOPSIS) "load-bearing for the science timeline"
  claim resolved in section 12 (b) in favour of code: `syn` is
  not loaded in the default profile, and the architecture-doc
  claim is to be retracted. The remaining operator action is
  the prose-rewrite pass on `docs/thesis/02-architecture.md:138-141`.
- The Denmark overfly precise timing window (sub-sat point
  enters the Denmark box at t~1.3 min, exits at t~2.3 min) is
  cited verbatim from the inline comment at
  `cfg/InOut/Orb_LEO.txt:18`. The orbital elements themselves
  read correctly (60 deg inclination, 346 deg RAAN, 61.6 deg
  true anomaly, 400 km circular), but the timing claim has
  not been confirmed by re-running the ground-track
  simulation. Verification: `make launch`, observe the 42 map
  window or `truth_42` HK over the first 5 minutes of sim
  time.
- The COSMOS DEBUG path's port-5013 telemetry stream is
  end-to-end wired (see section 13) but has not been observed
  receiving traffic with COSMOS as the consumer. Verification:
  `make launch`, then on the COSMOS server confirm that the
  DEBUG interface's `read_count` increments while
  `god_view_capture.py` is running.


## 12. Unverified claims and conflicts

### 12 (a) Unverified

Items resolved at HEAD on 2026-05-14 are kept here for the
thesis writer's audit trail.

- **The `ci` app's UDP listener port.** Verified on 2026-05-14:
  `CI_TRANSPORT=udp_tf` in
  `cfg/build/nos3_defs/toolchain-amd64-nos3.cmake:33`,
  which causes `fsw/apps/ci/CMakeLists.txt:17` to include
  `fsw/apps/ci/fsw/examples/udp_tf/`. The selected header
  defines `CI_CUSTOM_UDP_PORT = 5010`
  (`udp_tf/ci_platform_cfg.h:44`) and `udp_tf/ci_custom.c:141`
  writes it into the socket. `ci` binds UDP 5010.
- **The shape of the SCH-table watchpoint chain.** Verified on
  2026-05-14: `cfg/nos3_defs/tables/lc_def_wdt.c` (2232 lines)
  declares watchpoints over the HK MIDs of `generic_eps`,
  `generic_gnss`, `novatel_oem615`, `mgr`, and `sample`
  (`lc_def_wdt.c:40-51`). `sc_rts*.c` exists as 17 RTS files
  (`sc_rts001.c..003.c, 005.c, 025.c..037.c`); the
  Power-On Reset chain in `sc_rts001.c:33-160` enables RTS
  2-64, starts RTS 3 (Safe Mode), and pre-enables Science /
  Science_Reboot transitions (RTS 26, 25) and RTS 37. The
  LC-AP -> SC-RTS chain is present in source.
- **The exact Logstash filter line numbers.** Verified against
  HEAD on 2026-05-14: `noisy_app_spam_target` add_tag at
  `logstash.conf:155` (block 150-156); `beacon_cmd` add_tag
  at `:160` (block 158-161); `attack_armed` / `attack_trigger_ping` /
  `attack_loaded` add_tags at `:330`, `:333`, `:336`
  (block 326-338); `sb_pipe_overflow` add_tag at `:347`.
- **The cFE version assertion.** Verified on 2026-05-14 against
  `nos3/fsw/cfe/modules/core_api/fsw/inc/cfe_version.h:33-35`:
  `CFE_MAJOR_VERSION=6`, `CFE_MINOR_VERSION=7`,
  `CFE_REVISION=99`. The same file declares
  `CFE_BUILD_BASELINE "v7.0.0-rc4"` (`:30`) and the
  `CFE_VERSION_STRING` codename "Draco" (`:63-64`). The
  framing "legacy cFS 6.7.99" is true at the macro level, but
  the build is technically a Draco release-candidate
  pre-release; the thesis writer should make the macro-level
  framing explicit and not claim the build is older than RC4.
- **The Denmark overfly precise timing window** (RAAN tuned
  so the sub-sat point enters the Denmark box at t~1.3 min
  and exits at t~2.3 min). Cited verbatim from the inline
  comment in `cfg/InOut/Orb_LEO.txt:18`. UNVERIFIED at the
  observational level (the actual ground-track simulation
  output was not re-run). Same item recorded in section 11.
- **The COSMOS DEBUG-path mirror reaching cosmos at 5113.**
  Verified at the wiring level on 2026-05-14:
  `nos3/scripts/ci_launch.sh:96` publishes `-p 5113:5013/udp`
  on the COSMOS container, and `nos3/scripts/god_view_capture.py:71-81`
  defaults `NOS3_TLM_MIRROR_DEST` to `127.0.0.1:5113`. The
  chain is wired end-to-end in code; runtime observation
  (that COSMOS actually consumes from the mirror) is the
  remaining gap, recorded in section 11.

### 12 (b) Conflicts between code, docs, comments, commits

The thesis takes the code version as canonical for each
conflict below. Stale doc citations are quoted so the doc
sweep (not in scope of this file) knows what to change.

- **Docker network naming.** Truth: the per-spacecraft network
  is named `nos3-sc01` (with hyphen) per
  `scripts/ci_launch.sh:184` (`SC_NET="nos3-"$SC_NUM`,
  `SC_NUM=sc01`). Stale doc:
  `docs/thesis/02-architecture.md:25-30` and several sibling
  files (`03-communication/00-overview.md`,
  `04-apps/noisy_app.md`, others) write it as `nos3_sc01`
  (with underscore). Action: thesis follows code, writes
  `nos3-sc01`.

- **Which container is dual-homed.** Truth: COSMOS and the
  NOS Engine helpers are dual-homed; cFS is not.
  `scripts/ci_launch.sh:78-103` starts the COSMOS container
  with `--network=$NETWORK_NAME` (the shared `nos3-legacy-core`),
  then `:192` connects it to
  `nos3-sc01`. The NOS Engine helpers (`nos-time-driver`,
  `nos-terminal`, `nos-udp-terminal`, `nos-sim-bridge`) are
  dual-homed at `:264-269`. The FSW container at `:204-208`
  is only on `nos3-sc01`. Stale doc:
  `docs/thesis/02-architecture.md:35-38` -
  "The cFS container is dual-homed for exactly this reason
  ...". Action: thesis follows code, names COSMOS (and the
  NOS Engine helpers) as the dual-homed containers.

- **Whether `noisy_app` loads on every launch.** Truth:
  `noisy_app` IS loaded on every boot. Both
  `cfg/nos3_defs/cpu1_cfe_es_startup.scr` and
  `cfg/build/nos3_defs/cpu1_cfe_es_startup.scr` declare its
  row as `CFE_APP` (line 31); cFE loads `CFE_APP` rows on boot.
  The row is a simple toggle: flipping it to `OFF_APP` keeps the
  binary in the image but unloaded. The preceding
  `OFF_APP, ===_DTU_THESIS_MENU_===` row is only a visual
  separator/marker, not a conditional. This agrees with
  `docs/thesis/04-apps/noisy_app.md` and
  `docs/thesis/02-architecture.md`. Action: thesis follows code,
  treats `noisy_app` as loaded on every boot with a one-line
  disable toggle.

- **CryptoLib is the sole FSW-to-GSW path.** Truth: two
  parallel paths exist. `gsw/cosmos/config/tools/cmd_tlm_server/cmd_tlm_server.txt:6`
  declares `INTERFACE DEBUG udp_interface.rb nos-fsw 5012 5013`
  (plain UDP, bypasses CryptoLib) with 17 component targets
  attached. `cmd_tlm_server.txt:27` declares
  `INTERFACE RADIO udp_interface.rb cryptolib 6010 6011`
  (CryptoLib-wrapped) with the same set of `_RADIO`-suffixed
  duplicate targets. Both interfaces are active in the
  default configuration; the operator chooses which target
  to invoke at script-runner time. Stale doc:
  `docs/thesis/02-architecture.md:65-71` -
  "CryptoLib sits inline between them in the default
  configuration ..." Action: thesis follows code; the
  FSW-to-GSW boundary is the union of DEBUG and RADIO, not
  CryptoLib alone.

- **`parse_double_vector` usage in `generic_tt_c` and
  `generic_gnss`.** Truth: both parsers manually substitute
  `,`, `[`, `]` with space and then read with
  `stringstream`. `components/generic_tt_c/sim/src/generic_tt_c_data_point.cpp:35-41`
  and `components/generic_gnss/sim/src/generic_gnss_data_point.cpp:28`
  perform the substitution; the `parse_double_vector` helper
  is used by `blackboard_data_point.cpp` but not by these
  two. Stale doc:
  `docs/thesis/04-apps/generic_tt_c.md:88-90` and
  `docs/thesis/04-apps/generic_gnss.md:99-101` both say
  "parse both as `parse_double_vector` (which strips the
  brackets and yields three doubles each)". Action: thesis
  follows code; describe TT&C and GNSS parsers as
  manual-bracket-substitution and `blackboard` as the only
  user of `parse_double_vector`.

- **`syn` is loaded and load-bearing.** Truth: `syn` is
  disabled in `cfg/spacecraft/sc-mission-config.xml:72-74`
  (`<syn><enable>false</enable></syn>`) and the build-time
  startup script (`cfg/build/nos3_defs/cpu1_cfe_es_startup.scr`)
  does not include a `CFE_APP` entry for `syn`. Stale doc:
  `docs/thesis/02-architecture.md:138-141` -
  "load-bearing for the science timeline ..." Action: thesis
  follows code; `syn` is present in the source tree but not
  loaded by the default profile, and the architecture-doc
  "load-bearing for the science timeline" claim must be
  retracted.

- **Logstash `noisy_app_spam_target` tag scope.** Truth: the
  tag fires on 7 specific MIDs
  (`elk/logstash.conf:154-155`: `0x1806`, `0x1801`, `0x1803`,
  `0x1805`, `0x1804`, `0x1884`, `0x1880`). The current
  piggyback `noisy_app` bursts at exactly these 7 high-impact
  MIDs on the `SB_BURST` covert opcode (`0x04`), so the tag
  matches the burst targets directly. History note: the 7-MID
  list was authored in commit `a3ed42a` (2026-03-24) when
  noisy_app.c targeted exactly those 7 cFE-core service MIDs;
  the app then briefly evolved into a carpet-bomb storm
  (iterating `0x0000..0x1FFF`, 8192 MIDs), which left the tag
  as a partial signature; the subsequent piggyback rewrite
  re-narrowed the burst to the same 7 MIDs, re-aligning the
  tag. Action: thesis follows code; the tag catches the
  current `SB_BURST` targets, and the EVS-derived
  `piggyback_opcode`, `eps_spoof`, and `sb_pool_lock` tags
  plus `sb_pipe_overflow` do the heavy lifting for current-
  code detection. (The legacy `attack_armed` regex no longer
  fires: the current app does not emit its trigger strings.)

- **`cpu_killer` exists.** Truth: the startup script (source
  and build copies) lists `OFF_APP, cpu_killer, CPU_KILLER_Main, ...`
  but no `cpu_killer.c` exists under `nos3/fsw/apps/` or
  `nos3/components/`. Action: thesis follows code; describe
  `cpu_killer` as a menu placeholder (not an attacker that
  ships in this build), and flag it as a supply-chain
  attack-vector affordance even without an implementation.


## 13. Pass-2 changes

- Pass 2 ran on 2026-05-14, against HEAD `076dad6`.
- T1 cFE version assertion: verified at
  `nos3/fsw/cfe/modules/core_api/fsw/inc/cfe_version.h:33-35`
  (6.7.99) with the caveat that `CFE_BUILD_BASELINE` is
  `v7.0.0-rc4` and the `CFE_VERSION_STRING` codename is Draco
  (`:30`, `:63-64`). Section 1 now records both facts.
- T1 `ci` UDP listener port: verified to be 5010.
  `CI_TRANSPORT=udp_tf` in the amd64 toolchains selects
  `fsw/apps/ci/fsw/examples/udp_tf/ci_platform_cfg.h:44`
  (`CI_CUSTOM_UDP_PORT = 5010`); `udp_tf/ci_custom.c:141`
  writes the port into the socket. Sections 4.1, 6, 7.4, and
  12 (a) updated.
- T1 SCH/LC/SC chain: verified at HEAD.
  `cfg/nos3_defs/tables/lc_def_wdt.c:40-51` declares
  watchpoints on EPS, GNSS, NOVATEL, MGR, and SAMPLE HK MIDs;
  `cfg/nos3_defs/tables/sc_rts001.c:33-160` is the Power-On
  Reset RTS chain (enables RTS 2-64, starts RTS 3 Safe Mode,
  enables Science / Science_Reboot transitions). Section 4.1
  expanded to record the chain shape.
- T1 Logstash filter line numbers: verified against HEAD;
  exact add_tag line numbers
  (`noisy_app_spam_target` :155; `beacon_cmd` :160;
  `attack_armed` :330; `attack_trigger_ping` :333;
  `attack_loaded` :336; `sb_pipe_overflow` :347) recorded in
  section 4.14 and 12 (a).
- T2 conflicts rewritten so each entry in 12 (b) names the
  code-side truth, quotes the stale doc citation, and states
  the action ("thesis follows code"). The eight conflicts
  (Docker network spelling, dual-home target,
  noisy_app load gate, CryptoLib-as-sole-path,
  parse_double_vector usage, syn load state, Logstash
  spam-target tag scope, cpu_killer existence) are all
  resolved in favour of code.
- T3 noisy_app_spam_target tag origin: resolved.
  `git log -S "noisy_app_spam_target" -- nos3/elk/logstash.conf`
  identifies the introducing commit as `a3ed42a` (2026-03-24).
  `git show a3ed42a:nos3/fsw/apps/noisy_app/fsw/src/noisy_app.c`
  shows that at the time of introduction, noisy_app.c targeted
  exactly the seven cFE-core service MIDs that the tag still
  matches (`TARGET_ES_CMD_MID`, `TARGET_EVS_CMD_MID`,
  `TARGET_SB_CMD_MID`, `TARGET_TIME_CMD_MID`,
  `TARGET_TBL_CMD_MID`, `TARGET_CI_LAB_CMD_MID`,
  `TARGET_TO_LAB_CMD_MID`). The carpet-bomb form
  (`current_mid` iterating 0x0000..0x1FFF) came later, leaving
  the tag partial; the subsequent piggyback rewrite re-
  narrowed the `SB_BURST` opcode (`0x04`) to the same seven
  MIDs, so the tag is aligned again. Recorded in section
  12 (b).
- T3 COSMOS DEBUG / RADIO usage in default workflow:
  `gsw/cosmos/config/tools/cmd_tlm_server/cmd_tlm_server.txt`
  attaches 17 component targets to DEBUG (lines 8-25) and the
  same 17 with `_RADIO` suffix to RADIO (lines 29-44). Both
  interfaces have parallel target sets, so DEBUG is not
  development-only; it is the equal twin. The thesis "two
  parallel paths" framing in section 12 (b) and 7.5 is
  reinforced.
- T3 COSMOS DEBUG 5113 mirror chain: wired end-to-end in code.
  `scripts/ci_launch.sh:96` publishes `-p 5113:5013/udp` on
  the COSMOS container; `scripts/god_view_capture.py:71-81`
  defaults `NOS3_TLM_MIRROR_DEST` to `127.0.0.1:5113`. The
  runtime observation that COSMOS actually consumes from the
  mirror is the remaining gap (section 11).
