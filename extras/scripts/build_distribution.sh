#!/bin/bash
# build_distribution.sh - Create distributable packages for task-messenger
# Usage: ./build_distribution.sh [manager|worker|all]

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

# Get version from meson.build
VERSION=$(grep "project('task-messenger'" meson.build | grep -oP "version:\s*'\K[^']+")
if [[ -z "$VERSION" ]]; then
    echo "Error: Could not extract version from meson.build"
    exit 1
fi

# Detect architecture
ARCH=$(uname -m)
PLATFORM="linux"

# Build configuration
PREFIX="/opt/task-messenger"
BUILDTYPE="release"
STAGING_DIR="$PROJECT_ROOT/dist-staging"
OUTPUT_DIR="$PROJECT_ROOT/dist"

echo "=================================================="
echo "Task Messenger Distribution Builder (Linux)"
echo "=================================================="
echo "Component: $COMPONENT"
echo "Version: $VERSION"
echo "Platform: $PLATFORM-$ARCH"
echo "Prefix: $PREFIX"
echo "=================================================="

# Function to build and install a component
build_component() {
    local comp=$1
    local builddir="builddir-${comp}-dist"
    
    echo ""
    echo "Building $comp..."
    
    # Clean previous build
    rm -rf "$builddir"
    
    # Determine build options based on component
    local build_opts=()
    if [[ "$comp" == "manager" ]]; then
        build_opts+=("-Dbuild_worker=false")
        echo "Building manager only (FTXUI disabled for faster build)"
    elif [[ "$comp" == "worker" ]]; then
        build_opts+=("-Dbuild_manager=false")
        echo "Building worker only"
    fi
    
    # Setup meson
    meson setup "$builddir" \
        --prefix="$PREFIX" \
        --buildtype="$BUILDTYPE" \
        -Ddebug_logging=false \
        -Dprofiling_unwind=false \
        "${build_opts[@]}"
    
    # Compile
    meson compile -C "$builddir"
    
    # Install to staging directory
    DESTDIR="$STAGING_DIR/$comp" meson install -C "$builddir"
}

# Function to create archive for a component
create_archive() {
    local comp=$1
    local archive_name="task-messenger-${comp}-v${VERSION}-${PLATFORM}-${ARCH}.tar.gz"
    local archive_path="$OUTPUT_DIR/$archive_name"
    
    echo ""
    echo "Creating archive: $archive_name"
    
    # Create output directory
    mkdir -p "$OUTPUT_DIR"
    
    # Create temporary archive directory with component-specific naming
    local temp_archive_dir="$STAGING_DIR/archive-$comp"
    rm -rf "$temp_archive_dir"
    mkdir -p "$temp_archive_dir/task-message-$comp"
    
    # Navigate to staging
    local staging_prefix="$STAGING_DIR/$comp$PREFIX"
    
    # Copy files for this component
    local archive_root="$temp_archive_dir/task-message-$comp"
    
    if [[ "$comp" == "manager" ]]; then
        # Manager: executable, libzt, configs, identity directory, docs
        mkdir -p "$archive_root/bin"
        cp "$staging_prefix/bin/manager" "$archive_root/bin/"
        
        mkdir -p "$archive_root/lib"
        cp "$staging_prefix/lib/libzt.so" "$archive_root/lib/libzt.so"
        
        mkdir -p "$archive_root/config"
        cp "$staging_prefix/etc/task-messenger/config-manager.json" "$archive_root/config/"
        cp -r "$staging_prefix/etc/task-messenger/vn-manager-identity" "$archive_root/config/"
        
        mkdir -p "$archive_root/doc"
        cp -r "$staging_prefix/share/doc/task-messenger/"* "$archive_root/doc/"
        
        # Copy LICENSE to root for visibility
        cp "$PROJECT_ROOT/LICENSE" "$archive_root/"
    else
        # Worker: executable, libzt, configs, docs
        mkdir -p "$archive_root/bin"
        cp "$staging_prefix/bin/worker" "$archive_root/bin/"
        
        mkdir -p "$archive_root/lib"
        cp "$staging_prefix/lib/libzt.so" "$archive_root/lib/libzt.so"
        
        mkdir -p "$archive_root/config"
        cp "$staging_prefix/etc/task-messenger/config-worker.json" "$archive_root/config/"
        
        mkdir -p "$archive_root/doc"
        cp -r "$staging_prefix/share/doc/task-messenger/"* "$archive_root/doc/"
        
        # Copy LICENSE to root for visibility
        cp "$PROJECT_ROOT/LICENSE" "$archive_root/"
    fi
    
    # Copy installation scripts and related files
    mkdir -p "$archive_root/scripts"
    cp "$PROJECT_ROOT/extras/scripts/install_linux.sh" "$archive_root/scripts/"
    cp "$PROJECT_ROOT/extras/scripts/uninstall_linux.sh" "$archive_root/scripts/"
    chmod +x "$archive_root/scripts/"*.sh
    
    # Copy launchers
    mkdir -p "$archive_root/launchers"
    if [[ "$comp" == "manager" ]]; then
        cp "$PROJECT_ROOT/extras/launchers/start-manager.sh" "$archive_root/launchers/"
    else
        cp "$PROJECT_ROOT/extras/launchers/start-worker.sh" "$archive_root/launchers/"
    fi
    chmod +x "$archive_root/launchers/"*.sh
    
    # Copy desktop files
    mkdir -p "$archive_root/desktop"
    if [[ "$comp" == "manager" ]]; then
        cp "$PROJECT_ROOT/extras/desktop/task-messenger-manager.desktop" "$archive_root/desktop/"
    else
        cp "$PROJECT_ROOT/extras/desktop/task-messenger-worker.desktop" "$archive_root/desktop/"
    fi
    
    # Create installation instructions
    cat > "$archive_root/INSTALL.txt" << EOF
TaskMessenger $comp Installation Instructions

To install, run:
    cd task-message-$comp
    ./scripts/install_linux.sh

The component ($comp) is automatically detected.

For custom installation directory:
    ./scripts/install_linux.sh --install-dir /custom/path

For help:
    ./scripts/install_linux.sh --help

EOF
    
    # Create tar.gz from temporary directory
    cd "$temp_archive_dir"
    tar -czf "$archive_path" "task-message-$comp/"
    cd "$PROJECT_ROOT"
    
    # Clean up temporary directory
    rm -rf "$temp_archive_dir"
    
    # Generate SHA256 checksum
    echo "Generating checksum..."
    (cd "$OUTPUT_DIR" && sha256sum "$archive_name" > "${archive_name}.sha256")
    
    echo "Created: $archive_path"
    echo "Checksum: $OUTPUT_DIR/${archive_name}.sha256"
}

# Clean staging and output directories
rm -rf "$STAGING_DIR" "$OUTPUT_DIR"
mkdir -p "$STAGING_DIR" "$OUTPUT_DIR"

# Build and package based on component selection
if [[ "$COMPONENT" == "all" ]]; then
    for comp in manager worker; do
        build_component "$comp"
        create_archive "$comp"
    done
else
    build_component "$COMPONENT"
    create_archive "$COMPONENT"
fi

# Summary
echo ""
echo "=================================================="
echo "Distribution build complete!"
echo "=================================================="
echo "Output directory: $OUTPUT_DIR"
ls -lh "$OUTPUT_DIR"
echo "=================================================="
