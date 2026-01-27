#!/bin/bash

# TaskMessenger macOS Installation Script
# This script installs TaskMessenger (manager or worker) for the current user

set -e

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration - macOS standard paths
DEFAULT_INSTALL_BASE="$HOME/Library/Application Support/TaskMessenger"
CONFIG_BASE="$HOME/Library/Application Support/TaskMessenger/config"
BIN_SYMLINK_DIR="$HOME/.local/bin"

# Functions
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

show_usage() {
    cat << EOF
Usage: $0 [OPTIONS]

Options:
  --install-dir PATH     Custom installation directory (default: ~/Library/Application Support/TaskMessenger/tm-{manager|worker})
  --archive PATH         Path to distribution archive (auto-detected if not provided)
  --help                 Show this help message

Note: The component (manager or worker) is automatically detected from the extracted files.

Examples:
  $0
  $0 --install-dir /custom/path
  $0 --archive tm-manager-v1.0.0-macos-universal.tar.gz

EOF
}

detect_extracted_files() {
    local script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    local extracted_root="$(dirname "$script_dir")"
    
    # Check for marker file (shared library) to confirm extracted archive
    local lib_path="$extracted_root/lib/libzt.dylib"
    
    if [ -f "$lib_path" ]; then
        # Detect component by checking which executable exists
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

detect_archive() {
    local component=$1
    local pattern="tm-${component}-v*-macos-*.tar.gz"
    
    # Search in current directory and parent directory
    local archive=$(find . .. -maxdepth 1 -name "$pattern" 2>/dev/null | head -n 1)
    
    if [ -n "$archive" ]; then
        echo "$archive"
        return 0
    fi
    
    return 1
}

extract_version() {
    local archive=$1
    # Extract version from filename: tm-manager-v1.0.0-macos-universal.tar.gz -> 1.0.0
    echo "$archive" | sed -n 's/.*-v\([0-9]\+\.[0-9]\+\.[0-9]\+\).*/\1/p'
}

check_existing_installation() {
    local install_dir=$1
    local component=$2
    
    if [ -d "$install_dir" ]; then
        local installed_version=""
        if [ -f "$install_dir/VERSION" ]; then
            installed_version=$(cat "$install_dir/VERSION")
        fi
        
        if [ -n "$installed_version" ]; then
            print_warning "Found existing installation of $component (version $installed_version)"
        else
            print_warning "Found existing installation of $component"
        fi
        
        read -p "Do you want to upgrade? This will replace the existing installation. [y/N] " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            print_info "Installation cancelled by user"
            exit 0
        fi
        
        return 0
    fi
    
    return 1
}

backup_configs() {
    local config_dir=$1
    local component=$2
    
    if [ -f "$config_dir/config-${component}.json" ]; then
        local backup_file="$config_dir/config-${component}.json.backup.$(date +%Y%m%d-%H%M%S)"
        cp "$config_dir/config-${component}.json" "$backup_file"
        print_info "Backed up existing config to: $backup_file"
    fi
}

remove_quarantine() {
    local target_dir=$1
    local description=$2
    
    if [ ! -d "$target_dir" ]; then
        return 0
    fi
    
    print_info "Removing quarantine attributes from $description..."
    
    # Check if xattr command exists
    if ! command -v xattr &> /dev/null; then
        print_error "xattr command not found. This is required on macOS."
        print_error "Please ensure you are running on a standard macOS system."
        exit 1
    fi
    
    # Remove quarantine attributes recursively
    if ! xattr -dr com.apple.quarantine "$target_dir" 2>/dev/null; then
        # xattr returns error if attribute doesn't exist, which is fine
        # But if it's a permission error or other issue, we should know
        local exit_code=$?
        if [ $exit_code -ne 0 ] && [ $exit_code -ne 1 ]; then
            print_error "Failed to remove quarantine attributes from $target_dir"
            print_error "Exit code: $exit_code"
            exit 1
        fi
    fi
    
    print_success "Quarantine attributes removed from $description"
}

install_component() {
    local component=$1
    local extracted_dir=$2
    local install_dir=$3
    local version=$4
    
    print_info "Installing $component..."
    
    # Create installation directory
    mkdir -p "$install_dir/bin"
    
    # Copy binaries
    if [ -f "$extracted_dir/bin/tm-${component}" ]; then
        cp "$extracted_dir/bin/tm-${component}" "$install_dir/bin/"
        chmod +x "$install_dir/bin/tm-${component}"
    else
        print_error "Binary not found: $extracted_dir/bin/tm-${component}"
        exit 1
    fi
    
    # Copy shared library
    if [ -d "$extracted_dir/lib" ]; then
        mkdir -p "$install_dir/lib"
        cp -r "$extracted_dir/lib/"* "$install_dir/lib/"
    fi
    
    # Copy documentation
    if [ -d "$extracted_dir/doc" ]; then
        mkdir -p "$install_dir/doc"
        cp -r "$extracted_dir/doc/"* "$install_dir/doc/"
    fi
    
    # Remove quarantine attributes from binaries and libraries
    remove_quarantine "$install_dir/bin" "binaries"
    remove_quarantine "$install_dir/lib" "libraries"
    
    # Copy config file from archive to component-specific config directory
    local config_dir="$CONFIG_BASE/$component"
    local config_source_dir="$extracted_dir/config"
    local config_file="$config_source_dir/config-$component.json"
    if [ -f "$config_file" ]; then
        mkdir -p "$config_dir"
        cp "$config_file" "$config_dir/"
        print_success "Installed config: $config_dir/config-$component.json"
    fi
    
    # Copy identity directory for manager
    if [ "$component" = "manager" ]; then
        local identity_dir="$config_source_dir/vn-manager-identity"
        
        if [ -d "$identity_dir" ]; then
            mkdir -p "$config_dir"
            cp -r "$identity_dir" "$config_dir/"
            
            # Set restrictive permissions on secret file
            local secret_path="$config_dir/vn-manager-identity/identity.secret"
            if [ -f "$secret_path" ]; then
                chmod 600 "$secret_path"
                print_success "Installed identity files with restricted permissions"
            fi
        fi
    fi
    
    # Copy uninstall script
    local scripts_dir="$extracted_dir/scripts"
    if [ -f "$scripts_dir/uninstall_macos.sh" ]; then
        mkdir -p "$install_dir/scripts"
        cp "$scripts_dir/uninstall_macos.sh" "$install_dir/scripts/"
        chmod +x "$install_dir/scripts/uninstall_macos.sh"
        print_success "Installed uninstall script: $install_dir/scripts/uninstall_macos.sh"
    fi
    
    # Store version information
    echo "$version" > "$install_dir/VERSION"
    
    print_success "$component installed to: $install_dir"
}

create_symlink() {
    local install_dir=$1
    local component=$2
    
    mkdir -p "$BIN_SYMLINK_DIR"
    
    local symlink_path="$BIN_SYMLINK_DIR/tm-$component"
    local target_path="$install_dir/bin/tm-$component"
    
    # Remove existing symlink if present
    if [ -L "$symlink_path" ]; then
        rm "$symlink_path"
    fi
    
    # Create symlink directly to binary (no wrapper needed - RPATH handles library path)
    ln -s "$target_path" "$symlink_path"
    
    print_success "Created symlink: $symlink_path"
}

create_placeholder_icon() {
    local output_icns=$1
    local component=$2
    
    # Create temporary directory for icon generation
    local temp_iconset=$(mktemp -d)/AppIcon.iconset
    mkdir -p "$temp_iconset"
    
    # Choose color based on component
    local color="#4A90E2"  # Blue for default
    if [ "$component" = "manager" ]; then
        color="#50C878"  # Emerald green for manager
    else
        color="#5B9BD5"  # Steel blue for worker
    fi
    
    # Create base 1024x1024 image with colored background and text
    # Using sips to create a simple colored square
    local base_png=$(mktemp).png
    
    # Create a simple gradient using ImageMagick if available, otherwise solid color
    if command -v convert &> /dev/null; then
        # Use ImageMagick for better quality
        convert -size 1024x1024 "xc:$color" \
            -font Helvetica-Bold -pointsize 120 -fill white -gravity center \
            -annotate +0+0 "TM\n${component:0:1}" \
            "$base_png" 2>/dev/null
    else
        # Fallback: create simple colored PNG with sips
        # First create a white base
        sips -s format png --setProperty format png -z 1024 1024 /System/Library/CoreServices/CoreTypes.bundle/Contents/Resources/GenericDocumentIcon.icns --out "$base_png" 2>/dev/null || {
            # Ultimate fallback: create via Python if available
            python3 -c "
import os
try:
    from PIL import Image, ImageDraw, ImageFont
    img = Image.new('RGB', (1024, 1024), color='$color')
    draw = ImageDraw.Draw(img)
    text = 'TM\\n' + '${component}'.upper()[0]
    draw.text((512, 512), text, fill='white', anchor='mm')
    img.save('$base_png')
except ImportError:
    # Create minimal valid PNG
    img = Image.new('RGB', (1024, 1024), color='$color')
    img.save('$base_png')
" 2>/dev/null || return 1
        }
    fi
    
    # Generate all required icon sizes
    sips -z 16 16     "$base_png" --out "$temp_iconset/icon_16x16.png" &>/dev/null
    sips -z 32 32     "$base_png" --out "$temp_iconset/icon_16x16@2x.png" &>/dev/null
    sips -z 32 32     "$base_png" --out "$temp_iconset/icon_32x32.png" &>/dev/null
    sips -z 64 64     "$base_png" --out "$temp_iconset/icon_32x32@2x.png" &>/dev/null
    sips -z 128 128   "$base_png" --out "$temp_iconset/icon_128x128.png" &>/dev/null
    sips -z 256 256   "$base_png" --out "$temp_iconset/icon_128x128@2x.png" &>/dev/null
    sips -z 256 256   "$base_png" --out "$temp_iconset/icon_256x256.png" &>/dev/null
    sips -z 512 512   "$base_png" --out "$temp_iconset/icon_256x256@2x.png" &>/dev/null
    sips -z 512 512   "$base_png" --out "$temp_iconset/icon_512x512.png" &>/dev/null
    sips -z 1024 1024 "$base_png" --out "$temp_iconset/icon_512x512@2x.png" &>/dev/null
    
    # Convert to .icns
    iconutil -c icns "$temp_iconset" -o "$output_icns" 2>/dev/null
    
    # Cleanup
    rm -rf "$temp_iconset" "$base_png"
    
    return 0
}

create_app_bundle() {
    local component=$1
    local install_dir=$2
    local config_dir=$3
    local version=$4
    
    # Capitalize first letter for display name (bash 3.2 compatible)
    local component_cap="$(echo "${component:0:1}" | tr '[:lower:]' '[:upper:]')${component:1}"
    local display_name="TaskMessenger ${component_cap}"
    local app_path="$HOME/Desktop/${display_name}.app"
    
    print_info "Creating application bundle..."
    
    # Remove existing app if present
    if [ -d "$app_path" ]; then
        rm -rf "$app_path"
    fi
    
    # Create bundle structure
    mkdir -p "$app_path/Contents/MacOS"
    mkdir -p "$app_path/Contents/Resources"
    
    # Create launcher script that opens in Terminal
    cat > "$app_path/Contents/MacOS/TaskMessenger" << 'LAUNCHER_EOF'
#!/bin/bash
# TaskMessenger Launcher - opens in Terminal

# Create a temporary launch script
TEMP_SCRIPT=$(mktemp).command
cat > "$TEMP_SCRIPT" << 'INNER_EOF'
#!/bin/bash
cd "$HOME"
exec "INSTALL_DIR_PLACEHOLDER/bin/tm-COMPONENT_PLACEHOLDER" -c "CONFIG_DIR_PLACEHOLDER/config-COMPONENT_PLACEHOLDER.json"
INNER_EOF

# Replace placeholders in temp script
sed -i '' 's|INSTALL_DIR_PLACEHOLDER|INSTALL_DIR_VALUE|g' "$TEMP_SCRIPT"
sed -i '' 's|CONFIG_DIR_PLACEHOLDER|CONFIG_DIR_VALUE|g' "$TEMP_SCRIPT"
sed -i '' 's|COMPONENT_PLACEHOLDER|COMPONENT_VALUE|g' "$TEMP_SCRIPT"

chmod +x "$TEMP_SCRIPT"

# Open in Terminal and exit immediately
open -a Terminal "$TEMP_SCRIPT"
LAUNCHER_EOF

    # Replace the outer placeholders with actual values
    sed -i '' "s|INSTALL_DIR_VALUE|$install_dir|g" "$app_path/Contents/MacOS/TaskMessenger"
    sed -i '' "s|CONFIG_DIR_VALUE|$config_dir|g" "$app_path/Contents/MacOS/TaskMessenger"
    sed -i '' "s|COMPONENT_VALUE|$component|g" "$app_path/Contents/MacOS/TaskMessenger"
    
    chmod +x "$app_path/Contents/MacOS/TaskMessenger"
    
    # Create Info.plist
    cat > "$app_path/Contents/Info.plist" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>TaskMessenger</string>
    <key>CFBundleIdentifier</key>
    <string>com.taskmessenger.${component}</string>
    <key>CFBundleName</key>
    <string>${display_name}</string>
    <key>CFBundleDisplayName</key>
    <string>${display_name}</string>
    <key>CFBundleVersion</key>
    <string>${version}</string>
    <key>CFBundleShortVersionString</key>
    <string>${version}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleSignature</key>
    <string>TMgr</string>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
    <key>NSHumanReadableCopyright</key>
    <string>TaskMessenger</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon</string>
    <key>LSUIElement</key>
    <false/>
    <key>NSHighResolutionCapable</key>
    <true/>
</dict>
</plist>
EOF
    
    # Generate placeholder icon
    create_placeholder_icon "$app_path/Contents/Resources/AppIcon.icns" "$component"
    
    # Remove quarantine from the app bundle
    xattr -dr com.apple.quarantine "$app_path" 2>/dev/null || true
    
    print_success "Created application bundle: $app_path"
    print_info "Application is now available on your Desktop and searchable via Spotlight"
    print_info "You can drag it to your Dock or Applications folder"
}

check_path() {
    if [[ ":$PATH:" == *":$BIN_SYMLINK_DIR:"* ]]; then
        print_success "$BIN_SYMLINK_DIR is already in PATH"
    else
        print_warning "$BIN_SYMLINK_DIR is not in PATH"
        print_info "Add the following line to your ~/.zshrc or ~/.bash_profile:"
        echo ""
        echo "    export PATH=\"\$HOME/.local/bin:\$PATH\""
        echo ""
        print_info "Then reload your shell configuration:"
        echo ""
        echo "    source ~/.zshrc"
        echo ""
    fi
}

# Main script
main() {
    CUSTOM_INSTALL_DIR=""
    ARCHIVE=""
    
    # Parse options
    while [ $# -gt 0 ]; do
        case $1 in
            --install-dir)
                CUSTOM_INSTALL_DIR="$2"
                shift 2
                ;;
            --archive)
                ARCHIVE="$2"
                shift 2
                ;;
            --help)
                show_usage
                exit 0
                ;;
            *)
                print_error "Unknown option: $1"
                show_usage
                exit 1
                ;;
        esac
    done
    
    # Try to detect extracted files first
    local detection_result=$(detect_extracted_files)
    local extracted_dir=""
    local COMPONENT=""
    
    if [ $? -eq 0 ]; then
        # Successfully detected from extracted files
        COMPONENT=$(echo "$detection_result" | cut -d: -f1)
        extracted_dir=$(echo "$detection_result" | cut -d: -f2)
        print_info "Detected $COMPONENT component from extracted files"
        VERSION="unknown"
        if [ -f "$extracted_dir/VERSION" ]; then
            VERSION=$(cat "$extracted_dir/VERSION")
        fi
    else
        # Fall back to archive extraction
        if [ -z "$ARCHIVE" ]; then
            print_error "Could not detect component from extracted files"
            print_error ""
            print_error "Solutions:"
            print_error "  1. Extract a TaskMessenger distribution archive (manager or worker)"
            print_error "     and run this script from the extracted directory"
            print_error ""
            print_error "  2. Specify the archive path manually:"
            print_error "     $0 --archive 'tm-{component}-v1.0.0-macos-universal.tar.gz'"
            exit 1
        fi
        
        # Validate archive exists
        if [ ! -f "$ARCHIVE" ]; then
            print_error "Archive not found: $ARCHIVE"
            exit 1
        fi
        
        print_info "Extracting archive..."
        local temp_dir=$(mktemp -d)
        tar -xzf "$ARCHIVE" -C "$temp_dir"
        
        # Detect archive structure: tm-{manager|worker}/
        extracted_dir=$(find "$temp_dir" -maxdepth 1 -type d \( -name "tm-manager" -o -name "tm-worker" \) 2>/dev/null | head -n 1)
        
        if [ -z "$extracted_dir" ]; then
            print_error "Unexpected archive structure. Expected tm-manager/ or tm-worker/ directory."
            rm -rf "$temp_dir"
            exit 1
        fi
        
        # Detect component from directory name
        local dir_name=$(basename "$extracted_dir")
        if [[ "$dir_name" == "tm-manager" ]]; then
            COMPONENT="manager"
        else
            COMPONENT="worker"
        fi
        
        # Extract version from archive name
        VERSION=$(extract_version "$ARCHIVE")
        if [ -z "$VERSION" ]; then
            print_warning "Could not extract version from archive filename"
            VERSION="unknown"
        fi
    fi
    
    # Determine installation directory
    if [ -n "$CUSTOM_INSTALL_DIR" ]; then
        INSTALL_DIR="$CUSTOM_INSTALL_DIR"
    else
        INSTALL_DIR="$DEFAULT_INSTALL_BASE/tm-$COMPONENT"
    fi
    
    local CONFIG_DIR="$CONFIG_BASE/$COMPONENT"
    
    print_info "=========================================="
    print_info "TaskMessenger $COMPONENT Installation"
    print_info "=========================================="
    print_info "Component:        $COMPONENT"
    print_info "Version:          $VERSION"
    print_info "Install location: $INSTALL_DIR"
    print_info "Config location:  $CONFIG_DIR"
    print_info "Symlink location: $BIN_SYMLINK_DIR/tm-$COMPONENT"
    print_info "=========================================="
    echo ""
    
    # Check for existing installation
    if check_existing_installation "$INSTALL_DIR" "$COMPONENT"; then
        backup_configs "$CONFIG_DIR" "$COMPONENT"
    fi
    
    # Install component
    install_component "$COMPONENT" "$extracted_dir" "$INSTALL_DIR" "$VERSION"
    
    # Create symlink (no wrapper script needed on macOS - RPATH configured)
    create_symlink "$INSTALL_DIR" "$COMPONENT"
    
    # Create application bundle
    create_app_bundle "$COMPONENT" "$INSTALL_DIR" "$CONFIG_DIR" "$VERSION"
    
    # Check PATH
    check_path
    
    echo ""
    print_success "=========================================="
    print_success "Installation completed successfully!"
    print_success "=========================================="
    print_info "You can now run: tm-$COMPONENT"
    print_info "Or use the full path: $INSTALL_DIR/bin/tm-$COMPONENT"
    print_info "Config file: $CONFIG_DIR/config-$COMPONENT.json"
    if [ "$COMPONENT" = "manager" ]; then
        print_info "Identity files: $CONFIG_DIR/vn-manager-identity/"
    fi
    echo ""
}

main "$@"
