# Phase 3: Installation Script Improvements

## Context

During Windows installation script testing, several usability issues were identified and resolved. This document describes the improvements made to `install_windows.ps1` that should also be applied to `install_linux.sh` for consistency.

## Problems Identified

### 1. Manual Component Specification Required
**Problem:** Users had to specify `-Component manager` or `-Component worker` every time they ran the installation script.

**User Experience Issue:** 
- Each distribution archive contains only one component (manager OR worker)
- Requiring users to specify which component when it's already determined by the archive is redundant
- Error-prone: users might specify wrong component for the archive they downloaded

### 2. Redundant Archive Extraction
**Problem:** Script required `-Archive` parameter even when run from already-extracted distribution files.

**User Experience Issue:**
- Most users extract the archive first, then run the installation script
- Forcing them to specify the archive path again is redundant
- Creates unnecessary confusion about correct usage workflow

### 3. Help Parameter Validation Order
**Problem:** `-Help` parameter validation occurred after component parameter validation, requiring component to be specified just to see help.

**User Experience Issue:**
- Users couldn't run `.\install_windows.ps1 -Help` without specifying component
- Help text should always be accessible without other parameters

### 4. Unclear Error Messages
**Problem:** When component detection failed, error messages didn't clearly guide users to solutions.

**User Experience Issue:**
- Users unsure whether to extract archive or specify path
- No clear guidance on expected workflow

## Solutions Implemented

### 1. Automatic Component Detection

**Implementation:**
- Removed `-Component` parameter entirely from script interface
- Added detection function that checks for component-specific executables
- Returns hashtable with both the root directory and detected component type

**Code Pattern (PowerShell):**
```powershell
function Test-ExtractedFiles {
    $scriptDir = Split-Path -Parent $PSCommandPath
    $extractedRoot = Split-Path -Parent $scriptDir
    
    # Check for marker file (DLL) to confirm extracted archive
    $dllPath = Join-Path $extractedRoot "bin\zt-shared.dll"
    
    if (Test-Path $dllPath) {
        # Detect component by checking which executable exists
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
```

**Bash Equivalent Pattern:**
```bash
detect_extracted_files() {
    local script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    local extracted_root="$(dirname "$script_dir")"
    
    # Check for marker file
    if [ -f "$extracted_root/bin/libzt-shared.so" ]; then
        # Detect component
        if [ -f "$extracted_root/bin/manager" ]; then
            echo "manager:$extracted_root"
            return 0
        elif [ -f "$extracted_root/bin/worker" ]; then
            echo "worker:$extracted_root"
            return 0
        fi
    fi
    
    return 1
}
```

### 2. Smart Source Detection with Archive Fallback

**Implementation:**
- Check if running from extracted archive first (preferred)
- Fall back to archive extraction if not in extracted directory
- Automatically detect component from either source

**Code Pattern (PowerShell):**
```powershell
function Main {
    # First check for extracted files
    $extractedInfo = Test-ExtractedFiles
    
    if ($extractedInfo) {
        # Use extracted files directly
        $sourceDir = $extractedInfo.Root
        $Component = $extractedInfo.Component
        Write-Info "Using files from extracted archive"
    } else {
        # Fall back to archive extraction
        if (-not $Archive) {
            Write-ErrorMsg "Error with helpful guidance"
            exit 1
        }
        
        # Extract and detect component from archive
        # ...
    }
}
```

### 3. Updated Parameter Structure

**Before:**
```powershell
param(
    [Parameter(Mandatory=$true)]
    [ValidateSet("manager", "worker")]
    [string]$Component,
    
    [Parameter(Mandatory=$false)]
    [string]$InstallDir,
    
    [Parameter(Mandatory=$false)]
    [string]$Archive,
    
    [switch]$Help
)
```

**After:**
```powershell
param(
    [Parameter(Mandatory=$false)]
    [string]$InstallDir,
    
    [Parameter(Mandatory=$false)]
    [string]$Archive,
    
    [switch]$Help
)
```

### 4. Enhanced Error Messages

**Pattern:**
```powershell
Write-ErrorMsg "Could not detect component from extracted files"
Write-ErrorMsg ""
Write-ErrorMsg "Solutions:"
Write-ErrorMsg "  1. Extract a TaskMessenger distribution archive (manager or worker)"
Write-ErrorMsg "     and run this script from the extracted TaskMessenger directory"
Write-ErrorMsg ""
Write-ErrorMsg "  2. Specify the archive path manually:"
Write-ErrorMsg "     .\install_windows.ps1 -Archive 'path\to\task-messenger-{component}-v1.0.0-windows-x64.zip'"
```

### 5. Updated Help Text

**Pattern:**
```text
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
```

## Bug Fixes During Implementation

### Variable Reference Error
**Bug:** Used undefined variable `$extractedRoot` instead of `$extractedInfo`

**Fix:**
```powershell
# Wrong:
if ($extractedRoot) {
    $sourceDir = $extractedRoot
}

# Correct:
if ($extractedInfo) {
    $sourceDir = $extractedInfo.Root
    $Component = $extractedInfo.Component
}
```

## Testing Verification

### Success Criteria
1. ✅ User can run `.\install_windows.ps1` with no arguments from extracted archive
2. ✅ Script automatically detects manager vs worker component
3. ✅ Script works from both extracted directory and with `-Archive` parameter
4. ✅ Help displays without requiring other parameters
5. ✅ Clear error messages when component cannot be detected
6. ✅ No manual component specification needed

### Test Commands
```powershell
# From extracted archive (primary workflow)
cd task-messenger-manager-v1.0.0-windows-x64\TaskMessenger
.\scripts\install_windows.ps1

# With custom install directory
.\scripts\install_windows.ps1 -InstallDir "C:\Custom\Path"

# With archive path (fallback workflow)
.\scripts\install_windows.ps1 -Archive "path\to\archive.zip"

# Help text
.\scripts\install_windows.ps1 -Help
```

## Application to Linux Script

### Changes Needed for install_linux.sh

1. **Remove component parameter** from argument parsing
2. **Add detection function** similar to `Test-ExtractedFiles`:
   - Check for `bin/libzt-shared.so` as marker
   - Detect component by checking for `bin/manager` or `bin/worker`
   - Return component type and root directory
3. **Update main function** to call detection first, fall back to archive
4. **Update help text** to reflect auto-detection
5. **Enhance error messages** with clear guidance
6. **Update examples** in documentation

### Platform-Specific Considerations

**Linux Differences:**
- Executables: `manager` and `worker` (no `.exe` extension)
- Shared library: `libzt-shared.so` (not `zt-shared.dll`)
- Default paths: `~/.local/TaskMessenger` and `~/.config/task-messenger`
- PATH handling: Add to `.bashrc` or `.zshrc` instead of Windows registry
- Desktop files: Use `.desktop` format, install to `~/.local/share/applications`

**Keep Consistent:**
- Parameter names: `--install-dir`, `--archive`, `--help`
- Auto-detection logic
- Error message format and guidance
- User experience flow

## Benefits

### User Experience
- **Zero configuration** for standard workflow: extract and run
- **Intuitive behavior**: script is smart enough to know what to do
- **Less error-prone**: no manual component specification
- **Flexible**: still supports archive path if needed

### Maintenance
- **Single script per component**: Each archive contains appropriate installation script
- **Consistent behavior**: Manager and worker archives work identically
- **Self-contained**: Script knows its context automatically

### Documentation
- **Simpler instructions**: "Extract and run `install_windows.ps1`"
- **Fewer parameters to explain**: Only install directory is commonly customized
- **Clear examples**: Primary workflow is obvious

## Distribution Impact

### Archive Contents
Each distribution archive now includes a **self-aware** installation script:
- `task-messenger-manager-v1.0.0-windows-x64.zip` contains script that detects manager
- `task-messenger-worker-v1.0.0-windows-x64.zip` contains script that detects worker

### Build Process
No changes needed to `build_distribution.ps1` - it already includes the installation script in each archive. Just ensure the updated script is included in future builds.

## Summary

The installation script evolved from requiring explicit component specification to **automatic detection**, making the user experience simpler and more intuitive. Users now extract the archive and run the script with zero configuration, while the script intelligently determines what component it's installing and where to find the files.

This same pattern should be applied to the Linux installation script to maintain consistency across platforms.
