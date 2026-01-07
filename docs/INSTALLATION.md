# TaskMessenger Installation Guide

This document provides detailed instructions for installing TaskMessenger on Linux and Windows systems as a user application.

## Table of Contents

- [Overview](#overview)
- [Prerequisites](#prerequisites)
- [Linux Installation](#linux-installation)
- [Windows Installation](#windows-installation)
- [Configuration](#configuration)
- [Upgrading](#upgrading)
- [Uninstallation](#uninstallation)
- [Troubleshooting](#troubleshooting)

## Overview

TaskMessenger consists of two components:
- **Manager**: The task distribution server that coordinates task assignment
- **Worker**: The task processing client that executes assigned tasks

Each component can be installed independently based on your needs. Both components are installed as user applications (not system services) and run on-demand.

### Installation Locations

**Linux:**
- Binaries: `~/.local/share/task-messenger/{component}/`
- Configuration: `~/.config/task-messenger/`
- Symlinks: `~/.local/bin/{component}`
- Desktop entries: `~/.local/share/applications/`

**Windows:**
- Binaries: `%LOCALAPPDATA%\TaskMessenger\{component}\`
- Configuration: `%APPDATA%\task-messenger\`
- Start Menu: `Start Menu\Programs\TaskMessenger\`
- PATH: Automatically added to user PATH

## Prerequisites

### Linux

- A 64-bit Linux distribution (tested on Ubuntu 24.04 LTS)
- Bash shell
- tar and gzip utilities (usually pre-installed)
- Network connectivity for ZeroTier

### Windows

- Windows 10 or later (64-bit)
- PowerShell 5.1 or later (included with Windows)
- Network connectivity for ZeroTier

## Linux Installation

### Step 1: Download the Distribution Package

Download the appropriate archive for your component:
- `task-messenger-manager-v1.0.0-linux-x86_64.tar.gz`
- `task-messenger-worker-v1.0.0-linux-x86_64.tar.gz`

### Step 2: Extract the Archive

```bash
tar -xzf task-messenger-manager-v1.0.0-linux-x86_64.tar.gz
# or
tar -xzf task-messenger-worker-v1.0.0-linux-x86_64.tar.gz
```

### Step 3: Run the Installation Script

```bash
cd opt/task-messenger
./scripts/install_linux.sh manager
# or
./scripts/install_linux.sh worker
```

The script will:
1. Check for existing installations and offer to upgrade
2. Install the component to `~/.local/share/task-messenger/{component}/`
3. Create a symlink in `~/.local/bin/`
4. Install a desktop entry
5. Create a configuration template at `~/.config/task-messenger/config-{component}.json`

### Step 4: Configure PATH (if needed)

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

### Step 5: Configure the Application

Edit the configuration file:
```bash
nano ~/.config/task-messenger/config-manager.json
# or
nano ~/.config/task-messenger/config-worker.json
```

See [Configuration](#configuration) section for details.

### Custom Installation Directory

To install to a custom location:

```bash
./scripts/install_linux.sh manager --install-dir /custom/path
```

Note: Custom installations won't create desktop entries automatically.

## Windows Installation

### Step 1: Download the Distribution Package

Download the appropriate archive for your component:
- `task-messenger-manager-v1.0.0-windows-x64.zip`
- `task-messenger-worker-v1.0.0-windows-x64.zip`

### Step 2: Extract the Archive

Right-click the ZIP file and select "Extract All..." or use PowerShell:

```powershell
Expand-Archive -Path task-messenger-manager-v1.0.0-windows-x64.zip -DestinationPath .
```

### Step 3: Run the Installation Script

Open PowerShell as a **regular user** (not Administrator) and navigate to the extracted directory:

```powershell
cd TaskMessenger
.\scripts\install_windows.ps1 manager
# or
.\scripts\install_windows.ps1 worker
```

**Note:** If you get an execution policy error, run:
```powershell
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned
```

The script will:
1. Check for existing installations and offer to upgrade
2. Install the component to `%LOCALAPPDATA%\TaskMessenger\{component}\`
3. Add the installation directory to your user PATH
4. Create a Start Menu shortcut
5. Create a configuration template at `%APPDATA%\task-messenger\config-{component}.json`

### Step 4: Configure the Application

Edit the configuration file using Notepad or your preferred text editor:

```powershell
notepad $env:APPDATA\task-messenger\config-manager.json
# or
notepad $env:APPDATA\task-messenger\config-worker.json
```

See [Configuration](#configuration) section for details.

### Custom Installation Directory

To install to a custom location:

```powershell
.\scripts\install_windows.ps1 manager -InstallDir "C:\Custom\Path"
```

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

### Automatic Upgrade Detection

When you run the installation script and an existing installation is detected, you'll be prompted:

```
Found existing installation of manager (version 1.0.0)
Do you want to upgrade? This will replace the existing installation. [y/N]
```

### Upgrade Process

1. **Configuration Backup**: Your existing configuration will be automatically backed up with a timestamp:
   - Linux: `~/.config/task-messenger/config-{component}.json.backup.YYYYMMDD-HHMMSS`
   - Windows: `%APPDATA%\task-messenger\config-{component}.json.backup.YYYYMMDD-HHMMSS`

2. **Installation**: The new version will be installed, overwriting the old binaries

3. **Configuration**: Your existing configuration file is preserved

### Manual Upgrade Steps

If you prefer to upgrade manually:

**Linux:**
```bash
# Extract new version
tar -xzf task-messenger-manager-v1.1.0-linux-x86_64.tar.gz
cd opt/task-messenger

# Run installation (will prompt for upgrade)
./scripts/install_linux.sh manager
```

**Windows:**
```powershell
# Extract new version
Expand-Archive -Path task-messenger-manager-v1.1.0-windows-x64.zip -DestinationPath .
cd TaskMessenger

# Run installation (will prompt for upgrade)
.\scripts\install_windows.ps1 manager
```

### Downgrading

To downgrade to a previous version, run the installation script for the older version. The process is identical to upgrading.

## Uninstallation

### Linux

To uninstall a component:

```bash
# Navigate to installation or use the script from the distribution package
./scripts/uninstall_linux.sh manager
# or
./scripts/uninstall_linux.sh worker
```

To remove configuration files as well:

```bash
./scripts/uninstall_linux.sh manager --remove-config
```

To uninstall both components:

```bash
./scripts/uninstall_linux.sh all
```

### Windows

Open PowerShell and run:

```powershell
# Navigate to installation or use the script from the distribution package
.\scripts\uninstall_windows.ps1 manager
# or
.\scripts\uninstall_windows.ps1 worker
```

To remove configuration files as well:

```powershell
.\scripts\uninstall_windows.ps1 manager -RemoveConfig
```

To uninstall both components:

```powershell
.\scripts\uninstall_windows.ps1 all
```

### What Gets Removed

The uninstallation script removes:
- Binary files and libraries
- Symlinks (Linux) or PATH entries (Windows)
- Desktop entries (Linux) or Start Menu shortcuts (Windows)
- Installation directories (if empty)

Configuration files are preserved by default unless you use the `--remove-config` (Linux) or `-RemoveConfig` (Windows) option.

## Troubleshooting

### Linux Issues

#### "Command not found" after installation

**Problem**: The `manager` or `worker` command is not found after installation.

**Solution**: 
1. Verify `~/.local/bin` is in your PATH:
   ```bash
   echo $PATH | grep -q "$HOME/.local/bin" && echo "In PATH" || echo "Not in PATH"
   ```

2. If not in PATH, add it to your shell configuration:
   ```bash
   echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
   source ~/.bashrc
   ```

3. Alternatively, use the full path:
   ```bash
   ~/.local/share/task-messenger/manager/manager
   ```

#### "Permission denied" error

**Problem**: Installation script fails with permission denied.

**Solution**:
1. Make the script executable:
   ```bash
   chmod +x scripts/install_linux.sh
   ```

2. Ensure you're not running with sudo (user installation only)

#### Shared library not found

**Problem**: Error message like "libzt.so: cannot open shared object file"

**Solution**:
1. Verify the library was installed:
   ```bash
   ls -l ~/.local/share/task-messenger/manager/lib/libzt.so
   ```

2. Check RPATH is correctly set:
   ```bash
   readelf -d ~/.local/share/task-messenger/manager/manager | grep RPATH
   ```
   Should show: `[$ORIGIN/../lib]`

3. If RPATH is missing, reinstall using the installation script

#### Desktop entry not appearing

**Problem**: Application doesn't appear in application menu.

**Solution**:
1. Update desktop database:
   ```bash
   update-desktop-database ~/.local/share/applications
   ```

2. Log out and log back in

3. Check the desktop file exists:
   ```bash
   ls ~/.local/share/applications/task-messenger-*.desktop
   ```

### Windows Issues

#### Execution policy error

**Problem**: PowerShell script won't run due to execution policy.

**Solution**:
```powershell
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned
```

#### "Command not found" after installation

**Problem**: The `manager` or `worker` command is not found after installation.

**Solution**:
1. Restart your PowerShell terminal to reload PATH

2. If still not working, verify PATH was updated:
   ```powershell
   $env:Path -split ';' | Select-String TaskMessenger
   ```

3. Manually add to PATH if needed:
   ```powershell
   $currentPath = [Environment]::GetEnvironmentVariable("Path", "User")
   $newPath = "$env:LOCALAPPDATA\TaskMessenger\manager;$currentPath"
   [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
   ```

4. Alternatively, use the Start Menu shortcut or full path:
   ```powershell
   & "$env:LOCALAPPDATA\TaskMessenger\manager\manager.exe"
   ```

#### DLL not found error

**Problem**: Error message about missing `zt-shared.dll`.

**Solution**:
1. Verify the DLL is in the same directory as the executable:
   ```powershell
   dir "$env:LOCALAPPDATA\TaskMessenger\manager"
   ```

2. Reinstall if the DLL is missing

#### Start Menu shortcut doesn't work

**Problem**: Shortcut fails to launch the application.

**Solution**:
1. Right-click the shortcut and check "Target" path
2. Verify the executable exists at that location
3. Reinstall to recreate the shortcut

### General Issues

#### Configuration file not found warning

**Problem**: Application warns about missing configuration file.

**Solution**:
1. Create the configuration file if it doesn't exist:
   ```bash
   # Linux
   mkdir -p ~/.config/task-messenger
   nano ~/.config/task-messenger/config-manager.json
   ```
   ```powershell
   # Windows
   New-Item -ItemType Directory -Force -Path "$env:APPDATA\task-messenger"
   notepad "$env:APPDATA\task-messenger\config-manager.json"
   ```

2. Use the template from this guide or the distribution package

#### Connection to ZeroTier network fails

**Problem**: Application can't connect to ZeroTier network.

**Solution**:
1. Verify your ZeroTier network ID is correct (16 hexadecimal characters)
2. Ensure the network exists in your ZeroTier Central account
3. Check that your device is authorized in the network
4. Verify network connectivity to ZeroTier's servers
5. Check firewall settings allow ZeroTier traffic

#### Version mismatch after upgrade

**Problem**: `--version` shows old version after upgrade.

**Solution**:
1. Verify you ran the installation script for the new version
2. Check the VERSION file in the installation directory:
   ```bash
   # Linux
   cat ~/.local/share/task-messenger/manager/VERSION
   ```
   ```powershell
   # Windows
   Get-Content "$env:LOCALAPPDATA\TaskMessenger\manager\VERSION"
   ```

3. If version is correct in VERSION file but wrong in output, report this as a bug

## Running TaskMessenger

### Linux

After installation, you can run the applications in several ways:

1. **Direct command** (if `~/.local/bin` is in PATH):
   ```bash
   manager --help
   worker --help
   ```

2. **Using launcher scripts**:
   ```bash
   ~/.local/share/task-messenger/launchers/start-manager.sh
   ~/.local/share/task-messenger/launchers/start-worker.sh
   ```

3. **From desktop/application menu**: Search for "TaskMessenger Manager" or "TaskMessenger Worker"

### Windows

After installation, you can run the applications in several ways:

1. **Direct command** (from any PowerShell/Command Prompt):
   ```powershell
   manager --help
   worker --help
   ```

2. **Using launcher scripts**:
   ```powershell
   & "$env:LOCALAPPDATA\TaskMessenger\launchers\start-manager.bat"
   & "$env:LOCALAPPDATA\TaskMessenger\launchers\start-worker.bat"
   ```

3. **From Start Menu**: Start → TaskMessenger → TaskMessenger Manager/Worker

## Getting Help

For additional help:

1. Run the application with `--help` flag to see available options
2. Check the README files in the installation directory
3. Review log files (if logging to file is enabled)
4. Contact support or file an issue on the project repository

## Version Information

Current version covered by this guide: **1.0.0**

Last updated: 2026
