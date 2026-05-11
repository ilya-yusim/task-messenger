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

if [[ $# -lt 2 || "$1" != "-f" ]]; then
    echo "Usage: install_tm_worker_release.sh -f LOCAL_RUN" >&2
    exit 2
fi

local_file="$2"
local_file="${local_file//\$\{HOME\}/$HOME}"
[[ -s "$local_file" ]] || { echo "[install] -f file not found or empty: $local_file" >&2; exit 1; }
chmod +x "$local_file"
"$local_file" --accept -- --yes
bin="$HOME/.local/bin/tm-worker"
[[ -x "$bin" ]] || { echo "[install] expected symlink $bin not found after install" >&2; exit 1; }
echo "[install] done."
