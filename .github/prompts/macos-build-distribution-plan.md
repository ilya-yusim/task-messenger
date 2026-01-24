# Plan for macOS Build and Distribution Support

Based on the workspace structure, here's a comprehensive plan to add macOS build and distribution support using GitHub Actions macOS runners:

## Phase 1: Build System Compatibility

### 1.1 Verify libzt-wrapper macOS Support
The `libzt-wrapper` already has macOS build logic:
- Line 159-162 shows macOS build detection
- Uses `build.sh host <buildtype>` script (same as Linux)
- Produces `libzt.dylib` instead of `libzt.so`

**Action Items:**
- Test libzt build on macOS runner
- Verify RPATH configuration for `@loader_path/../lib` (macOS equivalent of `$ORIGIN`)
- Ensure `.dylib` is correctly linked

### 1.2 Update Meson Build Files
Current `meson.build` has conditional logic for Windows/Linux. Need to add macOS cases:

```meson
# In root meson.build and manager/worker meson.build
elif host_machine.system() == 'darwin'
  # macOS: Copy libzt.dylib
  custom_target('copy-zt-dylib-manager',
    output: 'libzt.dylib',
    command: ['cp', libzt_dylib_file, '@OUTPUT@'],
    build_by_default: true,
    install: false,
  )
```

### 1.3 ProcessUtils macOS Implementation
`processUtils.cpp` already has macOS implementations (lines 101-138):
- ✅ `get_process_usage()` - uses mach APIs
- ✅ `get_executable_path_impl()` - uses `_NSGetExecutablePath`

**No changes needed** - already compatible.

---

## Phase 2: Distribution Script

### 2.1 Create macOS Build Script
Create `extras/scripts/build_distribution_macos.sh` based on `build_distribution.sh`:

```bash
#!/bin/bash
# build_distribution_macos.sh - Create distributable packages for macOS

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_ROOT"

# Parse arguments
COMPONENT="${1:-all}"
if [[ ! "$COMPONENT" =~ ^(manager|worker|all)$ ]]; then
    echo "Usage: $0 [manager|worker|all]"
    exit 1
fi

# Get version
VERSION=$(grep "project('task-messenger'" meson.build | grep -oP "version:\s*'\K[^']+")
ARCH=$(uname -m)  # arm64 or x86_64
PLATFORM="macos"

# macOS-specific paths
PREFIX="/usr/local"
STAGING_DIR="$PROJECT_ROOT/dist-staging"
OUTPUT_DIR="$PROJECT_ROOT/dist"

# Build component function (similar to Linux)
build_component() {
    local comp=$1
    local builddir="builddir-${comp}-dist"
    
    echo "Building $comp for macOS..."
    rm -rf "$builddir"
    
    # Build options
    local build_opts=()
    if [[ "$comp" == "manager" ]]; then
        build_opts+=("-Dbuild_worker=false")
    elif [[ "$comp" == "worker" ]]; then
        build_opts+=("-Dbuild_manager=false")
    fi
    
    # Setup meson
    meson setup "$builddir" \
        --prefix="$PREFIX" \
        --buildtype="release" \
        -Ddebug_logging=false \
        -Dprofiling_unwind=false \
        "${build_opts[@]}"
    
    # Compile
    meson compile -C "$builddir"
    
    # Install to staging
    DESTDIR="$STAGING_DIR/$comp" meson install -C "$builddir"
}

# Create archive function
create_archive() {
    local comp=$1
    local archive_name="tm-${comp}-v${VERSION}-${PLATFORM}-${ARCH}.tar.gz"
    local archive_path="$OUTPUT_DIR/$archive_name"
    
    echo "Creating archive: $archive_name"
    
    mkdir -p "$OUTPUT_DIR"
    local temp_archive_dir="$STAGING_DIR/archive-$comp"
    rm -rf "$temp_archive_dir"
    mkdir -p "$temp_archive_dir/tm-$comp"
    
    local staging_prefix="$STAGING_DIR/$comp$PREFIX"
    local archive_root="$temp_archive_dir/tm-$comp"
    
    # Copy binaries and libraries
    mkdir -p "$archive_root/bin"
    cp "$staging_prefix/bin/tm-${comp}" "$archive_root/bin/"
    
    mkdir -p "$archive_root/lib"
    cp "$staging_prefix/lib/libzt.dylib" "$archive_root/lib/"
    
    # Copy configs
    mkdir -p "$archive_root/config"
    if [[ "$comp" == "manager" ]]; then
        cp "$staging_prefix/etc/task-messenger/config-manager.json" "$archive_root/config/"
        cp -r "$staging_prefix/etc/task-messenger/vn-manager-identity" "$archive_root/config/"
    else
        cp "$staging_prefix/etc/task-messenger/config-worker.json" "$archive_root/config/"
    fi
    
    # Copy docs and LICENSE
    mkdir -p "$archive_root/doc"
    cp -r "$staging_prefix/share/doc/task-messenger/"* "$archive_root/doc/"
    cp "$PROJECT_ROOT/LICENSE" "$archive_root/"
    
    # Copy installation scripts
    mkdir -p "$archive_root/scripts"
    cp "$PROJECT_ROOT/extras/scripts/install_macos.sh" "$archive_root/scripts/"
    cp "$PROJECT_ROOT/extras/scripts/uninstall_macos.sh" "$archive_root/scripts/"
    chmod +x "$archive_root/scripts/"*.sh
    
    # Create VERSION file
    echo "$VERSION" > "$archive_root/VERSION"
    
    # Create INSTALL.txt
    cat > "$archive_root/INSTALL.txt" << EOF
TaskMessenger $comp Installation Instructions (macOS)

To install, run:
    cd tm-$comp
    ./scripts/install_macos.sh

For custom installation directory:
    ./scripts/install_macos.sh --install-dir /custom/path

For help:
    ./scripts/install_macos.sh --help
EOF
    
    # Create tar.gz
    cd "$temp_archive_dir"
    tar -czf "$archive_path" "tm-$comp/"
    cd "$PROJECT_ROOT"
    
    echo "Created: $archive_path"
    
    # Generate checksum
    cd "$OUTPUT_DIR"
    shasum -a 256 "$archive_name" > "${archive_name}.sha256"
    cd "$PROJECT_ROOT"
}

# Main execution
rm -rf "$STAGING_DIR" "$OUTPUT_DIR"
mkdir -p "$STAGING_DIR" "$OUTPUT_DIR"

if [[ "$COMPONENT" == "all" ]]; then
    for comp in manager worker; do
        build_component "$comp"
        create_archive "$comp"
        rm -rf "$STAGING_DIR/archive-$comp"
    done
else
    build_component "$COMPONENT"
    create_archive "$COMPONENT"
    rm -rf "$STAGING_DIR/archive-$COMPONENT"
fi

echo ""
echo "=================================================="
echo "macOS Distribution build complete!"
echo "=================================================="
echo "Output directory: $OUTPUT_DIR"
ls -lh "$OUTPUT_DIR"
echo "=================================================="
```

### 2.2 Create macOS Installation Script
Create `extras/scripts/install_macos.sh` based on `install_linux.sh`:

```bash
#!/bin/bash
# install_macos.sh - Install TaskMessenger on macOS

set -e

# Color output functions (same as Linux script)
print_success() { echo -e "\033[0;32m$1\033[0m"; }
print_error() { echo -e "\033[0;31m$1\033[0m" >&2; }
print_warning() { echo -e "\033[0;33m$1\033[0m"; }
print_info() { echo -e "\033[0;34m$1\033[0m"; }

# macOS-specific paths
INSTALL_BASE="$HOME/Library/Application Support/TaskMessenger"
CONFIG_BASE="$HOME/Library/Application Support/TaskMessenger/config"
BIN_SYMLINK_DIR="$HOME/.local/bin"

detect_extracted_files() {
    local script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    local extracted_root="$(dirname "$script_dir")"
    
    # Check for dylib to confirm extracted archive
    local lib_path="$extracted_root/lib/libzt.dylib"
    
    if [ -f "$lib_path" ]; then
        local manager_path="$extracted_root/bin/tm-manager"
        local worker_path="$extracted_root/bin/tm-worker"
        
        if [ -f "$manager_path" ]; then
            echo "manager:$extracted_root"
            return 0
        elif [ -f "$worker_path" ]; then
            echo "worker:$extracted_root"
            return 0
        fi
    fi
    
    return 1
}

install_component() {
    local component=$1
    local extracted_dir=$2
    local install_dir=$3
    local version=$4
    
    print_info "Installing $component..."
    
    # Create installation directory
    mkdir -p "$install_dir/bin"
    mkdir -p "$install_dir/lib"
    
    # Copy binary
    cp "$extracted_dir/bin/tm-${component}" "$install_dir/bin/"
    chmod +x "$install_dir/bin/tm-${component}"
    
    # Copy dylib
    cp "$extracted_dir/lib/libzt.dylib" "$install_dir/lib/"
    
    # Fix dylib install name and rpath
    install_name_tool -id "@rpath/libzt.dylib" "$install_dir/lib/libzt.dylib"
    install_name_tool -add_rpath "@executable_path/../lib" "$install_dir/bin/tm-${component}"
    
    # Copy documentation
    if [ -d "$extracted_dir/doc" ]; then
        mkdir -p "$install_dir/doc"
        cp -r "$extracted_dir/doc/"* "$install_dir/doc/"
    fi
    
    print_success "Installed $component to: $install_dir"
}

setup_configs() {
    local component=$1
    local extracted_dir=$2
    local config_dir="$CONFIG_BASE/$component"
    
    mkdir -p "$config_dir"
    
    # Copy config template if doesn't exist
    if [ ! -f "$config_dir/config-${component}.json" ]; then
        if [ -f "$extracted_dir/config/config-${component}.json" ]; then
            cp "$extracted_dir/config/config-${component}.json" "$config_dir/"
            print_info "Created config: $config_dir/config-${component}.json"
        fi
    else
        print_info "Config already exists: $config_dir/config-${component}.json"
    fi
    
    # Copy identity for manager
    if [ "$component" = "manager" ] && [ -d "$extracted_dir/config/vn-manager-identity" ]; then
        if [ ! -d "$config_dir/vn-manager-identity" ]; then
            cp -r "$extracted_dir/config/vn-manager-identity" "$config_dir/"
            print_info "Created identity directory: $config_dir/vn-manager-identity/"
        fi
    fi
}

create_symlink() {
    local component=$1
    local install_dir=$2
    
    mkdir -p "$BIN_SYMLINK_DIR"
    
    local symlink="$BIN_SYMLINK_DIR/tm-$component"
    if [ -L "$symlink" ]; then
        rm "$symlink"
    fi
    
    ln -s "$install_dir/bin/tm-$component" "$symlink"
    print_success "Created symlink: $symlink"
}

check_path() {
    if [[ ":$PATH:" == *":$BIN_SYMLINK_DIR:"* ]]; then
        print_success "$BIN_SYMLINK_DIR is in PATH"
    else
        print_warning "$BIN_SYMLINK_DIR is not in PATH"
        print_info "Add to ~/.zshrc or ~/.bash_profile:"
        echo '    export PATH="$HOME/.local/bin:$PATH"'
    fi
}

main() {
    CUSTOM_INSTALL_DIR=""
    
    # Parse options
    while [ $# -gt 0 ]; do
        case $1 in
            --install-dir)
                CUSTOM_INSTALL_DIR="$2"
                shift 2
                ;;
            --help)
                cat << EOF
TaskMessenger macOS Installation Script

Usage: $0 [OPTIONS]

Options:
  --install-dir PATH     Custom installation directory
  --help                 Show this help message

Default installation paths:
  Binaries: $INSTALL_BASE/tm-{component}/bin/
  Config:   $CONFIG_BASE/{component}/
  Symlink:  $BIN_SYMLINK_DIR/tm-{component}

EOF
                exit 0
                ;;
            *)
                print_error "Unknown option: $1"
                exit 1
                ;;
        esac
    done
    
    # Detect component from extracted files
    local detection_result=$(detect_extracted_files)
    if [ $? -ne 0 ]; then
        print_error "Could not detect TaskMessenger component"
        print_error "Please run from extracted distribution directory"
        exit 1
    fi
    
    COMPONENT=$(echo "$detection_result" | cut -d: -f1)
    extracted_dir=$(echo "$detection_result" | cut -d: -f2)
    
    print_info "Detected component: $COMPONENT"
    
    # Determine installation directory
    if [ -n "$CUSTOM_INSTALL_DIR" ]; then
        INSTALL_DIR="$CUSTOM_INSTALL_DIR"
    else
        INSTALL_DIR="$INSTALL_BASE/tm-$COMPONENT"
    fi
    
    # Read version
    VERSION="unknown"
    if [ -f "$extracted_dir/VERSION" ]; then
        VERSION=$(cat "$extracted_dir/VERSION")
    fi
    
    print_info "Version: $VERSION"
    print_info "Install directory: $INSTALL_DIR"
    
    # Install
    install_component "$COMPONENT" "$extracted_dir" "$INSTALL_DIR" "$VERSION"
    setup_configs "$COMPONENT" "$extracted_dir"
    create_symlink "$COMPONENT" "$INSTALL_DIR"
    check_path
    
    echo ""
    print_success "=========================================="
    print_success "Installation completed successfully!"
    print_success "=========================================="
    print_info "Run: tm-$COMPONENT"
    print_info "Config: $CONFIG_BASE/$COMPONENT/config-$COMPONENT.json"
    echo ""
}

main "$@"
```

### 2.3 Create Uninstall Script
Create `extras/scripts/uninstall_macos.sh` (similar to `uninstall_linux.sh`).

---

## Phase 3: GitHub Actions Workflow

### 3.1 Create macOS Build Workflow
Create `.github/workflows/build-macos.yml`:

```yaml
name: Build macOS Distribution

on:
  push:
    branches: [ main, develop ]
    tags: [ 'v*' ]
  pull_request:
    branches: [ main, develop ]
  workflow_dispatch:

jobs:
  build-macos:
    strategy:
      matrix:
        component: [manager, worker]
        # GitHub provides both Intel and Apple Silicon runners
        runner: 
          - macos-13      # Intel x86_64
          - macos-14      # Apple Silicon arm64
    
    runs-on: ${{ matrix.runner }}
    
    steps:
      - name: Checkout repository
        uses: actions/checkout@v4
        with:
          submodules: recursive
      
      - name: Setup Python
        uses: actions/setup-python@v5
        with:
          python-version: '3.11'
      
      - name: Install dependencies
        run: |
          brew update
          brew install meson ninja pkg-config boost
          pip3 install meson
      
      - name: Get version
        id: version
        run: |
          VERSION=$(grep "project('task-messenger'" meson.build | grep -oP "version:\s*'\K[^']+")
          echo "version=$VERSION" >> $GITHUB_OUTPUT
      
      - name: Build distribution
        run: |
          chmod +x extras/scripts/build_distribution_macos.sh
          ./extras/scripts/build_distribution_macos.sh ${{ matrix.component }}
      
      - name: Upload artifacts
        uses: actions/upload-artifact@v4
        with:
          name: tm-${{ matrix.component }}-macos-${{ runner.arch }}
          path: |
            dist/*.tar.gz
            dist/*.sha256
          retention-days: 30
      
      - name: Create Release Assets (on tag)
        if: startsWith(github.ref, 'refs/tags/v')
        uses: softprops/action-gh-release@v1
        with:
          files: |
            dist/*.tar.gz
            dist/*.sha256
          draft: false
          prerelease: false
        env:
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
  
  build-universal:
    needs: build-macos
    runs-on: macos-14
    if: startsWith(github.ref, 'refs/tags/v')
    
    strategy:
      matrix:
        component: [manager, worker]
    
    steps:
      - name: Download Intel artifact
        uses: actions/download-artifact@v4
        with:
          name: tm-${{ matrix.component }}-macos-X64
          path: intel
      
      - name: Download ARM artifact
        uses: actions/download-artifact@v4
        with:
          name: tm-${{ matrix.component }}-macos-ARM64
          path: arm
      
      - name: Create Universal Binary
        run: |
          # Extract both archives
          cd intel && tar -xzf *.tar.gz && cd ..
          cd arm && tar -xzf *.tar.gz && cd ..
          
          # Create universal binary using lipo
          mkdir -p universal/tm-${{ matrix.component }}/bin
          lipo -create \
            intel/tm-${{ matrix.component }}/bin/tm-${{ matrix.component }} \
            arm/tm-${{ matrix.component }}/bin/tm-${{ matrix.component }} \
            -output universal/tm-${{ matrix.component }}/bin/tm-${{ matrix.component }}
          
          # Copy other files from arm64 version (configs, docs, etc)
          cp -r arm/tm-${{ matrix.component }}/config universal/tm-${{ matrix.component }}/
          cp -r arm/tm-${{ matrix.component }}/lib universal/tm-${{ matrix.component }}/
          cp -r arm/tm-${{ matrix.component }}/scripts universal/tm-${{ matrix.component }}/
          cp -r arm/tm-${{ matrix.component }}/doc universal/tm-${{ matrix.component }}/
          cp arm/tm-${{ matrix.component }}/LICENSE universal/tm-${{ matrix.component }}/
          
          # Create universal archive
          VERSION=$(cat arm/tm-${{ matrix.component }}/VERSION)
          cd universal
          tar -czf ../tm-${{ matrix.component }}-v${VERSION}-macos-universal.tar.gz tm-${{ matrix.component }}/
          cd ..
          
          # Generate checksum
          shasum -a 256 tm-${{ matrix.component }}-v${VERSION}-macos-universal.tar.gz > \
            tm-${{ matrix.component }}-v${VERSION}-macos-universal.tar.gz.sha256
      
      - name: Upload universal binary to release
        uses: softprops/action-gh-release@v1
        with:
          files: |
            tm-${{ matrix.component }}-*.tar.gz
            tm-${{ matrix.component }}-*.sha256
        env:
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
```

### 3.2 Update Existing Workflows
Update `.github/workflows/release.yml` to include macOS builds:

```yaml
# Add to matrix.os
matrix:
  os: ${{ (github.event_name == 'workflow_dispatch' && github.event.inputs.os != '') && fromJSON(format('["{0}"]', github.event.inputs.os)) || fromJSON('["windows-latest", "ubuntu-latest", "macos-13", "macos-14"]') }}
```

---

## Phase 4: Documentation Updates

### 4.1 Update README.md
Add macOS section to `README.md` after Linux installation:

````markdown
### macOS Distributions

```bash
# Build manager distribution
./extras/scripts/build_distribution_macos.sh manager

# Build worker distribution  
./extras/scripts/build_distribution_macos.sh worker
```

**Output Files** (in `dist/` directory):
- `tm-{component}-v{version}-macos-{arm64|x86_64}.tar.gz` - Architecture-specific
- `tm-{component}-v{version}-macos-universal.tar.gz` - Universal binary (both architectures)
- `.sha256` checksum files

**Installation:**
```bash
tar -xzf tm-manager-v1.0.0-macos-universal.tar.gz
cd tm-manager
./scripts/install_macos.sh
```
````

### 4.2 Create INSTALLATION-MACOS.md
Create `docs/INSTALLATION-MACOS.md`:

````markdown
# TaskMessenger Installation Guide (macOS)

## Prerequisites
- macOS 11.0 (Big Sur) or later
- Apple Silicon (arm64) or Intel (x86_64)

## Installation

### Option A: Universal Binary (Recommended)
Works on both Apple Silicon and Intel Macs:

```bash
tar -xzf tm-manager-v1.0.0-macos-universal.tar.gz
cd tm-manager
./scripts/install_macos.sh
```

### Option B: Architecture-Specific
Download the binary matching your architecture:
- Apple Silicon (M1/M2/M3): `macos-arm64`
- Intel: `macos-x86_64`

```bash
tar -xzf tm-manager-v1.0.0-macos-arm64.tar.gz
cd tm-manager
./scripts/install_macos.sh
```

## Installation Paths

**Default locations:**
- Binaries: `~/Library/Application Support/TaskMessenger/tm-{component}/bin/`
- Config: `~/Library/Application Support/TaskMessenger/config/{component}/`
- Symlinks: `~/.local/bin/tm-{component}`
- Logs: `~/Library/Logs/TaskMessenger/`

**Custom installation:**
```bash
./scripts/install_macos.sh --install-dir ~/custom/path
```

## Configuration

### Manager Configuration
```bash
~/Library/Application Support/TaskMessenger/config/manager/config-manager.json
```

### Worker Configuration
```bash
~/Library/Application Support/TaskMessenger/config/worker/config-worker.json
```

## Troubleshooting

### "cannot be opened because the developer cannot be verified"
This occurs with downloaded binaries. To allow execution:

```bash
# For manager
xattr -d com.apple.quarantine ~/Library/Application\ Support/TaskMessenger/tm-manager/bin/tm-manager

# For worker
xattr -d com.apple.quarantine ~/Library/Application\ Support/TaskMessenger/tm-worker/bin/tm-worker
```

Or use System Preferences:
1. Open **System Preferences** → **Security & Privacy**
2. Click **Open Anyway** next to the blocked application

### Shared library not found
```bash
# Verify dylib is present
ls ~/Library/Application\ Support/TaskMessenger/tm-manager/lib/libzt.dylib

# Check rpath
otool -l ~/Library/Application\ Support/TaskMessenger/tm-manager/bin/tm-manager | grep RPATH
# Should show: @executable_path/../lib

# Verify dylib install name
otool -L ~/Library/Application\ Support/TaskMessenger/tm-manager/lib/libzt.dylib
```

### PATH not updated
Add to `~/.zshrc` (default shell on macOS):
```bash
export PATH="$HOME/.local/bin:$PATH"
```

Then reload:
```bash
source ~/.zshrc
```

## Uninstallation

```bash
~/Library/Application\ Support/TaskMessenger/tm-manager/scripts/uninstall_macos.sh --remove-config
```

## Building from Source

```bash
# Install dependencies
brew install meson ninja pkg-config boost python3

# Build
meson setup builddir --buildtype=release
meson compile -C builddir
```
````

---

## Phase 5: Testing Plan

### 5.1 Local Testing (Before CI)
If you gain access to a Mac:
1. Run `build_distribution_macos.sh` manually
2. Test installation on clean macOS VM
3. Verify binary signatures and code signing requirements

### 5.2 CI Testing Strategy
1. **Initial PR**: Add workflow without release publishing
2. **Validation**: Run on `workflow_dispatch` to test artifact generation
3. **Verification**: Download artifacts and test manually
4. **Release**: Enable release publishing once validated

### 5.3 Integration Tests
Add to existing test suite:
```bash
# In .github/workflows/build-macos.yml
- name: Smoke test
  run: |
    cd dist
    tar -xzf *.tar.gz
    cd tm-*/
    ./bin/tm-${{ matrix.component }} --version
    ./bin/tm-${{ matrix.component }} --help
```

---

## Phase 6: Release Process

### 6.1 Version Tagging
When ready to release:
```bash
git tag -a v1.0.0 -m "Release v1.0.0 with macOS support"
git push origin v1.0.0
```

This triggers:
1. Linux builds (`build_distribution.sh`)
2. Windows builds (`build_distribution.ps1`)
3. **macOS builds** (new workflow)
4. Universal binary creation
5. GitHub Release with all artifacts

### 6.2 Release Assets
Final release will include:
- `tm-manager-v1.0.0-linux-x64.tar.gz` / `.run`
- `tm-worker-v1.0.0-linux-x64.tar.gz` / `.run`
- `tm-manager-v1.0.0-windows-x64.zip` / `.exe`
- `tm-worker-v1.0.0-windows-x64.zip` / `.exe`
- `tm-manager-v1.0.0-macos-arm64.tar.gz` *(new)*
- `tm-manager-v1.0.0-macos-x86_64.tar.gz` *(new)*
- `tm-manager-v1.0.0-macos-universal.tar.gz` *(new)*
- `tm-worker-v1.0.0-macos-*.tar.gz` *(new)*

---

## Summary of Files to Create

1. **Build Scripts**:
   - `extras/scripts/build_distribution_macos.sh`
   - `extras/scripts/install_macos.sh`
   - `extras/scripts/uninstall_macos.sh`

2. **CI Workflows**:
   - `.github/workflows/build-macos.yml`

3. **Documentation**:
   - `docs/INSTALLATION-MACOS.md`
   - Update `README.md`
   - Update `docs/INSTALLATION.md`

4. **Meson Updates**:
   - Update `meson.build` (libzt dylib copy)
   - Update `manager/meson.build` (dylib copy)
   - Update `worker/meson.build` (dylib copy)

5. **Testing**:
   - Add smoke tests to workflow
   - Verify RPATH and install names

---

## Implementation Order

1. **Week 1**: Create build scripts + Meson updates
2. **Week 2**: GitHub Actions workflow (without release)
3. **Week 3**: Test artifacts via workflow_dispatch
4. **Week 4**: Documentation + enable release publishing
5. **Week 5**: Create first tagged release with macOS support

---

## Key Considerations

### Architecture Support
- **macos-13**: Intel x86_64 (last Intel runner)
- **macos-14**: Apple Silicon arm64 (M1+)
- Universal binaries combine both using `lipo`

### Dynamic Library Handling
- Use `install_name_tool` to fix dylib paths
- RPATH must be `@executable_path/../lib`
- Install name should be `@rpath/libzt.dylib`

### Code Signing (Future)
While not required for initial release, consider:
- Ad-hoc signing with `codesign -s -`
- Notarization for distribution outside App Store
- Developer ID signing for trusted execution

### Homebrew Distribution (Future)
After validating GitHub releases, consider:
- Creating Homebrew tap: `homebrew-task-messenger`
- Formula for easy installation: `brew install task-messenger/tap/tm-manager`
- Automatic updates via Homebrew

---

## Benefits of This Approach

1. **No Mac Required**: GitHub Actions provides free macOS runners
2. **Native Performance**: Both architectures get optimized builds
3. **Universal Binary**: Single download works on all Macs
4. **Consistent Process**: Mirrors existing Linux/Windows workflows
5. **Automated Releases**: Tag-triggered builds and publishing
6. **Easy Installation**: Native macOS paths and conventions

This plan leverages the existing cross-platform architecture and GitHub's free macOS runners to provide comprehensive macOS distribution support without requiring local Mac hardware.
