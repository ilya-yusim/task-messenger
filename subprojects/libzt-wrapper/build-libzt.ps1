# build-libzt.ps1
param (
    [string]$LibztDir,
    [string]$BuildType = 'Debug'
)

$env:CMAKE_ARGS = "-DCMAKE_POLICY_VERSION_MINIMUM=3.10"

# NOTE: File override / patch logic has been centralized in sync_overrides.py (Meson run_command)
# prior to invoking this script on all platforms. The manual copy steps formerly here are removed
# to avoid duplication. This script now assumes the libzt tree already contains the overridden files.
cd $LibztDir

# source the build script
. .\build.ps1
# Invoke Build-Host with the requested build type (Debug or Release).
Build-Host -BuildType $BuildType -Arch "x64"

# New-Item -ItemType SymbolicLink -Path "dist\native" -Target "dist\win-x64-host-release"
# New-Item -ItemType Junction -Path "dist\native" -Target "dist\win-x64-host-release"
# New-Item -ItemType Junction -Path "dist\native" -Target "dist\win-x64-host-debug"
$nativePath = "dist\\native"

# Choose target path based on build type used for libzt to match the dist layout
if ($BuildType -ieq 'Debug') {
    $targetPath = "dist\\win-x64-host-debug"
} else {
    $targetPath = "dist\\win-x64-host-release"
}

# Remove existing junction if it exists to recreate it for the correct build type
if (Test-Path $nativePath) {
    Remove-Item $nativePath -Force
}

New-Item -ItemType Junction -Path $nativePath -Target $targetPath