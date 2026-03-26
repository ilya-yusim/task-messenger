#!/usr/bin/env python3
"""Download prebuilt OpenBLAS binaries and prepare them for MSVC linking.

Called by meson.build during configure on Windows. On Linux, system packages
are used instead (apt install libopenblas-dev, etc.).

Workflow:
  1. Download the prebuilt archive from GitHub Releases.
  2. Verify SHA-256 if a checksum is recorded in checksums.json.
  3. Extract to the output directory, stripping the archive's root folder.
  4. On Windows/MSVC: generate an import library (openblas.lib) from the DLL
     using dumpbin + lib so that cl.exe can link against it.
"""

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path


def get_platform_key() -> str:
    """Return a key like 'windows-x64' describing this host."""
    system = platform.system().lower()
    machine = platform.machine().lower()
    if machine in ("x86_64", "amd64"):
        arch = "x64"
    elif machine in ("aarch64", "arm64"):
        arch = "arm64"
    else:
        arch = machine
    return f"{system}-{arch}"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(1 << 16):
            h.update(chunk)
    return h.hexdigest()


def download_file(url: str, dest: Path) -> None:
    print(f"Downloading {url}")
    urllib.request.urlretrieve(url, dest)  # noqa: S310 — URL is from checksums.json, not user input
    print(f"  saved to {dest.name}")


# ---------------------------------------------------------------------------
# MSVC import‑library generation
# ---------------------------------------------------------------------------

def _parse_dumpbin_exports(text: str) -> list[str]:
    """Extract exported symbol names from ``dumpbin /exports`` output."""
    exports: list[str] = []
    in_table = False
    for line in text.splitlines():
        stripped = line.strip()
        # The header row contains "ordinal", "hint", "RVA", "name"
        if "ordinal" in stripped and "hint" in stripped and "RVA" in stripped:
            in_table = True
            continue
        if in_table:
            if not stripped:
                if exports:
                    break  # blank line after data → end of section
                continue
            parts = stripped.split()
            # Typical line: "   1    0 00063A70 cblas_caxpy"
            if len(parts) >= 4 and parts[0].isdigit():
                exports.append(parts[3])
    return exports


def generate_msvc_import_lib(dll_path: Path, lib_path: Path) -> bool:
    """Create an MSVC import library from a DLL via dumpbin + lib.exe."""
    # --- extract exports --------------------------------------------------
    result = subprocess.run(
        ["dumpbin", "/exports", str(dll_path)],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(f"  dumpbin failed: {result.stderr.strip()}", file=sys.stderr)
        return False

    exports = _parse_dumpbin_exports(result.stdout)
    if not exports:
        print("  no exports found in DLL", file=sys.stderr)
        return False
    print(f"  found {len(exports)} exports in {dll_path.name}")

    # --- write .def -------------------------------------------------------
    def_path = lib_path.with_suffix(".def")
    with open(def_path, "w") as f:
        f.write(f"LIBRARY {dll_path.name}\n")
        f.write("EXPORTS\n")
        for name in exports:
            f.write(f"  {name}\n")

    # --- generate import lib ----------------------------------------------
    result = subprocess.run(
        ["lib", f"/def:{def_path}", f"/out:{lib_path}", "/machine:x64"],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(f"  lib.exe failed: {result.stderr.strip()}", file=sys.stderr)
        return False

    print(f"  generated {lib_path.name}")
    return True


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(description="Download prebuilt OpenBLAS")
    parser.add_argument("--version", required=True)
    parser.add_argument("--outdir", required=True, help="Destination directory")
    parser.add_argument("--checksums", required=True, help="Path to checksums.json")
    args = parser.parse_args()

    version: str = args.version
    outdir = Path(args.outdir)
    checksums_path = Path(args.checksums)

    platform_key = get_platform_key()
    print(f"platform={platform_key}  version={version}")

    # --- resolve URL and expected hash ------------------------------------
    checksums: dict = {}
    if checksums_path.exists():
        checksums = json.loads(checksums_path.read_text())

    info: dict = checksums.get(version, {}).get(platform_key, {})

    if not info:
        # Construct default URL for known platforms
        if platform_key == "windows-x64":
            info = {
                "url": f"https://github.com/OpenMathLib/OpenBLAS/releases/download/v{version}/OpenBLAS-{version}-x64.zip",
                "sha256": "",
                "archive_name": f"OpenBLAS-{version}-x64.zip",
                "strip_prefix": "",
            }
        else:
            print(
                f"No prebuilt binary URL for '{platform_key}'. "
                "Use system packages instead (-Dopenblas-wrapper:blas_use_system=true).",
                file=sys.stderr,
            )
            sys.exit(1)

    url: str = info["url"]
    expected_sha: str = info.get("sha256", "")
    strip_prefix: str = info.get("strip_prefix", "")
    archive_name: str = info.get("archive_name", url.rsplit("/", 1)[-1])

    # --- download ---------------------------------------------------------
    with tempfile.TemporaryDirectory() as tmpdir:
        archive = Path(tmpdir) / archive_name
        try:
            download_file(url, archive)
        except Exception as exc:
            print(f"Download failed: {exc}", file=sys.stderr)
            sys.exit(1)

        if not archive.exists() or archive.stat().st_size == 0:
            print("Download produced an empty file", file=sys.stderr)
            sys.exit(1)

        # --- checksum -----------------------------------------------------
        actual_sha = sha256_file(archive)
        if expected_sha:
            if actual_sha != expected_sha:
                print(
                    f"SHA-256 MISMATCH!\n"
                    f"  expected: {expected_sha}\n"
                    f"  actual:   {actual_sha}",
                    file=sys.stderr,
                )
                sys.exit(1)
            print(f"  SHA-256 verified: {actual_sha[:16]}…")
        else:
            print(f"  SHA-256 (record in checksums.json): {actual_sha}")

        # --- extract ------------------------------------------------------
        extract_dir = Path(tmpdir) / "extracted"
        try:
            if archive_name.endswith(".zip"):
                with zipfile.ZipFile(archive) as zf:
                    zf.extractall(extract_dir)
            else:
                print(f"Unsupported archive format: {archive_name}", file=sys.stderr)
                sys.exit(1)
        except zipfile.BadZipFile:
            print(
                "Downloaded file is not a valid ZIP archive.\n"
                "  The URL may have returned an error page instead of a binary.",
                file=sys.stderr,
            )
            sys.exit(1)

        # Determine source root (strip top-level folder from archive)
        src: Path | None = None
        if strip_prefix:
            candidate = extract_dir / strip_prefix
            if candidate.exists():
                src = candidate
            else:
                print(f"  Note: expected prefix '{strip_prefix}' not found, auto-detecting …")

        if src is None:
            # Auto-detect: single top-level directory?
            entries = [e for e in extract_dir.iterdir() if e.is_dir()]
            if len(entries) == 1:
                src = entries[0]
                print(f"  Using archive root: {src.name}")
            elif extract_dir.exists() and any(extract_dir.iterdir()):
                src = extract_dir
                print(f"  Using flat archive layout")
            else:
                print("Archive appears empty after extraction", file=sys.stderr)
                sys.exit(1)

        # --- copy to output -----------------------------------------------
        if outdir.exists():
            shutil.rmtree(outdir)
        shutil.copytree(src, outdir)

    # --- Windows: generate MSVC import library if needed ----------------
    if platform_key.startswith("windows"):
        lib_dir = outdir / "lib"
        # Check for import library at the archive root (flat layout) or in lib/
        msvc_lib = outdir / "libopenblas.lib"
        msvc_lib_alt = lib_dir / "libopenblas.lib" if lib_dir.exists() else None

        if msvc_lib.exists():
            print(f"  MSVC import library already present: {msvc_lib.name}")
            # Rename libopenblas.lib → openblas.lib so Meson find_library('openblas') works portably
            portable_lib = outdir / "openblas.lib"
            if not portable_lib.exists():
                shutil.copy2(msvc_lib, portable_lib)
                print(f"  Created portable copy: {portable_lib.name}")
        elif msvc_lib_alt and msvc_lib_alt.exists():
            print(f"  MSVC import library already present: lib/{msvc_lib_alt.name}")
            portable_lib = lib_dir / "openblas.lib"
            if not portable_lib.exists():
                shutil.copy2(msvc_lib_alt, portable_lib)
                print(f"  Created portable copy: lib/{portable_lib.name}")
        else:
            # No prebuilt import lib — try to generate from DLL
            bin_dir = outdir / "bin"
            dll_candidates = sorted(bin_dir.glob("*.dll")) if bin_dir.exists() else []
            if not dll_candidates:
                dll_candidates = sorted(outdir.glob("*.dll"))

            if dll_candidates:
                target_lib = lib_dir / "openblas.lib" if lib_dir.exists() else outdir / "openblas.lib"
                print(f"Generating MSVC import library from {dll_candidates[0].name} …")
                if not generate_msvc_import_lib(dll_candidates[0], target_lib):
                    print(
                        "WARNING: could not generate import library — "
                        "MSVC linking may fail.\n"
                        "  Ensure dumpbin.exe and lib.exe are on PATH "
                        "(activate VS Developer environment).",
                        file=sys.stderr,
                    )
            else:
                print("WARNING: no DLL found in archive", file=sys.stderr)

    print("OpenBLAS setup complete")


if __name__ == "__main__":
    main()
