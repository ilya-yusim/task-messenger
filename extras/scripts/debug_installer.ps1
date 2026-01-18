# debug_installer.ps1 - Test self-extracting installer creation without rebuilding
# Usage: .\debug_installer.ps1 [manager|worker] [-StagingDir <path>]

param(
    [Parameter(Position=0, Mandatory=$true)]
    [ValidateSet("manager", "worker")]
    [string]$Component,
    
    [Parameter()]
    [string]$StagingDir = "",
    
    [Parameter()]
    [switch]$SkipZip
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
$OutputDir = Join-Path $ProjectRoot "dist"

Write-Host "=================================================="
Write-Host "Task Messenger Installer Debug Tool"
Write-Host "=================================================="
Write-Host "Component: $Component"
Write-Host "Version: $Version"
Write-Host "Platform: $Platform-$Arch"
Write-Host "=================================================="

# Determine staging directory
if ([string]::IsNullOrEmpty($StagingDir)) {
    $StagingDir = Join-Path $ProjectRoot "dist-staging"
}

$Comp = $Component
$CompStagingDir = Join-Path $StagingDir $Comp

# Verify staging directory exists
$PrefixPath = $Prefix -replace '^[A-Z]:', ''
$CompStagingPrefix = Join-Path $CompStagingDir $PrefixPath

if (-not (Test-Path $CompStagingPrefix)) {
    Write-Warning "Staging directory not found: $CompStagingPrefix"
    Write-Host "Attempting to create staging directory from build output..."

    # Determine builddir for the component
    $BuildDir = if ($Comp -eq "manager") { "builddir-manager-dist" } else { "builddir-worker-dist" }
    $BuildDirPath = Join-Path $ProjectRoot $BuildDir
    if (-not (Test-Path $BuildDirPath)) {
        Write-Error "Build directory not found: $BuildDirPath. Cannot create staging directory."
        exit 1
    }

    # Create the expected staging directory structure
    $CompStagingDir = Join-Path $StagingDir $Comp
    $CompStagingPrefix = Join-Path $CompStagingDir $PrefixPath
    New-Item -ItemType Directory -Force -Path (Join-Path $CompStagingPrefix "bin") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $CompStagingPrefix "etc\task-messenger") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $CompStagingPrefix "share\doc\task-messenger") | Out-Null

    # Copy binaries
    if ($Comp -eq "manager") {
        Copy-Item (Join-Path $BuildDirPath "manager\tm-manager.exe") (Join-Path $CompStagingPrefix "bin\tm-manager.exe") -Force
        Copy-Item (Join-Path $BuildDirPath "manager\zt-shared.dll") (Join-Path $CompStagingPrefix "bin\zt-shared.dll") -Force
    } else {
        Copy-Item (Join-Path $BuildDirPath "worker\tm-worker.exe") (Join-Path $CompStagingPrefix "bin\tm-worker.exe") -Force
        Copy-Item (Join-Path $BuildDirPath "worker\zt-shared.dll") (Join-Path $CompStagingPrefix "bin\zt-shared.dll") -Force
    }

    # Copy config files
    $ConfigFile = "config-$Comp.json"
    $ConfigSrc = Join-Path $ProjectRoot "config\$ConfigFile"
    $ConfigDst = Join-Path $CompStagingPrefix "etc\task-messenger\$ConfigFile"
    if (Test-Path $ConfigSrc) {
        Copy-Item $ConfigSrc $ConfigDst -Force
    }

    # Copy manager identity (if manager)
    if ($Comp -eq "manager") {
        $IdentitySrc = Join-Path $ProjectRoot "config\vn-manager-identity"
        $IdentityDst = Join-Path $CompStagingPrefix "etc\task-messenger\vn-manager-identity"
        if (Test-Path $IdentitySrc) {
            Copy-Item $IdentitySrc $IdentityDst -Recurse -Force
        }
    }

    # Copy documentation
    $DocSrc = Join-Path $ProjectRoot "docs"
    $DocDst = Join-Path $CompStagingPrefix "share\doc\task-messenger"
    if (Test-Path $DocSrc) {
        Copy-Item $DocSrc\* $DocDst -Recurse -Force
    }

    Write-Host "Staging directory created at: $CompStagingPrefix"
}

Write-Host "Using staging directory: $CompStagingPrefix"
Write-Host ""

# Function to create archive for a component
function Create-Archive {
    param([string]$Comp)
    
    $ArchiveName = "tm-$Comp-v$Version-$Platform-$Arch.zip"
    $ArchivePath = Join-Path $OutputDir $ArchiveName
    
    Write-Host "Creating archive: $ArchiveName"
    
    # Create output directory
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    
    # Create a temporary directory for the archive contents
    $TempArchiveDir = Join-Path $StagingDir "archive-$Comp-debug"
    if (Test-Path $TempArchiveDir) {
        Remove-Item -Recurse -Force $TempArchiveDir
    }
    
    # Create component-specific directory structure
    $ComponentName = if ($Comp -eq "manager") { "tm-manager" } else { "tm-worker" }
    $TaskMessengerDir = Join-Path $TempArchiveDir $ComponentName
    New-Item -ItemType Directory -Force -Path $TaskMessengerDir | Out-Null
    
    # Create bin directory
    $BinDir = Join-Path $TaskMessengerDir "bin"
    New-Item -ItemType Directory -Force -Path $BinDir | Out-Null
    
    # Copy files based on component
    if ($Comp -eq "manager") {
        # Manager: executable, DLL
        Copy-Item (Join-Path $CompStagingPrefix "bin\tm-manager.exe") $BinDir
        Copy-Item (Join-Path $CompStagingPrefix "bin\zt-shared.dll") $BinDir
    } else {
        # Worker: executable, DLL
        Copy-Item (Join-Path $CompStagingPrefix "bin\tm-worker.exe") $BinDir
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
        Copy-Item (Join-Path $ProjectRoot "extras\launchers\start-tm-manager.bat") $LaunchersDir
    } else {
        Copy-Item (Join-Path $ProjectRoot "extras\launchers\start-tm-worker.bat") $LaunchersDir
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
    
    $ComponentName = if ($Comp -eq "manager") { "tm-manager" } else { "tm-worker" }
    $InstallerName = "tm-$Comp-v$Version-$Platform-$Arch-installer.exe"
    $InstallerPath = Join-Path $OutputDir $InstallerName
    $ZipArchive = Join-Path $OutputDir "tm-$Comp-v$Version-$Platform-$Arch.zip"
    
    if (-not (Test-Path $ZipArchive)) {
        Write-Error "ZIP archive not found: $ZipArchive"
        Write-Host "Run without -SkipZip first to create the archive"
        exit 1
    }
    
    Write-Host ""
    Write-Host "Creating self-extracting installer: $InstallerName"
    
    # Create temporary directory for IExpress files
    $IExpressTemp = Join-Path $StagingDir "iexpress-$Comp-debug"
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
set LOGFILE=%TEMP%\\tm_install_debug.log
echo Starting installer > %LOGFILE%
title TaskMessenger $Comp Installer v$Version
echo ================================================
echo TaskMessenger $Comp Installer v$Version
echo ================================================
echo.
echo Extracting files...
echo.
powershell -NoProfile -ExecutionPolicy Bypass -Command "Write-Host 'Expanding archive...'; Expand-Archive -Path '%~dp0tm-$Comp-v$Version-$Platform-$Arch.zip' -DestinationPath '%TEMP%\TaskMessengerInstall' -Force; Write-Host 'Extraction complete.'"
if errorlevel 1 (
    echo.
    echo [ERROR] Failed to extract archive
    echo Press any key to exit...
    pause >nul
    exit /b 1
)
echo.
echo Extracted archive >> %LOGFILE%
echo Starting installation...
echo.
cd /d "%TEMP%\TaskMessengerInstall\$ComponentName"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\install_windows.ps1"
set INSTALL_EXIT_CODE=%errorlevel%
echo.
if %INSTALL_EXIT_CODE% neq 0 (
    echo [ERROR] Installation encountered errors (Exit code: %INSTALL_EXIT_CODE%)
    echo.
) else (
    echo [SUCCESS] Installation completed successfully!
    echo.
)
echo Cleaning up temporary files...
cd /d "%TEMP%"
rd /s /q "%TEMP%\TaskMessengerInstall" 2>nul
echo.
if %INSTALL_EXIT_CODE% equ 0 (
    echo Installation was successful!
) else (
    echo Installation failed. Please check the errors above.
)
echo.
echo  installation finished >> %LOGFILE%
echo Press any key to close this window...
pause >nul
exit /b %INSTALL_EXIT_CODE%
"@
    Set-Content -Path $ExtractAndInstallBat -Value $ExtractScript -Encoding ASCII
    
    Write-Host "Created batch script: $ExtractAndInstallBat"
    Write-Host ""
    Write-Host "Batch script contents:"
    Write-Host "----------------------------------------"
    Get-Content $ExtractAndInstallBat | ForEach-Object { Write-Host $_ }
    Write-Host "----------------------------------------"
    Write-Host ""
    
    # Create IExpress SED directive file
    $SedFile = Join-Path $IExpressTemp "installer.sed"
    $ZipFileName = "tm-$Comp-v$Version-$Platform-$Arch.zip"
    
    # Build strings with explicit variable expansion to avoid here-string issues
    $InstallPromptText = "Install TaskMessenger $Comp v${Version}?"
    $FriendlyNameText = "TaskMessenger $Comp v${Version} Installer"
    $FinishMessageText = "TaskMessenger $Comp v${Version} has been successfully installed! You can now run it from the Start Menu or by typing 'tm-$Comp' in a terminal."
    
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
AppLaunched=cmd.exe /c "extract_and_install.bat"
PostInstallCmd=<NONE>
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

#AppLaunched=cmd.exe /k "extract_and_install.bat & exit"
#AppLaunched=cmd.exe /k "extract_and_install.bat"
#AppLaunched=cmd.exe /c start "" cmd.exe /c "extract_and_install.bat"
#
    # Ensure proper line endings for Windows (CRLF)
    $SedContentWithCRLF = $SedContent -replace "`n", "`r`n"
    
    # Write SED file
    [System.IO.File]::WriteAllText($SedFile, $SedContentWithCRLF, [System.Text.Encoding]::ASCII)
    
    Write-Host "Created SED file: $SedFile"
    Write-Host ""
    Write-Host "SED file contents:"
    Write-Host "----------------------------------------"
    Get-Content $SedFile | ForEach-Object { Write-Host $_ }
    Write-Host "----------------------------------------"
    Write-Host ""
    
    # Get short path name for IExpress (it doesn't handle long paths well)
    $fso = New-Object -ComObject Scripting.FileSystemObject
    $sedFileShort = $fso.GetFile($SedFile).ShortPath
    
    # Run IExpress to create self-extracting installer
    Write-Host "Running IExpress..."
    Write-Host "Command: iexpress.exe /N $sedFileShort"
    Write-Host ""
    $IExpressExe = "$env:SystemRoot\System32\iexpress.exe"
    
    # Create a log file for IExpress output
    $LogFile = Join-Path $OutputDir "iexpress-$Comp-debug.log"
    
    # Run IExpress and capture output
    $ProcessInfo = New-Object System.Diagnostics.ProcessStartInfo
    $ProcessInfo.FileName = $IExpressExe
    $ProcessInfo.Arguments = "/N $sedFileShort"
    $ProcessInfo.RedirectStandardOutput = $true
    $ProcessInfo.RedirectStandardError = $true
    $ProcessInfo.UseShellExecute = $false
    $ProcessInfo.CreateNoWindow = $false
    
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
    Write-Host "Log file: $LogFile"
    Write-Host ""
    
    if ($stdout) {
        Write-Host "IExpress STDOUT:"
        Write-Host $stdout
        Write-Host ""
    }
    
    if ($stderr) {
        Write-Host "IExpress STDERR:"
        Write-Host $stderr
        Write-Host ""
    }
    
    if ($exitCode -ne 0) {
        Write-Warning "IExpress failed with exit code: $exitCode"
        Write-Host ""
        Write-Host "Debug information:"
        Write-Host "  SED file: $SedFile"
        Write-Host "  Temp directory: $IExpressTemp"
        Write-Host "  Log file: $LogFile"
        Write-Host ""
        Write-Host "Temp directory kept for inspection"
        Write-Host ""
        Write-Host "To manually test IExpress, run:"
        Write-Host "  iexpress.exe /N `"$SedFile`""
        Write-Host ""
        Write-Host "To test the batch script directly:"
        Write-Host "  cd `"$IExpressTemp`""
        Write-Host "  .\extract_and_install.bat"
        exit 1
    }
    
    if (Test-Path $InstallerPath) {
        Write-Host "Created self-extracting installer: $InstallerPath"
        Write-Host ""
        Write-Host "Installer size: $((Get-Item $InstallerPath).Length / 1MB) MB"
        Write-Host ""
        Write-Host "To test the installer:"
        Write-Host "  .\dist\$InstallerName"
        Write-Host ""
        Write-Host "Debug files kept in: $IExpressTemp"
    } else {
        Write-Warning "Installer not found after IExpress completed"
        Write-Host "Temp files kept in: $IExpressTemp"
    }
}

# Create archive unless skipped
if (-not $SkipZip) {
    Create-Archive -Comp $Component
} else {
    Write-Host "Skipping ZIP creation (using existing archive)"
    Write-Host ""
}

# Create self-extracting installer
Create-SelfExtractingInstaller -Comp $Component

Write-Host ""
Write-Host "=================================================="
Write-Host "Debug installer creation complete!"
Write-Host "=================================================="
