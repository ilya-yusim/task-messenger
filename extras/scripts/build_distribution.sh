#!/bin/bash
# build_distribution.sh - Create distributable packages for task-messenger
# Usage: ./build_distribution.sh [dispatcher|worker|all]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_ROOT"

# Parse arguments
COMPONENT="${1:-all}"
if [[ ! "$COMPONENT" =~ ^(dispatcher|worker|rendezvous|all)$ ]]; then
    echo "Usage: $0 [dispatcher|worker|rendezvous|all]"
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
    if [[ "$comp" == "dispatcher" ]]; then
        build_opts+=("-Dbuild_worker=false" "-Dbuild_rendezvous=false")
        echo "Building dispatcher only (FTXUI disabled for faster build)"
    elif [[ "$comp" == "worker" ]]; then
        build_opts+=("-Dbuild_dispatcher=false" "-Dbuild_rendezvous=false" "-Dbuild_generators=false")
        echo "Building worker only"
    elif [[ "$comp" == "rendezvous" ]]; then
        build_opts+=("-Dbuild_dispatcher=false" "-Dbuild_worker=false" "-Dbuild_generators=false")
        echo "Building rendezvous service only"
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

# Bundle libopenblas.so.* alongside the executable so the installed binary
# does not rely on the user having libopenblas installed at runtime.
#
# Detects whether the executable's NEEDED entries reference libopenblas
# (via readelf/objdump — does not require RPATH resolution to succeed,
# which previously caused silent skips because the archive's lib/ dir
# isn't yet populated when this runs).
#
# Locates the .so in this order and copies it into the archive's lib/
# under the SONAME the binary actually requests:
#   1. The openblas-wrapper subproject's local install dir
#      (subprojects/openblas-wrapper/dist/linux-x64/lib).
#   2. ldd output, with LD_LIBRARY_PATH augmented to include the above.
#
# If the binary needs libopenblas but no .so can be located, this
# function FAILS LOUDLY (exit 1) — silent skips here have caused
# releases to ship without libopenblas.
bundle_libopenblas() {
    local exe_path=$1
    local archive_lib_dir=$2

    if [[ ! -x "$exe_path" ]]; then
        return 0
    fi

    # Find the NEEDED SONAME (e.g. libopenblas.so.0). Prefer readelf,
    # fall back to objdump.
    local needed=""
    if command -v readelf >/dev/null 2>&1; then
        needed=$(readelf -d "$exe_path" 2>/dev/null \
            | awk '/NEEDED.*libopenblas/ {match($0,/\[[^]]+\]/); print substr($0,RSTART+1,RLENGTH-2); exit}')
    fi
    if [[ -z "$needed" ]] && command -v objdump >/dev/null 2>&1; then
        needed=$(objdump -p "$exe_path" 2>/dev/null \
            | awk '/NEEDED.*libopenblas/ {print $2; exit}')
    fi

    if [[ -z "$needed" ]]; then
        # Binary doesn't link libopenblas at all; nothing to bundle.
        return 0
    fi

    echo "  binary needs $needed; locating it..."

    local subproj_lib="$PROJECT_ROOT/subprojects/openblas-wrapper/dist/linux-x64/lib"
    local resolved=""

    # 1) Direct hit on SONAME inside the subproject.
    if [[ -e "$subproj_lib/$needed" ]]; then
        resolved=$(readlink -f "$subproj_lib/$needed" 2>/dev/null || echo "$subproj_lib/$needed")
    fi

    # 2) Fall back to libopenblas.so symlink inside the subproject.
    if [[ -z "$resolved" && ( -L "$subproj_lib/libopenblas.so" || -f "$subproj_lib/libopenblas.so" ) ]]; then
        resolved=$(readlink -f "$subproj_lib/libopenblas.so" 2>/dev/null || echo "")
    fi

    # 3) ldd, augmenting LD_LIBRARY_PATH so the subproject dir is searched.
    if [[ -z "$resolved" ]] && command -v ldd >/dev/null 2>&1; then
        local r
        r=$(LD_LIBRARY_PATH="$subproj_lib:${LD_LIBRARY_PATH:-}" \
            ldd "$exe_path" 2>/dev/null \
            | awk -v n="$needed" '$1 == n {print $3; exit}')
        if [[ -n "$r" && "$r" != "not" && -f "$r" ]]; then
            resolved="$r"
        fi
    fi

    if [[ -z "$resolved" ]]; then
        echo "  ERROR: $exe_path NEEDs $needed but it could not be found in:" >&2
        echo "    - $subproj_lib" >&2
        echo "    - any ldd-discoverable system path" >&2
        echo "  Refusing to ship a broken release. Build OpenBLAS via the openblas-wrapper subproject before packaging." >&2
        exit 1
    fi

    mkdir -p "$archive_lib_dir"
    # Dereference symlinks so the archive contains the real shared object,
    # named with the SONAME the binary actually requests.
    cp -L "$resolved" "$archive_lib_dir/$needed"
    echo "  bundled libopenblas: $resolved -> $archive_lib_dir/$needed"
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
    
    if [[ "$comp" == "dispatcher" ]]; then
        # Dispatcher: executable, libzt, config, dashboard, docs.
        # Note: the "dispatcher" product is actually the interactive generator executable,
        # renamed to tm-dispatcher in the bundle so launchers/install scripts find it.
        mkdir -p "$archive_root/bin"
        cp "$staging_prefix/bin/tm-generator-interactive" "$archive_root/bin/tm-dispatcher"

        mkdir -p "$archive_root/lib"
        cp "$staging_prefix/lib/libzt.so" "$archive_root/lib/libzt.so"

        # Bundle libopenblas if the dispatcher binary links it (BLAS skills enabled).
        bundle_libopenblas "$archive_root/bin/tm-dispatcher" "$archive_root/lib"

        mkdir -p "$archive_root/config"
        cp "$staging_prefix/etc/task-messenger/config-dispatcher.json" "$archive_root/config/"

        # Dashboard assets for dispatcher monitoring UI
        if [[ -d "$staging_prefix/bin/dashboard" ]]; then
            cp -r "$staging_prefix/bin/dashboard" "$archive_root/dashboard"
        fi

        mkdir -p "$archive_root/doc"
        cp -r "$staging_prefix/share/doc/task-messenger/"* "$archive_root/doc/"

        # Copy LICENSE to root for visibility
        cp "$PROJECT_ROOT/LICENSE" "$archive_root/"
    elif [[ "$comp" == "worker" ]]; then
        # Worker: executable, libzt, configs, docs
        mkdir -p "$archive_root/bin"
        cp "$staging_prefix/bin/tm-worker" "$archive_root/bin/"

        mkdir -p "$archive_root/lib"
        cp "$staging_prefix/lib/libzt.so" "$archive_root/lib/libzt.so"

        # Bundle libopenblas (worker links it for BLAS-backed skills).
        bundle_libopenblas "$archive_root/bin/tm-worker" "$archive_root/lib"

        mkdir -p "$archive_root/config"
        cp "$staging_prefix/etc/task-messenger/config-worker.json" "$archive_root/config/"

        mkdir -p "$archive_root/doc"
        cp -r "$staging_prefix/share/doc/task-messenger/"* "$archive_root/doc/"

        # Copy LICENSE to root for visibility
        cp "$PROJECT_ROOT/LICENSE" "$archive_root/"
    elif [[ "$comp" == "rendezvous" ]]; then
        # Rendezvous: executable, libzt, config, identity, dashboard, docs
        mkdir -p "$archive_root/bin"
        cp "$staging_prefix/bin/tm-rendezvous" "$archive_root/bin/"

        mkdir -p "$archive_root/lib"
        cp "$staging_prefix/lib/libzt.so" "$archive_root/lib/libzt.so"

        # Bundle libopenblas if the rendezvous binary links it.
        bundle_libopenblas "$archive_root/bin/tm-rendezvous" "$archive_root/lib"

        mkdir -p "$archive_root/config"
        cp "$staging_prefix/etc/task-messenger/config-rendezvous.json" "$archive_root/config/"
        cp -r "$staging_prefix/etc/task-messenger/vn-rendezvous-identity" "$archive_root/config/"

        # Dashboard assets are served by rendezvous service UI
        if [[ -d "$staging_prefix/bin/dashboard" ]]; then
            cp -r "$staging_prefix/bin/dashboard" "$archive_root/dashboard"
        fi

        mkdir -p "$archive_root/doc"
        cp -r "$staging_prefix/share/doc/task-messenger/"* "$archive_root/doc/"

        cp "$PROJECT_ROOT/LICENSE" "$archive_root/"
    fi
    
    # Copy installation scripts and related files
    mkdir -p "$archive_root/scripts"
    cp "$PROJECT_ROOT/extras/scripts/install_linux.sh" "$archive_root/scripts/"
    cp "$PROJECT_ROOT/extras/scripts/uninstall_linux.sh" "$archive_root/scripts/"
    chmod +x "$archive_root/scripts/"*.sh
    
    # Copy launchers
    mkdir -p "$archive_root/launchers"
    if [[ "$comp" == "dispatcher" ]]; then
        cp "$PROJECT_ROOT/extras/launchers/start-tm-dispatcher.sh" "$archive_root/launchers/"
    elif [[ "$comp" == "worker" ]]; then
        cp "$PROJECT_ROOT/extras/launchers/start-tm-worker.sh" "$archive_root/launchers/"
    elif [[ "$comp" == "rendezvous" ]]; then
        cp "$PROJECT_ROOT/extras/launchers/start-tm-rendezvous.sh" "$archive_root/launchers/"
    fi
    chmod +x "$archive_root/launchers/"*.sh
    
    # Copy desktop files
    mkdir -p "$archive_root/desktop"
    if [[ "$comp" == "dispatcher" ]]; then
        cp "$PROJECT_ROOT/extras/desktop/tm-dispatcher.desktop" "$archive_root/desktop/"
    elif [[ "$comp" == "worker" ]]; then
        cp "$PROJECT_ROOT/extras/desktop/tm-worker.desktop" "$archive_root/desktop/"
    elif [[ "$comp" == "rendezvous" ]]; then
        cp "$PROJECT_ROOT/extras/desktop/tm-rendezvous.desktop" "$archive_root/desktop/"
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
    for comp in dispatcher worker rendezvous; do
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
