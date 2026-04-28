#!/usr/bin/env bash
# Stop tm-worker instances spawned by start_workers_local.sh.
#
# Usage:
#   stop_workers_local.sh -r RUN_ID [-g GRACE_SECONDS]
#   stop_workers_local.sh -r latest
set -euo pipefail

usage() {
    cat >&2 <<'EOF'
Usage: stop_workers_local.sh -r RUN_ID [-g GRACE_SECONDS]

  -r RUN_ID         Run ID (e.g. 20260427-153012) or "latest".
  -g GRACE_SECONDS  Seconds to wait for graceful exit before SIGKILL. Default 5.
EOF
    exit 2
}

run=""
grace=5
while [[ $# -gt 0 ]]; do
    case "$1" in
        -r) run="$2"; shift 2 ;;
        -g) grace="$2"; shift 2 ;;
        -h|--help) usage ;;
        *) echo "Unknown arg: $1" >&2; usage ;;
    esac
done

[[ -n "$run" ]] || usage

cache_root="${XDG_CACHE_HOME:-$HOME/.cache}/tm-worker-farm/runs"

if [[ "$run" == "latest" ]]; then
    if [[ ! -f "$cache_root/latest.txt" ]]; then
        echo "No latest run pointer at $cache_root/latest.txt" >&2
        exit 1
    fi
    run="$(<"$cache_root/latest.txt")"
fi

run_dir="$cache_root/$run"
manifest="$run_dir/manifest.json"
if [[ ! -f "$manifest" ]]; then
    echo "Manifest not found: $manifest" >&2
    exit 1
fi

# Extract pids from manifest. Prefer python3 if present (handles JSON robustly);
# otherwise fall back to grep.
pids=()
if command -v python3 >/dev/null 2>&1; then
    while IFS= read -r p; do
        [[ -n "$p" ]] && pids+=( "$p" )
    done < <(python3 -c '
import json, sys
m = json.load(open(sys.argv[1]))
for w in m["workers"]:
    print(w["pid"])
' "$manifest")
else
    # crude fallback: grep "pid":NNN
    while IFS= read -r p; do
        pids+=( "$p" )
    done < <(grep -oE '"pid"[[:space:]]*:[[:space:]]*[0-9]+' "$manifest" | grep -oE '[0-9]+')
fi

if (( ${#pids[@]} == 0 )); then
    echo "No worker pids found in $manifest"
    exit 0
fi

echo "Stopping ${#pids[@]} worker(s) from run $run..."
for pid in "${pids[@]}"; do
    if kill -0 "$pid" 2>/dev/null; then
        echo "  SIGTERM $pid"
        kill -TERM "$pid" 2>/dev/null || true
    else
        echo "  PID $pid already gone"
    fi
done

# Wait up to GRACE seconds for clean exits.
for ((i=0; i<grace*4; i++)); do
    alive=0
    for pid in "${pids[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then alive=1; break; fi
    done
    (( alive == 0 )) && break
    sleep 0.25
done

# Escalate.
for pid in "${pids[@]}"; do
    if kill -0 "$pid" 2>/dev/null; then
        echo "  PID $pid still alive after ${grace}s; SIGKILL"
        kill -KILL "$pid" 2>/dev/null || true
    fi
done

echo
echo "Run $run stopped."
echo "Logs and manifest preserved at: $run_dir"
