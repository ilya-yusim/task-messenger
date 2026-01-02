#!/usr/bin/env bash
set -euo pipefail

# Install Xvfb, x11vnc and Hotspot (KDAB hotspot) on Debian/Ubuntu codespaces.
# Usage: scripts/install_hotspot_deps.sh [--yes]

YES=0
if [[ "${1:-}" == "--yes" ]]; then
  YES=1
fi

log(){
  printf "[install_hotspot_deps] %s\n" "$*"
}

require_sudo(){
  if [[ $EUID -ne 0 ]]; then
    if command -v sudo >/dev/null 2>&1; then
      SUDO=sudo
    else
      log "This script requires root privileges (and sudo). Please run as root or install sudo." >&2
      exit 1
    fi
  else
    SUDO=""
  fi
}

detect_pkg_manager(){
  if command -v apt-get >/dev/null 2>&1; then
    echo "apt"
  elif command -v dnf >/dev/null 2>&1; then
    echo "dnf"
  else
    echo "unknown"
  fi
}

install_apt_packages(){
  ${SUDO} apt-get update -y
  # install common dependencies
  ${SUDO} apt-get install -y --no-install-recommends x11vnc xvfb wget curl ca-certificates
}

install_hotspot_via_apt(){
  # try apt first
  if ${SUDO} apt-get install -y hotspot >/dev/null 2>&1; then
    log "Installed hotspot via apt"
    return 0
  fi
  return 1
}

install_hotspot_appimage(){
  REPO="KDAB/hotspot"
  API_URL="https://api.github.com/repos/${REPO}/releases/latest"

  log "Querying GitHub releases for ${REPO}..."
  if ! command -v curl >/dev/null 2>&1; then
    log "curl not found; can't query GitHub releases" >&2
    return 1
  fi

  # try to find an AppImage asset first, then fallback to any linux/x86_64 asset
  body=$(curl -sSfL "${API_URL}") || {
    log "Failed to query GitHub API" >&2
    return 1
  }

  url=$(printf "%s" "$body" | grep -i "browser_download_url" | cut -d '"' -f4 | grep -i appimage | head -n1 || true)
  if [[ -z "$url" ]]; then
    url=$(printf "%s" "$body" | grep -i "browser_download_url" | cut -d '"' -f4 | grep -i linux | grep -i x86_64 | head -n1 || true)
  fi
  if [[ -z "$url" ]]; then
    url=$(printf "%s" "$body" | grep -i "browser_download_url" | cut -d '"' -f4 | head -n1 || true)
  fi

  if [[ -z "$url" ]]; then
    log "Could not find a downloadable Hotspot asset in GitHub releases" >&2
    return 1
  fi

  log "Found hotspot asset: $url"
  tmpf=$(mktemp -u /tmp/hotspot_XXXX)
  tmpf="${tmpf}.$(basename "$url")"

  log "Downloading to $tmpf..."
  curl -L --fail -o "$tmpf" "$url"
  chmod +x "$tmpf"

  # install under /opt/hotspot and symlink wrapper to /usr/local/bin/hotspot
  ${SUDO} mkdir -p /opt/hotspot
  dest=/opt/hotspot/$(basename "$tmpf")
  ${SUDO} mv "$tmpf" "$dest"

  # create a small wrapper script
  wrapper=/usr/local/bin/hotspot
  ${SUDO} bash -c "cat > ${wrapper} <<'SH'
#!/usr/bin/env bash
exec /opt/hotspot/$(basename "$dest") "$@"
SH"
  ${SUDO} chmod +x "$wrapper"

  log "Installed hotspot wrapper to $wrapper (executes the AppImage in /opt/hotspot)."
  return 0
}

main(){
  require_sudo

  log "Detected package manager: $(detect_pkg_manager)"

  if ! command -v x11vnc >/dev/null 2>&1 || ! command -v Xvfb >/dev/null 2>&1; then
    if [[ "$(detect_pkg_manager)" == "apt" ]]; then
      log "Installing x11vnc and Xvfb (apt)..."
      if [[ $YES -ne 1 ]]; then
        printf "About to run: sudo apt-get update && apt-get install -y x11vnc xvfb\nProceed? [y/N]: "
        read -r ans || true
        if [[ ! "$ans" =~ ^[Yy] ]]; then
          log "User cancelled"
          exit 1
        fi
      fi
      install_apt_packages
    else
      log "Unsupported package manager. Please manually install x11vnc and Xvfb."
      exit 1
    fi
  else
    log "x11vnc and Xvfb already installed"
  fi

  # Install hotspot
  if command -v hotspot >/dev/null 2>&1; then
    log "hotspot already in PATH"
    return 0
  fi

  if [[ "$(detect_pkg_manager)" == "apt" ]]; then
    log "Trying to install hotspot via apt..."
    if install_hotspot_via_apt; then
      return 0
    fi
  fi

  log "Falling back to downloading Hotspot AppImage from GitHub releases..."
  if install_hotspot_appimage; then
    log "Hotspot installed successfully"
    return 0
  fi

  log "Failed to install hotspot automatically. Please install it manually."
  log "On Debian/Ubuntu you can try: sudo apt-get install hotspot  # if available"
  log "Or download KDAB Hotspot from: https://github.com/KDAB/hotspot/releases"
  exit 1
}

main "$@"
