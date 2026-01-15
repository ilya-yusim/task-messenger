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
    
    # Determine build options based on component
    $BuildOptions = @()
    if ($Comp -eq "manager") {
        $BuildOptions += "-Dbuild_worker=false"
        Write-Host "Building manager only (FTXUI disabled for faster build)"
    } elseif ($Comp -eq "worker") {
        $BuildOptions += "-Dbuild_manager=false"
        Write-Host "Building worker only"
    }
    
    # Setup meson
    meson setup $BuildDir `
        --prefix="$Prefix" `
        --buildtype=$BuildType `
        -Ddebug_logging=false `
        -Dprofiling_unwind=false `
        @BuildOptions
    
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
    
    # Create component-specific directory structure
    $ComponentName = if ($Comp -eq "manager") { "TaskMessageManager" } else { "TaskMessageWorker" }
    $TaskMessengerDir = Join-Path $TempArchiveDir $ComponentName
    New-Item -ItemType Directory -Force -Path $TaskMessengerDir | Out-Null
    
    # Create bin directory
    $BinDir = Join-Path $TaskMessengerDir "bin"
    New-Item -ItemType Directory -Force -Path $BinDir | Out-Null
    
    # Copy files based on component
    if ($Comp -eq "manager") {
        # Manager: executable, DLL
        Copy-Item (Join-Path $CompStagingPrefix "bin\manager.exe") $BinDir
        Copy-Item (Join-Path $CompStagingPrefix "bin\zt-shared.dll") $BinDir
    } else {
        # Worker: executable, DLL
        Copy-Item (Join-Path $CompStagingPrefix "bin\worker.exe") $BinDir
        Copy-Item (Join-Path $CompStagingPrefix "bin\zt-shared.dll") $BinDir
    }
    
    # Copy config files
    $ConfigDir = Join-Path $TaskMessengerDir "config"
    New-Item -ItemType Directory -Force -Path $ConfigDir | Out-Null
    Copy-Item (Join-Path $CompStagingPrefix "etc\task-messenger\config-$Comp.json") $ConfigDir
    
    # Copy manager identity directory (only for manager component)
    if ($Comp -eq "manager") {
        Copy-Item (Join-Path $CompStagingPrefix "etc\task-messenger\vn-manager-identity") $ConfigDir -Recurse
    }
    
    # Copy documentation
    $DocDir = Join-Path $TaskMessengerDir "doc"
    New-Item -ItemType Directory -Force -Path $DocDir | Out-Null
    Copy-Item (Join-Path $CompStagingPrefix "share\doc\task-messenger\*") $DocDir
    
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
    
    Write-Host "Created: $ArchivePath"
    
    # Clean up temp directory
    Remove-Item -Recurse -Force $TempArchiveDir
}

# Function to create self-extracting installer using IExpress
function Create-SelfExtractingInstaller {
    param([string]$Comp)
    
    $ComponentName = if ($Comp -eq "manager") { "TaskMessageManager" } else { "TaskMessageWorker" }
    $InstallerName = "task-messenger-$Comp-v$Version-$Platform-$Arch-installer.exe"
    $InstallerPath = Join-Path $OutputDir $InstallerName
    $ZipArchive = Join-Path $OutputDir "task-messenger-$Comp-v$Version-$Platform-$Arch.zip"
    
    if (-not (Test-Path $ZipArchive)) {
        Write-Warning "ZIP archive not found, skipping self-extracting installer creation"
        return
    }
    
    Write-Host ""
    Write-Host "Creating self-extracting installer: $InstallerName"
    
    # Create temporary directory for IExpress files
    $IExpressTemp = Join-Path $StagingDir "iexpress-$Comp"
    if (Test-Path $IExpressTemp) {
        Remove-Item -Recurse -Force $IExpressTemp
    }
    New-Item -ItemType Directory -Force -Path $IExpressTemp | Out-Null
    
    # Copy ZIP archive to temp directory
    Copy-Item $ZipArchive $IExpressTemp
    
    # Create extraction and installation batch script
    $ExtractAndInstallBat = Join-Path $IExpressTemp "extract_and_install.bat"
    $ExtractScript = @"
@echo off
echo ================================================
echo TaskMessenger $Comp Installer v$Version
echo ================================================
echo.
echo Extracting files...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Expand-Archive -Path '%~dp0task-messenger-$Comp-v$Version-$Platform-$Arch.zip' -DestinationPath '%TEMP%\TaskMessengerInstall' -Force"
if errorlevel 1 (
    echo Error: Failed to extract archive
    pause
    exit /b 1
)
echo.
echo Starting installation...
cd /d "%TEMP%\TaskMessengerInstall\$ComponentName"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\install_windows.ps1"
if errorlevel 1 (
    echo.
    echo Installation encountered errors.
) else (
    echo.
    echo Installation completed!
)
echo.
echo Cleaning up temporary files...
rd /s /q "%TEMP%\TaskMessengerInstall"
echo.
echo Press any key to exit...
pause >nul
"@
    Set-Content -Path $ExtractAndInstallBat -Value $ExtractScript -Encoding ASCII
    
    # Create IExpress SED directive file
    $SedFile = Join-Path $IExpressTemp "installer.sed"
    $ZipFileName = "task-messenger-$Comp-v$Version-$Platform-$Arch.zip"
    
    # Build strings with explicit variable expansion to avoid here-string issues
    $InstallPromptText = "Install TaskMessenger $Comp v${Version}?"
    $FriendlyNameText = "TaskMessenger $Comp v${Version} Installer"
    
    $SedContent = @"
[Version]
Class=IEXPRESS
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=0
HideExtractAnimation=0
UseLongFileName=1
InsideCompressed=0
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=%InstallPrompt%
DisplayLicense=%DisplayLicense%
FinishMessage=%FinishMessage%
TargetName=%TargetName%
FriendlyName=%FriendlyName%
AppLaunched=%AppLaunched%
PostInstallCmd=%PostInstallCmd%
AdminQuietInstCmd=%AdminQuietInstCmd%
UserQuietInstCmd=%UserQuietInstCmd%
SourceFiles=SourceFiles
[Strings]
InstallPrompt=$InstallPromptText
DisplayLicense=
FinishMessage=
TargetName=$InstallerPath
FriendlyName=$FriendlyNameText
AppLaunched=cmd.exe /c extract_and_install.bat
PostInstallCmd=<None>
AdminQuietInstCmd=
UserQuietInstCmd=
FILE0=extract_and_install.bat
FILE1=$ZipFileName
[SourceFiles]
SourceFiles0=$IExpressTemp
[SourceFiles0]
%FILE0%=
%FILE1%=
"@
    # Ensure proper line endings for Windows (CRLF)
    $SedContentWithCRLF = $SedContent -replace "`n", "`r`n"
    
    # Write SED file
    [System.IO.File]::WriteAllText($SedFile, $SedContentWithCRLF, [System.Text.Encoding]::ASCII)
    
    # Verify the file was created
    if (-not (Test-Path $SedFile)) {
        Write-Error "Failed to create SED file at: $SedFile"
        return
    }
    
    Write-Host "SED file created at: $SedFile"
    Write-Host "Temp directory: $IExpressTemp"
    
    # Get short path name for IExpress (it doesn't handle long paths well)
    $fso = New-Object -ComObject Scripting.FileSystemObject
    $sedFileShort = $fso.GetFile($SedFile).ShortPath
    
    # Run IExpress to create self-extracting installer
    Write-Host "Running IExpress..."
    Write-Host "Using short path: $sedFileShort"
    $IExpressExe = "$env:SystemRoot\System32\iexpress.exe"
    
    # Create a log file for IExpress output
    $LogFile = Join-Path $OutputDir "iexpress-$Comp.log"
    
    # Run IExpress and capture output
    $ProcessInfo = New-Object System.Diagnostics.ProcessStartInfo
    $ProcessInfo.FileName = $IExpressExe
    $ProcessInfo.Arguments = "/N $sedFileShort"  # No quotes around short path
    $ProcessInfo.RedirectStandardOutput = $true
    $ProcessInfo.RedirectStandardError = $true
    $ProcessInfo.UseShellExecute = $false
    $ProcessInfo.CreateNoWindow = $false  # Show window to see progress
    
    $Process = New-Object System.Diagnostics.Process
    $Process.StartInfo = $ProcessInfo
    $Process.Start() | Out-Null
    
    $stdout = $Process.StandardOutput.ReadToEnd()
    $stderr = $Process.StandardError.ReadToEnd()
    $Process.WaitForExit()
    $exitCode = $Process.ExitCode
    
    # Log output
    "IExpress Exit Code: $exitCode" | Out-File -FilePath $LogFile
    "STDOUT:`r`n$stdout" | Out-File -FilePath $LogFile -Append
    "STDERR:`r`n$stderr" | Out-File -FilePath $LogFile -Append
    
    Write-Host "IExpress exit code: $exitCode"
    if ($exitCode -ne 0) {
        Write-Warning "IExpress failed with exit code: $exitCode"
        Write-Host "Log file: $LogFile"
        Write-Host "SED file: $SedFile"
        Write-Host "Temp directory kept for inspection: $IExpressTemp"
        Write-Host ""
        Write-Host "To manually test IExpress, run:"
        Write-Host "  iexpress.exe /N `"$SedFile`""
        return
    }
    
    if (Test-Path $InstallerPath) {
        Write-Host "Created self-extracting installer: $InstallerPath"
        
        # Clean up ZIP archive (no longer needed since installer contains it)
        Write-Host "Removing intermediate ZIP archive..."
        Remove-Item -Force $ZipArchive -ErrorAction SilentlyContinue
        
        # Clean up IExpress log file (only needed for debugging failures)
        Remove-Item -Force $LogFile -ErrorAction SilentlyContinue
        
        # Clean up IExpress temporary files (.DDF files in output directory)
        Get-ChildItem -Path $OutputDir -Filter "*.DDF" | Remove-Item -Force -ErrorAction SilentlyContinue
        
        # Clean up temp directory (with retry in case IExpress still has file handles)
        $CleanupAttempts = 0
        while ($CleanupAttempts -lt 3) {
            try {
                Remove-Item -Recurse -Force $IExpressTemp -ErrorAction Stop
                break
            } catch {
                $CleanupAttempts++
                if ($CleanupAttempts -lt 3) {
                    Start-Sleep -Milliseconds 500
                } else {
                    Write-Warning "Could not clean up temp directory: $IExpressTemp (IExpress may still have files open)"
                }
            }
        }
    } else {
        Write-Warning "Failed to create self-extracting installer (timeout after $MaxWaitSeconds seconds)"
        if ($IExpressOutput) {
            Write-Warning "IExpress output: $IExpressOutput"
        }
        Write-Host "Temp files kept in: $IExpressTemp"
        Write-Host "SED file: $SedFile"
    }
}

# Clean staging directory (but keep output directory to accumulate builds)
if (Test-Path $StagingDir) {
    Remove-Item -Recurse -Force $StagingDir
}
# Create output directory if it doesn't exist
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
}
New-Item -ItemType Directory -Force -Path $StagingDir | Out-Null

# Build and package based on component selection
if ($Component -eq "all") {
    foreach ($Comp in @("manager", "worker")) {
        Build-Component -Comp $Comp
        Create-Archive -Comp $Comp
        Create-SelfExtractingInstaller -Comp $Comp
    }
} else {
    Build-Component -Comp $Component
    Create-Archive -Comp $Component
    Create-SelfExtractingInstaller -Comp $Component
}

# Summary
Write-Host ""
Write-Host "=================================================="
Write-Host "Distribution build complete!"
Write-Host "=================================================="
Write-Host "Output directory: $OutputDir"
Get-ChildItem $OutputDir | Format-Table Name, Length -AutoSize
Write-Host "=================================================="
