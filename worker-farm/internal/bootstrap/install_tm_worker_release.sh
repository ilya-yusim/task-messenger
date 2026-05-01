#!/usr/bin/env bash
# Install tm-worker on a Linux host from a published GitHub release.
#
# Designed to be self-contained: runs inside a GitHub Codespace, a Cloud
# VM, or any Linux box. Downloads the makeself .run installer for the
# requested release tag and executes it with --accept, which installs to:
#
#     ~/.local/share/task-messenger/tm-worker/
#     ~/.local/bin/tm-worker            (symlink)
#
# Usage:
#   install_tm_worker_release.sh [-t TAG] [-r OWNER/REPO] [-f LOCAL_RUN] [--keep]
#
#   -t TAG          Release tag, e.g. v0.4.2. Default: "latest" (resolves
#                   to the most recent non-draft release for the repo).
#   -r OWNER/REPO   Override repo. Default: ilya-yusim/task-messenger,
#                   or the value of $TM_REPO if set.
#   -f LOCAL_RUN    Skip download; install the given pre-fetched .run file.
#                   Useful when the host can't reach a draft release (no
#                   auth) but the caller staged the asset via scp.
#   --keep          Don't delete the temporary download directory.
#   -h, --help      Show this help.
set -euo pipefail

DEFAULT_REPO="${TM_REPO:-ilya-yusim/task-messenger}"

usage() {
    sed -n '2,/^set -euo pipefail$/p' "$0" | sed 's/^# \{0,1\}//' | head -n 30
    exit 2
}

tag="latest"
repo="$DEFAULT_REPO"
keep=0
local_file=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        -t) tag="$2"; shift 2 ;;
        -r) repo="$2"; shift 2 ;;
        -f) local_file="$2"; shift 2 ;;
        --keep) keep=1; shift ;;
        -h|--help) usage ;;
        *) echo "Unknown arg: $1" >&2; usage ;;
    esac
done

# Detect arch — releases publish only x86_64 today.
arch="$(uname -m)"
case "$arch" in
    x86_64|amd64) arch_tag="x86_64" ;;
    *)
        echo "Unsupported arch '$arch'. Releases publish only x86_64 binaries today." >&2
        exit 1
        ;;
esac

tmp="$(mktemp -d -t tm-worker-install.XXXXXX)"
trap '[[ $keep -eq 0 ]] && rm -rf "$tmp"' EXIT

if [[ -n "$local_file" ]]; then
    if [[ ! -s "$local_file" ]]; then
        echo "[install] -f file not found or empty: $local_file" >&2
        exit 1
    fi
    asset_path="$local_file"
    echo "[install] using pre-downloaded asset: $asset_path"
else
    # Resolve "latest" to a real tag.
    have_gh=0
    if command -v gh >/dev/null 2>&1 && gh auth status >/dev/null 2>&1; then
        have_gh=1
    fi

    resolve_latest() {
        if (( have_gh )); then
            gh release view --repo "$repo" --json tagName --jq .tagName 2>/dev/null && return 0
        fi
        local url loc
        url="https://github.com/$repo/releases/latest"
        loc="$(curl -fsSLI -o /dev/null -w '%{url_effective}' "$url")"
        echo "${loc##*/tag/}"
    }

    if [[ "$tag" == "latest" ]]; then
        tag="$(resolve_latest)"
        if [[ -z "$tag" ]]; then
            echo "Could not resolve latest release for $repo" >&2
            exit 1
        fi
    fi

    echo "[install] repo=$repo tag=$tag arch=$arch_tag"

    # Discover the actual worker .run asset name. Filenames embed the meson
    # project version (e.g. v0.0.2), not the release tag, so we can't just
    # construct it from $tag.
    pattern="tm-worker-v.*-linux-${arch_tag}\\.run"
    asset=""
    if (( have_gh )); then
        asset="$(gh release view --repo "$repo" "$tag" --json assets \
            --jq ".assets[].name | select(test(\"^${pattern}$\"))" 2>/dev/null \
            | head -n1 || true)"
    fi
    if [[ -z "$asset" ]]; then
        # Public releases only; drafts are invisible without auth.
        asset="$(curl -fsSL "https://api.github.com/repos/$repo/releases/tags/$tag" 2>/dev/null \
            | grep -oE "\"name\": *\"${pattern}\"" \
            | head -n1 \
            | sed -E 's/.*"name": *"([^"]+)".*/\1/' || true)"
    fi
    if [[ -z "$asset" ]]; then
        echo "[install] could not find a tm-worker .run asset on $repo $tag." >&2
        echo "[install] If this is a draft release, the host's gh must be authed for $repo," >&2
        echo "[install] or pass a pre-downloaded asset via: -f /path/to/tm-worker-vX.Y.Z-linux-x86_64.run" >&2
        exit 1
    fi

    cd "$tmp"
    echo "[install] downloading $asset"
    if (( have_gh )); then
        gh release download --repo "$repo" "$tag" --pattern "$asset" --dir . \
            || { echo "[install] gh release download failed" >&2; exit 1; }
    else
        url="https://github.com/$repo/releases/download/$tag/$asset"
        curl -fL --retry 3 -o "$asset" "$url"
    fi

    if [[ ! -s "$asset" ]]; then
        echo "[install] download failed: $asset is missing or empty" >&2
        exit 1
    fi
    asset_path="$tmp/$asset"
fi

chmod +x "$asset_path"

echo "[install] running $asset_path --accept -- --yes"
# makeself forwards args after `--` to the embedded startup script
# (install_linux.sh). --accept suppresses the LICENSE prompt; --yes makes
# install_linux.sh non-interactive on existing-install upgrades.
"$asset_path" --accept -- --yes

# tm-worker dynamically links against libopenblas (BLAS skills are enabled in
# release builds). Make sure the runtime lib is present. Best-effort: only
# attempts apt-get when the .so is missing AND we have a Debian/Ubuntu box
# with sudo. Other distros / sandboxed environments will get a clear hint.
ensure_libopenblas() {
    if ldconfig -p 2>/dev/null | grep -q 'libopenblas\.so\.0'; then
        return 0
    fi
    echo "[install] libopenblas.so.0 not found; attempting to install..."
    if ! command -v apt-get >/dev/null 2>&1; then
        echo "[install] WARNING: no apt-get available. Install libopenblas0 manually for your distro." >&2
        return 0
    fi
    local sudo_cmd=""
    if [[ $EUID -ne 0 ]]; then
        if command -v sudo >/dev/null 2>&1; then
            sudo_cmd="sudo -n"
        else
            echo "[install] WARNING: not root and no sudo; cannot apt-get install libopenblas0." >&2
            return 0
        fi
    fi
    if ! $sudo_cmd apt-get update -qq; then
        echo "[install] WARNING: apt-get update failed (no passwordless sudo?). Install libopenblas0 manually." >&2
        return 0
    fi
    $sudo_cmd apt-get install -y -qq libopenblas0 || {
        echo "[install] WARNING: failed to install libopenblas0; tm-worker will fail to start until it's present." >&2
        return 0
    }
    echo "[install] libopenblas0 installed."
}
ensure_libopenblas

# Verify.
bin="$HOME/.local/bin/tm-worker"
if [[ ! -x "$bin" ]]; then
    echo "[install] expected symlink $bin not found after install" >&2
    exit 1
fi

# Make sure ~/.local/bin will be on PATH for future shells (non-fatal).
case ":$PATH:" in
    *":$HOME/.local/bin:"*) ;;
    *) echo "[install] note: \$HOME/.local/bin is not on PATH for this shell." ;;
esac

echo
echo "[install] done."
echo "[install] binary: $bin"
echo "[install] version:"
"$bin" --version 2>/dev/null || true
