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
Usage: $0 [component] [OPTIONS]

Arguments:
  component              Either 'manager', 'worker', or 'all' (auto-detected if not specified)

Options:
  --install-dir PATH     Custom installation directory (default: ~/.local/share/task-messenger)
  --remove-config        Also remove configuration files
  --help                 Show this help message

Note: If component is not specified, the script will detect installed components. If more than one is found in the same installation, it will prompt you to choose.

Examples:
  $0
  $0 manager
  $0 worker --remove-config
  $0 all --install-dir /custom/path

EOF
}

check_installation() {
    local install_dir=$1
    local component=$2
    
    if [ "$component" = "all" ]; then
        if [ ! -d "$install_dir/manager" ] && [ ! -d "$install_dir/worker" ]; then
            print_error "No TaskMessenger installation found at: $install_dir"
            exit 1
        fi
    else
        if [ ! -d "$install_dir/$component" ]; then
            print_error "$component is not installed at: $install_dir/$component"
            exit 1
        fi
    fi
}

get_installed_version() {
    local install_dir=$1
    local component=$2
    
    local version_file="$install_dir/$component/VERSION"
    if [ -f "$version_file" ]; then
        cat "$version_file"
    else
        echo "unknown"
    fi
}

get_installed_components() {
    local install_dir=$1
    local components=()
    
    if [ -d "$install_dir/manager" ]; then
        components+=("manager")
    fi
    
    if [ -d "$install_dir/worker" ]; then
        components+=("worker")
    fi
    
    echo "${components[@]}"
}

select_component() {
    local installed_components=("$@")
    local count=${#installed_components[@]}
    
    if [ $count -eq 0 ]; then
        return 1
    fi
    
    if [ $count -eq 1 ]; then
        echo "${installed_components[0]}"
        return 0
    fi
    
    print_info "Multiple components are installed:"
    echo "  1. manager"
    echo "  2. worker"
    echo "  3. all (both)"
    echo ""
    
    while true; do
        read -p "Select component to uninstall [1-3]: " choice
        
        case $choice in
            1) echo "manager"; return 0 ;;
            2) echo "worker"; return 0 ;;
            3) echo "all"; return 0 ;;
            *) print_warning "Invalid choice. Please enter 1, 2, or 3." ;;
        esac
    done
}

get_component_from_script_location() {
    local install_dir=$1
    
    # Get script location
    local script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    
    # Check if script is in a component directory
    local manager_dir="$install_dir/manager"
    local worker_dir="$install_dir/worker"
    
    if [ "$script_dir" = "$manager_dir" ]; then
        echo "manager"
        return 0
    elif [ "$script_dir" = "$worker_dir" ]; then
        echo "worker"
        return 0
    fi
    
    # Not in a component directory - return nothing to trigger prompt
    return 1
}

remove_component() {
    local install_dir=$1
    local component=$2
    
    local component_dir="$install_dir/$component"
    
    if [ ! -d "$component_dir" ]; then
        print_warning "$component is not installed"
        return
    fi
    
    local version=$(get_installed_version "$install_dir" "$component")
    print_info "Removing $component (version $version)..."
    
    # Remove installation directory
    rm -rf "$component_dir"
    print_success "Removed installation directory: $component_dir"
    
    # Remove symlink
    local symlink_path="$BIN_SYMLINK_DIR/$component"
    if [ -L "$symlink_path" ] || [ -f "$symlink_path" ]; then
        rm -f "$symlink_path"
        print_success "Removed symlink: $symlink_path"
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
    
    local config_file="$CONFIG_DIR/config-$component.json"
    
    if [ -f "$config_file" ]; then
        print_warning "Configuration file found: $config_file"
        read -p "Do you want to remove the configuration file? [y/N] " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            rm -f "$config_file"
            print_success "Removed configuration file: $config_file"
            
            # Also remove backup files
            local backup_pattern="$config_file.backup.*"
            if ls $backup_pattern &> /dev/null; then
                rm -f $backup_pattern
                print_success "Removed configuration backups"
            fi
        else
            print_info "Configuration file preserved"
        fi
    fi
}

cleanup_empty_directories() {
    local install_dir=$1
    
    # Remove install directory if empty
    if [ -d "$install_dir" ]; then
        if [ -z "$(ls -A "$install_dir")" ]; then
            rmdir "$install_dir"
            print_info "Removed empty installation directory: $install_dir"
        fi
    fi
    
    # Remove config directory if empty
    if [ -d "$CONFIG_DIR" ]; then
        if [ -z "$(ls -A "$CONFIG_DIR")" ]; then
            rmdir "$CONFIG_DIR"
            print_info "Removed empty configuration directory: $CONFIG_DIR"
        fi
    fi
}

remove_from_path_instruction() {
    print_info "If you added $BIN_SYMLINK_DIR to your PATH, you may want to remove it"
    print_info "from your ~/.bashrc or ~/.zshrc file"
}

# Main script
main() {
    # Parse arguments
    COMPONENT=""
    INSTALL_DIR="$DEFAULT_INSTALL_DIR"
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
                if [ "$COMPONENT" != "manager" ] && [ "$COMPONENT" != "worker" ] && [ "$COMPONENT" != "all" ]; then
                    print_error "Invalid component: $COMPONENT. Must be 'manager', 'worker', or 'all'"
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
                INSTALL_DIR="$2"
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
    
    # Auto-detect installed components if component not specified
    if [ -z "$COMPONENT" ]; then
        COMPONENT=$(get_component_from_script_location "$INSTALL_DIR")
        
        if [ -z "$COMPONENT" ]; then
            # Script is not in a component directory - prompt user
            local installed_components=($(get_installed_components "$INSTALL_DIR"))
            
            if [ ${#installed_components[@]} -eq 0 ]; then
                print_error "No TaskMessenger installation found at: $INSTALL_DIR"
                exit 1
            fi
            
            COMPONENT=$(select_component "${installed_components[@]}")
            
            if [ -z "$COMPONENT" ]; then
                print_error "Failed to select component"
                exit 1
            fi
        fi
        
        print_info "Component to uninstall: $COMPONENT"
    fi
    
    # Check installation
    check_installation "$INSTALL_DIR" "$COMPONENT"
    
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
    
    # Remove components
    if [ "$COMPONENT" = "all" ]; then
        remove_component "$INSTALL_DIR" "manager"
        remove_component "$INSTALL_DIR" "worker"
        
        if [ "$REMOVE_CONFIG" = true ]; then
            remove_config "manager"
            remove_config "worker"
        fi
    else
        remove_component "$INSTALL_DIR" "$COMPONENT"
        
        if [ "$REMOVE_CONFIG" = true ]; then
            remove_config "$COMPONENT"
        fi
    fi
    
    # Cleanup empty directories
    cleanup_empty_directories "$INSTALL_DIR"
    
    # Show PATH instruction if needed
    if [ "$COMPONENT" = "all" ] || [ ! -d "$INSTALL_DIR/manager" ] && [ ! -d "$INSTALL_DIR/worker" ]; then
        remove_from_path_instruction
    fi
    
    echo ""
    print_success "=========================================="
    print_success "Uninstallation completed successfully!"
    print_success "=========================================="
    echo ""
}

main "$@"
