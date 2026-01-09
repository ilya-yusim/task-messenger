# TaskMessenger Windows Installation Script
# This script installs TaskMessenger (manager or worker) for the current user

param(
    [Parameter(Mandatory=$false)]
    [string]$InstallDir,
    
    [Parameter(Mandatory=$false)]
    [string]$Archive,
    
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
TaskMessenger Windows Installation Script

Usage: .\install_windows.ps1 [OPTIONS]

Options:
  -InstallDir PATH       Custom installation directory (default: %LOCALAPPDATA%\TaskMessenger)
  -Archive PATH          Path to distribution archive (auto-detected if not provided)
  -Help                  Show this help message

Note: The component (manager or worker) is automatically detected from the extracted files.

Examples:
  .\install_windows.ps1
  .\install_windows.ps1 -InstallDir "C:\Custom\Path"
  .\install_windows.ps1 -Archive "task-messenger-manager-v1.0.0-windows-x64.zip"

"@
}

function Get-DefaultInstallDir {
    param([string]$Component)
    
    if ($Component -eq "manager") {
        return Join-Path $env:LOCALAPPDATA "TaskMessageManager"
    } else {
        return Join-Path $env:LOCALAPPDATA "TaskMessageWorker"
    }
}

function Get-ConfigDir {
    return Join-Path $env:APPDATA "task-messenger"
}

function Find-Archive {
    param([string]$Component)
    
    $pattern = "task-messenger-$Component-v*-windows-*.zip"
    
    # Search in current directory and parent directory
    $archive = Get-ChildItem -Path @(".", "..") -Filter $pattern -ErrorAction SilentlyContinue | Select-Object -First 1
    
    if ($archive) {
        return $archive.FullName
    }
    
    return $null
}

function Test-ExtractedFiles {
    # Get script directory
    $scriptDir = Split-Path -Parent $PSCommandPath
    $extractedRoot = Split-Path -Parent $scriptDir
    
    # Check if we're running from an extracted archive by looking for DLL
    $dllPath = Join-Path $extractedRoot "bin\zt-shared.dll"
    
    if (Test-Path $dllPath) {
        # Detect which component by checking which executable exists
        $managerPath = Join-Path $extractedRoot "bin\manager.exe"
        $workerPath = Join-Path $extractedRoot "bin\worker.exe"
        
        if (Test-Path $managerPath) {
            return @{ Root = $extractedRoot; Component = "manager" }
        } elseif (Test-Path $workerPath) {
            return @{ Root = $extractedRoot; Component = "worker" }
        }
    }
    
    return $null
}

function Get-ComponentName {
    param([string]$Component)
    
    if ($Component -eq "manager") {
        return "TaskMessageManager"
    } else {
        return "TaskMessageWorker"
    }
}

function Get-VersionFromArchive {
    param([string]$ArchiveName)
    
    # Extract version from filename: task-messenger-manager-v1.0.0-windows-x64.zip -> 1.0.0
    if ($ArchiveName -match '-v(\d+\.\d+\.\d+)-') {
        return $matches[1]
    }
    
    return "unknown"
}

function Test-ExistingInstallation {
    param(
        [string]$InstallDir,
        [string]$Component
    )
    
    $componentDir = Join-Path $InstallDir $Component
    
    if (Test-Path $componentDir) {
        $versionFile = Join-Path $componentDir "VERSION"
        $installedVersion = ""
        
        if (Test-Path $versionFile) {
            $installedVersion = Get-Content $versionFile -Raw
            $installedVersion = $installedVersion.Trim()
        }
        
        if ($installedVersion) {
            Write-Warning "Found existing installation of $Component (version $installedVersion)"
        } else {
            Write-Warning "Found existing installation of $Component"
        }
        
        $response = Read-Host "Do you want to upgrade? This will replace the existing installation. [y/N]"
        if ($response -notmatch '^[Yy]$') {
            Write-Info "Installation cancelled by user"
            exit 0
        }
        
        return $true
    }
    
    return $false
}

function Backup-Config {
    param(
        [string]$ConfigDir,
        [string]$Component
    )
    
    $configFile = Join-Path $ConfigDir "config-$Component.json"
    
    if (Test-Path $configFile) {
        $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
        $backupFile = "$configFile.backup.$timestamp"
        Copy-Item $configFile $backupFile
        Write-Info "Backed up existing config to: $backupFile"
    }
}

function Install-Component {
    param(
        [string]$Component,
        [string]$SourceDir,
        [string]$InstallDir,
        [string]$Version,
        [bool]$CleanupTemp = $false
    )
    
    Write-Info "Installing $Component from extracted files..."
    
    # Create installation directory (no subdirectory for component)
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
    
    # Use provided source directory
    $extractedDir = $SourceDir
    
    # Move files to installation directory
    Write-Info "Installing files..."
    
    # Copy binary
    $binaryPath = Join-Path $extractedDir "bin\$Component.exe"
    if (Test-Path $binaryPath) {
        Copy-Item $binaryPath $InstallDir -Force
    } else {
        Write-ErrorMsg "Binary not found: $binaryPath"
        Remove-Item -Path $tempDir -Recurse -Force
        exit 1
    }
    
    # Copy shared library
    $libDir = Join-Path $extractedDir "bin"
    $dllPath = Join-Path $libDir "zt-shared.dll"
    if (Test-Path $dllPath) {
        Copy-Item $dllPath $InstallDir -Force
    }
    
    # Copy config file from etc/
    $etcDir = Join-Path $extractedDir "etc"
    $configFile = Join-Path $etcDir "config-$Component.json"
    if (Test-Path $configFile) {
        Copy-Item $configFile $InstallDir -Force
    }
    
    # Copy identity directory for manager
    if ($Component -eq "manager") {
        $identityDir = Join-Path $etcDir ".vn_manager_identity"
        
        if (Test-Path $identityDir) {
            Copy-Item $identityDir $InstallDir -Recurse -Force
            
            # Set restrictive permissions on secret file
            $secretPath = Join-Path $InstallDir ".vn_manager_identity\identity.secret"
            if (Test-Path $secretPath) {
                $acl = Get-Acl $secretPath
                $acl.SetAccessRuleProtection($true, $false)
                $rule = New-Object System.Security.AccessControl.FileSystemAccessRule(
                    [System.Security.Principal.WindowsIdentity]::GetCurrent().Name,
                    "FullControl",
                    "Allow"
                )
                $acl.SetAccessRule($rule)
                Set-Acl $secretPath $acl
            }
        }
    }
    
    # Copy documentation
    $docSourceDir = Join-Path $extractedDir "share\doc"
    if (Test-Path $docSourceDir) {
        $docDestDir = Join-Path $InstallDir "doc"
        New-Item -ItemType Directory -Path $docDestDir -Force | Out-Null
        Copy-Item -Path "$docSourceDir\*" -Destination $docDestDir -Recurse -Force
    }
    
    # Copy uninstall script
    $uninstallScript = Join-Path $extractedDir "scripts\uninstall_windows.ps1"
    if (Test-Path $uninstallScript) {
        Copy-Item -Path $uninstallScript -Destination $InstallDir -Force
    }
    
    # Store version information
    $versionFile = Join-Path $InstallDir "VERSION"
    Set-Content -Path $versionFile -Value $Version
    
    # Clean up temporary directory if it was created from archive extraction
    if ($CleanupTemp -and (Test-Path $SourceDir)) {
        $tempParent = Split-Path -Parent $SourceDir
        if ($tempParent -like "*Temp*") {
            Remove-Item -Path $tempParent -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
    
    Write-Success "$Component installed to: $InstallDir"
}

function Add-ToPath {
    param([string]$InstallDir)
    
    # Get current user PATH
    $currentPath = [Environment]::GetEnvironmentVariable("Path", "User")
    
    if ($currentPath -notlike "*$InstallDir*") {
        $newPath = "$InstallDir;$currentPath"
        [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
        
        # Also update current session
        $env:Path = "$InstallDir;$env:Path"
        
        Write-Success "Added to PATH: $InstallDir"
        Write-Info "You may need to restart your terminal for PATH changes to take effect"
    } else {
        Write-Success "Already in PATH: $InstallDir"
    }
}

function New-ConfigTemplate {
    param([string]$Component)
    
    $configDir = Get-ConfigDir
    New-Item -ItemType Directory -Path $configDir -Force | Out-Null
    
    $configFile = Join-Path $configDir "config-$Component.json"
    
    # Only create config template if it doesn't exist
    if (-not (Test-Path $configFile)) {
        $configContent = @"
{
  "network": {
    "zerotier_network_id": "",
    "zerotier_identity_path": ""
  },
  "logging": {
    "level": "info",
    "file": ""
  }
}
"@
        Set-Content -Path $configFile -Value $configContent
        Write-Success "Created config template: $configFile"
        Write-Warning "Please edit the config file to set your ZeroTier network ID and other settings"
    } else {
        Write-Info "Config file already exists: $configFile"
    }
}

function New-StartMenuShortcut {
    param(
        [string]$Component,
        [string]$InstallDir
    )
    
    $componentName = Get-ComponentName -Component $Component
    $startMenuDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\$componentName"
    New-Item -ItemType Directory -Path $startMenuDir -Force | Out-Null
    
    $exePath = Join-Path $InstallDir "$Component.exe"
    $shortcutPath = Join-Path $startMenuDir "$componentName.lnk"
    
    # Get config file path from installation directory
    $configFile = Join-Path $InstallDir "config-$Component.json"
    
    $WScriptShell = New-Object -ComObject WScript.Shell
    $shortcut = $WScriptShell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = $exePath
    
    # Add --ui argument for worker component
    if ($Component -eq "worker") {
        $shortcut.Arguments = "-c `"$configFile`" --ui"
    } else {
        $shortcut.Arguments = "-c `"$configFile`""
    }
    
    $shortcut.WorkingDirectory = $InstallDir
    
    # Set description
    if ($Component -eq "manager") {
        $shortcut.Description = "TaskMessenger task distribution manager"
    } else {
        $shortcut.Description = "TaskMessenger task processing worker"
    }
    
    $shortcut.Save()
    
    Write-Success "Created Start Menu shortcut: $shortcutPath"
}

# Main script
function Main {
    if ($Help) {
        Show-Usage
        exit 0
    }
    
    # Set default install directory if not provided
    if (-not $InstallDir) {
        $InstallDir = Get-DefaultInstallDir -Component $Component
    }
    
    # First, check if we're running from an extracted archive and detect component
    $extractedInfo = Test-ExtractedFiles
    $sourceDir = $null
    $version = "unknown"
    $cleanupTemp = $false
    
    if ($extractedInfo) {
        Write-Info "Using files from extracted archive at: $($extractedInfo.Root)"
        $sourceDir = $extractedInfo.Root
        $Component = $extractedInfo.Component
        
        # Update install directory based on detected component if not specified
        if (-not $PSBoundParameters.ContainsKey('InstallDir')) {
            $InstallDir = Get-DefaultInstallDir -Component $Component
        }
        
        # Try to get version from INSTALL.txt or default to 1.0.0
        $installTxt = Join-Path $extractedInfo.Root "INSTALL.txt"
        if (Test-Path $installTxt) {
            $content = Get-Content $installTxt -Raw
            if ($content -match 'v(\d+\.\d+\.\d+)') {
                $version = $matches[1]
            }
        }
        
        # If version still unknown, check parent directory name
        if ($version -eq "unknown") {
            $parentName = Split-Path $extractedInfo.Root -Leaf
            if ($parentName -match 'v(\d+\.\d+\.\d+)') {
                $version = $matches[1]
            } elseif ($parentName -match '(\d+\.\d+\.\d+)') {
                $version = $matches[1]
            } else {
                $version = "1.0.0"
            }
        }
    } else {
        # Fall back to archive-based installation
        if (-not $Archive) {
            Write-ErrorMsg "Could not detect component from extracted files"
            Write-ErrorMsg ""
            Write-ErrorMsg "Solutions:"
            Write-ErrorMsg "  1. Extract a TaskMessenger distribution archive (manager or worker)"
            Write-ErrorMsg "     and run this script from the extracted TaskMessenger directory"
            Write-ErrorMsg ""
            Write-ErrorMsg "  2. Specify the archive path manually:"
            Write-ErrorMsg "     .\install_windows.ps1 -Archive 'path\to\task-messenger-{component}-v1.0.0-windows-x64.zip'"
            exit 1
        }
        
        # Validate archive exists
        if (-not (Test-Path $Archive)) {
            Write-ErrorMsg "Archive not found: $Archive"
            exit 1
        }
        
        # Extract archive to temporary directory
        Write-Info "Extracting archive..."
        $tempDir = Join-Path $env:TEMP "taskmessenger-install-$(Get-Random)"
        Expand-Archive -Path $Archive -DestinationPath $tempDir -Force
        
        # Detect component from extracted files first
        $managerCheck = Join-Path $tempDir "TaskMessageManager"
        $workerCheck = Join-Path $tempDir "TaskMessageWorker"
        
        if (Test-Path $managerCheck) {
            $sourceDir = $managerCheck
            $Component = "manager"
        } elseif (Test-Path $workerCheck) {
            $sourceDir = $workerCheck
            $Component = "worker"
        } else {
            Write-ErrorMsg "Unexpected archive structure. Expected TaskMessageManager or TaskMessageWorker directory."
            Remove-Item -Path $tempDir -Recurse -Force -ErrorAction SilentlyContinue
            exit 1
        }
        
        # Update install directory based on detected component if not specified
        if (-not $PSBoundParameters.ContainsKey('InstallDir')) {
            $InstallDir = Get-DefaultInstallDir -Component $Component
        }
        
        # Detect component from extracted files
        $managerPath = Join-Path $sourceDir "bin\manager.exe"
        $workerPath = Join-Path $sourceDir "bin\worker.exe"
        
        if (Test-Path $managerPath) {
            $Component = "manager"
        } elseif (Test-Path $workerPath) {
            $Component = "worker"
        } else {
            Write-ErrorMsg "Could not detect component. No manager.exe or worker.exe found."
            Remove-Item -Path $tempDir -Recurse -Force -ErrorAction SilentlyContinue
            exit 1
        }
        
        Write-Info "Detected component: $Component"
        
        # Extract version from archive name
        $archiveName = Split-Path $Archive -Leaf
        $version = Get-VersionFromArchive -ArchiveName $archiveName
        $cleanupTemp = $true
    }
    
    # Validate we detected a component
    if (-not $Component) {
        Write-ErrorMsg "Failed to detect component (manager or worker)"
        exit 1
    }
    
    # Get config directory
    $configDir = Get-ConfigDir
    
    Write-Info "=========================================="
    Write-Info "TaskMessenger $Component Installation"
    Write-Info "=========================================="
    Write-Info "Component:        $Component"
    Write-Info "Version:          $version"
    Write-Info "Source:           $sourceDir"
    Write-Info "Install location: $InstallDir"
    Write-Info "Config location:  $configDir"
    Write-Info "=========================================="
    Write-Host ""
    
    # Check for existing installation
    if (Test-ExistingInstallation -InstallDir $InstallDir -Component $Component) {
        Backup-Config -ConfigDir $configDir -Component $Component
    }
    
    # Install component
    Install-Component -Component $Component -SourceDir $sourceDir -InstallDir $InstallDir -Version $version -CleanupTemp $cleanupTemp
    
    # Add to PATH
    Add-ToPath -InstallDir $InstallDir
    
    # Setup configuration
    New-ConfigTemplate -Component $Component
    
    # Create Start Menu shortcut
    New-StartMenuShortcut -Component $Component -InstallDir $InstallDir
    
    Write-Host ""
    Write-Success "=========================================="
    Write-Success "Installation completed successfully!"
    Write-Success "=========================================="
    Write-Info "You can now run: $Component"
    Write-Info "Or find it in the Start Menu under TaskMessenger"
    Write-Host ""
}

# Run main function
Main
