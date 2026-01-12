# build-libzt.ps1
param (
    [string]$LibztDir,
    [string]$BuildType = 'Debug'
)

$env:CMAKE_ARGS = "-DCMAKE_POLICY_VERSION_MINIMUM=3.10 -DBUILD_SHARED_LIB=ON -DBUILD_STATIC_LIB=ON"

# NOTE: File override / patch logic has been centralized in sync_overrides.py (Meson run_command)
# prior to invoking this script on all platforms. The manual copy steps formerly here are removed
# to avoid duplication. This script now assumes the libzt tree already contains the overridden files.
cd $LibztDir

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
. .\build.ps1
# Invoke Build-Host with the CMake-cased build type
Write-Host "Invoking Build-Host -BuildType $cmakeBuildType -Arch x64"
Build-Host -BuildType $cmakeBuildType -Arch "x64"

if ($LASTEXITCODE -ne 0) {
    Write-Error "Build-Host failed with exit code $LASTEXITCODE"
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