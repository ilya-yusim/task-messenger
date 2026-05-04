#!/bin/bash

# TaskMessenger Worker Farm Launcher Script

set -e

INSTALL_DIR="$HOME/.local/share/task-messenger"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Determine worker-farm executable path
if [ -f "$SCRIPT_DIR/../bin/tm-worker-farm" ]; then
    WORKER_FARM_BIN="$SCRIPT_DIR/../bin/tm-worker-farm"
elif [ -f "$INSTALL_DIR/tm-worker/bin/tm-worker-farm" ]; then
    WORKER_FARM_BIN="$INSTALL_DIR/tm-worker/bin/tm-worker-farm"
elif command -v tm-worker-farm &> /dev/null; then
    WORKER_FARM_BIN="tm-worker-farm"
else
    echo "ERROR: Could not find worker-farm executable" >&2
    exit 1
fi

exec "$WORKER_FARM_BIN" "$@"