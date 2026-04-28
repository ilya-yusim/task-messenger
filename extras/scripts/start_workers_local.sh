#!/usr/bin/env bash
# Start N tm-worker instances locally, recording a run manifest so they can
# later be stopped via stop_workers_local.sh.
#
# Each worker runs headless (--noui) and is backgrounded with its own log
# file. Manifest + logs are placed under:
#
#     ${XDG_CACHE_HOME:-$HOME/.cache}/tm-worker-farm/runs/<run-id>/
#
# Usage:
#   start_workers_local.sh -n COUNT [-b WORKER_BIN] [-c CONFIG] [-- EXTRA_ARGS...]
#
# Examples:
#   start_workers_local.sh -n 3
#   start_workers_local.sh -n 2 -- --mode async
set -euo pipefail

usage() {
    cat >&2 <<'EOF'
Usage: start_workers_local.sh -n COUNT [-b WORKER_BIN] [-c CONFIG] [-- EXTRA_ARGS...]

  -n COUNT       Number of worker instances to start (required, >=1).
  -b WORKER_BIN  Path to tm-worker. Default: <repo>/builddir/worker/tm-worker.
  -c CONFIG      Path to config-worker.json. Default: <repo>/config/config-worker.json.
  --             Everything after -- is forwarded to every worker as extra args.
EOF
    exit 2
}

count=""
worker_bin=""
config=""

# parse leading flags; stop at "--"
while [[ $# -gt 0 ]]; do
    case "$1" in
        -n) count="$2"; shift 2 ;;
        -b) worker_bin="$2"; shift 2 ;;
        -c) config="$2"; shift 2 ;;
        --) shift; break ;;
        -h|--help) usage ;;
        *) echo "Unknown arg: $1" >&2; usage ;;
    esac
done
extra_args=( "$@" )

if [[ -z "$count" ]]; then usage; fi
if ! [[ "$count" =~ ^[0-9]+$ ]] || (( count < 1 )); then
    echo "COUNT must be a positive integer" >&2
    exit 2
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/../.." && pwd)"

: "${worker_bin:=$repo_root/builddir/worker/tm-worker}"
: "${config:=$repo_root/config/config-worker.json}"

# Expand a leading ~/ since callers (including the codespace wrapper) may
# pass quoted paths where bash skips tilde expansion.
[[ "$worker_bin" == "~/"* ]] && worker_bin="$HOME/${worker_bin#\~/}"
[[ "$config"     == "~/"* ]] && config="$HOME/${config#\~/}"

# Resolve worker_bin: accept either a path or a bare command on PATH.
if [[ "$worker_bin" == */* ]]; then
    if [[ ! -x "$worker_bin" ]]; then
        echo "Worker executable not found or not executable: $worker_bin" >&2
        exit 1
    fi
else
    resolved="$(command -v "$worker_bin" || true)"
    if [[ -z "$resolved" ]]; then
        echo "Worker command '$worker_bin' not found on PATH." >&2
        echo "Install via install_tm_worker_release.sh or pass -b /path/to/tm-worker." >&2
        exit 1
    fi
    worker_bin="$resolved"
fi
if [[ ! -f "$config" ]]; then
    echo "Worker config not found: $config" >&2
    exit 1
fi

cache_root="${XDG_CACHE_HOME:-$HOME/.cache}/tm-worker-farm/runs"
run_id="$(date -u +%Y%m%d-%H%M%S)"
run_dir="$cache_root/$run_id"
mkdir -p "$run_dir"

base_args=( -c "$config" --mode blocking --noui "${extra_args[@]}" )

# Build manifest workers array as we go.
worker_entries=()
for ((i=1; i<=count; i++)); do
    id=$(printf '%02d' "$i")
    log="$run_dir/worker-$id.log"
    pidfile="$run_dir/worker-$id.pid"

    echo "[$id/$count] Starting: $worker_bin ${base_args[*]}"
    # Use setsid so the worker survives this shell and so we can signal the
    # whole process group later if needed. Fall back if setsid is missing.
    if command -v setsid >/dev/null 2>&1; then
        setsid "$worker_bin" "${base_args[@]}" >"$log" 2>&1 < /dev/null &
    else
        "$worker_bin" "${base_args[@]}" >"$log" 2>&1 < /dev/null &
    fi
    pid=$!
    echo "$pid" > "$pidfile"
    disown "$pid" 2>/dev/null || true

    worker_entries+=( "$(printf '{"id":"%s","pid":%d,"log":"%s","pidfile":"%s"}' \
        "$id" "$pid" "$log" "$pidfile")" )
done

# Compose manifest.json (no jq dependency).
started_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
host_name="$(hostname)"
# Render args as a JSON array.
args_json="["
first=1
for a in "${base_args[@]}"; do
    # naive escape: replace " and \
    esc=${a//\\/\\\\}
    esc=${esc//\"/\\\"}
    if (( first )); then first=0; else args_json+=","; fi
    args_json+="\"$esc\""
done
args_json+="]"

workers_json="["
first=1
for w in "${worker_entries[@]}"; do
    if (( first )); then first=0; else workers_json+=","; fi
    workers_json+="$w"
done
workers_json+="]"

cat > "$run_dir/manifest.json" <<EOF
{
  "run_id": "$run_id",
  "started_at": "$started_at",
  "host": "local",
  "hostname": "$host_name",
  "os": "$(uname -s | tr '[:upper:]' '[:lower:]')",
  "base_dir": "$run_dir",
  "worker_bin": "$worker_bin",
  "config": "$config",
  "args": $args_json,
  "workers": $workers_json
}
EOF

# latest pointer
echo "$run_id" > "$cache_root/latest.txt" || true

echo
echo "Run ID:   $run_id"
echo "Run dir:  $run_dir"
echo "Manifest: $run_dir/manifest.json"
echo
echo "To stop:  $script_dir/stop_workers_local.sh -r $run_id"
