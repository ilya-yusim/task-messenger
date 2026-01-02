#!/usr/bin/env bash
set -eu

# Manage a headless Hotspot session served over VNC.
# Usage: scripts/start_hotspot_vnc.sh start|stop|status [--password <file>]

BASEDIR=$(cd "$(dirname "$0")/.." && pwd)
PID_DIR=/tmp/hotspot_vnc
mkdir -p "$PID_DIR"

XVFB_DISPLAY=:99
XVFB_SCREEN=0
XVFB_RES="1280x800x24"
VNC_PORT=5901

HOTSPOT_CMD="hotspot"
X11VNC_CMD="x11vnc"

usage(){
  cat <<EOF
Usage: $0 start|stop|status [--password /path/to/passfile]

start  - start Xvfb, hotspot and x11vnc
stop   - stop the running services
status - print PIDs and log locations

Logs and PID files live under $PID_DIR
EOF
}

start(){
  PASSFILE=""
  # use safe expansion so set -u doesn't fail when no args provided
  if [[ "${1:-}" == "--password" ]]; then
    if [[ -z "${2:-}" ]]; then
      echo "Missing password file after --password" >&2
      exit 2
    fi
    PASSFILE="${2:-}"
  fi

  echo "Starting Xvfb on $XVFB_DISPLAY..."
  # If a stale lock exists for the display and no process owns it, remove it
  LOCK_FILE="/tmp/.X${XVFB_DISPLAY#:}-lock"
  SOCKET_FILE="/tmp/.X11-unix/X${XVFB_DISPLAY#:}"
  if [[ -f "$LOCK_FILE" ]]; then
    lockpid=$(cat "$LOCK_FILE" 2>/dev/null || echo "")
    if [[ -n "$lockpid" && $(ps -p "$lockpid" -o pid= 2>/dev/null || true) ]]; then
      echo "X display $XVFB_DISPLAY already in use by PID $lockpid" >&2
      echo "If you expect to reuse this display, stop that process first or run status." >&2
      return 1
    else
      echo "Removing stale lock/socket for $XVFB_DISPLAY"
      rm -f "$LOCK_FILE" "$SOCKET_FILE" || true
    fi
  fi

  Xvfb "$XVFB_DISPLAY" -screen "$XVFB_SCREEN" "$XVFB_RES" &> "$PID_DIR/xvfb.log" &
  echo $! > "$PID_DIR/xvfb.pid"
  sleep 0.3

  export DISPLAY="$XVFB_DISPLAY"
  echo "Starting hotspot (background)..."
  # run hotspot directly on the display we just started
  if ! command -v "$HOTSPOT_CMD" >/dev/null 2>&1; then
    echo "hotspot not found in PATH: $HOTSPOT_CMD" >&2
  fi
  "$HOTSPOT_CMD" &> "$PID_DIR/hotspot.log" &
  echo $! > "$PID_DIR/hotspot.pid"
  sleep 0.3

  echo "Starting x11vnc on display $XVFB_DISPLAY port $VNC_PORT..."
  if [[ -n "$PASSFILE" ]]; then
    x11vnc -display "$XVFB_DISPLAY" -rfbauth "$PASSFILE" -forever -shared -rfbport "$VNC_PORT" &> "$PID_DIR/x11vnc.log" &
  else
    x11vnc -display "$XVFB_DISPLAY" -nopw -forever -shared -rfbport "$VNC_PORT" &> "$PID_DIR/x11vnc.log" &
  fi
  echo $! > "$PID_DIR/x11vnc.pid"

  echo "Started. Logs: $PID_DIR/*.log" 
}

stop(){
  echo "Stopping hotspot/vnc/xvfb..."
  for p in x11vnc hotspot Xvfb; do
    if [[ -f "$PID_DIR/${p,,}.pid" ]]; then
      pid=$(cat "$PID_DIR/${p,,}.pid")
      echo "Killing $p (PID $pid)"
      kill "$pid" 2>/dev/null || true
      rm -f "$PID_DIR/${p,,}.pid"
    fi
  done
  echo "Stopped." 
}

status(){
  echo "PID directory: $PID_DIR"
  for f in "$PID_DIR"/*.pid; do
    [[ -e "$f" ]] || continue
    name=$(basename "$f" .pid)
    pid=$(cat "$f" 2>/dev/null || echo "")
    printf "%-10s PID=%-8s " "$name" "$pid"
    if [[ -n "$pid" && -d /proc/$pid ]]; then
      ps -p $pid -o pid,cmd --no-headers
    else
      echo "(not running)"
    fi
  done
  echo "Logs:"
  ls -la "$PID_DIR"/*.log 2>/dev/null || true
}

if [[ $# -lt 1 ]]; then
  usage
  exit 1
fi

cmd="$1"
shift || true
case "$cmd" in
  start)
    start "$@"
    ;;
  stop)
    stop
    ;;
  status)
    status
    ;;
  *)
    usage
    exit 1
    ;;
esac
