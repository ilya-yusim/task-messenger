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

## Additional Improvements (After Initial Implementation)

### 6. Config File Management Overhaul

**Problem:** Script generated empty config templates in AppData, but archives contain pre-configured files.

**Solution:**
- Copy config file from archive's `etc/` directory to installation directory
- Use installed config file instead of AppData template
- Start Menu shortcut points to installed config with `-c` argument

**Implementation:**
```powershell
# Copy config file from archive
$etcDir = Join-Path $extractedDir "etc"
$configFile = Join-Path $etcDir "config-$Component.json"
if (Test-Path $configFile) {
    Copy-Item $configFile $componentDir -Force
}

# Shortcut uses installed config
$configFile = Join-Path $componentDir "config-$Component.json"
$shortcut.Arguments = "-c `"$configFile`""
```

**Benefits:**
- ✅ Users get actual config from distribution, not empty template
- ✅ Config versioned with application (not separate in AppData)
- ✅ Executable launched with correct config path automatically

**Linux Equivalent:**
```bash
# Copy config from archive
local etc_dir="$extracted_dir/etc"
local config_file="$etc_dir/config-$component.json"
if [ -f "$config_file" ]; then
    cp "$config_file" "$component_dir/"
fi

# Desktop file uses installed config
Exec=$component_dir/$component -c "$component_dir/config-$component.json"
```

### 7. Identity Directory Relocation

**Problem:** Identity files were in `bin/` directory but should be in `etc/` with config.

**Change:** Build and installation scripts updated to:
- Archive places `.vn_manager_identity/` in `etc/` directory
- Installation copies from `etc/.vn_manager_identity/` to component directory
- Only `identity.public` and `identity.secret` included (not peers, roots, etc.)

**Implementation:**
```powershell
# Copy identity directory for manager
if ($Component -eq "manager") {
    $identityDir = Join-Path $etcDir ".vn_manager_identity"
    
    if (Test-Path $identityDir) {
        Copy-Item $identityDir $componentDir -Recurse -Force
        
        # Set restrictive permissions on secret file
        $secretPath = Join-Path $componentDir ".vn_manager_identity\identity.secret"
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
```

**Archive Structure:**
```
TaskMessenger/
├── bin/
│   ├── manager.exe
│   └── zt-shared.dll
├── etc/
│   ├── config-manager.json
│   └── .vn_manager_identity/
│       ├── identity.public
│       └── identity.secret
└── share/doc/...
```

**Installed Structure:**
```
%LOCALAPPDATA%\TaskMessenger\manager\
├── manager.exe
├── zt-shared.dll
├── config-manager.json
├── .vn_manager_identity/
│   ├── identity.public
│   └── identity.secret (restricted permissions)
├── doc/
└── VERSION
```

**Linux Differences:**
- Permissions: `chmod 600` on identity.secret
- Location: `~/.local/TaskMessenger/manager/.vn_manager_identity/`
- Library: `libzt-shared.so` instead of DLL

### 8. Start Menu Shortcut with Arguments

**Enhancement:** Shortcuts now include config file argument.

**Implementation:**
```powershell
$shortcut.Arguments = "-c `"$configFile`""
```

**Result:** Double-clicking shortcut launches as:
```
manager.exe -c "C:\Users\username\AppData\Local\TaskMessenger\manager\config-manager.json"
```

**Linux Desktop File Equivalent:**
```desktop
[Desktop Entry]
Name=TaskMessenger Manager
Exec=/home/username/.local/TaskMessenger/manager/manager -c "/home/username/.local/TaskMessenger/manager/config-manager.json"
Terminal=false
Type=Application
```

### 9. Streamlined File Copying Logic

**Change:** Removed obsolete identity file copying from `bin/` directory.

**Before:**
```powershell
$identityPublic = Join-Path $libDir "identity.public"
$identitySecret = Join-Path $libDir "identity.secret"
# Copy from bin/
```

**After:**
```powershell
$identityDir = Join-Path $etcDir ".vn_manager_identity"
# Copy entire directory from etc/
```

**Impact:** Cleaner code, matches actual archive structure.

## Linux Script Implementation Checklist

### Must Implement (Parity with Windows)

- [ ] Remove `-c|--component` parameter from argument parsing
- [ ] Add `detect_extracted_files()` function checking for `libzt-shared.so`
- [ ] Update main flow to try detection first, fall back to archive
- [ ] Update help text to remove component parameter
- [ ] Enhance error messages with clear guidance
- [ ] Copy config file from `etc/config-{component}.json` to installation directory
- [ ] Copy `.vn_manager_identity/` from `etc/` (manager only)
- [ ] Desktop file includes `-c` argument pointing to installed config
- [ ] Set `chmod 600` on `.vn_manager_identity/identity.secret`
- [ ] Update archive structure: move identity directory to `etc/`

### Platform-Specific Implementations

**File Paths:**
```bash
# Linux paths
DEFAULT_INSTALL_DIR="$HOME/.local/TaskMessenger"
CONFIG_DIR="$HOME/.config/task-messenger"
DESKTOP_FILE_DIR="$HOME/.local/share/applications"

# Check for extracted files
MARKER_FILE="bin/libzt-shared.so"
```

**Detection Function:**
```bash
detect_extracted_files() {
    local script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    local extracted_root="$(dirname "$script_dir")"
    
    if [ -f "$extracted_root/bin/libzt-shared.so" ]; then
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

**Config and Identity Installation:**
```bash
# Copy config from etc/
cp "$extracted_dir/etc/config-$component.json" "$component_dir/"

# Copy identity directory (manager only)
if [ "$component" = "manager" ]; then
    if [ -d "$extracted_dir/etc/.vn_manager_identity" ]; then
        cp -r "$extracted_dir/etc/.vn_manager_identity" "$component_dir/"
        
        # Restrict permissions on secret
        chmod 600 "$component_dir/.vn_manager_identity/identity.secret"
    fi
fi
```

**Desktop File with Config Argument:**
```bash
cat > "$desktop_file" << EOF
[Desktop Entry]
Name=TaskMessenger $component
Exec=$component_dir/$component -c "$component_dir/config-$component.json"
Icon=$component_dir/icon.png
Terminal=false
Type=Application
Categories=Network;
EOF
```

### Build Script Updates (Already Done)

- [x] `build_distribution.ps1`: Copy `.vn_manager_identity/` from `etc/` to archive `etc/`
- [x] `build_distribution.sh`: Copy `.vn_manager_identity/` from `etc/` to archive `etc/`
- [x] Both scripts place identity directory in `etc/` not `bin/`

### Meson Build Updates (Already Done)

- [x] Install `.vn_manager_identity/` to `etc/task-messenger/` (only identity.public and identity.secret)
- [x] Install config files to `etc/task-messenger/`

## Summary

The installation script evolved from requiring explicit component specification to **automatic detection**, making the user experience simpler and more intuitive. Users now extract the archive and run the script with zero configuration, while the script intelligently determines what component it's installing and where to find the files.

Additional improvements include proper config file management (using distributed configs instead of empty templates), identity directory relocation to `etc/` for consistency, and Start Menu shortcuts that automatically pass the correct config file path.

**Critical for Linux:** All these improvements must be applied to `install_linux.sh` to maintain cross-platform consistency. The Linux script currently still requires manual component specification and doesn't implement the smart detection or proper config/identity file handling.
