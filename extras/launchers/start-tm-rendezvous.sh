#!/bin/bash

# TaskMessenger Rendezvous Launcher Script for Linux
# This script checks for required configuration and launches the rendezvous service

set -e

# Configuration
CONFIG_DIR="$HOME/.config/task-messenger"
CONFIG_FILE="$CONFIG_DIR/config-rendezvous.json"
INSTALL_DIR="$HOME/.local/share/task-messenger"

# Colors
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Get the directory of this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Determine rendezvous executable path
if [ -f "$SCRIPT_DIR/../rendezvous/tm-rendezvous" ]; then
    RENDEZVOUS_BIN="$SCRIPT_DIR/../rendezvous/tm-rendezvous"
elif [ -f "$INSTALL_DIR/tm-rendezvous/tm-rendezvous" ]; then
    RENDEZVOUS_BIN="$INSTALL_DIR/tm-rendezvous/tm-rendezvous"
elif command -v tm-rendezvous &> /dev/null; then
    RENDEZVOUS_BIN="tm-rendezvous"
else
    echo -e "${RED}ERROR: Could not find tm-rendezvous executable${NC}" >&2
    exit 1
fi

# Check if config file exists
if [ ! -f "$CONFIG_FILE" ]; then
    echo -e "${YELLOW}WARNING: Configuration file not found: $CONFIG_FILE${NC}" >&2
    echo "Run the installer to deploy the default rendezvous config, or pass -c <path>." >&2
fi

# Launch rendezvous with all passed arguments
exec "$RENDEZVOUS_BIN" "$@"
