#!/bin/bash -i
#
# Convenience script for NOS3 development
# Use with the Dockerfile in the deployment repository
# https://github.com/nasa-itc/deployment
#

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
source $SCRIPT_DIR/env.sh

# Kill auxiliary capture scripts if still running
pkill -f "cpu_monitor.sh"         2>/dev/null || true
pkill -f "cfs_evs_capture.sh"     2>/dev/null || true
pkill -f "sim_logs_capture.sh"    2>/dev/null || true
pkill -f "god_view_capture.py"    2>/dev/null || true
pkill -f "passive_listener.py"    2>/dev/null || true
# Passive wire-truth tcpdump sidecar (shares the FSW netns)
"$SCRIPT_DIR/start_tcpdump_sidecar.sh" stop 2>/dev/null || true

# NOS3 GPIO
rm -rf /tmp/gpio_fake

# NOS3 Stored HK
rm -rf $BASE_DIR/fsw/build/exe/cpu1/scratch/*

# Docker stop - tear down compose stacks where they live (the previous
# `cd $SCRIPT_DIR; compose down` ran from scripts/ which has no compose
# file, so ELK was never torn down by `make stop`).
(cd "$BASE_DIR/elk"     2>/dev/null && $DFLAG compose down > /dev/null 2>&1) || true
(cd "$BASE_DIR/gsw/ait" 2>/dev/null && $DFLAG compose down > /dev/null 2>&1) || true
$DCALL ps --filter ancestor="$DBOX" -aq | xargs $DCALL stop > /dev/null 2>&1 &
$DCALL ps --filter=name="sc_*" -aq | xargs $DCALL stop > /dev/null 2>&1 &
$DCALL ps --filter=name="nos_*" -aq | xargs $DCALL stop > /dev/null 2>&1 &
$DCALL ps --filter=name="ait*" -aq | xargs $DCALL stop > /dev/null 2>&1 &
$DCALL ps --filter=name="influxdb*" -aq | xargs $DCALL stop > /dev/null 2>&1 &
$DCALL ps --filter=name="ttc-command*" -aq | xargs $DCALL stop > /dev/null 2>&1 &
$DCALL ps --filter=name="openc3*" -aq | xargs $DCALL stop > /dev/null 2>&1 &
$DCALL ps --filter ancestor="ballaerospace/cosmos:4.5.0" -aq | xargs $DCALL stop > /dev/null 2>&1 &
$DCALL ps --filter=name="cosmos-openc3-operator-1" -aq | xargs $DCALL stop > /dev/null 2>&1 &

# Intentionally wait to complete
wait 

# Docker cleanup
$DCALL container prune -f > /dev/null 2>&1
$DNETWORK ls --filter=name="nos" | xargs $DNETWORK rm > /dev/null 2>&1
$DNETWORK ls --filter=name="cosmos-openc3-operator-1" | xargs $DNETWORK rm > /dev/null 2>&1
rm /dev/shm/Blackboard 2> /dev/null

# 42 (files inside are written by the root-user 42 container, so sudo is required;
# mirrors the sudo cp/rm pattern in scripts/ci_launch.sh for the same path)
sudo rm -rf "$USER_NOS3_DIR/42/NOS3InOut"
rm -rf /tmp/gpio*

# COSMOS
yes | rm $GSW_DIR/Gemfile > /dev/null 2>&1
yes | rm $GSW_DIR/Gemfile.lock > /dev/null 2>&1

# Wipe simulator and attack logs so each run starts clean
rm -f $BASE_DIR/omni_logs/*.log
rm -f $BASE_DIR/attack_logs/cfs_god_view.json

# Delete only telemetry indices from Elasticsearch so Kibana starts fresh
# but preserves dashboards, visualizations, and index patterns (.kibana index)
curl -s -X DELETE "http://localhost:9200/nos3-telemetry-*" > /dev/null 2>&1 || true

exit 0
