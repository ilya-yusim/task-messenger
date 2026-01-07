# build_distribution.ps1 - Create distributable packages for task-messenger on Windows
# Usage: .\build_distribution.ps1 [manager|worker|all]

param(
    [Parameter(Position=0)]
    [ValidateSet("manager", "worker", "all")]
    [string]$Component = "all"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
Set-Location $ProjectRoot

# Get version from meson.build
$MesonBuildContent = Get-Content "meson.build" -Raw
if ($MesonBuildContent -match "version:\s*'([^']+)'") {
    $Version = $Matches[1]
} else {
    Write-Error "Could not extract version from meson.build"
    exit 1
}

# Detect architecture
$Arch = if ([Environment]::Is64BitOperatingSystem) { "x64" } else { "x86" }
$Platform = "windows"

# Build configuration
$Prefix = "C:\TaskMessenger"
$BuildType = "release"
$StagingDir = Join-Path $ProjectRoot "dist-staging"
$OutputDir = Join-Path $ProjectRoot "dist"

Write-Host "=================================================="
Write-Host "Task Messenger Distribution Builder (Windows)"
Write-Host "=================================================="
Write-Host "Component: $Component"
Write-Host "Version: $Version"
Write-Host "Platform: $Platform-$Arch"
Write-Host "Prefix: $Prefix"
Write-Host "=================================================="

# Function to build and install a component
function Build-Component {
    param([string]$Comp)
    
    $BuildDir = "builddir-$Comp-dist"
    
    Write-Host ""
    Write-Host "Building $Comp..."
    
    # Clean previous build
    if (Test-Path $BuildDir) {
        Remove-Item -Recurse -Force $BuildDir
    }
    
    # Setup meson
    meson setup $BuildDir `
        --prefix="$Prefix" `
        --buildtype=$BuildType `
        -Ddebug_logging=false `
        -Dprofiling_unwind=false
    
    # Compile
    meson compile -C $BuildDir
    
    # Install to staging directory
    $CompStagingDir = Join-Path $StagingDir $Comp
    $env:DESTDIR = $CompStagingDir
    meson install -C $BuildDir --no-rebuild
    Remove-Item Env:\DESTDIR
}

# Function to create archive for a component
function Create-Archive {
    param([string]$Comp)
    
    $ArchiveName = "task-messenger-$Comp-v$Version-$Platform-$Arch.zip"
    $ArchivePath = Join-Path $OutputDir $ArchiveName
    
    Write-Host ""
    Write-Host "Creating archive: $ArchiveName"
    
    # Create output directory
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    
    # Navigate to staging (strip drive letter from prefix for staging path)
    $PrefixPath = $Prefix -replace '^[A-Z]:', ''
    $CompStagingRoot = Join-Path $StagingDir $Comp
    $CompStagingPrefix = Join-Path $CompStagingRoot $PrefixPath
    
    if (-not (Test-Path $CompStagingPrefix)) {
        Write-Error "Staging directory not found: $CompStagingPrefix"
        exit 1
    }
    
    # Create a temporary directory for the archive contents
    $TempArchiveDir = Join-Path $StagingDir "archive-$Comp"
    if (Test-Path $TempArchiveDir) {
        Remove-Item -Recurse -Force $TempArchiveDir
    }
    
    # Create TaskMessenger directory structure
    $TaskMessengerDir = Join-Path $TempArchiveDir "TaskMessenger"
    New-Item -ItemType Directory -Force -Path $TaskMessengerDir | Out-Null
    
    # Create bin directory
    $BinDir = Join-Path $TaskMessengerDir "bin"
    New-Item -ItemType Directory -Force -Path $BinDir | Out-Null
    
    # Copy files based on component
    if ($Comp -eq "manager") {
        # Manager: executable, identity files, DLL
        Copy-Item (Join-Path $CompStagingPrefix "bin\manager.exe") $BinDir
        Copy-Item (Join-Path $CompStagingPrefix "bin\identity.public") $BinDir
        Copy-Item (Join-Path $CompStagingPrefix "bin\identity.secret") $BinDir
        Copy-Item (Join-Path $CompStagingPrefix "bin\zt-shared.dll") $BinDir
    } else {
        # Worker: executable, DLL
        Copy-Item (Join-Path $CompStagingPrefix "bin\worker.exe") $BinDir
        Copy-Item (Join-Path $CompStagingPrefix "bin\zt-shared.dll") $BinDir
    }
    
    # Copy config files
    $EtcDir = Join-Path $TaskMessengerDir "etc"
    New-Item -ItemType Directory -Force -Path $EtcDir | Out-Null
    Copy-Item (Join-Path $CompStagingPrefix "etc\task-messenger\config-$Comp.json") $EtcDir
    
    # Copy documentation
    $DocDir = Join-Path $TaskMessengerDir "share\doc"
    New-Item -ItemType Directory -Force -Path $DocDir | Out-Null
    Copy-Item (Join-Path $CompStagingPrefix "share\doc\task-messenger") $DocDir -Recurse
    
    # Copy installation scripts
    $ScriptsDir = Join-Path $TaskMessengerDir "scripts"
    New-Item -ItemType Directory -Force -Path $ScriptsDir | Out-Null
    Copy-Item (Join-Path $ProjectRoot "extras\scripts\install_windows.ps1") $ScriptsDir
    Copy-Item (Join-Path $ProjectRoot "extras\scripts\uninstall_windows.ps1") $ScriptsDir
    
    # Copy launchers
    $LaunchersDir = Join-Path $TaskMessengerDir "launchers"
    New-Item -ItemType Directory -Force -Path $LaunchersDir | Out-Null
    if ($Comp -eq "manager") {
        Copy-Item (Join-Path $ProjectRoot "extras\launchers\start-manager.bat") $LaunchersDir
    } else {
        Copy-Item (Join-Path $ProjectRoot "extras\launchers\start-worker.bat") $LaunchersDir
    }
    
    # Create installation instructions
    $InstallText = @"
TaskMessenger Installation Instructions

To install $Comp, run PowerShell as a regular user (NOT as Administrator):
    cd TaskMessenger
    .\scripts\install_windows.ps1 $Comp

For custom installation directory:
    .\scripts\install_windows.ps1 $Comp -InstallDir "C:\Custom\Path"

For help:
    .\scripts\install_windows.ps1 -Help

"@
    Set-Content -Path (Join-Path $TaskMessengerDir "INSTALL.txt") -Value $InstallText
    
    # Create ZIP archive
    Compress-Archive -Path "$TempArchiveDir\*" -DestinationPath $ArchivePath -Force
    
    # Generate SHA256 checksum
    Write-Host "Generating checksum..."
    $Hash = (Get-FileHash -Path $ArchivePath -Algorithm SHA256).Hash
    $ChecksumFile = "$ArchivePath.sha256"
    "$Hash  $ArchiveName" | Out-File -FilePath $ChecksumFile -Encoding ASCII
    
    Write-Host "Created: $ArchivePath"
    Write-Host "Checksum: $ChecksumFile"
    
    # Clean up temp directory
    Remove-Item -Recurse -Force $TempArchiveDir
}

# Clean staging and output directories
if (Test-Path $StagingDir) {
    Remove-Item -Recurse -Force $StagingDir
}
if (Test-Path $OutputDir) {
    Remove-Item -Recurse -Force $OutputDir
}
New-Item -ItemType Directory -Force -Path $StagingDir | Out-Null
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

# Build and package based on component selection
if ($Component -eq "all") {
    foreach ($Comp in @("manager", "worker")) {
        Build-Component -Comp $Comp
        Create-Archive -Comp $Comp
    }
} else {
    Build-Component -Comp $Component
    Create-Archive -Comp $Component
}

# Summary
Write-Host ""
Write-Host "=================================================="
Write-Host "Distribution build complete!"
Write-Host "=================================================="
Write-Host "Output directory: $OutputDir"
Get-ChildItem $OutputDir | Format-Table Name, Length -AutoSize
Write-Host "=================================================="
