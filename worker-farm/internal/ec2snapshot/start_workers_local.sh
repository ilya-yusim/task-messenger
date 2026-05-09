#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${HOME:-}" ]]; then
    home_from_passwd="$(getent passwd "$(id -un)" 2>/dev/null | cut -d: -f6 || true)"
    if [[ -n "$home_from_passwd" ]]; then
        export HOME="$home_from_passwd"
    else
        export HOME="/root"
    fi
fi

case ":${PATH:-}:" in
    *":$HOME/.local/bin:"*) ;;
    *) export PATH="$HOME/.local/bin:${PATH:-}" ;;
esac

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

count=""; worker_bin=""; config=""
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
if [[ -n "${BASH_SOURCE[0]:-}" ]]; then script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"; repo_root="$(cd -- "$script_dir/../.." && pwd)"; else script_dir="$PWD"; repo_root="$PWD"; fi
: "${worker_bin:=$repo_root/builddir/worker/tm-worker}"
: "${config:=$repo_root/config/config-worker.json}"

# Expand leading ~/ for quoted values forwarded through SSM.
[[ "$worker_bin" == "~/"* ]] && worker_bin="$HOME/${worker_bin#\~/}"
[[ "$config" == "~/"* ]] && config="$HOME/${config#\~/}"

# Resolve worker_bin as either absolute/relative path or PATH command.
if [[ "$worker_bin" == */* ]]; then
    if [[ ! -x "$worker_bin" ]]; then
        echo "Worker executable not found or not executable: $worker_bin" >&2
        exit 1
    fi
else
    resolved="$(command -v "$worker_bin" || true)"
    if [[ -z "$resolved" ]]; then
        echo "Worker command '$worker_bin' not found on PATH." >&2
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
worker_entries=()
for ((i=1; i<=count; i++)); do
    id=$(printf '%02d' "$i")
    log="$run_dir/worker-$id.log"
    pidfile="$run_dir/worker-$id.pid"
    "$worker_bin" "${base_args[@]}" >"$log" 2>&1 < /dev/null &
    pid=$!
    echo "$pid" > "$pidfile"
    disown "$pid" 2>/dev/null || true
    worker_entries+=( "$(printf '{\"id\":\"%s\",\"pid\":%d,\"log\":\"%s\",\"pidfile\":\"%s\"}' "$id" "$pid" "$log" "$pidfile")" )
done
started_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
host_name="$(hostname)"
args_json="["; first=1; for a in "${base_args[@]}"; do esc=${a//\\/\\\\}; esc=${esc//\"/\\\"}; if (( first )); then first=0; else args_json+=","; fi; args_json+="\"$esc\""; done; args_json+="]"
workers_json="["; first=1; for w in "${worker_entries[@]}"; do if (( first )); then first=0; else workers_json+=","; fi; workers_json+="$w"; done; workers_json+="]"
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
echo "$run_id" > "$cache_root/latest.txt" || true
echo; echo "Run ID:   $run_id"; echo "Run dir:  $run_dir"; echo "Manifest: $run_dir/manifest.json"; echo; echo "To stop:  $script_dir/stop_workers_local.sh -r $run_id"
