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
    
    # Setup meson
    meson setup "$builddir" \
        --prefix="$PREFIX" \
        --buildtype="$BUILDTYPE" \
        -Ddebug_logging=false \
        -Dprofiling_unwind=false
    
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
    
    # Navigate to staging
    cd "$STAGING_DIR/$comp$PREFIX"
    
    # Create tar.gz with only the files for this component
    if [[ "$comp" == "manager" ]]; then
        # Manager: executable, identity files, libzt, configs, docs
        tar -czf "$archive_path" \
            bin/manager \
            bin/identity.public \
            bin/identity.secret \
            lib/libzt.so \
            etc/task-messenger/config-manager.json \
            share/doc/task-messenger/
    else
        # Worker: executable, libzt, configs, docs
        tar -czf "$archive_path" \
            bin/worker \
            lib/libzt.so \
            etc/task-messenger/config-worker.json \
            share/doc/task-messenger/
    fi
    
    cd "$PROJECT_ROOT"
    
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
