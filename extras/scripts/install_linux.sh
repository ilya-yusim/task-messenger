#!/bin/bash

# TaskMessenger Linux Installation Script
# This script installs TaskMessenger (manager or worker) for the current user

set -e

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
DEFAULT_INSTALL_DIR="$HOME/.local/share/task-messenger"
CONFIG_DIR="$HOME/.config/task-messenger"
BIN_SYMLINK_DIR="$HOME/.local/bin"
DESKTOP_DIR="$HOME/.local/share/applications"

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
Usage: $0 <component> [OPTIONS]

Arguments:
  component              Either 'manager' or 'worker'

Options:
  --install-dir PATH     Custom installation directory (default: ~/.local/share/task-messenger)
  --archive PATH         Path to distribution archive (auto-detected if not provided)
  --help                 Show this help message

Examples:
  $0 manager
  $0 worker --install-dir /custom/path
  $0 manager --archive task-messenger-manager-v1.0.0-linux-x86_64.tar.gz

EOF
}

detect_archive() {
    local component=$1
    local pattern="task-messenger-${component}-v*-linux-*.tar.gz"
    
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
    # Extract version from filename: task-messenger-manager-v1.0.0-linux-x86_64.tar.gz -> 1.0.0
    echo "$archive" | sed -n 's/.*-v\([0-9]\+\.[0-9]\+\.[0-9]\+\).*/\1/p'
}

check_existing_installation() {
    local install_dir=$1
    local component=$2
    
    if [ -d "$install_dir/$component" ]; then
        local installed_version=""
        if [ -f "$install_dir/$component/VERSION" ]; then
            installed_version=$(cat "$install_dir/$component/VERSION")
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

install_component() {
    local component=$1
    local archive=$2
    local install_dir=$3
    local version=$4
    
    print_info "Installing $component from $archive..."
    
    # Create installation directory
    mkdir -p "$install_dir/$component"
    
    # Extract archive to temporary directory
    local temp_dir=$(mktemp -d)
    print_info "Extracting archive to temporary location..."
    tar -xzf "$archive" -C "$temp_dir"
    
    # Find the extracted directory (should be opt/task-messenger)
    local extracted_dir="$temp_dir/opt/task-messenger"
    if [ ! -d "$extracted_dir" ]; then
        print_error "Unexpected archive structure. Expected opt/task-messenger directory."
        rm -rf "$temp_dir"
        exit 1
    fi
    
    # Move files to installation directory
    print_info "Installing files..."
    
    # Copy binaries
    if [ -f "$extracted_dir/bin/${component}" ]; then
        cp "$extracted_dir/bin/${component}" "$install_dir/$component/"
        chmod +x "$install_dir/$component/${component}"
    else
        print_error "Binary not found: $extracted_dir/bin/${component}"
        rm -rf "$temp_dir"
        exit 1
    fi
    
    # Copy shared library
    if [ -d "$extracted_dir/lib" ]; then
        mkdir -p "$install_dir/$component/lib"
        cp -r "$extracted_dir/lib/"* "$install_dir/$component/lib/"
    fi
    
    # Copy identity files for manager
    if [ "$component" = "manager" ]; then
        if [ -f "$extracted_dir/bin/identity.public" ] && [ -f "$extracted_dir/bin/identity.secret" ]; then
            cp "$extracted_dir/bin/identity.public" "$install_dir/$component/"
            cp "$extracted_dir/bin/identity.secret" "$install_dir/$component/"
            chmod 600 "$install_dir/$component/identity.secret"
        fi
    fi
    
    # Copy documentation
    if [ -d "$extracted_dir/share/doc" ]; then
        mkdir -p "$install_dir/$component/doc"
        cp -r "$extracted_dir/share/doc/"* "$install_dir/$component/doc/"
    fi
    
    # Store version information
    echo "$version" > "$install_dir/$component/VERSION"
    
    # Clean up temporary directory
    rm -rf "$temp_dir"
    
    print_success "$component installed to: $install_dir/$component"
}

create_symlink() {
    local install_dir=$1
    local component=$2
    
    mkdir -p "$BIN_SYMLINK_DIR"
    
    local symlink_path="$BIN_SYMLINK_DIR/$component"
    local target_path="$install_dir/$component/$component"
    
    if [ -L "$symlink_path" ] || [ -f "$symlink_path" ]; then
        rm -f "$symlink_path"
    fi
    
    ln -s "$target_path" "$symlink_path"
    print_success "Created symlink: $symlink_path -> $target_path"
}

setup_configs() {
    local component=$1
    
    mkdir -p "$CONFIG_DIR"
    
    local config_file="$CONFIG_DIR/config-${component}.json"
    
    # Only create config template if it doesn't exist
    if [ ! -f "$config_file" ]; then
        cat > "$config_file" << EOF
{
  "network": {
    "zerotier_network_id": "",
    "zerotier_identity_path": ""
  },
  "logging": {
    "level": "info",
    "file": ""
  }
}
EOF
        print_success "Created config template: $config_file"
        print_warning "Please edit the config file to set your ZeroTier network ID and other settings"
    else
        print_info "Config file already exists: $config_file"
    fi
}

install_desktop_entry() {
    local component=$1
    local install_dir=$2
    
    # Find desktop file in the distribution
    local script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    local desktop_file="$script_dir/../desktop/task-messenger-${component}.desktop"
    
    if [ ! -f "$desktop_file" ]; then
        print_warning "Desktop entry file not found: $desktop_file"
        return
    fi
    
    mkdir -p "$DESKTOP_DIR"
    
    # Copy and update Exec path in desktop entry
    local installed_desktop="$DESKTOP_DIR/task-messenger-${component}.desktop"
    sed "s|Exec=.*|Exec=$install_dir/$component/$component|" "$desktop_file" > "$installed_desktop"
    chmod +x "$installed_desktop"
    
    print_success "Installed desktop entry: $installed_desktop"
    
    # Update desktop database if available
    if command -v update-desktop-database &> /dev/null; then
        update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true
    fi
}

check_path() {
    if [[ ":$PATH:" == *":$BIN_SYMLINK_DIR:"* ]]; then
        print_success "$BIN_SYMLINK_DIR is already in PATH"
    else
        print_warning "$BIN_SYMLINK_DIR is not in PATH"
        print_info "Add the following line to your ~/.bashrc or ~/.zshrc:"
        echo ""
        echo "    export PATH=\"\$HOME/.local/bin:\$PATH\""
        echo ""
    fi
}

# Main script
main() {
    # Parse arguments
    if [ $# -lt 1 ]; then
        show_usage
        exit 1
    fi
    
    COMPONENT=$1
    shift
    
    # Validate component
    if [ "$COMPONENT" != "manager" ] && [ "$COMPONENT" != "worker" ]; then
        print_error "Invalid component: $COMPONENT. Must be 'manager' or 'worker'"
        show_usage
        exit 1
    fi
    
    INSTALL_DIR="$DEFAULT_INSTALL_DIR"
    ARCHIVE=""
    
    # Parse options
    while [ $# -gt 0 ]; do
        case $1 in
            --install-dir)
                INSTALL_DIR="$2"
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
    
    # Detect archive if not provided
    if [ -z "$ARCHIVE" ]; then
        print_info "Auto-detecting distribution archive..."
        ARCHIVE=$(detect_archive "$COMPONENT")
        if [ -z "$ARCHIVE" ]; then
            print_error "Could not find distribution archive for $COMPONENT"
            print_info "Please specify the archive path with --archive option"
            exit 1
        fi
        print_info "Found archive: $ARCHIVE"
    fi
    
    # Validate archive exists
    if [ ! -f "$ARCHIVE" ]; then
        print_error "Archive not found: $ARCHIVE"
        exit 1
    fi
    
    # Extract version from archive name
    VERSION=$(extract_version "$ARCHIVE")
    if [ -z "$VERSION" ]; then
        print_warning "Could not extract version from archive filename"
        VERSION="unknown"
    fi
    
    print_info "=========================================="
    print_info "TaskMessenger $COMPONENT Installation"
    print_info "=========================================="
    print_info "Component:        $COMPONENT"
    print_info "Version:          $VERSION"
    print_info "Archive:          $ARCHIVE"
    print_info "Install location: $INSTALL_DIR/$COMPONENT"
    print_info "Config location:  $CONFIG_DIR"
    print_info "Symlink location: $BIN_SYMLINK_DIR/$COMPONENT"
    print_info "=========================================="
    echo ""
    
    # Check for existing installation
    if check_existing_installation "$INSTALL_DIR" "$COMPONENT"; then
        backup_configs "$CONFIG_DIR" "$COMPONENT"
    fi
    
    # Install component
    install_component "$COMPONENT" "$ARCHIVE" "$INSTALL_DIR" "$VERSION"
    
    # Create symlink
    create_symlink "$INSTALL_DIR" "$COMPONENT"
    
    # Setup configuration
    setup_configs "$COMPONENT"
    
    # Install desktop entry
    install_desktop_entry "$COMPONENT" "$INSTALL_DIR"
    
    # Check PATH
    check_path
    
    echo ""
    print_success "=========================================="
    print_success "Installation completed successfully!"
    print_success "=========================================="
    print_info "You can now run: $COMPONENT"
    print_info "Or use the full path: $INSTALL_DIR/$COMPONENT/$COMPONENT"
    echo ""
}

main "$@"
