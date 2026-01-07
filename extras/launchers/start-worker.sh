#!/bin/bash

# TaskMessenger Worker Launcher Script for Linux
# This script checks for required configuration and launches the worker

set -e

# Configuration
CONFIG_DIR="$HOME/.config/task-messenger"
CONFIG_FILE="$CONFIG_DIR/config-worker.json"
INSTALL_DIR="$HOME/.local/share/task-messenger"

# Colors
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Get the directory of this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Determine worker executable path
if [ -f "$SCRIPT_DIR/../worker/worker" ]; then
    # Running from installation directory
    WORKER_BIN="$SCRIPT_DIR/../worker/worker"
elif [ -f "$INSTALL_DIR/worker/worker" ]; then
    # Running from default installation
    WORKER_BIN="$INSTALL_DIR/worker/worker"
elif command -v worker &> /dev/null; then
    # Worker is in PATH
    WORKER_BIN="worker"
else
    echo -e "${RED}ERROR: Could not find worker executable${NC}" >&2
    exit 1
fi

# Check if config file exists
if [ ! -f "$CONFIG_FILE" ]; then
    echo -e "${YELLOW}WARNING: Configuration file not found: $CONFIG_FILE${NC}" >&2
    echo "Please create a configuration file or run the worker with appropriate arguments." >&2
    echo "" >&2
    echo "You can create a template config by running the installation script," >&2
    echo "or create it manually with the following structure:" >&2
    echo "" >&2
    cat << EOF >&2
{
  "network": {
    "zerotier_network_id": "your_network_id",
    "zerotier_identity_path": ""
  },
  "logging": {
    "level": "info",
    "file": ""
  }
}
EOF
    echo "" >&2
fi

# Launch worker with all passed arguments
exec "$WORKER_BIN" "$@"
