#!/bin/bash

# TaskMessenger Linux Uninstallation Script
# This script removes TaskMessenger (manager or worker) for the current user

set -e

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
DEFAULT_INSTALL_BASE="$HOME/.local/share"
CONFIG_BASE="$HOME/.config"
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
Usage: $0 [component] [OPTIONS]

Arguments:
  component              Either 'manager' or 'worker' (auto-detected if not specified)

Options:
  --install-dir PATH     Custom installation directory (default: ~/.local/share/task-message-{manager|worker})
  --remove-config        Also remove configuration files from ~/.config/task-message-{manager|worker}
  --help                 Show this help message

Note: If component is not specified, the script will attempt to detect it from the script location.

Examples:
  $0
  $0 manager
  $0 worker --remove-config
  $0 manager --install-dir /custom/path

EOF
}

check_installation() {
    local install_dir=$1
    local component=$2
    
    if [ ! -d "$install_dir" ]; then
        print_error "$component is not installed at: $install_dir"
        exit 1
    fi
}

get_installed_version() {
    local install_dir=$1
    
    local version_file="$install_dir/VERSION"
    if [ -f "$version_file" ]; then
        cat "$version_file"
    else
        echo "unknown"
    fi
}

get_component_from_script_location() {
    # Get script location
    local script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    local parent_dir="$(dirname "$script_dir")"
    local dir_name="$(basename "$parent_dir")"
    
    # Check if parent directory matches component pattern
    if [[ "$dir_name" == "tm-manager" ]]; then
        echo "manager:$parent_dir"
        return 0
    elif [[ "$dir_name" == "tm-worker" ]]; then
        echo "worker:$parent_dir"
        return 0
    fi
    
    return 1
}

remove_component() {
    local install_dir=$1
    
    if [ ! -d "$install_dir" ]; then
        print_warning "Installation directory not found: $install_dir"
        return
    fi
    
    # Extract component name from directory
    local dir_name="$(basename "$install_dir")"
    local component="${dir_name#tm-}"
    
    local version=$(get_installed_version "$install_dir")
    print_info "Removing $component (version $version)..."
    
    # Remove installation directory
    rm -rf "$install_dir"
    print_success "Removed installation directory: $install_dir"
    
    # Remove wrapper script
    local wrapper_path="$BIN_SYMLINK_DIR/tm-$component"
    if [ -f "$wrapper_path" ]; then
        rm -f "$wrapper_path"
        print_success "Removed wrapper script: $wrapper_path"
    fi
    
    # Remove desktop entry
    local desktop_file="$DESKTOP_DIR/task-messenger-$component.desktop"
    if [ -f "$desktop_file" ]; then
        rm -f "$desktop_file"
        print_success "Removed desktop entry: $desktop_file"
        
        # Update desktop database if available
        if command -v update-desktop-database &> /dev/null; then
            update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true
        fi
    fi
}

remove_config() {
    local component=$1
    local config_dir="$CONFIG_BASE/task-message-$component"
    
    if [ ! -d "$config_dir" ]; then
        print_info "No configuration directory found: $config_dir"
        return
    fi
    
    print_info "Removing configuration directory: $config_dir"
    rm -rf "$config_dir"
    print_success "Removed configuration directory and all contents"
}

cleanup_empty_directories() {
    # No cleanup needed - component-specific config directories are already removed
    return 0
}

remove_from_path_instruction() {
    print_info "If you added $BIN_SYMLINK_DIR to your PATH, you may want to remove it"
    print_info "from your ~/.bashrc or ~/.zshrc file"
}

# Main script
main() {
    # Parse arguments
    COMPONENT=""
    CUSTOM_INSTALL_DIR=""
    REMOVE_CONFIG=false
    
    # Check if first argument is a component or option
    if [ $# -gt 0 ]; then
        case $1 in
            --install-dir|--remove-config|--help)
                # First arg is an option, no component specified
                ;;
            *)
                # First arg is component
                COMPONENT=$1
                shift
                
                # Validate component if specified
                if [ "$COMPONENT" != "manager" ] && [ "$COMPONENT" != "worker" ]; then
                    print_error "Invalid component: $COMPONENT. Must be 'manager' or 'worker'"
                    show_usage
                    exit 1
                fi
                ;;
        esac
    fi
    
    # Parse options
    while [ $# -gt 0 ]; do
        case $1 in
            --install-dir)
                CUSTOM_INSTALL_DIR="$2"
                shift 2
                ;;
            --remove-config)
                REMOVE_CONFIG=true
                shift
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
    
    # Auto-detect component and install directory if not specified
    if [ -z "$COMPONENT" ]; then
        local detection_result=$(get_component_from_script_location)
        
        if [ $? -eq 0 ]; then
            COMPONENT=$(echo "$detection_result" | cut -d: -f1)
            if [ -z "$CUSTOM_INSTALL_DIR" ]; then
                INSTALL_DIR=$(echo "$detection_result" | cut -d: -f2)
            fi
            print_info "Auto-detected component: $COMPONENT"
        else
            print_error "Could not auto-detect component. Please specify 'manager' or 'worker'."
            show_usage
            exit 1
        fi
    fi
    
    # Determine installation directory
    if [ -z "$INSTALL_DIR" ]; then
        if [ -n "$CUSTOM_INSTALL_DIR" ]; then
            INSTALL_DIR="$CUSTOM_INSTALL_DIR"
        else
            INSTALL_DIR="$DEFAULT_INSTALL_BASE/task-message-$COMPONENT"
        fi
    fi
    
    # Check installation
    check_installation "$INSTALL_DIR" "$COMPONENT"
    
    local CONFIG_DIR="$CONFIG_BASE/task-message-$COMPONENT"
    
    print_info "=========================================="
    print_info "TaskMessenger Uninstallation"
    print_info "=========================================="
    print_info "Component:        $COMPONENT"
    print_info "Install location: $INSTALL_DIR"
    print_info "Config location:  $CONFIG_DIR"
    print_info "Remove config:    $REMOVE_CONFIG"
    print_info "=========================================="
    echo ""
    
    read -p "Are you sure you want to uninstall? [y/N] " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        print_info "Uninstallation cancelled by user"
        exit 0
    fi
    
    # Remove component
    remove_component "$INSTALL_DIR"
    
    # Remove config if requested
    if [ "$REMOVE_CONFIG" = true ]; then
        remove_config "$COMPONENT"
    fi
    
    # Cleanup empty directories
    cleanup_empty_directories
    
    echo ""
    print_success "=========================================="
    print_success "Uninstallation completed successfully!"
    print_success "=========================================="
    echo ""
}

main "$@"
