# TaskMessenger Windows Uninstallation Script
# This script removes TaskMessenger (manager or worker) for the current user

param(
    [Parameter(Mandatory=$false, Position=0)]
    [ValidateSet("manager", "worker", "all")]
    [string]$Component,
    
    [Parameter(Mandatory=$false)]
    [string]$InstallDir,
    
    [switch]$RemoveConfig,
    
    [switch]$Help
)

# Color output functions
function Write-Info {
    param([string]$Message)
    Write-Host "[INFO] $Message" -ForegroundColor Blue
}

function Write-Success {
    param([string]$Message)
    Write-Host "[SUCCESS] $Message" -ForegroundColor Green
}

function Write-Warning {
    param([string]$Message)
    Write-Host "[WARNING] $Message" -ForegroundColor Yellow
}

function Write-ErrorMsg {
    param([string]$Message)
    Write-Host "[ERROR] $Message" -ForegroundColor Red
}

function Show-Usage {
    @"
TaskMessenger Windows Uninstallation Script

Usage: .\uninstall_windows.ps1 [component] [OPTIONS]

Arguments:
  component              Either 'manager', 'worker', or 'all' (auto-detected if not specified)

Options:
  -InstallDir PATH       Custom installation directory (default: %LOCALAPPDATA%\TaskMessenger)
  -RemoveConfig          Also remove configuration files
  -Help                  Show this help message

Note: If component is not specified, the script will detect installed components and prompt you to choose.

Examples:
  .\uninstall_windows.ps1
  .\uninstall_windows.ps1 manager
  .\uninstall_windows.ps1 worker -RemoveConfig
  .\uninstall_windows.ps1 all -InstallDir "C:\Custom\Path"

"@
}

function Get-DefaultInstallDir {
    return Join-Path $env:LOCALAPPDATA "TaskMessenger"
}

function Get-ConfigDir {
    return Join-Path $env:APPDATA "task-messenger"
}

function Test-Installation {
    param(
        [string]$InstallDir,
        [string]$Component
    )
    
    if ($Component -eq "all") {
        $managerDir = Join-Path $InstallDir "manager"
        $workerDir = Join-Path $InstallDir "worker"
        
        if (-not (Test-Path $managerDir) -and -not (Test-Path $workerDir)) {
            Write-ErrorMsg "No TaskMessenger installation found at: $InstallDir"
            exit 1
        }
    } else {
        $componentDir = Join-Path $InstallDir $Component
        
        if (-not (Test-Path $componentDir)) {
            Write-ErrorMsg "$Component is not installed at: $componentDir"
            exit 1
        }
    }
}

function Get-InstalledVersion {
    param(
        [string]$InstallDir,
        [string]$Component
    )
    
    $versionFile = Join-Path $InstallDir "$Component\VERSION"
    
    if (Test-Path $versionFile) {
        $version = Get-Content $versionFile -Raw
        return $version.Trim()
    }
    
    return "unknown"
}

function Get-InstalledComponents {
    param([string]$InstallDir)
    
    $components = @()
    
    $managerDir = Join-Path $InstallDir "manager"
    $workerDir = Join-Path $InstallDir "worker"
    
    if (Test-Path $managerDir) {
        $components += "manager"
    }
    
    if (Test-Path $workerDir) {
        $components += "worker"
    }
    
    return $components
}

function Select-Component {
    param([string[]]$InstalledComponents)
    
    if ($InstalledComponents.Count -eq 0) {
        return $null
    }
    
    if ($InstalledComponents.Count -eq 1) {
        return $InstalledComponents[0]
    }
    
    Write-Info "Multiple components are installed:"
    Write-Host "  1. manager"
    Write-Host "  2. worker"
    Write-Host "  3. all (both)"
    Write-Host ""
    
    do {
        $choice = Read-Host "Select component to uninstall [1-3]"
        
        switch ($choice) {
            "1" { return "manager" }
            "2" { return "worker" }
            "3" { return "all" }
            default { Write-Warning "Invalid choice. Please enter 1, 2, or 3." }
        }
    } while ($true)
}

function Get-ComponentFromScriptLocation {
    param([string]$InstallDir)
    
    # Get script location
    $scriptPath = $PSCommandPath
    $scriptDir = Split-Path -Parent $scriptPath
    
    # Check if script is in a component directory
    $managerDir = Join-Path $InstallDir "manager"
    $workerDir = Join-Path $InstallDir "worker"
    
    if ($scriptDir -eq $managerDir) {
        return "manager"
    } elseif ($scriptDir -eq $workerDir) {
        return "worker"
    }
    
    # Not in a component directory - return null to trigger prompt
    return $null
}

function Remove-Component {
    param(
        [string]$InstallDir,
        [string]$Component
    )
    
    $componentDir = Join-Path $InstallDir $Component
    
    if (-not (Test-Path $componentDir)) {
        Write-Warning "$Component is not installed"
        return
    }
    
    $version = Get-InstalledVersion -InstallDir $InstallDir -Component $Component
    Write-Info "Removing $Component (version $version)..."
    
    # Remove installation directory
    Remove-Item -Path $componentDir -Recurse -Force
    Write-Success "Removed installation directory: $componentDir"
    
    # Remove from PATH
    Remove-FromPath -ComponentDir $componentDir
    
    # Remove Start Menu shortcuts
    Remove-StartMenuShortcut -Component $Component
}

function Remove-FromPath {
    param([string]$ComponentDir)
    
    $currentPath = [Environment]::GetEnvironmentVariable("Path", "User")
    
    if ($currentPath -like "*$ComponentDir*") {
        # Remove the component directory from PATH
        $pathArray = $currentPath -split ';' | Where-Object { $_ -ne $ComponentDir -and $_ -ne "" }
        $newPath = $pathArray -join ';'
        
        [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
        
        # Also update current session
        $env:Path = ($env:Path -split ';' | Where-Object { $_ -ne $ComponentDir -and $_ -ne "" }) -join ';'
        
        Write-Success "Removed from PATH: $ComponentDir"
        Write-Info "You may need to restart your terminal for PATH changes to take effect"
    }
}

function Remove-StartMenuShortcut {
    param([string]$Component)
    
    $startMenuDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\TaskMessenger"
    $shortcutPath = Join-Path $startMenuDir "TaskMessenger $Component.lnk"
    
    if (Test-Path $shortcutPath) {
        Remove-Item $shortcutPath -Force
        Write-Success "Removed Start Menu shortcut: $shortcutPath"
    }
    
    # Remove Start Menu directory if empty
    if ((Test-Path $startMenuDir) -and ((Get-ChildItem $startMenuDir).Count -eq 0)) {
        Remove-Item $startMenuDir -Force
        Write-Info "Removed empty Start Menu directory: $startMenuDir"
    }
}

function Remove-ConfigFile {
    param([string]$Component)
    
    $configDir = Get-ConfigDir
    $configFile = Join-Path $configDir "config-$Component.json"
    
    if (Test-Path $configFile) {
        Write-Warning "Configuration file found: $configFile"
        $response = Read-Host "Do you want to remove the configuration file? [y/N]"
        
        if ($response -match '^[Yy]$') {
            Remove-Item $configFile -Force
            Write-Success "Removed configuration file: $configFile"
            
            # Also remove backup files
            $backupPattern = "$configFile.backup.*"
            $backupFiles = Get-ChildItem -Path (Split-Path $configFile) -Filter (Split-Path $backupPattern -Leaf) -ErrorAction SilentlyContinue
            
            if ($backupFiles) {
                $backupFiles | Remove-Item -Force
                Write-Success "Removed configuration backups"
            }
        } else {
            Write-Info "Configuration file preserved"
        }
    }
}

function Remove-EmptyDirectories {
    param([string]$InstallDir)
    
    # Remove install directory if empty
    if ((Test-Path $InstallDir) -and ((Get-ChildItem $InstallDir -ErrorAction SilentlyContinue).Count -eq 0)) {
        Remove-Item $InstallDir -Force
        Write-Info "Removed empty installation directory: $InstallDir"
    }
    
    # Remove config directory if empty
    $configDir = Get-ConfigDir
    if ((Test-Path $configDir) -and ((Get-ChildItem $configDir -ErrorAction SilentlyContinue).Count -eq 0)) {
        Remove-Item $configDir -Force
        Write-Info "Removed empty configuration directory: $configDir"
    }
}

# Main script
function Main {
    if ($Help) {
        Show-Usage
        exit 0
    }
    
    # Set default install directory if not provided
    if (-not $InstallDir) {
        $InstallDir = Get-DefaultInstallDir
    }
    
    # Auto-detect installed components if component not specified
    if (-not $Component) {
        $Component = Get-ComponentFromScriptLocation -InstallDir $InstallDir
        
        if (-not $Component) {
            # Script is not in a component directory - prompt user
            $installedComponents = Get-InstalledComponents -InstallDir $InstallDir
            
            if ($installedComponents.Count -eq 0) {
                Write-ErrorMsg "No TaskMessenger installation found at: $InstallDir"
                exit 1
            }
            
            $Component = Select-Component -InstalledComponents $installedComponents
            
            if (-not $Component) {
                Write-ErrorMsg "Failed to select component"
                exit 1
            }
        }
        
        Write-Info "Component to uninstall: $Component"
    }
    
    # Check installation
    Test-Installation -InstallDir $InstallDir -Component $Component
    
    # Get config directory
    $configDir = Get-ConfigDir
    
    Write-Info "=========================================="
    Write-Info "TaskMessenger Uninstallation"
    Write-Info "=========================================="
    Write-Info "Component:        $Component"
    Write-Info "Install location: $InstallDir"
    Write-Info "Config location:  $configDir"
    Write-Info "Remove config:    $RemoveConfig"
    Write-Info "=========================================="
    Write-Host ""
    
    $response = Read-Host "Are you sure you want to uninstall? [y/N]"
    if ($response -notmatch '^[Yy]$') {
        Write-Info "Uninstallation cancelled by user"
        exit 0
    }
    
    # Remove components
    if ($Component -eq "all") {
        Remove-Component -InstallDir $InstallDir -Component "manager"
        Remove-Component -InstallDir $InstallDir -Component "worker"
        
        if ($RemoveConfig) {
            Remove-ConfigFile -Component "manager"
            Remove-ConfigFile -Component "worker"
        }
    } else {
        Remove-Component -InstallDir $InstallDir -Component $Component
        
        if ($RemoveConfig) {
            Remove-ConfigFile -Component $Component
        }
    }
    
    # Cleanup empty directories
    Remove-EmptyDirectories -InstallDir $InstallDir
    
    Write-Host ""
    Write-Success "=========================================="
    Write-Success "Uninstallation completed successfully!"
    Write-Success "=========================================="
    Write-Host ""
}

# Run main function
Main
