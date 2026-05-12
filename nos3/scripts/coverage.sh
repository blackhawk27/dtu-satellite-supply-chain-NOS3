#!/bin/bash
#
# Run the cFS code-coverage workflow inside the NOS3 build container.
#
# The host has neither the cFS toolchain nor gcovr/lcov, so all build,
# test, and report steps execute inside ivvitc/nos3-64. This script is
# the host-side wrapper invoked by `make code-coverage`; the in-container
# work is handled by the `code-coverage-internal` Makefile target.

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
source "$SCRIPT_DIR/env.sh"

if [ ! -d "$USER_NOS3_DIR" ]; then
    echo ""
    echo "    Need to run make prep first!"
    echo ""
    exit 1
fi

if [ ! -d "$BASE_DIR/cfg/build" ]; then
    echo ""
    echo "    Need to run make config first!"
    echo ""
    exit 1
fi

# Run the coverage build, tests, and report generation inside the container.
#
# Note: we deliberately do not reuse $DFLAGS_CPUS here because it includes
# `-it`, which fails in non-TTY contexts (CI, hooks, automated runs). The
# coverage workflow has no interactive prompts, so plain `--rm` suffices.
USER_ID=$(id -u "$(stat -c '%U' "$SCRIPT_DIR/env.sh")")
GROUP_ID=$(getent group "$(stat -c '%G' "$SCRIPT_DIR/env.sh")" | cut -d: -f3)

# Redirect the cFS coverage build tree off the project mount onto a
# Linux-native filesystem. On WSL2/devcontainer setups the project lives on
# Windows via 9p, where cmake's many tiny file ops slow the cFS configure
# step from minutes to 30+ minutes. NOS3_COV_BUILD_DIR can override this if
# a user wants the artifacts somewhere specific. On native Linux this is
# also fine because $HOME is on the same disk.
COV_HOST_DIR="${NOS3_COV_BUILD_DIR:-$HOME/.nos3-cov-build}"
mkdir -p "$COV_HOST_DIR"

# The bind-mount maps the fast Linux-native dir to $BASE_DIR/fsw/build-cov
# inside the container, so the Makefile target's $(COVBUILDDIR) path works
# unchanged but the storage is fast.
$DCALL run --rm \
    -v /etc/passwd:/etc/passwd:ro \
    -v /etc/group:/etc/group:ro \
    -u "$USER_ID:$GROUP_ID" \
    --cpus="$NUM_CPUS" \
    -v "$BASE_DIR:$BASE_DIR" \
    -v "$COV_HOST_DIR:$BASE_DIR/fsw/build-cov" \
    --name "nos_code_coverage" \
    -w "$BASE_DIR" \
    "$DBOX" make -j"$NUM_CPUS" code-coverage-internal
