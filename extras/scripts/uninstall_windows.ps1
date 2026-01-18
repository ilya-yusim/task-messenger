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

function Exit-WithError {
    param(
        [string]$Message,
        [int]$ExitCode = 1
    )
    Write-ErrorMsg $Message
    Write-Host ""
    Write-Host "Press any key to exit..."
    $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
    exit $ExitCode
}

function Show-Usage {
    @"
TaskMessenger Windows Uninstallation Script

Usage: .\uninstall_windows.ps1 [component] [OPTIONS]

Arguments:
  component              Either 'manager', 'worker', or 'all' (auto-detected if not specified)

Options:
  -InstallDir PATH       Custom installation directory (optional, auto-detected by default)
  -RemoveConfig          Also remove configuration files
  -Help                  Show this help message

Note: If component is not specified, the script will detect the installation from its location or prompt you to choose.

Installation directories:
  Manager: %LOCALAPPDATA%\TaskMessageManager
  Worker:  %LOCALAPPDATA%\TaskMessageWorker

Examples:
  .\uninstall_windows.ps1
  .\uninstall_windows.ps1 manager
  .\uninstall_windows.ps1 worker -RemoveConfig
  .\uninstall_windows.ps1 all

"@
}

function Get-DefaultInstallDir {
    param([string]$Component)
    
    if ($Component -eq "manager") {
        return Join-Path $env:LOCALAPPDATA "TaskMessenger\tm-manager"
    } elseif ($Component -eq "worker") {
        return Join-Path $env:LOCALAPPDATA "TaskMessenger\tm-worker"
    } else {
        # For 'all', return parent directory
        return $env:LOCALAPPDATA
    }
}

function Get-ConfigDir {
    param([string]$Component)
    
    if ($Component -eq "manager") {
        return Join-Path $env:APPDATA "TaskMessenger\tm-manager"
    } else {
        return Join-Path $env:APPDATA "TaskMessenger\tm-worker"
    }
}

function Test-Installation {
    param([string]$Component)
    
    if ($Component -eq "all") {
        $managerBinDir = Join-Path $env:LOCALAPPDATA "TaskMessenger\tm-manager"
        $workerBinDir = Join-Path $env:LOCALAPPDATA "TaskMessenger\tm-worker"
        $managerCfgDir = Join-Path $env:APPDATA "TaskMessenger\tm-manager"
        $workerCfgDir = Join-Path $env:APPDATA "TaskMessenger\tm-worker"
        
        if (-not (Test-Path $managerBinDir) -and -not (Test-Path $workerBinDir) -and
            -not (Test-Path $managerCfgDir) -and -not (Test-Path $workerCfgDir)) {
            Exit-WithError "No TaskMessenger installation found"
        }
    } else {
        $installDir = Get-DefaultInstallDir -Component $Component
        $configDir = Get-ConfigDir -Component $Component
        
        if (-not (Test-Path $installDir) -and -not (Test-Path $configDir)) {
            Exit-WithError "$Component is not installed at: $installDir or $configDir"
        }
    }
}

function Get-InstalledVersion {
    param([string]$Component)
    
    $installDir = Get-DefaultInstallDir -Component $Component
    $versionFile = Join-Path $installDir "VERSION"
    
    if (Test-Path $versionFile) {
        $version = Get-Content $versionFile -Raw
        return $version.Trim()
    }
    
    return "unknown"
}

function Get-InstalledComponents {
    $components = @()
    
    $managerDir = Join-Path $env:LOCALAPPDATA "TaskMessenger\tm-manager"
    $workerDir = Join-Path $env:LOCALAPPDATA "TaskMessenger\tm-worker"
    
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
        Exit-WithError "No TaskMessenger installation found"
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
    # Get script location
    $scriptPath = $PSCommandPath
    $scriptDir = Split-Path -Parent $scriptPath
    
    # Check if script is in a component directory
    $managerDir = Join-Path $env:LOCALAPPDATA "TaskMessenger\tm-manager"
    $workerDir = Join-Path $env:LOCALAPPDATA "TaskMessenger\tm-worker"
    
    if ($scriptDir -eq $managerDir) {
        return "manager"
    } elseif ($scriptDir -eq $workerDir) {
        return "worker"
    }
    
    # Not in a component directory - return null to trigger prompt
    return $null
}

function Remove-Component {
    param([string]$Component)
    
    $installDir = Get-DefaultInstallDir -Component $Component
    $configDir = Get-ConfigDir -Component $Component
    
    if (-not (Test-Path $installDir) -and -not (Test-Path $configDir)) {
        Write-Warning "$Component is not installed"
        return
    }
    
    $version = Get-InstalledVersion -Component $Component
    Write-Info "Removing $Component (version $version)..."
    
    # Remove installation directory (binaries in LOCALAPPDATA)
    if (Test-Path $installDir) {
        Remove-Item -Path $installDir -Recurse -Force
        Write-Success "Removed installation directory: $installDir"
    }
    
    # Remove config directory (configs and identity in APPDATA)
    if (Test-Path $configDir) {
        Remove-Item -Path $configDir -Recurse -Force
        Write-Success "Removed config directory: $configDir"
    }
    
    # Remove from PATH
    Remove-FromPath -InstallDir $installDir
    
    # Remove Start Menu shortcuts
    Remove-StartMenuShortcut -Component $Component
    
    # Remove from Windows Add/Remove Programs
    Unregister-FromWindowsUninstall -Component $Component
}

function Unregister-FromWindowsUninstall {
    param([string]$Component)
    
    $componentName = Get-ComponentName -Component $Component
    $uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$componentName"
    
    if (Test-Path $uninstallKey) {
        Remove-Item -Path $uninstallKey -Recurse -Force
        Write-Success "Removed from Windows Programs and Features"
    }
}

function Remove-FromPath {
    param([string]$InstallDir)
    
    $currentPath = [Environment]::GetEnvironmentVariable("Path", "User")
    
    if ($currentPath -like "*$InstallDir*") {
        # Remove the installation directory from PATH
        $pathArray = $currentPath -split ';' | Where-Object { $_ -ne $InstallDir -and $_ -ne "" }
        $newPath = $pathArray -join ';'
        
        [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
        
        # Also update current session
        $env:Path = ($env:Path -split ';' | Where-Object { $_ -ne $InstallDir -and $_ -ne "" }) -join ';'
        
        Write-Success "Removed from PATH: $InstallDir"
        Write-Info "You may need to restart your terminal for PATH changes to take effect"
    }
}

function Remove-StartMenuShortcut {
    param([string]$Component)
    
    $componentName = if ($Component -eq "manager") { "TMManager" } else { "TMWorker" }
    $startMenuDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\$componentName"
    
    if (Test-Path $startMenuDir) {
        Remove-Item $startMenuDir -Recurse -Force
        Write-Success "Removed Start Menu folder: $startMenuDir"
    }
}

function Remove-ConfigFile {
    param([string]$Component)
    
    $configDir = Get-ConfigDir -Component $Component
    $configFile = Join-Path $configDir "config-$Component.json"
    
    if (Test-Path $configFile) {
        Remove-Item $configFile -Force
        Write-Success "Removed configuration file: $configFile"
        
        # Also remove backup files
        $backupPattern = "$configFile.backup.*"
        $backupFiles = Get-ChildItem -Path (Split-Path $configFile) -Filter (Split-Path $backupPattern -Leaf) -ErrorAction SilentlyContinue
        
        if ($backupFiles) {
            $backupFiles | Remove-Item -Force
            Write-Success "Removed configuration backups"
        }
    }
}

function Remove-EmptyDirectories {
    param(
        [string]$InstallDir,
        [string]$Component
    )
    
    # Remove install directory if empty
    if ((Test-Path $InstallDir) -and ((Get-ChildItem $InstallDir -ErrorAction SilentlyContinue).Count -eq 0)) {
        Remove-Item $InstallDir -Force
        Write-Info "Removed empty installation directory: $InstallDir"
    }
    
    # Remove config directory if empty
    $configDir = Get-ConfigDir -Component $Component
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
        $InstallDir = Get-DefaultInstallDir -Component ($Component -or "manager")
    }
    
    # Auto-detect installed components if component not specified
    if (-not $Component) {
        $Component = Get-ComponentFromScriptLocation
        
        if (-not $Component) {
            # Script is not in a component directory - prompt user
            $installedComponents = Get-InstalledComponents
            
            if ($installedComponents.Count -eq 0) {
                Exit-WithError "No TaskMessenger installation found"
            }
            
            $Component = Select-Component -InstalledComponents $installedComponents
            
            if (-not $Component) {
                Exit-WithError "Failed to select component"
            }
        }
        
        Write-Info "Component to uninstall: $Component"
    }
    
    # Check installation
    Test-Installation -Component $Component
    
    Write-Info "=========================================="
    Write-Info "TaskMessenger Uninstallation"
    Write-Info "=========================================="
    Write-Info "Component:        $Component"
    if ($Component -ne "all") {
        $InstallDir = Get-DefaultInstallDir -Component $Component
        $configDir = Get-ConfigDir -Component $Component
        Write-Info "Install location: $InstallDir"
        Write-Info "Config location:  $configDir"
    } else {
        Write-Info "Install locations: %LOCALAPPDATA%\TaskMessenger\tm-manager and tm-worker"
        Write-Info "Config locations:  %APPDATA%\TaskMessenger\tm-manager and tm-worker"
    }
    Write-Info "Remove config:    $RemoveConfig"
    Write-Info "=========================================="
    Write-Host ""
    
    # Remove components
    if ($Component -eq "all") {
        Remove-Component -Component "manager"
        Remove-Component -Component "worker"
        
        if ($RemoveConfig) {
            Remove-ConfigFile -Component "manager"
            Remove-ConfigFile -Component "worker"
        }
    } else {
        Remove-Component -Component $Component
        
        if ($RemoveConfig) {
            Remove-ConfigFile -Component $Component
        }
    }
    
    Write-Host ""
    Write-Success "=========================================="
    Write-Success "Uninstallation completed successfully!"
    Write-Success "=========================================="
    Write-Host ""
}

# Run main function
Main
