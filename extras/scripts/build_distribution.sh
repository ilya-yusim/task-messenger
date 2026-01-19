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

# Track generated files for summary
declare -a GENERATED_FILES=()
MAKESELF_BIN=""
MAKESELF_AVAILABLE=false

echo "=================================================="
echo "Task Messenger Distribution Builder (Linux)"
echo "=================================================="
echo "Component: $COMPONENT"
echo "Version: $VERSION"
echo "Platform: $PLATFORM-$ARCH"
echo "Prefix: $PREFIX"
echo "=================================================="

# Function to ensure makeself is available
ensure_makeself() {
    # Check if makeself is in PATH
    if command -v makeself &> /dev/null; then
        MAKESELF_BIN="makeself"
        MAKESELF_AVAILABLE=true
        echo "Found makeself in PATH: $(which makeself)"
        return 0
    fi
    
    # Check for local copy
    local local_makeself="$SCRIPT_DIR/makeself/makeself.sh"
    if [[ -f "$local_makeself" ]]; then
        MAKESELF_BIN="$local_makeself"
        MAKESELF_AVAILABLE=true
        echo "Found local makeself: $local_makeself"
        return 0
    fi
    
    # Download makeself
    echo "Makeself not found. Downloading version 2.5.0..."
    local makeself_dir="$SCRIPT_DIR/makeself"
    mkdir -p "$makeself_dir"
    
    local makeself_url="https://github.com/megastep/makeself/releases/download/release-2.5.0/makeself-2.5.0.run"
    local makeself_run="$makeself_dir/makeself-2.5.0.run"
    
    if command -v wget &> /dev/null; then
        wget -q -O "$makeself_run" "$makeself_url" || {
            echo "Warning: Failed to download makeself. Skipping .run generation."
            return 1
        }
    elif command -v curl &> /dev/null; then
        curl -sL -o "$makeself_run" "$makeself_url" || {
            echo "Warning: Failed to download makeself. Skipping .run generation."
            return 1
        }
    else
        echo "Warning: Neither wget nor curl available. Cannot download makeself. Skipping .run generation."
        return 1
    fi
    
    chmod +x "$makeself_run"
    
    # Extract makeself (it's self-extracting)
    echo "Extracting makeself..."
    cd "$makeself_dir"
    ./makeself-2.5.0.run --target "$makeself_dir" --quiet || {
        echo "Warning: Failed to extract makeself. Skipping .run generation."
        cd "$PROJECT_ROOT"
        return 1
    }
    cd "$PROJECT_ROOT"
    
    # Verify makeself.sh exists
    if [[ -f "$local_makeself" ]]; then
        MAKESELF_BIN="$local_makeself"
        MAKESELF_AVAILABLE=true
        echo "Successfully installed makeself to: $local_makeself"
        return 0
    else
        echo "Warning: Makeself extraction did not create expected file. Skipping .run generation."
        return 1
    fi
}

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
    local archive_name="tm-${comp}-v${VERSION}-${PLATFORM}-${ARCH}.tar.gz"
    local archive_path="$OUTPUT_DIR/$archive_name"
    
    echo ""
    echo "Creating archive: $archive_name"
    
    # Create output directory
    mkdir -p "$OUTPUT_DIR"
    
    # Create temporary archive directory with component-specific naming
    local temp_archive_dir="$STAGING_DIR/archive-$comp"
    rm -rf "$temp_archive_dir"
    mkdir -p "$temp_archive_dir/tm-$comp"
    
    # Navigate to staging
    local staging_prefix="$STAGING_DIR/$comp$PREFIX"
    
    # Copy files for this component
    local archive_root="$temp_archive_dir/tm-$comp"
    
    if [[ "$comp" == "manager" ]]; then
        # Manager: executable, libzt, configs, identity directory, docs
        mkdir -p "$archive_root/bin"
        cp "$staging_prefix/bin/tm-manager" "$archive_root/bin/"
        
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
        cp "$staging_prefix/bin/tm-worker" "$archive_root/bin/"
        
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
        cp "$PROJECT_ROOT/extras/launchers/start-tm-manager.sh" "$archive_root/launchers/"
    else
        cp "$PROJECT_ROOT/extras/launchers/start-tm-worker.sh" "$archive_root/launchers/"
    fi
    chmod +x "$archive_root/launchers/"*.sh
    
    # Copy desktop files
    mkdir -p "$archive_root/desktop"
    if [[ "$comp" == "manager" ]]; then
        cp "$PROJECT_ROOT/extras/desktop/tm-manager.desktop" "$archive_root/desktop/"
    else
        cp "$PROJECT_ROOT/extras/desktop/tm-worker.desktop" "$archive_root/desktop/"
    fi
    
    # Create installation instructions
    cat > "$archive_root/INSTALL.txt" << EOF
TaskMessenger $comp Installation Instructions

To install, run:
    cd tm-$comp
    ./scripts/install_linux.sh

The component ($comp) is automatically detected.

For custom installation directory:
    ./scripts/install_linux.sh --install-dir /custom/path

For help:
    ./scripts/install_linux.sh --help

EOF
    
    # Create tar.gz from temporary directory
    cd "$temp_archive_dir"
    tar -czf "$archive_path" "tm-$comp/"
    cd "$PROJECT_ROOT"
    
    # Note: Don't clean up temp_archive_dir yet - needed for makeself
    
    echo "Created: $archive_path"
    
    # Track generated files
    GENERATED_FILES+=("$archive_name")
}

# Function to create makeself self-extracting archive
create_makeself_archive() {
    local comp=$1
    
    if [[ "$MAKESELF_AVAILABLE" != "true" ]]; then
        return 0
    fi
    
    local run_name="tm-${comp}-v${VERSION}-${PLATFORM}-${ARCH}.run"
    local run_path="$OUTPUT_DIR/$run_name"
    
    echo ""
    echo "Creating Makeself archive: $run_name"
    
    # Use the same staging directory that was created for tar.gz
    local temp_archive_dir="$STAGING_DIR/archive-$comp"
    local staging_archive_dir="$temp_archive_dir/tm-$comp"
    
    # Recreate the archive directory if it doesn't exist
    if [[ ! -d "$staging_archive_dir" ]]; then
        echo "Warning: Archive directory not found. Skipping .run generation for $comp."
        return 1
    fi
    
    # Build makeself command
    local makeself_label="TaskMessenger ${comp^} v$VERSION Installer"
    
    # Execute makeself
    "$MAKESELF_BIN" \
        --nox11 \
        --license "$staging_archive_dir/LICENSE" \
        "$staging_archive_dir" \
        "$run_path" \
        "$makeself_label" \
        ./scripts/install_linux.sh || {
        echo "Warning: Makeself generation failed for $comp. Skipping."
        return 1
    }
    
    # Make the .run file executable
    chmod +x "$run_path"
    
    echo "Created: $run_path"
    
    # Track generated files
    GENERATED_FILES+=("$run_name")
}

# Ensure makeself is available
ensure_makeself

# Clean staging and output directories
rm -rf "$STAGING_DIR" "$OUTPUT_DIR"
mkdir -p "$STAGING_DIR" "$OUTPUT_DIR"

# Build and package based on component selection
if [[ "$COMPONENT" == "all" ]]; then
    for comp in manager worker; do
        build_component "$comp"
        create_archive "$comp"
        create_makeself_archive "$comp"
        # Clean up temporary archive directory after both formats are created
        rm -rf "$STAGING_DIR/archive-$comp"
    done
else
    build_component "$COMPONENT"
    create_archive "$COMPONENT"
    create_makeself_archive "$COMPONENT"
    # Clean up temporary archive directory after both formats are created
    rm -rf "$STAGING_DIR/archive-$COMPONENT"
fi

# Summary
echo ""
echo "=================================================="
echo "Distribution build complete!"
echo "=================================================="
echo "Version: $VERSION"
echo "Platform: $PLATFORM-$ARCH"
echo "Output directory: $OUTPUT_DIR"
echo ""
echo "Generated files:"
echo "--------------------------------------------------"

if [[ ${#GENERATED_FILES[@]} -gt 0 ]]; then
    for file in "${GENERATED_FILES[@]}"; do
        if [[ -f "$OUTPUT_DIR/$file" ]]; then
            size=$(du -h "$OUTPUT_DIR/$file" | cut -f1)
            if [[ "$file" =~ \.run$ ]]; then
                echo "  🚀 $file ($size)"
            else
                echo "  📦 $file ($size)"
            fi
        fi
    done
    echo "--------------------------------------------------"
    echo "Total files: ${#GENERATED_FILES[@]}"
else
    echo "  No files generated."
fi

echo ""
if [[ "$MAKESELF_AVAILABLE" == "true" ]]; then
    echo "✅ Created both .tar.gz and .run archives"
    echo ""
    echo "Installation options:"
    echo "  • Extract and run: tar -xzf <file>.tar.gz && cd task-message-* && ./scripts/install_linux.sh"
    echo "  • Direct install:  chmod +x <file>.run && ./<file>.run"
    echo "  • Custom extract:  ./<file>.run --target /custom/path"
else
    echo "⚠️  Makeself not available - only .tar.gz archives created"
    echo ""
    echo "Installation:"
    echo "  tar -xzf <file>.tar.gz && cd task-message-* && ./scripts/install_linux.sh"
fi

echo "=================================================="
