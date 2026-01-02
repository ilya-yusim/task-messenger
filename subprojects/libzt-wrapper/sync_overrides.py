#!/usr/bin/env python3
"""Synchronize locally patched/override files into the vendored libzt tree.

This script makes the Windows-specific copy logic in build-libzt.ps1 portable.
It is idempotent: files are only copied when content differs (SHA-256 compare).

Run every Meson configure; cheap when nothing changed.

Override sources are kept alongside this script (authoritative copies):
    build.ps1
    CMakeLists.txt
    NodeService.cpp / NodeService.hpp  -> src/
    zts_errno.cpp  -> src/
    zts_errno.hpp  -> include/

Usage (Meson run_command):
    run_command(py, join_paths(meson.current_source_dir(), 'sync_overrides.py'), libzt_dir)

Exit code 0 always (warnings don't abort the build) unless an unexpected
exception occurs.
"""
from __future__ import annotations
import hashlib
import pathlib
import shutil
import sys
from typing import Iterable, Tuple

ROOT = pathlib.Path(__file__).parent.resolve()

# (source_relative_path, destination_relative_path)
OVERRIDES: Tuple[Tuple[str, str], ...] = (
    ("build.ps1", "build.ps1"),
    ("CMakeLists.txt", "CMakeLists.txt"),
    ("NodeService.cpp", "src/NodeService.cpp"),
    ("NodeService.hpp", "src/NodeService.hpp"),
    ("zts_errno.cpp", "src/zts_errno.cpp"),
    ("zts_errno.hpp", "include/zts_errno.hpp"),
)

def sha256(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 64), b''):
            h.update(chunk)
    return h.hexdigest()

def sync(libzt_root: pathlib.Path) -> int:
    if not libzt_root.exists():
        print(f"[sync-overrides] libzt directory not found yet: {libzt_root}")
        return 0

    changed = False
    for src_rel, dst_rel in OVERRIDES:
        src = ROOT / src_rel
        dst = libzt_root / dst_rel
        if not src.exists():
            print(f"[sync-overrides][WARN] Missing override source: {src_rel}")
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        need_copy = True
        if dst.exists():
            try:
                if sha256(src) == sha256(dst):
                    need_copy = False
            except Exception as e:  # pragma: no cover - defensive
                print(f"[sync-overrides][WARN] Hash check failed for {dst_rel}: {e}")
        if need_copy:
            shutil.copy2(src, dst)
            print(f"[sync-overrides] Updated {dst_rel} <= {src_rel}")
            changed = True
    if not changed:
        print("[sync-overrides] All overrides up to date")
    return 0

def main(argv: Iterable[str]) -> int:
    if len(argv) < 2:
        print("Usage: sync_overrides.py <path-to-libzt-root>", file=sys.stderr)
        return 0
    libzt_root = pathlib.Path(argv[1]).resolve()
    try:
        return sync(libzt_root)
    except Exception as e:  # pragma: no cover - defensive
        print(f"[sync-overrides][ERROR] {e}", file=sys.stderr)
        return 1

if __name__ == "__main__":  # pragma: no cover
    sys.exit(main(sys.argv))
