#!/usr/bin/env bash
# Run a game folder with the Linux PC viewer.
#   tools/run-linux.sh <game_dir> [engine options]
# Build first with tools/build-desktop.sh (ASAN=1 for sanitizers).
set -euo pipefail
cd "$(dirname "$0")/.."
if [ $# -lt 1 ]; then
    echo "usage: $0 <game_dir> [engine options]" >&2
    exit 1
fi
export KK4S_ROOT="${KK4S_ROOT:-$PWD/run-data}"
mkdir -p "$KK4S_ROOT"
exec ./out-desktop/kk4s "$@"
