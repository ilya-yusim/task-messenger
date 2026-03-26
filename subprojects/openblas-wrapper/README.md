openblas-wrapper
================

Meson subproject that provides a BLAS dependency for task-messenger skills.
Encapsulates all platform-specific logic so the root `meson.build` only needs:

```meson
blas_dep = dependency('openblas-wrapper',
                       fallback: ['openblas-wrapper', 'openblas_dep'],
                       required: false)
```

Platform Strategy
-----------------

| Platform        | Default behaviour                         | Override                |
|-----------------|-------------------------------------------|-------------------------|
| **macOS**       | Apple Accelerate framework (always)       | —                       |
| **Windows x64** | Download prebuilt from GitHub Releases    | `-Dopenblas-wrapper:blas_use_system=true` |
| **Linux**       | System package (`libopenblas-dev` etc.)   | `-Dopenblas-wrapper:blas_use_system=true` |

On Windows the prebuilt archive is fetched once during `meson setup` and cached
in `dist/win-x64/` (git-ignored).  An MSVC import library (`openblas.lib`) is
generated automatically from the DLL via `dumpbin` + `lib.exe`.

Options
-------
| Option           | Type | Default | Description |
|------------------|------|---------|-------------|
| `blas_use_system`| bool | false   | Use system-installed OpenBLAS (pkg-config / cmake / vcpkg) instead of downloading |

Directory Layout
----------------
```
openblas-wrapper/
├── meson.build              # Wrapper logic
├── meson_options.txt        # Build option: blas_use_system
├── UPSTREAM_VERSION         # Pinned OpenBLAS version (e.g. "0.3.28")
├── checksums.json           # SHA-256 per platform archive
├── download_openblas.py     # Download + extract + import-lib generation
├── .gitignore               # Ignores dist/
├── README.md                # This file
└── dist/                    # (created at configure time, git-ignored)
    └── win-x64/
        ├── bin/             # libopenblas.dll
        ├── include/         # cblas.h, openblas_config.h, …
        └── lib/             # openblas.lib (MSVC), libopenblas.dll.a (MinGW)
```

Updating the Pinned Version
---------------------------
1. Edit `UPSTREAM_VERSION` to the new version string (e.g. `0.3.29`).
2. Update or add the corresponding entry in `checksums.json` with the new URL
   and SHA-256 hash.  If you leave `sha256` empty, the download script prints
   the computed hash on first run so you can record it.
3. Delete `dist/` to force a fresh download: `rm -rf subprojects/openblas-wrapper/dist`
4. Reconfigure: `meson setup --reconfigure builddir`

Forcing System OpenBLAS
-----------------------
```sh
meson setup builddir -Dopenblas-wrapper:blas_use_system=true
```

This skips the download and uses whatever `dependency('openblas')` finds via
pkg-config or CMake (e.g. from vcpkg, apt, brew).

Runtime DLL Deployment (Windows)
--------------------------------
`meson install` copies `libopenblas.dll` to the install prefix `bin/` directory.

For **development builds** (running from `builddir/`), ensure the DLL is
discoverable:
- Copy `subprojects/openblas-wrapper/dist/win-x64/bin/libopenblas.dll` next to
  your executable, or
- Add `subprojects/openblas-wrapper/dist/win-x64/bin` to your `PATH`.

Installing System OpenBLAS
--------------------------
```sh
# Ubuntu / Debian
sudo apt install libopenblas-dev

# Fedora
sudo dnf install openblas-devel

# Arch Linux
sudo pacman -S openblas

# macOS (not needed — Accelerate is built-in)
brew install openblas

# Windows (vcpkg)
vcpkg install openblas
```
