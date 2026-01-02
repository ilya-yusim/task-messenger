#!/usr/bin/env bash
set -euo pipefail

# perf_record_all_threads.sh
# Usage: perf_record_all_threads.sh [options] <PID>
#
# Records perf samples for all threads of a given PID and produces per-thread
# flamegraphs using perf_per_thread_flames.sh.
#
# Options:
#   -d, --duration <seconds>   Recording duration (default: 15)
#   -F, --freq <hz>            Sampling frequency (default: 99)
#   --dwarf                    Use DWARF unwinding (--call-graph dwarf)
#   --out, --out-dir <path>    Output directory for perf.data and svgs
#   --flame-dir <path>         Path to FlameGraph scripts (default: /tmp/FlameGraph)
#   --flames                   Generate per-thread flamegraphs (disabled by default)
#   --kallsyms-fallback        Attempt to create kallsyms fallback if /proc/kallsyms is masked (disabled by default)
#
# Environment:
#   TIDS (optional)             Comma-separated list of TIDs to record (overrides -p)

PID=""
DURATION=15
FREQ=99
OUT_DIR=/tmp/perf_thread_svgs
FLAME_DIR=/tmp/FlameGraph
PERF_BIN=${PERF_BIN:-perf}
DWARF=0
GENERATE_FLAMES=0
GENERATE_KALLSYMS_FALLBACK=0

usage() {
  cat <<'EOUSAGE'
Usage: perf_record_all_threads.sh [options] <PID>

Options:
  -d, --duration <seconds>   Recording duration (default: 15)
  -F, --freq <hz>            Sampling frequency (default: 99)
  --dwarf                    Use DWARF unwinding (--call-graph dwarf)
  --out, --out-dir <path>    Output directory for perf.data and svgs
  --flame-dir <path>         Path to FlameGraph scripts (default: /tmp/FlameGraph)
  --flames                   Generate per-thread flamegraphs (disabled by default)
  --kallsyms-fallback        Attempt to create kallsyms fallback if /proc/kallsyms is masked (disabled by default)
  --help                     Show this help

Environment:
  TIDS (optional)             Comma-separated list of TIDs to record (overrides -p)

EOUSAGE
}

# Parse options
while [[ $# -gt 0 ]]; do
  case "$1" in
    -d|--duration)
      DURATION="$2"; shift 2;;
    -F|--freq)
      FREQ="$2"; shift 2;;
    --dwarf)
      DWARF=1; shift;;
    --out|--out-dir)
      OUT_DIR="$2"; shift 2;;
    --flame-dir)
      FLAME_DIR="$2"; shift 2;;
    --flames)
      GENERATE_FLAMES=1; shift;;
    --kallsyms-fallback)
      GENERATE_KALLSYMS_FALLBACK=1; shift;;
    --help)
      usage; exit 0;;
    --)
      shift; break;;
    -*)
      echo "Unknown option: $1" >&2; usage; exit 2;;
    *)
      if [[ -z "$PID" ]]; then
        PID="$1"
      else
        echo "Ignoring extra positional argument: $1" >&2
      fi
      shift;;
  esac
done

if [[ -z "$PID" ]]; then
  echo "Error: PID is required" >&2
  usage
  exit 2
fi

# If PID is not numeric, treat it as a process name and resolve to a PID
if ! [[ "$PID" =~ ^[0-9]+$ ]]; then
  proc_name="$PID"
  echo "Treating '$proc_name' as a process name; resolving to PID..."
  if command -v pgrep >/dev/null 2>&1; then
    PID=$(pgrep -f "$proc_name" | head -n1 || true)
  else
    # fallback: search ps output (case-insensitive) and pick the first match
    PID=$(ps -eo pid,cmd | awk -v name="$proc_name" 'BEGIN{IGNORECASE=1} $0 ~ name {print $1; exit}')
  fi
  if [[ -z "$PID" ]]; then
    echo "Could not find a process matching name '$proc_name'" >&2
    exit 3
  fi
  echo "Resolved '$proc_name' -> PID $PID"
fi


# Put outputs into a PID-named subdirectory under OUT_DIR
OUT_DIR="${OUT_DIR%/}/$PID"
PERF_DATA=${OUT_DIR}/perf.data

if [[ ! -d "/proc/$PID" ]]; then
  echo "PID $PID not found" >&2
  exit 3
fi

mkdir -p "$OUT_DIR"

# Check perf exists
if ! command -v "$PERF_BIN" >/dev/null 2>&1; then
  echo "Error: '$PERF_BIN' not found in PATH. You need to install Linux 'perf' tools on the host or in the container." >&2
  echo "On Debian/Ubuntu try: sudo apt update && sudo apt install linux-tools-$(uname -r) linux-tools-common" >&2
  echo "If running in a container, prefer running perf on the host and copying the resulting perf.data into this workspace." >&2
  exit 4
fi

# Decide call-graph options
if [[ "$DWARF" -eq 1 ]]; then
  CALL_OPTS=("-F" "$FREQ" "--call-graph" "dwarf")
else
  CALL_OPTS=("-F" "$FREQ" "-g")
fi

# If TIDS env var is set, use it (comma separated). Otherwise use -p <PID> to
# capture all threads for the process (including threads created during the
# recording). Using -p is usually what you want.
if [[ -n "${TIDS:-}" ]]; then
  echo "Using explicit TIDs from TIDS env: $TIDS"
  IFS=',' read -r -a tids_array <<< "$TIDS"
  tids_args=()
  for t in "${tids_array[@]}"; do
    tids_args+=("-t" "$t")
  done

  echo "Running: sudo $PERF_BIN record ${CALL_OPTS[*]} -o $PERF_DATA ${tids_args[*]} -- sleep $DURATION"
  sudo "$PERF_BIN" record "${CALL_OPTS[@]}" -o "$PERF_DATA" "${tids_array[@]/#/ -t }" -- sleep "$DURATION"
else
  echo "Recording for PID $PID (all threads) for $DURATION seconds -> $PERF_DATA"
  echo "Running: sudo $PERF_BIN record ${CALL_OPTS[*]} -p $PID -o $PERF_DATA -- sleep $DURATION"
  sudo "$PERF_BIN" record "${CALL_OPTS[@]}" -p "$PID" -o "$PERF_DATA" -- sleep "$DURATION"
fi

# Check we have data
if [[ ! -f "$PERF_DATA" ]]; then
  echo "perf data not written: $PERF_DATA" >&2
  exit 4
fi

if [[ "$GENERATE_FLAMES" -eq 1 ]]; then
  # Call the existing per-thread flamegraph generator script from this repo
  SCRIPT_DIR=$(dirname "${BASH_SOURCE[0]}")
  PERF_FLAMES="$SCRIPT_DIR/perf_per_thread_flames.sh"
  if [[ ! -x "$PERF_FLAMES" ]]; then
    echo "perf_per_thread_flames.sh not found or not executable at $PERF_FLAMES" >&2
    exit 5
  fi
  # Pass FlameGraph dir as optional fourth argument if provided
  "$PERF_FLAMES" "$OUT_DIR" "$PERF_DATA" "$FLAME_DIR"
fi

# If /proc/kallsyms exists but appears masked (many zero addresses), create a
# fallback symbol file from System.map so downstream tools can use kernel
# symbols. This is conservative and non-fatal: if no System.map is found we
# simply continue.
mask_check() {
  # Look for a few common kernel symbols and see if their addresses are zero
  # When kptr_restrict hides pointers you'll often see lines with 000...0
  if [[ -r /proc/kallsyms ]]; then
    # Check a sample of symbols; if most are zero, consider it masked.
    zeros=0
    total=0
    for s in start_kernel sys_call_table do_exit; do
      # grep for the symbol and extract the address field
      addr=$(grep -m1 "\b$s\b" /proc/kallsyms | awk '{print $1}' || true)
      if [[ -z "$addr" ]]; then
        continue
      fi
      total=$((total+1))
      if [[ "$addr" =~ ^0+$ ]]; then
        zeros=$((zeros+1))
      fi
    done
    if [[ $total -gt 0 && $zeros -ge $(( (total*2)/3 )) ]]; then
      return 0  # masked
    fi
  fi
  return 1
}

if [[ "$GENERATE_KALLSYMS_FALLBACK" -eq 1 ]] && mask_check; then
  echo "/proc/kallsyms appears to have masked/zeroed addresses. Searching for System.map fallback..."
  # Try common locations for System.map or vmlinux debug file
  SYS_MAPS=("/boot/System.map-$(uname -r)" "/lib/modules/$(uname -r)/build/System.map" "/usr/lib/debug/boot/vmlinux-$(uname -r)")
  for f in "${SYS_MAPS[@]}"; do
    if [[ -r "$f" ]]; then
      cp "$f" "$OUT_DIR/kallsyms.fallback"
      echo "Copied $f -> $OUT_DIR/kallsyms.fallback (use this if /proc/kallsyms is masked)"
      break
    fi
  done
  if [[ ! -f "$OUT_DIR/kallsyms.fallback" ]]; then
    echo "No System.map/vmlinux found in common locations; profiler may not resolve kernel symbols." >&2
  fi
fi

# Determine which user should own the output files. If the script was run with
# sudo, prefer SUDO_USER. Otherwise use the invoking user (real UID).
finalize() {
  # If OUT_DIR wasn't created or is empty, nothing to do
  if [[ -z "${OUT_DIR:-}" || ! -e "$OUT_DIR" ]]; then
    return
  fi

  # Prefer SUDO_USER when available (script run via sudo). Fallback to the
  # real user id/name.
  if [[ -n "${SUDO_USER:-}" ]]; then
    target_user="$SUDO_USER"
  else
    # $USER may not reflect the original user in some contexts; use id -un
    target_user=$(id -un 2>/dev/null || printf "%s" "$USER")
  fi

  # Attempt to chown the output directory and its contents back to the user.
  # Use sudo when necessary (e.g., script was run as root). If sudo isn't
  # available or fails, print a warning but don't treat it as fatal since the
  # main work already completed.
  if chown --help >/dev/null 2>&1; then
    if chown -R "$target_user":"$target_user" "$OUT_DIR" >/dev/null 2>&1; then
      echo "Changed ownership of $OUT_DIR to $target_user"
      return
    fi
  fi

  # Try using sudo if direct chown failed and sudo is available
  if command -v sudo >/dev/null 2>&1; then
    if sudo chown -R "$target_user":"$target_user" "$OUT_DIR" >/dev/null 2>&1; then
      echo "Changed ownership of $OUT_DIR to $target_user (via sudo)"
      return
    fi
  fi

  echo "Warning: failed to change ownership of $OUT_DIR to $target_user" >&2
}

trap finalize EXIT

if [[ "$GENERATE_FLAMES" -eq 1 ]]; then
  echo "Per-thread flamegraphs written to $OUT_DIR/svgs"
else
  echo "perf.data written to $PERF_DATA"
fi
