#
# Top Level Mission Makefile
#
.PHONY: help

BUILDTYPE ?= debug
INSTALLPREFIX ?= exe
FSWBUILDDIR ?= $(CURDIR)/fsw/build
GSWBUILDDIR ?= $(CURDIR)/gsw/build
SIMBUILDDIR ?= $(CURDIR)/sims/build
COVERAGEDIR ?= $(CURDIR)/fsw/build/amd64-posix/default_cpu1

SC1_CFG ?= 

export SYSTEM_TEST_FILE_PATH = /scripts/gsw/system_test.rb

export CFS_APP_PATH = ../components
export MISSION_DEFS = ../cfg/build/
export MISSIONCONFIG = ../cfg/build/nos3


# The "prep" step requires extra options that are specified via environment variables.
# Certain special ones should be passed via cache (-D) options to CMake.
# These are only needed for the "prep" target but they are computed globally anyway.
PREP_OPTS :=

ifneq ($(INSTALLPREFIX),)
PREP_OPTS += -DCMAKE_INSTALL_PREFIX=$(INSTALLPREFIX)
endif

ifneq ($(VERBOSE),)
PREP_OPTS += --trace
endif

ifneq ($(BUILDTYPE),)
PREP_OPTS += -DCMAKE_BUILD_TYPE=$(BUILDTYPE)
endif


# The "LOCALTGTS" defines the top-level targets that are implemented in this makefile
# Any other target may also be given, in that case it will simply be passed through.
LOCALTGTS := all build-kibana-dashboards checkout clean clean-docker clean-fsw clean-sim clean-gsw code-coverage config debug fsw gcov gsw help help-all launch list-kibana-dashboards load-dashboards load-run log prep purge-logs refresh-kibana-fields save-run sim start-elk stop stop-gsw stop-sim uninstall
OTHERTGTS := $(filter-out $(LOCALTGTS),$(MAKECMDGOALS))

# As this makefile does not build any real files, treat everything as a PHONY target
# This ensures that the rule gets executed even if a file by that name does exist
.PHONY: $(LOCALTGTS) $(OTHERTGTS)

#
# Commands
#
all: ## Build everything: config, fsw, sim, gsw
	@if [ ! -f ./cfg/build/current_config_path.txt ]; then \
		echo "Running make config (initial)..."; \
		$(MAKE) config; \
	else \
		echo "Skipping make config (already configured)"; \
	fi
	$(MAKE) fsw
	$(MAKE) sim
	$(MAKE) gsw

build-cryptolib: ## Build CryptoLib Component, ## -DSTANDALONE_TCP=0 if using udp for cryptolib in the loop
	mkdir -p $(GSWBUILDDIR)
	cd $(GSWBUILDDIR) && cmake $(PREP_OPTS) -DSTANDALONE_TCP=1 -DCRYPTO_RX_GROUND_PORT=$(CRYPTO_RX_GROUND_PORT) -DCRYPTO_TX_GROUND_PORT=$(CRYPTO_TX_GROUND_PORT) -DCRYPTO_TX_RADIO_PORT=$(CRYPTO_TX_RADIO_PORT) -DCRYPTO_RX_RADIO_PORT=$(CRYPTO_RX_RADIO_PORT) -DSA_FILE=OFF -DSUPPORT=1 -DCRYPTO_LIBGCRYPT=1 -DSA_INTERNAL=1 -DMC_INTERNAL=1 -DKEY_INTERNAL=1 ../../components/cryptolib
	$(MAKE) --no-print-directory -C $(GSWBUILDDIR)

build-fsw: ## Build the flight software (cFS or F')
ifeq ($(FLIGHT_SOFTWARE), fprime)
	cd fsw/fprime/fprime-nos3 && fprime-util generate && fprime-util build
else
	mkdir -p $(FSWBUILDDIR)
	cd $(FSWBUILDDIR) && cmake $(PREP_OPTS) ../cfe
	$(MAKE) --no-print-directory -C $(FSWBUILDDIR) mission-install
endif

build-sim: ## Build Simulator
	mkdir -p $(SIMBUILDDIR)
	cd $(SIMBUILDDIR) && cmake -DCMAKE_INSTALL_PREFIX=$(SIMBUILDDIR) ..
	$(MAKE) --no-print-directory -C $(SIMBUILDDIR) install

build-test: ## Build unit tests
ifeq ($(FLIGHT_SOFTWARE), fprime)
	# TODO
else
	mkdir -p $(FSWBUILDDIR)
	cd $(FSWBUILDDIR) && cmake $(PREP_OPTS) -DENABLE_UNIT_TESTS=true ../cfe
	$(MAKE) --no-print-directory -C $(FSWBUILDDIR) mission-install
endif

checkout: ## Run a checkout application (may require reconfiguration)
	./scripts/checkout.sh

#This could currently break if not using COSMOS in the config.
ci-launch: ## Headless Launch for System Testing
	@export SYSTEM_TEST_FILE_PATH=$(SYSTEM_TEST_FILE_PATH) && \
	./scripts/ci_launch.sh && \
	./scripts/system_tests.sh && \
	./scripts/stop.sh

#This could currently break if not using COSMOS in the config.
system-tests: ## System Testing with GUI
	@export SYSTEM_TEST_FILE_PATH=../..$(SYSTEM_TEST_FILE_PATH) && \
	./cfg/build/launch.sh && \
	./scripts/system_tests.sh

#Be sure that your nos3-mission.xml has been set to YAMCS
yamcs-operator:  ## Launch as a YAMCS operator viewpoint
	@export SYSTEM_TEST_FILE_PATH=$(SYSTEM_TEST_FILE_PATH) && \
	./scripts/ci_launch.sh --use-yamcs

#Be sure that your nos3-mission.xml has been set to COSMOS
cosmos-operator: ## Launch as a COSMOS operator viewpoint
	@export SYSTEM_TEST_FILE_PATH=../..$(SYSTEM_TEST_FILE_PATH) && \
	./scripts/ci_launch.sh --use-cosmos-gui 

clean: ## Clean all build files and configurations
	$(MAKE) clean-fsw
	$(MAKE) clean-sim
	$(MAKE) clean-gsw
	rm -rf cfg/build

clean-fsw: ## Clean only flight software build artifacts
	rm -rf cfg/build/nos3_defs
	rm -rf docs/coverage/coverage_report.*
	rm -rf fsw/build
	rm -rf fsw/fprime/fprime-nos3/build-artifacts
	rm -rf fsw/fprime/fprime-nos3/build-fprime-automatic-native
	rm -rf fsw/fprime/fprime-nos3/fprime-venv
	rm -rf fsw/fprime/fprime-nos3/logs

clean-sim: ## Clean only simulator build artifacts
	rm -rf sims/build

clean-gsw: ## Clean only GSW build artifacts
	rm -rf gsw/build
	rm -rf gsw/cosmos/build
	rm -rf /tmp/nos3
	./scripts/gsw/gsw_openc3_clean.sh

config: ## Run configuration setup
	@if [ -n "$(SC1_CFG)" ]; then \
		SC1_CFG="$(SC1_CFG)" ./scripts/cfg/config.sh; \
	else \
		./scripts/cfg/config.sh; \
	fi

debug: ## Launch the debug container terminal
	./scripts/debug.sh

fsw:  ## Build Flight Software binaries
	./cfg/build/fsw_build.sh

gcov: ## Build Code Coverage results
	cd $(COVERAGEDIR) && ctest -O ctest.log
	lcov -c --directory . --output-file $(COVERAGEDIR)/coverage.info
	genhtml $(COVERAGEDIR)/coverage.info --output-directory $(COVERAGEDIR)/results

code-coverage: ## Build code coverage file, does not work via shared folders in VirtualBox
	$(MAKE) build-test 
	$(MAKE) test-fsw
	mkdir -p docs/coverage
	gcovr --gcov-ignore-parse-errors --merge-mode-functions=merge-use-line-0 --xml-pretty -o docs/coverage/coverage_report.xml
	gcovr --gcov-ignore-parse-errors --merge-mode-functions=merge-use-line-0 --html --html-details -o docs/coverage/coverage_report.html
	chmod 777 ./docs/coverage/coverage_report.*

gsw: ## Build Ground Software binaries
	./scripts/gsw/build_cryptolib.sh
	./cfg/build/gsw_build.sh

help: ## Display this help message
	@echo ""
	@echo "Usage: make <target>"
	@echo ""
	@printf "%-20s %s\n" "all"           "Build everything: config, fsw, sim, gsw"
	@printf "%-20s %s\n" "clean"         "Clean all build files and configurations"
	@printf "%-20s %s\n" "help"          "Display this help message"
	@printf "%-20s %s\n" "help-all"      "Display advanced help information"
	@printf "%-20s %s\n" "launch"        "Launch NOS3 System"
	@printf "%-20s %s\n" "prep"          "Prepare full development environment"
	@printf "%-20s %s\n" "save-run"      "Archive current sim run logs, then stop (RUN_LABEL=name optional)"
	@printf "%-20s %s\n" "load-run"      "Load a saved run into Kibana (RUN=<name> required)"
	@printf "%-20s %s\n" "stop"          "Stop entire system"
	@printf "%-20s %s\n" "uninstall"     "Remove all build artifacts and containers"
	@echo ""

help-all: ## Displays advanced help information
	@echo ""
	@echo "Usage: make <target>"
	@echo ""

	@echo "Setup:"
	@printf "\t%-20s %s\n" "prep"           "Prepare full development environment"
	@printf "\t%-20s %s\n" "prep-gsw"       "Prepare Ground Software"
	@printf "\t%-20s %s\n" "prep-sat"       "Prepare Satellite IP Routes"
	@printf "\t%-20s %s\n" "start-gsw"      "Launch Ground Software"
	@printf "\t%-20s %s\n" "start-sat"      "Satellite Launch"

	@echo ""
	@echo "Building:"
	@printf "\t%-20s %s\n" "build-cryptolib" "Build CryptoLib Component"
	@printf "\t%-20s %s\n" "build-fsw"      "Build the Flight Software (cFS or F')"
	@printf "\t%-20s %s\n" "build-sim"      "Build Simulator"
	@printf "\t%-20s %s\n" "build-test"     "Build Unit Tests"
	@printf "\t%-20s %s\n" "config"			"Configuration Setup.  Optional: [SC1_CFG=spacecraft/sc-minimal-config.xml]"
	@printf "\t%-20s %s\n" "igniter"        "Launch Configuration GUI Igniter"

	@echo ""
	@echo "Running:"
	@printf "\t%-20s %s\n" "checkout"       "Run a checkout application (may require reconfiguration)"
	@printf "\t%-20s %s\n" "debug"          "Launch the debug container terminal"
	@printf "\t%-20s %s\n" "fsw"            "Build Flight Software Binaries"
	@printf "\t%-20s %s\n" "gcov"           "Build Code Coverage Results"
	@printf "\t%-20s %s\n" "gsw"            "Build Ground Software Binaries"
	@printf "\t%-20s %s\n" "sim"            "Build Simulation Binaries"
	@printf "\t%-20s %s\n" "cosmos-operator" "Launch as COSMOS operator viewpoint - requires configuration"
	@printf "\t%-20s %s\n" "yamcs-operator" "Launch as YAMCS operator viewpoint - requires configuration"

	@echo ""
	@echo "Testing:"
	@printf "\t%-20s %s\n" "code-coverage"  "Produce code coverage report (does not work via shared folders)"
	@printf "\t%-20s %s\n" "ci-launch"      "Headless Launch for System Testing"
	@printf "\t%-20s %s\n" "system-tests"   "GUI Launch for System Testing"
	@printf "\t%-20s %s\n" "test-fsw"       "Test Flight Software"

	@echo ""
	@echo "ELK / Kibana:"
	@printf "\t%-25s %s\n" "start-elk"              "Bring up ELK stack idempotently with health check"
	@printf "\t%-25s %s\n" "build-kibana-dashboards" "(Re)build the 15 NOS3 Kibana dashboards via Saved Objects API"
	@printf "\t%-25s %s\n" "list-kibana-dashboards"  "List the dashboard catalog the builder owns"
	@printf "\t%-25s %s\n" "load-dashboards"         "Legacy ndjson importer (prefer build-kibana-dashboards)"
	@printf "\t%-25s %s\n" "refresh-kibana-fields"   "Refresh nos3-telemetry-* index-pattern field cache"

	@echo ""
	@echo "Cleanup:"
	@printf "\t%-20s %s\n" "clean"          "Clean all build files and configurations"
	@printf "\t%-20s %s\n" "clean-docker"   "Prune exited containers, dangling images, build cache"
	@printf "\t%-20s %s\n" "clean-fsw"      "Clean only Flight Software build artifacts"
	@printf "\t%-20s %s\n" "clean-gsw"      "Clean only Ground Software build artifacts"
	@printf "\t%-20s %s\n" "clean-sim"      "Clean only Simulation build artifacts"
	@printf "\t%-20s %s\n" "log"            "Log Outputs"
	@printf "\t%-20s %s\n" "purge-logs"     "Truncate live telemetry/log files (preserves nos3_runs archives)"
	@printf "\t%-20s %s\n" "save-run"       "Archive current sim run logs, then stop (telemetry preserved)"
	@printf "\t%-20s %s\n" "load-run"       "Load a saved run into Kibana: make load-run RUN=<name>"
	@printf "\t%-20s %s\n" "stop"           "Stop entire System (use KEEP_TLM=1 to preserve indices)"
	@printf "\t%-20s %s\n" "stop-gsw"       "Stop Ground Software"
	@printf "\t%-20s %s\n" "stop-sim"       "Stop sim only (preserves ELK, logs, indices)"
	@printf "\t%-20s %s\n" "uninstall"      "Remove all build artifacts and containers"
	@echo ""

igniter: ## Launch Configuration GUI Igniter
	./scripts/cfg/igniter_launch.sh

launch: ## Launch NOS3 System
	@pkill -f '[g]od_view_capture\.py' 2>/dev/null || true
	@pkill -f '[c]fs_evs_capture\.sh'  2>/dev/null || true
	@pkill -f '[s]im_logs_capture\.sh' 2>/dev/null || true
	@pkill -f '[c]pu_monitor\.sh'      2>/dev/null || true
	@mkdir -p omni_logs attack_logs
	@touch attack_logs/cfs_god_view.json omni_logs/tlm_hk_decoded.log
	@: > omni_logs/tlm_hk_decoded.log 2>/dev/null || true
	@: > attack_logs/cfs_god_view.json 2>/dev/null || true
	@sudo rm -f $(CURDIR)/fsw/build/exe/cpu1/data/mgr.bin 2>/dev/null || true
	@$(MAKE) --no-print-directory purge-logs
	@$(MAKE) --no-print-directory start-elk
	@echo "Deleting nos3-telemetry-* indices so Kibana starts clean..."
	@curl -s -X DELETE 'http://localhost:9200/nos3-telemetry-*' > /dev/null 2>&1 || true
	@echo "Removing stale cosmos-openc3-operator-1 container so launch script can recreate it..."
	@docker rm -f cosmos-openc3-operator-1 > /dev/null 2>&1 || true
	./cfg/build/launch.sh
	@echo "Starting cFS EVS capture (omni_logs/cfs_evs.log)..."
	@./scripts/cfs_evs_capture.sh &
	@echo "Starting simulator telemetry capture (omni_logs/nos3-*.log)..."
	@./scripts/sim_logs_capture.sh &
	@echo "Starting cFS CPU monitor (omni_logs/cpu_monitor.log)..."
	@./scripts/cpu_monitor.sh &
	@echo "Starting cFS Software Bus god-view capture (attack_logs/cfs_god_view.json)..."
	@sleep 10 && python3 $(CURDIR)/scripts/god_view_capture.py >> omni_logs/god_view.log 2>&1 &
	@echo "Waiting 15s for capture scripts to populate log files before Logstash rescan..."
	@sleep 15
	@echo "Restarting Logstash so it picks up freshly-created log files..."
	@docker restart nos3-logstash > /dev/null 2>&1 || true
	@for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do \
	  if curl -fsS http://localhost:9600/_node/stats > /dev/null 2>&1; then \
	    echo "Logstash is responsive."; break; \
	  fi; \
	  sleep 2; \
	done
	@$(MAKE) --no-print-directory build-kibana-dashboards
	@$(MAKE) --no-print-directory refresh-kibana-fields

start-elk: ## Bring up the ELK stack (idempotent). Waits for Elasticsearch to leave RED so the launch target can DELETE stale indices and Logstash can index fresh telemetry. No-op if nos3-elasticsearch is already running.
	@docker network inspect nos3-core > /dev/null 2>&1 || docker network create nos3-core > /dev/null
	@if ! docker ps --format '{{.Names}}' | grep -q '^nos3-elasticsearch$$'; then \
	  echo "Starting ELK stack..."; \
	  (cd elk && docker compose up -d > /dev/null); \
	fi
	@for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do \
	  status=$$(curl -fsS http://localhost:9200/_cluster/health 2>/dev/null | python3 -c 'import sys,json; print(json.load(sys.stdin).get("status",""))' 2>/dev/null); \
	  if [ "$$status" = "yellow" ] || [ "$$status" = "green" ]; then \
	    echo "Elasticsearch is $$status."; break; \
	  fi; \
	  sleep 2; \
	done

refresh-kibana-fields: ## Re-populate field cache on nos3-telemetry* index-patterns (idempotent). Closes a daily-rollover bug where Lens panels render "No data" until the saved-object's cached field list is rewritten. Safe to run any time.
	@echo "Refreshing Kibana index-pattern field caches..."
	@for i in 1 2 3 4 5 6 7 8 9 10; do \
	  curl -fsS http://localhost:5601/api/status > /dev/null 2>&1 && break; \
	  sleep 2; \
	done
	@python3 elk/refresh_index_pattern_fields.py || \
	  echo "  WARN: field-cache refresh failed; run 'make refresh-kibana-fields' once telemetry is flowing."

build-kibana-dashboards: ## (Re)build all 15 NOS3 Kibana dashboards via Saved Objects API. Requires Kibana up. Idempotent. Override with DASHBOARDS=id1,id2 (only) or SKIP=id1,id2 (exclude).
	@echo "Building NOS3 Kibana dashboards via Saved Objects API..."
	@python3 elk/build_kibana_dashboards.py \
	  $(if $(DASHBOARDS),--only $(DASHBOARDS)) \
	  $(if $(SKIP),--exclude $(SKIP))

list-kibana-dashboards: ## Print the catalog of Kibana saved objects this builder owns (15 dashboards + 1 saved search).
	@python3 elk/build_kibana_dashboards.py --list

load-dashboards: ## Legacy: import the checked-in nos3-eo1-dashboards.ndjson via Kibana _import. Prefer build-kibana-dashboards.
	@echo "Loading EO1 Kibana dashboards from elk/dashboards/nos3-eo1-dashboards.ndjson..."
	@python3 elk/load_dashboards.py

purge-logs: ## Truncate live telemetry/log files (leaves $$HOME/nos3_runs archives alone)
	@echo "Truncating attack_logs/cfs_god_view.json and omni_logs/*.log..."
	@: > attack_logs/cfs_god_view.json 2>/dev/null || true
	@for f in omni_logs/*.log; do [ -f "$$f" ] && : > "$$f"; done; true
	@echo "Done."

clean-docker: ## Prune exited containers, dangling images, build cache (keeps volumes and in-use images)
	@echo "Removing exited and created containers..."
	@docker ps -a --filter status=exited  --format '{{.Names}}' | xargs -r docker rm -f
	@docker ps -a --filter status=created --format '{{.Names}}' | xargs -r docker rm -f
	@echo "Pruning dangling images and build cache..."
	@docker container prune -f
	@docker image prune -f
	@docker builder prune -f
	@echo "Done. Run 'docker system df' to confirm."

log: ## Log outputs
	./scripts/log.sh

prep: ## Prepare full development enviornment
	./scripts/cfg/prepare.sh

prep-gsw: ## Prepare Ground Software
	./scripts/cfg/prep_gsw.sh

prep-sat: ## Prepare Satelite IP Routes
	./scripts/cfg/prep_sat.sh

sim: ## Build Sims
	./scripts/build_sim.sh

start-gsw: ## Launch Ground Software
	./scripts/gsw/launch_gsw.sh

start-sat: ## Satellite Launch
	./scripts/fsw/launch_sat.sh

NOS3_RUNS_DIR ?= $(HOME)/nos3_runs
RUN_LABEL     ?= run_$(shell date +%Y%m%d_%H%M%S)

save-run: ## Archive current sim run logs then stop (telemetry indices preserved). Optional: RUN_LABEL=my_attack_scenario
	@DEST=$(NOS3_RUNS_DIR)/$(RUN_LABEL); \
	echo "Saving run to $$DEST ..."; \
	mkdir -p $$DEST/omni_logs; \
	cp -r omni_logs/*.log $$DEST/omni_logs/ 2>/dev/null || true; \
	cp attack_logs/cfs_god_view.json $$DEST/ 2>/dev/null || true; \
	echo "Run label : $(RUN_LABEL)"          > $$DEST/metadata.txt; \
	echo "Saved at  : $$(date)"             >> $$DEST/metadata.txt; \
	echo "Git commit: $$(git rev-parse --short HEAD 2>/dev/null || echo n/a)" >> $$DEST/metadata.txt; \
	echo "Done. Stopping system (keeping telemetry indices for this saved run)..."; \
	$(MAKE) stop KEEP_TLM=1

load-run: ## Load a saved run into Kibana (ELK only, no sim). Usage: make load-run RUN=<name>
	@if [ -z "$(RUN)" ]; then \
	  echo ""; \
	  echo "Usage: make load-run RUN=<run_name>"; \
	  echo ""; \
	  echo "Available runs:"; \
	  ls $(NOS3_RUNS_DIR) 2>/dev/null || echo "  (none found in $(NOS3_RUNS_DIR))"; \
	  echo ""; \
	  exit 1; \
	fi
	@SRC=$(NOS3_RUNS_DIR)/$(RUN); \
	if [ ! -d $$SRC ]; then \
	  echo "Run '$(RUN)' not found in $(NOS3_RUNS_DIR)"; exit 1; \
	fi; \
	echo "Loading run: $(RUN)"; \
	cat $$SRC/metadata.txt 2>/dev/null && echo ""; \
	rm -f omni_logs/*.log; \
	cp -r $$SRC/omni_logs/*.log omni_logs/ 2>/dev/null || true; \
	cp $$SRC/cfs_god_view.json attack_logs/ 2>/dev/null || true; \
	echo "Deleting old telemetry indices..."; \
	curl -s -X DELETE "http://localhost:9200/nos3-telemetry-*" > /dev/null 2>&1 || true; \
	echo "Starting ELK stack and restarting Logstash..."; \
	cd elk && docker compose up -d; \
	docker restart nos3-logstash > /dev/null 2>&1 || true; \
	echo ""; \
	echo "Logstash is re-indexing. Kibana will be ready at http://localhost:5601 in ~30s"
	@$(MAKE) --no-print-directory refresh-kibana-fields

stop: ## Stop entire system. Deletes nos3-telemetry-* indices unless KEEP_TLM=1 (save-run sets it)
	@if [ -z "$(KEEP_TLM)" ]; then \
	  echo "Deleting nos3-telemetry-* indices (pass KEEP_TLM=1 to preserve)..."; \
	  curl -s -X DELETE 'http://localhost:9200/nos3-telemetry-*' > /dev/null 2>&1 || true; \
	else \
	  echo "Preserving nos3-telemetry-* indices (KEEP_TLM=$(KEEP_TLM))"; \
	fi
	@pkill -f '[g]od_view_capture\.py' 2>/dev/null || true
	@pkill -f '[c]fs_evs_capture\.sh'  2>/dev/null || true
	@pkill -f '[s]im_logs_capture\.sh' 2>/dev/null || true
	@pkill -f '[c]pu_monitor\.sh'      2>/dev/null || true
	./scripts/stop.sh
	@if [ -f ./cfg/build/current_config_path.txt ]; then \
	  echo "Cleaning up temporary config file..."; \
	  CONFIG_FILE=$$(cat ./cfg/build/current_config_path.txt | tr -d '\n'); \
	  if [ "$$(basename $$CONFIG_FILE)" != "nos3-mission.xml" ]; then \
	    echo "Removing $$CONFIG_FILE"; \
	    rm -f "$$CONFIG_FILE"; \
	  fi; \
	  rm -f ./cfg/build/current_config_path.txt; \
	fi

stop-gsw: ## Stop Ground Sfotware
	./scripts/gsw/stop_gsw.sh

stop-sim: ## Stop the running sim only. Preserves in-progress build containers, ELK, omni_logs/attack_logs, and nos3-telemetry-* indices. Use 'make stop' for a full teardown.
	@pkill -f '[g]od_view_capture\.py' 2>/dev/null || true
	@pkill -f '[c]fs_evs_capture\.sh'  2>/dev/null || true
	@pkill -f '[s]im_logs_capture\.sh' 2>/dev/null || true
	@pkill -f '[c]pu_monitor\.sh'      2>/dev/null || true
	bash ./scripts/stop_sim.sh

test-fsw: ## Test Flight Software 
	cd $(COVERAGEDIR) && ctest --output-on-failure -O ctest.log

uninstall: ## Remove all build artifacts
	$(MAKE) clean
	./scripts/cfg/uninstall.sh