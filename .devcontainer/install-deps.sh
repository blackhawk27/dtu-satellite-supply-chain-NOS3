#!/usr/bin/env bash
# Devcontainer post-create dependency installer.
#
# Why this script exists: archive.ubuntu.com is unreachable from this
# corporate network (TCP connects, server returns empty replies), so a
# plain `apt-get update` fails on rebuild. mirrors.edge.kernel.org over
# HTTPS works, so we add it as a secondary apt source before installing.
# If archive.ubuntu.com later becomes reachable, both sources coexist
# fine; apt picks the highest version available.
#
# Run from postCreateCommand in .devcontainer/devcontainer.json.

set -euo pipefail

sudo tee /etc/apt/sources.list.d/kernel-mirror.list >/dev/null <<'EOF'
deb https://mirrors.edge.kernel.org/ubuntu/ jammy main universe
deb https://mirrors.edge.kernel.org/ubuntu/ jammy-updates main universe
deb https://mirrors.edge.kernel.org/ubuntu/ jammy-security main universe
deb https://mirrors.edge.kernel.org/ubuntu/ jammy-backports main universe
EOF

sudo apt-get update
sudo apt-get install -y \
    python3-pip python3-venv python3-dev \
    make git build-essential \
    tmux terminator \
    x11-xserver-utils x11-apps x11-utils

# Disable bridge-nf-call-iptables so intra-bridge container traffic on
# Docker user-defined networks (nos3-sc01) actually flows. With it on
# (kernel default in this WSL2 + Docker-in-Docker setup) SYNs leave the
# source veth but never arrive at the destination veth: iptables drops
# bridged packets without any visible counter increment, so every sim
# fails to connect to fortytwo and 42 hangs in inet_csk_accept before
# ever reaching HandoffToGui (no Cam/Map/UnitSphere windows).
# Reapplied here on each rebuild because docker startup can re-enable it.
sudo sysctl -w net.bridge.bridge-nf-call-iptables=0
sudo tee /etc/sysctl.d/99-nos3-bridge.conf >/dev/null <<'EOF'
net.bridge.bridge-nf-call-iptables = 0
EOF
