# macOS Build Implementation Plan

## Overview

This document provides the complete implementation details for adding macOS build support to task-messenger, focusing on:
1. Meson build file updates (4 files)
2. macOS build script creation
3. GitHub Actions workflow integration

---

## Part 1: Meson Build File Updates

### 1.1 Root `meson.build`

**File**: `meson.build`  
**Location**: Lines 60-73  
**Purpose**: Add darwin branch for libzt dylib path

**Current Code**:
```meson
if host_machine.system() == 'windows'
  libzt_lib_path = libzt_sp.get_variable('libzt_lib_path')
  libzt_dll_file = join_paths(libzt_lib_path, 'zt-shared.dll')
elif host_machine.system() == 'linux'
  libzt_lib_path = libzt_sp.get_variable('libzt_lib_path')
  libzt_so_file = join_paths(libzt_lib_path, 'libzt.so')
endif
```

**New Code** (add darwin branch):
```meson
if host_machine.system() == 'windows'
  libzt_lib_path = libzt_sp.get_variable('libzt_lib_path')
  libzt_dll_file = join_paths(libzt_lib_path, 'zt-shared.dll')
elif host_machine.system() == 'linux'
  libzt_lib_path = libzt_sp.get_variable('libzt_lib_path')
  libzt_so_file = join_paths(libzt_lib_path, 'libzt.so')
elif host_machine.system() == 'darwin'
  libzt_lib_path = libzt_sp.get_variable('libzt_lib_path')
  libzt_dylib_file = join_paths(libzt_lib_path, 'libzt.dylib')
endif
```

---

### 1.2 `manager/meson.build`

**File**: `manager/meson.build`  
**Location**: Lines 36-51  
**Purpose**: Add darwin branch for copying libzt.dylib in development builds

**Current Code**:
```meson
if host_machine.system() == 'windows'
  # Windows: Copy zt-shared.dll
  custom_target('copy-zt-dll-manager',
    output: 'zt-shared.dll',
    command: ['powershell', '-Command', 'Copy-Item', '-Path', libzt_dll_file, '-Destination', '@OUTPUT@', '-Force'],
    build_by_default: true,
    install: false,
  )
elif host_machine.system() == 'linux'
  # Linux: Copy libzt.so
  custom_target('copy-zt-so-manager',
    output: 'libzt.so',
    command: ['cp', libzt_so_file, '@OUTPUT@'],
    build_by_default: true,
    install: false,
  )
endif
```

**New Code** (add darwin branch):
```meson
if host_machine.system() == 'windows'
  # Windows: Copy zt-shared.dll
  custom_target('copy-zt-dll-manager',
    output: 'zt-shared.dll',
    command: ['powershell', '-Command', 'Copy-Item', '-Path', libzt_dll_file, '-Destination', '@OUTPUT@', '-Force'],
    build_by_default: true,
    install: false,
  )
elif host_machine.system() == 'linux'
  # Linux: Copy libzt.so
  custom_target('copy-zt-so-manager',
    output: 'libzt.so',
    command: ['cp', libzt_so_file, '@OUTPUT@'],
    build_by_default: true,
    install: false,
  )
elif host_machine.system() == 'darwin'
  # macOS: Copy libzt.dylib
  custom_target('copy-zt-dylib-manager',
    output: 'libzt.dylib',
    command: ['cp', libzt_dylib_file, '@OUTPUT@'],
    build_by_default: true,
    install: false,
  )
endif
```

---

### 1.3 `worker/meson.build`

**File**: `worker/meson.build`  
**Location**: Lines 56-71  
**Purpose**: Add darwin branch for copying libzt.dylib (same pattern as manager)

**Current Code**:
```meson
if host_machine.system() == 'windows'
  # Windows: Copy zt-shared.dll
  custom_target('copy-zt-dll-worker',
    output: 'zt-shared.dll',
    command: ['powershell', '-Command', 'Copy-Item', '-Path', libzt_dll_file, '-Destination', '@OUTPUT@', '-Force'],
    build_by_default: true,
    install: false,
  )
elif host_machine.system() == 'linux'
  # Linux: Copy libzt.so
  custom_target('copy-zt-so-worker',
    output: 'libzt.so',
    command: ['cp', libzt_so_file, '@OUTPUT@'],
    build_by_default: true,
    install: false,
  )
endif
```

**New Code** (add darwin branch):
```meson
if host_machine.system() == 'windows'
  # Windows: Copy zt-shared.dll
  custom_target('copy-zt-dll-worker',
    output: 'zt-shared.dll',
    command: ['powershell', '-Command', 'Copy-Item', '-Path', libzt_dll_file, '-Destination', '@OUTPUT@', '-Force'],
    build_by_default: true,
    install: false,
  )
elif host_machine.system() == 'linux'
  # Linux: Copy libzt.so
  custom_target('copy-zt-so-worker',
    output: 'libzt.so',
    command: ['cp', libzt_so_file, '@OUTPUT@'],
    build_by_default: true,
    install: false,
  )
elif host_machine.system() == 'darwin'
  # macOS: Copy libzt.dylib
  custom_target('copy-zt-dylib-worker',
    output: 'libzt.dylib',
    command: ['cp', libzt_dylib_file, '@OUTPUT@'],
    build_by_default: true,
    install: false,
  )
endif
```

---

### 1.4 `subprojects/libzt-wrapper/meson.build`

**File**: `subprojects/libzt-wrapper/meson.build`  
**Two changes needed**

#### Change 1: RPATH Configuration

**Location**: Lines 174-179  
**Purpose**: Add macOS RPATH using @executable_path

**Current Code**:
```meson
if host_machine.system() == 'linux'
  # Use $ORIGIN to allow executables to find the library relative to their location
  link_args += ['-Wl,-rpath,$ORIGIN/../lib']
endif
```

**New Code**:
```meson
if host_machine.system() == 'linux'
  # Use $ORIGIN to allow executables to find the library relative to their location
  link_args += ['-Wl,-rpath,$ORIGIN/../lib']
elif host_machine.system() == 'darwin'
  # Use @executable_path for macOS
  link_args += ['-Wl,-rpath,@executable_path/../lib']
endif
```

#### Change 2: Library Installation

**Location**: Lines 205-212  
**Purpose**: Install libzt.dylib on macOS instead of libzt.so

**Current Code**:
```meson
else
  # Linux/macOS: Install shared library to lib directory
  so_file = join_paths(libzt_lib_path, 'libzt.so')
  install_data(so_file, install_dir: join_paths(get_option('prefix'), 'lib'))
  set_variable('libzt_lib_path', libzt_lib_path)
endif
```

**New Code**:
```meson
else
  # Linux/macOS: Install shared library to lib directory
  if host_machine.system() == 'darwin'
    dylib_file = join_paths(libzt_lib_path, 'libzt.dylib')
    install_data(dylib_file, install_dir: join_paths(get_option('prefix'), 'lib'))
  else
    so_file = join_paths(libzt_lib_path, 'libzt.so')
    install_data(so_file, install_dir: join_paths(get_option('prefix'), 'lib'))
  endif
  set_variable('libzt_lib_path', libzt_lib_path)
endif
```

---

## Part 2: macOS Build Script

**File**: `extras/scripts/build_distribution_macos.sh`  
**Template**: Based on `extras/scripts/build_distribution.sh`

### Complete Script

```bash
#!/bin/bash
# build_distribution_macos.sh - Create distributable packages for macOS
#
# Usage: ./build_distribution_macos.sh [manager|worker|all]
#
# This script builds task-messenger distribution packages for macOS.
# It creates tar.gz archives suitable for distribution.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_ROOT"

# Parse component argument
COMPONENT="${1:-all}"
if [[ ! "$COMPONENT" =~ ^(manager|worker|all)$ ]]; then
    echo "❌ Error: Invalid component '$COMPONENT'"
    echo "Usage: $0 [manager|worker|all]"
    exit 1
fi

# Extract version from meson.build
VERSION=$(grep "project('task-messenger'" meson.build | grep -oP "version:\s*'\K[^']+")
if [ -z "$VERSION" ]; then
    echo "❌ Error: Could not extract version from meson.build"
    exit 1
fi

# Detect platform and architecture
PLATFORM="macos"
ARCH=$(uname -m)  # x86_64 or arm64

# Configuration
PREFIX="/usr/local"
BUILDTYPE="release"
STAGING_DIR="$PROJECT_ROOT/dist-staging"
OUTPUT_DIR="$PROJECT_ROOT/dist"

# Track generated files
GENERATED_FILES=()

echo "=================================================="
echo "📦 Task Messenger macOS Distribution Builder"
echo "=================================================="
echo "Component: $COMPONENT"
echo "Version:   $VERSION"
echo "Platform:  $PLATFORM"
echo "Arch:      $ARCH"
echo "Prefix:    $PREFIX"
echo "=================================================="
echo ""

# Build component function
build_component() {
    local comp=$1
    local builddir="builddir-${comp}-dist"
    
    echo "🔨 Building $comp for macOS..."
    
    # Clean previous build
    if [ -d "$builddir" ]; then
        echo "   Cleaning previous build directory..."
        rm -rf "$builddir"
    fi
    
    # Determine build options
    local build_opts=()
    if [[ "$comp" == "manager" ]]; then
        build_opts+=("-Dbuild_worker=false")
        echo "   Build mode: Manager only"
    elif [[ "$comp" == "worker" ]]; then
        build_opts+=("-Dbuild_manager=false")
        echo "   Build mode: Worker only"
    fi
    
    # Setup meson
    echo "   Setting up meson..."
    meson setup "$builddir" \
        --prefix="$PREFIX" \
        --buildtype="$BUILDTYPE" \
        -Ddebug_logging=false \
        -Dprofiling_unwind=false \
        "${build_opts[@]}" || {
            echo "❌ Meson setup failed for $comp"
            return 1
        }
    
    # Compile
    echo "   Compiling..."
    meson compile -C "$builddir" || {
        echo "❌ Compilation failed for $comp"
        return 1
    }
    
    # Install to staging
    echo "   Installing to staging directory..."
    DESTDIR="$STAGING_DIR/$comp" meson install -C "$builddir" || {
        echo "❌ Installation failed for $comp"
        return 1
    }
    
    echo "✅ Build complete for $comp"
    echo ""
}

# Create archive function
create_archive() {
    local comp=$1
    local archive_name="tm-${comp}-v${VERSION}-${PLATFORM}-${ARCH}.tar.gz"
    local archive_path="$OUTPUT_DIR/$archive_name"
    
    echo "📦 Creating archive: $archive_name"
    
    # Create output directory
    mkdir -p "$OUTPUT_DIR"
    
    # Create temporary archive directory
    local temp_archive_dir="$STAGING_DIR/archive-$comp"
    rm -rf "$temp_archive_dir"
    mkdir -p "$temp_archive_dir/tm-$comp"
    
    local staging_prefix="$STAGING_DIR/$comp$PREFIX"
    local archive_root="$temp_archive_dir/tm-$comp"
    
    # Verify staging directory exists
    if [ ! -d "$staging_prefix" ]; then
        echo "❌ Error: Staging directory not found: $staging_prefix"
        return 1
    fi
    
    # Copy binaries
    echo "   Copying binaries..."
    mkdir -p "$archive_root/bin"
    if [ -f "$staging_prefix/bin/tm-${comp}" ]; then
        cp "$staging_prefix/bin/tm-${comp}" "$archive_root/bin/"
        chmod +x "$archive_root/bin/tm-${comp}"
    else
        echo "❌ Error: Binary not found: $staging_prefix/bin/tm-${comp}"
        return 1
    fi
    
    # Copy shared library (libzt.dylib)
    echo "   Copying shared library..."
    mkdir -p "$archive_root/lib"
    if [ -f "$staging_prefix/lib/libzt.dylib" ]; then
        cp "$staging_prefix/lib/libzt.dylib" "$archive_root/lib/"
    else
        echo "❌ Error: libzt.dylib not found: $staging_prefix/lib/libzt.dylib"
        return 1
    fi
    
    # Copy configuration files
    echo "   Copying configuration files..."
    mkdir -p "$archive_root/config"
    if [[ "$comp" == "manager" ]]; then
        # Manager: config + identity directory
        cp "$staging_prefix/etc/task-messenger/config-manager.json" "$archive_root/config/"
        if [ -d "$staging_prefix/etc/task-messenger/vn-manager-identity" ]; then
            cp -r "$staging_prefix/etc/task-messenger/vn-manager-identity" "$archive_root/config/"
        fi
    else
        # Worker: config only
        cp "$staging_prefix/etc/task-messenger/config-worker.json" "$archive_root/config/"
    fi
    
    # Copy documentation
    echo "   Copying documentation..."
    mkdir -p "$archive_root/doc"
    if [ -d "$staging_prefix/share/doc/task-messenger" ]; then
        cp -r "$staging_prefix/share/doc/task-messenger/"* "$archive_root/doc/" 2>/dev/null || true
    fi
    
    # Copy LICENSE
    if [ -f "$PROJECT_ROOT/LICENSE" ]; then
        cp "$PROJECT_ROOT/LICENSE" "$archive_root/"
    fi
    
    # Copy installation scripts (placeholder for now)
    echo "   Copying installation scripts..."
    mkdir -p "$archive_root/scripts"
    
    # Create placeholder install script
    cat > "$archive_root/scripts/install_macos.sh" << 'EOF'
#!/bin/bash
echo "macOS installation script - Coming soon"
echo "For now, manually copy files to desired location"
EOF
    chmod +x "$archive_root/scripts/install_macos.sh"
    
    # Create placeholder uninstall script
    cat > "$archive_root/scripts/uninstall_macos.sh" << 'EOF'
#!/bin/bash
echo "macOS uninstallation script - Coming soon"
EOF
    chmod +x "$archive_root/scripts/uninstall_macos.sh"
    
    # Copy launcher scripts if they exist
    echo "   Copying launcher scripts..."
    mkdir -p "$archive_root/launchers"
    if [ -d "$PROJECT_ROOT/extras/launchers" ]; then
        if [[ "$comp" == "manager" ]]; then
            if [ -f "$PROJECT_ROOT/extras/launchers/start-tm-manager.sh" ]; then
                cp "$PROJECT_ROOT/extras/launchers/start-tm-manager.sh" "$archive_root/launchers/"
                chmod +x "$archive_root/launchers/start-tm-manager.sh"
            fi
        else
            if [ -f "$PROJECT_ROOT/extras/launchers/start-tm-worker.sh" ]; then
                cp "$PROJECT_ROOT/extras/launchers/start-tm-worker.sh" "$archive_root/launchers/"
                chmod +x "$archive_root/launchers/start-tm-worker.sh"
            fi
        fi
    fi
    
    # Create VERSION file
    echo "$VERSION" > "$archive_root/VERSION"
    
    # Create INSTALL.txt
    cat > "$archive_root/INSTALL.txt" << EOF
TaskMessenger $comp Installation Instructions (macOS)

Quick Start:
    1. Extract this archive
    2. Run: ./scripts/install_macos.sh

Default installation paths:
  - Binaries: ~/Library/Application Support/TaskMessenger/tm-$comp/bin/
  - Config:   ~/Library/Application Support/TaskMessenger/config/$comp/
  - Symlink:  ~/.local/bin/tm-$comp

After installation, add ~/.local/bin to your PATH if not already present:
    export PATH="\$HOME/.local/bin:\$PATH"

For more information, see: doc/INSTALLATION.md
EOF
    
    # Create tar.gz archive
    echo "   Creating tar.gz archive..."
    cd "$temp_archive_dir"
    tar -czf "$archive_path" "tm-$comp/" || {
        echo "❌ Failed to create tar.gz archive"
        cd "$PROJECT_ROOT"
        return 1
    }
    cd "$PROJECT_ROOT"
    
    echo "✅ Created: $archive_path"
    
    # Generate SHA-256 checksum
    echo "   Generating checksum..."
    cd "$OUTPUT_DIR"
    shasum -a 256 "$archive_name" > "${archive_name}.sha256"
    cd "$PROJECT_ROOT"
    
    echo "✅ Created: ${archive_path}.sha256"
    
    # Track generated files
    GENERATED_FILES+=("$archive_path")
    GENERATED_FILES+=("${archive_path}.sha256")
    
    echo ""
}

# Main execution
echo "🧹 Cleaning previous builds..."
rm -rf "$STAGING_DIR" "$OUTPUT_DIR"
mkdir -p "$STAGING_DIR" "$OUTPUT_DIR"
echo ""

if [[ "$COMPONENT" == "all" ]]; then
    # Build both components
    for comp in manager worker; do
        build_component "$comp" || {
            echo "❌ Build failed for $comp"
            exit 1
        }
        
        create_archive "$comp" || {
            echo "❌ Archive creation failed for $comp"
            exit 1
        }
        
        # Clean up temporary archive directory
        rm -rf "$STAGING_DIR/archive-$comp"
    done
else
    # Build single component
    build_component "$COMPONENT" || {
        echo "❌ Build failed for $COMPONENT"
        exit 1
    }
    
    create_archive "$COMPONENT" || {
        echo "❌ Archive creation failed for $COMPONENT"
        exit 1
    }
    
    # Clean up temporary archive directory
    rm -rf "$STAGING_DIR/archive-$COMPONENT"
fi

# Display summary
echo ""
echo "=================================================="
echo "✅ macOS Distribution Build Complete!"
echo "=================================================="
echo "Version:  $VERSION"
echo "Platform: $PLATFORM-$ARCH"
echo ""
echo "📦 Generated files:"
for file in "${GENERATED_FILES[@]}"; do
    if [ -f "$file" ]; then
        size=$(du -h "$file" | cut -f1)
        echo "   $size  $(basename "$file")"
    fi
done
echo ""
echo "Output directory: $OUTPUT_DIR"
echo "=================================================="
```

---

## Part 3: GitHub Actions Workflow Updates

**File**: `.github/workflows/release.yml`

### Change 1: Add macOS to workflow_dispatch Options

**Location**: Lines 14-18  
**Add macOS options**:

```yaml
os:
  description: 'OS to build (leave empty for all)'
  required: false
  type: choice
  options:
    - ''
    - 'windows-latest'
    - 'ubuntu-latest'
    - 'macos-13'      # Intel x86_64
    - 'macos-14'      # Apple Silicon arm64
    - 'macos-latest'  # Current latest (arm64)
```

### Change 2: Update OS Matrix

**Location**: Line 41  
**Current**:
```yaml
os: ${{ (github.event_name == 'workflow_dispatch' && github.event.inputs.os != '') && fromJSON(format('["{0}"]', github.event.inputs.os)) || fromJSON('["windows-latest", "ubuntu-latest"]') }}
```

**New**:
```yaml
os: ${{ (github.event_name == 'workflow_dispatch' && github.event.inputs.os != '') && fromJSON(format('["{0}"]', github.event.inputs.os)) || fromJSON('["windows-latest", "ubuntu-latest", "macos-13", "macos-14"]') }}
```

### Change 3: Add macOS Version Update Step

**Location**: After line 77 (after Linux version update)  
**Add**:

```yaml
- name: Update meson.build version (macOS)
  if: runner.os == 'macOS'
  shell: bash
  run: |
    version="${{ steps.version.outputs.version }}"
    sed -i '' "s/version:[[:space:]]*'[^']*'/version: '$version'/" meson.build
```

### Change 4: Add macOS Dependencies

**Location**: After line 95 (after Linux dependencies)  
**Add**:

```yaml
- name: Install macOS build dependencies
  if: runner.os == 'macOS'
  run: |
    brew update
    brew install cmake ninja pkg-config
```

### Change 5: Add macOS Build Step

**Location**: After line 153 (after Linux build step)  
**Add**:

```yaml
- name: Build distribution (macOS)
  if: runner.os == 'macOS'
  shell: bash
  run: |
    chmod +x ./extras/scripts/build_distribution_macos.sh
    ./extras/scripts/build_distribution_macos.sh ${{ matrix.component }}
```

### Change 6: Add macOS Diagnostics (Optional)

**Location**: After macOS build step  
**Add**:

```yaml
- name: Diagnose FTXUI build (macOS worker)
  if: runner.os == 'macOS' && matrix.component == 'worker'
  shell: bash
  run: |
    echo "=== FTXUI Diagnostic Information (macOS) ==="
    echo ""
    echo "Checking builddir-worker-dist for ftxui..."
    if [ -d "builddir-worker-dist/subprojects" ]; then
      ls -la builddir-worker-dist/subprojects/
    fi
    echo ""
    echo "Checking for ftxui in meson-info..."
    if [ -f "builddir-worker-dist/meson-info/intro-dependencies.json" ]; then
      cat builddir-worker-dist/meson-info/intro-dependencies.json | grep -i ftxui || echo "No ftxui dependencies found"
    fi
    echo ""
    echo "Checking staged files for FTXUI libraries..."
    if [ -d "dist-staging/worker" ]; then
      find dist-staging/worker -name "*.dylib" -o -name "*.a"
    fi
```

### Change 7: Add macOS Log Upload

**Location**: After macOS diagnostics  
**Add**:

```yaml
- name: Upload build logs on failure (macOS)
  if: failure() && runner.os == 'macOS'
  uses: actions/upload-artifact@v4
  with:
    name: build-logs-${{ matrix.component }}-macos-${{ runner.arch }}
    path: |
      builddir-${{ matrix.component }}-dist/meson-logs/
      builddir-*/meson-logs/
      builddir-*/subprojects/ftxui*/meson-log.txt
      builddir-*/subprojects/ftxui*/CMakeFiles/CMakeOutput.log
      builddir-*/subprojects/ftxui*/CMakeFiles/CMakeError.log
      subprojects/libzt-wrapper/libzt/build.log
    if-no-files-found: ignore
```

### Change 8: Add macOS Artifact Upload

**Location**: After Linux artifact upload (after line 201)  
**Add**:

```yaml
- name: Upload artifacts (macOS)
  if: runner.os == 'macOS'
  uses: actions/upload-artifact@v4
  with:
    name: ${{ matrix.component }}-${{ runner.os }}-${{ runner.arch }}
    path: |
      dist/*.tar.gz
      dist/*.sha256
    if-no-files-found: error
```

---

## Implementation Checklist

### Phase 1: Meson Updates
- [ ] Update `meson.build` (add darwin libzt_dylib_file)
- [ ] Update `manager/meson.build` (add darwin copy target)
- [ ] Update `worker/meson.build` (add darwin copy target)
- [ ] Update `subprojects/libzt-wrapper/meson.build` (RPATH + installation)

### Phase 2: Build Script
- [ ] Create `extras/scripts/build_distribution_macos.sh`
- [ ] Set executable: `chmod +x extras/scripts/build_distribution_macos.sh`
- [ ] Test locally if Mac available

### Phase 3: Workflow Integration
- [ ] Update `.github/workflows/release.yml`:
  - [ ] Add macOS to workflow_dispatch options
  - [ ] Update OS matrix
  - [ ] Add macOS version update step
  - [ ] Add macOS dependencies
  - [ ] Add macOS build step
  - [ ] Add macOS diagnostics (optional)
  - [ ] Add macOS log upload
  - [ ] Add macOS artifact upload

### Phase 4: Testing
- [ ] Create feature branch
- [ ] Commit all changes
- [ ] Test via workflow_dispatch (single component, single OS)
- [ ] Validate artifact contents
- [ ] Test both architectures (macos-13 and macos-14)

---

## Expected Results

### Build Output

Running `./extras/scripts/build_distribution_macos.sh manager` should produce:

```
dist/
├── tm-manager-v{version}-macos-{arch}.tar.gz
└── tm-manager-v{version}-macos-{arch}.tar.gz.sha256
```

### Archive Contents

```
tm-manager/
├── bin/
│   └── tm-manager
├── lib/
│   └── libzt.dylib
├── config/
│   ├── config-manager.json
│   └── vn-manager-identity/
├── doc/
│   └── (documentation files)
├── scripts/
│   ├── install_macos.sh (placeholder)
│   └── uninstall_macos.sh (placeholder)
├── launchers/
│   └── start-tm-manager.sh (if exists)
├── INSTALL.txt
├── LICENSE
└── VERSION
```

### GitHub Release Assets (After Tag Push)

- Windows: `*.exe` installers
- Linux: `*.run` installers
- **macOS (new)**:
  - `tm-manager-v{version}-macos-x86_64.tar.gz` (Intel)
  - `tm-manager-v{version}-macos-x86_64.tar.gz.sha256`
  - `tm-manager-v{version}-macos-arm64.tar.gz` (Apple Silicon)
  - `tm-manager-v{version}-macos-arm64.tar.gz.sha256`
  - Same for worker

---

## Notes

1. **Installation scripts** are placeholders - full implementation comes later
2. **Universal binaries** can be added in Phase 2 (combine Intel + ARM with `lipo`)
3. **Code signing** not included in initial implementation
4. **RPATH** should work with `@executable_path/../lib` pattern
5. **Testing** requires GitHub Actions - no local Mac needed
