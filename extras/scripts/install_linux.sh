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
Usage: $0 [OPTIONS]

Options:
  --install-dir PATH     Custom installation directory (default: ~/.local/share/task-message-{manager|worker})
  --archive PATH         Path to distribution archive (auto-detected if not provided)
  --help                 Show this help message

Note: The component (manager or worker) is automatically detected from the extracted files.

Examples:
  $0
  $0 --install-dir /custom/path
  $0 --archive task-messenger-manager-v1.0.0-linux-x86_64.tar.gz

EOF
}

detect_extracted_files() {
    local script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    local extracted_root="$(dirname "$script_dir")"
    
    # Check for marker file (shared library) to confirm extracted archive
    local lib_path="$extracted_root/lib/libzt.so"
    
    if [ -f "$lib_path" ]; then
        # Detect component by checking which executable exists
        local manager_path="$extracted_root/bin/manager"
        local worker_path="$extracted_root/bin/worker"
        
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

install_component() {
    local component=$1
    local extracted_dir=$2
    local install_dir=$3
    local version=$4
    
    print_info "Installing $component..."
    
    # Create installation directory
    mkdir -p "$install_dir/bin"
    
    # Copy binaries
    if [ -f "$extracted_dir/bin/${component}" ]; then
        cp "$extracted_dir/bin/${component}" "$install_dir/bin/"
        chmod +x "$install_dir/bin/${component}"
    else
        print_error "Binary not found: $extracted_dir/bin/${component}"
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
    
    # Copy config file from archive to component-specific XDG config directory
    local config_dir="$CONFIG_BASE/task-message-$component"
    local config_source_dir="$extracted_dir/config"
    local config_file="$config_source_dir/config-$component.json"
    if [ -f "$config_file" ]; then
        mkdir -p "$config_dir"
        cp "$config_file" "$config_dir/"
        print_success "Installed config: $config_dir/config-$component.json"
    fi
    
    # Copy identity directory for manager (from config/ to component-specific XDG config directory)
    # Note: Fixed bug - previous version incorrectly looked for identity files in bin/
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
    if [ -f "$scripts_dir/uninstall_linux.sh" ]; then
        mkdir -p "$install_dir/scripts"
        cp "$scripts_dir/uninstall_linux.sh" "$install_dir/scripts/"
        chmod +x "$install_dir/scripts/uninstall_linux.sh"
        print_success "Installed uninstall script: $install_dir/scripts/uninstall_linux.sh"
    fi
    
    # Store version information
    echo "$version" > "$install_dir/VERSION"
    
    print_success "$component installed to: $install_dir"
}

create_wrapper_script() {
    local install_dir=$1
    local component=$2
    
    mkdir -p "$BIN_SYMLINK_DIR"
    
    local wrapper_path="$BIN_SYMLINK_DIR/$component"
    local target_path="$install_dir/bin/$component"
    local lib_path="$install_dir/lib"
    
    # Create wrapper script that sets LD_LIBRARY_PATH
    cat > "$wrapper_path" << EOF
#!/bin/bash
export LD_LIBRARY_PATH="$lib_path:\$LD_LIBRARY_PATH"
exec "$target_path" "\$@"
EOF
    
    chmod +x "$wrapper_path"
    print_success "Created wrapper script: $wrapper_path"
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
    
    # Build Exec command with library path and config argument
    local config_dir="$CONFIG_BASE/task-message-$component"
    local config_path="$config_dir/config-$component.json"
    local lib_path="$install_dir/lib"
    local exec_cmd="env LD_LIBRARY_PATH=\\\"$lib_path\\\" $install_dir/bin/$component -c \\\"$config_path\\\""
    # Worker defaults to UI enabled
    if [ "$component" = "worker" ]; then
        exec_cmd="$exec_cmd"
    fi
    # Copy and update Exec path in desktop entry
    local installed_desktop="$DESKTOP_DIR/task-messenger-${component}.desktop"
    sed "s|Exec=.*|Exec=$exec_cmd|" "$desktop_file" > "$installed_desktop"
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
            print_error "     $0 --archive 'task-messenger-{component}-v1.0.0-linux-x86_64.tar.gz'"
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
        
        # Detect archive structure: task-message-{manager|worker}/
        extracted_dir=$(find "$temp_dir" -maxdepth 1 -type d \( -name "task-message-manager" -o -name "task-message-worker" \) 2>/dev/null | head -n 1)
        
        if [ -z "$extracted_dir" ]; then
            print_error "Unexpected archive structure. Expected task-message-manager/ or task-message-worker/ directory."
            rm -rf "$temp_dir"
            exit 1
        fi
        
        # Detect component from directory name
        local dir_name=$(basename "$extracted_dir")
        if [[ "$dir_name" == "task-message-manager" ]]; then
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
        INSTALL_DIR="$DEFAULT_INSTALL_BASE/task-message-$COMPONENT"
    fi
    
    local CONFIG_DIR="$CONFIG_BASE/task-message-$COMPONENT"
    
    print_info "=========================================="
    print_info "TaskMessenger $COMPONENT Installation"
    print_info "=========================================="
    print_info "Component:        $COMPONENT"
    print_info "Version:          $VERSION"
    print_info "Install location: $INSTALL_DIR"
    print_info "Config location:  $CONFIG_DIR"
    print_info "Symlink location: $BIN_SYMLINK_DIR/$COMPONENT"
    print_info "=========================================="
    echo ""
    
    # Check for existing installation
    if check_existing_installation "$INSTALL_DIR" "$COMPONENT"; then
        backup_configs "$CONFIG_DIR" "$COMPONENT"
    fi
    
    # Install component
    install_component "$COMPONENT" "$extracted_dir" "$INSTALL_DIR" "$VERSION"
    
    # Create wrapper script
    create_wrapper_script "$INSTALL_DIR" "$COMPONENT"
    
    # Install desktop entry
    install_desktop_entry "$COMPONENT" "$INSTALL_DIR"
    
    # Check PATH
    check_path
    
    local CONFIG_DIR="$CONFIG_BASE/task-message-$COMPONENT"
    
    echo ""
    print_success "=========================================="
    print_success "Installation completed successfully!"
    print_success "=========================================="
    print_info "You can now run: $COMPONENT"
    print_info "Or use the full path: $INSTALL_DIR/bin/$COMPONENT"
    print_info "Config file: $CONFIG_DIR/config-$COMPONENT.json"
    if [ "$COMPONENT" = "manager" ]; then
        print_info "Identity files: $CONFIG_DIR/vn-manager-identity/"
    fi
    echo ""
}

main "$@"
