# TaskMessenger Installation Guide

This document provides instructions for installing TaskMessenger on Windows, Linux, and macOS systems.

## Table of Contents

- [Overview](#overview)
- [Prerequisites](#prerequisites)
- [Installation](#installation)
  - [Windows Installation](#windows-installation)
  - [Linux Installation](#linux-installation)
  - [macOS Installation](#macos-installation)
- [Configuration](#configuration)
- [Running TaskMessenger](#running-taskmessenger)
- [Upgrading](#upgrading)
- [Uninstallation](#uninstallation)
- [Troubleshooting](#troubleshooting)

## Overview

TaskMessenger consists of two components:
- **Worker**: The task processing client that executes assigned tasks
- **Manager**: The task distribution server that coordinates task assignment

Each component can be installed independently based on your needs. Both components are installed as user applications (not system services) and run on-demand.

### Installation Locations

**Windows:**
- Binaries: `%LOCALAPPDATA%\TaskMessenger\{component}\`
- Configuration: `%APPDATA%\task-messenger\`
- Start Menu: `Start Menu\Programs\TaskMessenger\`

**Linux:**
- Binaries: `~/.local/share/task-messenger/{component}/`
- Configuration: `~/.config/task-messenger/`
- Symlinks: `~/.local/bin/{component}`
- Desktop entries: `~/.local/share/applications/`

**macOS:**
- Binaries: `~/Library/Application Support/TaskMessenger/{component}/`
- Configuration: `~/Library/Application Support/TaskMessenger/config/`
- Symlinks: `~/.local/bin/{component}`

## Prerequisites

**Windows:**
- Windows 10 or later (64-bit)
- Network connectivity for ZeroTier

**Linux:**
- A 64-bit Linux distribution (tested on Ubuntu 24.04 LTS)
- Bash shell
- Network connectivity for ZeroTier

**macOS:**
- macOS 11 (Big Sur) or later
- Apple Silicon (ARM64) or Intel (x86_64)
- Network connectivity for ZeroTier

## Installation

### Windows Installation

TaskMessenger provides self-extracting `.exe` installers for Windows.

#### Step 1: Download the Installer

Download the appropriate installer for your component:
- `tm-worker-v1.0.0-windows-x64-installer.exe`
- `tm-manager-v1.0.0-windows-x64-installer.exe`

#### Step 2: Run the Installer

1. Double-click the downloaded `.exe` file
2. If Windows SmartScreen appears, click "More info" then "Run anyway"
3. The installer will automatically extract and install the application
4. Follow any on-screen prompts

The installer will:
- Install the component to `%LOCALAPPDATA%\TaskMessenger\{component}\`
- Add the installation directory to your user PATH
- Create a Start Menu shortcut
- Create a configuration template at `%APPDATA%\task-messenger\config-{component}.json`

#### Step 3: Configure the Application

Edit the configuration file using Notepad or your preferred text editor:

```powershell
notepad %APPDATA%\task-messenger\config-worker.json
# or
notepad %APPDATA%\task-messenger\config-manager.json
```

See [Configuration](#configuration) section for details.

### Linux Installation

TaskMessenger provides self-extracting `.run` installers for Linux.

#### Step 1: Download the Installer

Download the appropriate installer for your component:
- `tm-worker-v1.0.0-linux-x64-installer.run`
- `tm-manager-v1.0.0-linux-x64-installer.run`

#### Step 2: Make Executable and Run

```bash
chmod +x tm-worker-v1.0.0-linux-x64-installer.run
./tm-worker-v1.0.0-linux-x64-installer.run
```

Or for manager:
```bash
chmod +x tm-manager-v1.0.0-linux-x64-installer.run
./tm-manager-v1.0.0-linux-x64-installer.run
```

The installer will automatically extract and install the application.

**Advanced options:**
```bash
# Extract to a custom temporary location
./tm-worker-v1.0.0-linux-x64-installer.run --target /tmp/custom

# Keep extracted files for inspection (don't auto-delete)
./tm-worker-v1.0.0-linux-x64-installer.run --keep

# Extract only, don't run installer
./tm-worker-v1.0.0-linux-x64-installer.run --noexec

# See all available options
./tm-worker-v1.0.0-linux-x64-installer.run --help
```

#### What the Installer Does

The installer will:
1. Check for existing installations and offer to upgrade
2. Install the component to `~/.local/share/task-messenger/{component}/`
3. Create a symlink in `~/.local/bin/`
4. Install a desktop entry
5. Create a configuration template at `~/.config/task-messenger/config-{component}.json`

#### Step 3: Configure PATH (if needed)

If `~/.local/bin` is not in your PATH, add it to your shell configuration:

**For Bash** (`~/.bashrc`):
```bash
export PATH="$HOME/.local/bin:$PATH"
```

**For Zsh** (`~/.zshrc`):
```bash
export PATH="$HOME/.local/bin:$PATH"
```

Then reload your shell:
```bash
source ~/.bashrc  # or source ~/.zshrc
```

#### Step 4: Configure the Application

Edit the configuration file:
```bash
nano ~/.config/task-messenger/config-worker.json
# or
nano ~/.config/task-messenger/config-manager.json
```

See [Configuration](#configuration) section for details.

### macOS Installation

TaskMessenger provides `.command` installers for macOS that can be run by double-clicking.

#### Step 1: Download the Installer

Download the appropriate installer for your component and architecture:
- `tm-worker-v1.0.0-macos-arm64.command` (Apple Silicon)
- `tm-manager-v1.0.0-macos-arm64.command` (Apple Silicon)

#### Step 2: Run the Installer

**Option A: Double-click (Recommended)**
1. Double-click the downloaded `.command` file
2. If prompted about security, go to System Settings → Privacy & Security and click "Open Anyway"
3. The installer will open a Terminal window and run automatically
4. Follow any on-screen prompts

**Option B: Terminal**
```bash
chmod +x tm-worker-v1.0.0-macos-arm64.command
./tm-worker-v1.0.0-macos-arm64.command
```

The installer will:
1. Check for existing installations and offer to upgrade
2. Install the component to `~/Library/Application Support/TaskMessenger/{component}/`
3. Create a symlink in `~/.local/bin/`
4. Create a configuration template

#### Step 3: Configure PATH (if needed)

If `~/.local/bin` is not in your PATH, add it to your shell configuration:

**For Zsh** (`~/.zshrc` - default on modern macOS):
```bash
export PATH="$HOME/.local/bin:$PATH"
```

**For Bash** (`~/.bash_profile` or `~/.bashrc`):
```bash
export PATH="$HOME/.local/bin:$PATH"
```

Then reload your shell:
```bash
source ~/.zshrc  # or source ~/.bash_profile
```

#### Step 4: Configure the Application

Edit the configuration file:
```bash
nano ~/Library/Application\ Support/TaskMessenger/config/config-worker.json
# or
nano ~/Library/Application\ Support/TaskMessenger/config/config-manager.json
```

See [Configuration](#configuration) section for details.

## Configuration

Both manager and worker require configuration before first use. The installation creates a template configuration file that you must edit.

### Configuration File Structure

```json
{
  "network": {
    "zerotier_network_id": "your_16_digit_network_id",
    "zerotier_identity_path": ""
  },
  "logging": {
    "level": "info",
    "file": ""
  }
}
```

### Required Settings

**`zerotier_network_id`**: Your ZeroTier network ID (16 hexadecimal characters)
- Obtain this from your ZeroTier Central account
- Example: `8056c2e21c000001`

### Optional Settings

**`zerotier_identity_path`**: Path to custom ZeroTier identity files
- Leave empty to use default identity files
- Manager uses bundled identity files by default

**`logging.level`**: Log verbosity
- Values: `trace`, `debug`, `info`, `warning`, `error`
- Default: `info`

**`logging.file`**: Log file path
- Leave empty to log to console only
- Specify a path to also write logs to a file

### Example Configuration

```json
{
  "network": {
    "zerotier_network_id": "8056c2e21c000001",
    "zerotier_identity_path": ""
  },
  "logging": {
    "level": "info",
    "file": "/home/user/.local/share/task-messenger/manager/manager.log"
  }
}
```

## Upgrading

When you run the installer and an existing installation is detected, you'll be prompted to upgrade.

### Upgrade Process

1. **Configuration Backup**: Your existing configuration will be automatically backed up with a timestamp
2. **Installation**: The new version will be installed, overwriting the old binaries
3. **Configuration**: Your existing configuration file is preserved

To upgrade, simply download and run the new installer - it will handle the upgrade automatically.

## Uninstallation

### Windows

Run the uninstaller from the Start Menu:
- Start → TaskMessenger → Uninstall Worker (or Uninstall Manager)

Or use the uninstall script if available in your installation directory.

### Linux

Run the uninstall script from your installation:

```bash
~/.local/share/task-messenger/worker/scripts/uninstall_linux.sh
# or
~/.local/share/task-messenger/manager/scripts/uninstall_linux.sh
```

### macOS

Run the uninstall script from your installation:

```bash
~/Library/Application\ Support/TaskMessenger/worker/scripts/uninstall_macos.sh
# or
~/Library/Application\ Support/TaskMessenger/manager/scripts/uninstall_macos.sh
```

### What Gets Removed

The uninstallation removes:
- Binary files and libraries
- Symlinks or PATH entries
- Desktop entries or shortcuts
- Installation directories

Configuration files are typically preserved unless you explicitly remove them.

## Troubleshooting

### Common Issues

#### "Command not found" after installation

**Windows:**
1. Restart your terminal/PowerShell to reload PATH
2. Alternatively, use the Start Menu shortcut

**Linux/macOS:**
1. Verify `~/.local/bin` is in your PATH:
   ```bash
   echo $PATH | grep -q "$HOME/.local/bin" && echo "In PATH" || echo "Not in PATH"
   ```
2. Add to PATH if needed (see installation instructions above)
3. Restart your terminal

#### Permission or security errors

**Windows:**
- If SmartScreen blocks the installer, click "More info" → "Run anyway"

**macOS:**
- If blocked by Gatekeeper, go to System Settings → Privacy & Security → click "Open Anyway"

**Linux:**
- Ensure the `.run` file is executable: `chmod +x installer-name.run`

#### Installer fails to run

1. Verify the downloaded file is complete and not corrupted
2. Re-download the installer if needed
3. Check that you have sufficient disk space
4. Ensure you're not running as root/Administrator (user installation only)

#### Configuration file issues

If the application can't find its configuration:

**Windows:**
```powershell
# Verify config exists
Test-Path "$env:APPDATA\task-messenger\config-worker.json"
```

**Linux:**
```bash
# Verify config exists
ls -l ~/.config/task-messenger/config-worker.json
```

**macOS:**
```bash
# Verify config exists
ls -l ~/Library/Application\ Support/TaskMessenger/config/config-worker.json
```

Create the configuration file using the template from the [Configuration](#configuration) section if it doesn't exist.

#### Connection issues

If unable to connect to ZeroTier network:
1. Verify your ZeroTier network ID is correct (16 hexadecimal characters)
2. Check network connectivity
3. Verify the network exists in ZeroTier Central
4. Ensure your device is authorized in the network

## Running TaskMessenger

### Windows

After installation, you can run the applications:

1. **From Start Menu**: Start → TaskMessenger → Worker or Manager
2. **From Command Prompt/PowerShell**: Type `tm-worker` or `tm-manager`
3. **With configuration file**:
   ```powershell
   tm-worker -c %APPDATA%\task-messenger\config-worker.json
   tm-manager -c %APPDATA%\task-messenger\config-manager.json
   ```
4. **View help**: `tm-worker --help` or `tm-manager --help`

### Linux

After installation, you can run the applications:

1. **From terminal** (if `~/.local/bin` is in PATH):
   ```bash
   tm-worker --help
   tm-manager --help
   ```

2. **With configuration file**:
   ```bash
   tm-worker -c ~/.config/task-messenger/config-worker.json
   tm-manager -c ~/.config/task-messenger/config-manager.json
   ```

3. **From desktop/application menu**: Search for "TaskMessenger Worker" or "TaskMessenger Manager"

### macOS

After installation, you can run the applications:

1. **From terminal** (if `~/.local/bin` is in PATH):
   ```bash
   tm-worker --help
   tm-manager --help
   ```

2. **With configuration file**:
   ```bash
   tm-worker -c ~/Library/Application\ Support/TaskMessenger/config/config-worker.json
   tm-manager -c ~/Library/Application\ Support/TaskMessenger/config/config-manager.json
   ```

3. **Direct path**:
   ```bash
   ~/Library/Application\ Support/TaskMessenger/worker/tm-worker --help
   ~/Library/Application\ Support/TaskMessenger/manager/tm-manager --help
   ```

## Getting Help

For additional help:

1. Run with `--help` flag: `tm-worker --help` or `tm-manager --help`
2. Check log files if logging is enabled
3. Review this installation guide
4. Contact support or file an issue on the project repository

---

**Version:** 1.0.0  
**Last Updated:** 2026
