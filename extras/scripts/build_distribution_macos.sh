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
VERSION=$(grep "project('task-messenger'" meson.build | sed -n "s/.*version:[[:space:]]*'\([^']*\)'.*/\1/p")
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
    
    # Copy installation scripts
    echo "   Copying installation scripts..."
    mkdir -p "$archive_root/scripts"
    
    # Copy install script
    if [ -f "$PROJECT_ROOT/extras/scripts/install_macos.sh" ]; then
        cp "$PROJECT_ROOT/extras/scripts/install_macos.sh" "$archive_root/scripts/"
        chmod +x "$archive_root/scripts/install_macos.sh"
    else
        echo "⚠️  Warning: install_macos.sh not found"
    fi
    
    # Copy uninstall script
    if [ -f "$PROJECT_ROOT/extras/scripts/uninstall_macos.sh" ]; then
        cp "$PROJECT_ROOT/extras/scripts/uninstall_macos.sh" "$archive_root/scripts/"
        chmod +x "$archive_root/scripts/uninstall_macos.sh"
    else
        echo "⚠️  Warning: uninstall_macos.sh not found"
    fi
    
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
