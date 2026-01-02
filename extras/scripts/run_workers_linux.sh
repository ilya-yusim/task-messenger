#!/usr/bin/env bash
# Minimal Linux analogue of run_workers.ps1
# Starts COUNT instances of worker in blocking mode (each backgrounded).
# Instances are not tracked for later shutdown; they continue after script exits.
# Usage: ./run_workers_linux.sh COUNT
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 COUNT" >&2
  exit 1
fi
COUNT="$1"
if ! [[ "$COUNT" =~ ^[0-9]+$ ]]; then
  echo "Count must be a positive integer" >&2
  exit 1
fi
if (( COUNT < 1 )); then
  echo "Count must be 1 or greater" >&2
  exit 1
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
exe="$(realpath "$script_dir/../builddir-worker/worker/worker")"

if [[ ! -x "$exe" ]]; then
  echo "Executable not found or not executable: $exe" >&2
  echo "Build it first (Meson Build worker)." >&2
  exit 1
fi

echo "Starting ${COUNT} instances of: ${exe}"
pids=()

for i in $(seq 1 "$COUNT"); do
  "$exe" -c config-worker.json --mode blocking &
  pid=$!
  pids+=("$pid")
  echo "Started instance ${i}/${COUNT} with PID ${pid}"
  disown "$pid" 2>/dev/null || true
  # Slight stagger
  sleep 0.05
done

echo "Launched $COUNT instance(s). PIDs: ${pids[*]}"
echo "They will keep running in background."