# build-libzt.ps1
param (
    [string]$LibztDir,
    [string]$BuildType = 'Debug'
)

# Enable verbose error output
$ErrorActionPreference = "Stop"
$VerbosePreference = "Continue"

Write-Host "========================================="
Write-Host "Starting libzt build"
Write-Host "LibztDir: $LibztDir"
Write-Host "BuildType: $BuildType"
Write-Host "========================================="

# Let CMake auto-detect the Windows SDK version instead of using a hardcoded one
# Query available Windows SDK versions
$sdkPath = "C:\Program Files (x86)\Windows Kits\10\Lib"
if (Test-Path $sdkPath) {
    $availableSDKs = Get-ChildItem $sdkPath | Where-Object { $_.PSIsContainer } | Select-Object -ExpandProperty Name
    Write-Host "Available Windows SDK versions: $($availableSDKs -join ', ')"
    $latestSDK = $availableSDKs | Sort-Object -Descending | Select-Object -First 1
    Write-Host "Using Windows SDK: $latestSDK"
    $env:WindowsSDKVersion = "$latestSDK\"
}

$env:CMAKE_ARGS = "-DCMAKE_POLICY_VERSION_MINIMUM=3.10 -DBUILD_SHARED_LIB=ON -DBUILD_STATIC_LIB=ON -DCMAKE_SYSTEM_VERSION=10.0"

# NOTE: File override / patch logic has been centralized in sync_overrides.py (Meson run_command)
# prior to invoking this script on all platforms. The manual copy steps formerly here are removed
# to avoid duplication. This script now assumes the libzt tree already contains the overridden files.

Write-Host "Changing directory to: $LibztDir"
if (-not (Test-Path $LibztDir)) {
    Write-Error "LibztDir does not exist: $LibztDir"
    exit 1
}
cd $LibztDir

Write-Host "Current directory: $(Get-Location)"
Write-Host "Looking for build.ps1..."
if (-not (Test-Path ".\build.ps1")) {
    Write-Error "build.ps1 not found in $LibztDir"
    exit 1
}
Write-Host "build.ps1 found"

# Convert Meson build type (lowercase) to CMake build type (proper case)
# Meson: 'debug', 'relwithdebinfo', 'release'
# CMake: 'Debug', 'RelWithDebInfo', 'Release'
$cmakeBuildType = switch ($BuildType.ToLower()) {
    'debug' { 'Debug' }
    'relwithdebinfo' { 'RelWithDebInfo' }
    'release' { 'Release' }
    default { $BuildType } # Fallback to original if unknown
}

# source the build script
Write-Host "Sourcing build.ps1..."
try {
    . .\build.ps1
    Write-Host "build.ps1 sourced successfully"
} catch {
    Write-Error "Failed to source build.ps1: $_"
    exit 1
}

# Invoke Build-Host with the CMake-cased build type
Write-Host "Invoking Build-Host -BuildType $cmakeBuildType -Arch x64"

$logFile = Join-Path $LibztDir "build.log"
try {
    Build-Host -BuildType $cmakeBuildType -Arch "x64" 2>&1 | Tee-Object -FilePath $logFile
    Write-Host "Build-Host completed"
} catch {
    Write-Error "Build-Host failed: $_"
    if (Test-Path $logFile) {
        Write-Host "`n=== Last 50 lines of build.log ==="
        Get-Content $logFile -Tail 50
    }
    exit 1
}

if ($LASTEXITCODE -ne 0) {
    Write-Error "Build-Host failed with exit code $LASTEXITCODE"
    if (Test-Path $logFile) {
        Write-Host "`n=== Last 50 lines of build.log ==="
        Get-Content $logFile -Tail 50
    }
    exit $LASTEXITCODE
}

# Determine the target and cache paths based on build type
# These use lowercase for consistency with libzt's output structure
$buildTypeLower = $cmakeBuildType.ToLower()
$targetPath = "dist\\win-x64-host-$buildTypeLower"
$cachePath = "cache\\win-x64-host-$buildTypeLower"

# Copy the DLL import library from build cache to dist
# The CMake build creates zt-shared.lib in the cache directory but libzt's build.ps1 doesn't copy it
# Note: CMake outputs to lib\$BuildType (proper case) subdirectory
$importLibSrc = Join-Path $cachePath "lib\\$cmakeBuildType\\zt-shared.lib"
$importLibDst = Join-Path $targetPath "lib\\zt-shared.lib"

Write-Host "Looking for import library at: $importLibSrc"
if (Test-Path $importLibSrc) {
    Copy-Item $importLibSrc $importLibDst -Force
    Write-Host "Copied DLL import library: $importLibSrc -> $importLibDst"
} else {
    Write-Error "DLL import library not found at: $importLibSrc"
    Write-Host "Directory contents of cache:"
    Get-ChildItem $cachePath -Recurse | Select-Object FullName
    exit 1
}

# Copy the DLL to match the import library name
# libzt builds the DLL as zt-shared.dll in cache, we copy it to dist
$dllSrc = Join-Path $cachePath "lib\\$cmakeBuildType\\zt-shared.dll"
$dllDst = Join-Path $targetPath "lib\\zt-shared.dll"
if (Test-Path $dllSrc) {
    Copy-Item $dllSrc $dllDst -Force
    Write-Host "Copied shared library DLL: $dllSrc -> $dllDst"
} else {
    Write-Warning "Shared library DLL not found at: $dllSrc"
    Write-Warning "Runtime loading may fail!"
}

# Note: No longer creating native junction - meson.build directly references
# the platform-specific distribution directory