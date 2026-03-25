#!/bin/bash

# TaskMessenger Dispatcher Launcher Script for Linux
# This script checks for required configuration and launches the dispatcher

set -e

# Configuration
CONFIG_DIR="$HOME/.config/task-messenger"
CONFIG_FILE="$CONFIG_DIR/config-dispatcher.json"
INSTALL_DIR="$HOME/.local/share/task-messenger"

# Colors
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Get the directory of this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Determine dispatcher executable path
if [ -f "$SCRIPT_DIR/../dispatcher/tm-dispatcher" ]; then
    # Running from installation directory
    DISPATCHER_BIN="$SCRIPT_DIR/../dispatcher/tm-dispatcher"
elif [ -f "$INSTALL_DIR/tm-dispatcher/tm-dispatcher" ]; then
    # Running from default installation
    DISPATCHER_BIN="$INSTALL_DIR/tm-dispatcher/tm-dispatcher"
elif command -v tm-dispatcher &> /dev/null; then
    # Dispatcher is in PATH
    DISPATCHER_BIN="tm-dispatcher"
else
    echo -e "${RED}ERROR: Could not find dispatcher executable${NC}" >&2
    exit 1
fi

# Check if config file exists
if [ ! -f "$CONFIG_FILE" ]; then
    echo -e "${YELLOW}WARNING: Configuration file not found: $CONFIG_FILE${NC}" >&2
    echo "Please create a configuration file or run the dispatcher with appropriate arguments." >&2
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

# Launch dispatcher with all passed arguments
exec "$DISPATCHER_BIN" "$@"
