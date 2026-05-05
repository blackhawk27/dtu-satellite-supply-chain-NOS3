#!/bin/bash
#
# Start OnAIR AI/ML framework inside the FSW container.
# Waits 20s for cFS and SBN to be ready before connecting.
#

sleep 20
python3 cf/onair/driver.py cf/onair/cfs_sample.ini
