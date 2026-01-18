#!/bin/bash

# TaskMessenger Manager Launcher Script for Linux
# This script checks for required configuration and launches the manager

set -e

# Configuration
CONFIG_DIR="$HOME/.config/task-messenger"
CONFIG_FILE="$CONFIG_DIR/config-manager.json"
INSTALL_DIR="$HOME/.local/share/task-messenger"

# Colors
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Get the directory of this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Determine manager executable path
if [ -f "$SCRIPT_DIR/../manager/tm-manager" ]; then
    # Running from installation directory
    MANAGER_BIN="$SCRIPT_DIR/../manager/tm-manager"
elif [ -f "$INSTALL_DIR/tm-manager/tm-manager" ]; then
    # Running from default installation
    MANAGER_BIN="$INSTALL_DIR/tm-manager/tm-manager"
elif command -v tm-manager &> /dev/null; then
    # Manager is in PATH
    MANAGER_BIN="tm-manager"
else
    echo -e "${RED}ERROR: Could not find manager executable${NC}" >&2
    exit 1
fi

# Check if config file exists
if [ ! -f "$CONFIG_FILE" ]; then
    echo -e "${YELLOW}WARNING: Configuration file not found: $CONFIG_FILE${NC}" >&2
    echo "Please create a configuration file or run the manager with appropriate arguments." >&2
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

# Launch manager with all passed arguments
exec "$MANAGER_BIN" "$@"
