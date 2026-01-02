libzt-wrapper Build Overview
============================

Purpose
-------
`libzt-wrapper` provides a reproducible, patchable staging area for building ZeroTier's `libzt` as a Meson subproject on Windows and Linux/macOS. It mirrors the upstream repository, applies local patches and overrides, maps Meson build types to libzt/CMake build types, and exposes a Meson dependency (`libzt-wrapper`) for downstream targets.

Directory Layout
---------------
- `libzt/` : Vendored upstream ZeroTier libzt Git repository (cloned recursively).
- `meson.build` : Wrapper logic (clone if missing, apply patches, build, expose dependency).
- `meson_options.txt` : Provides wrapper-specific options (e.g. `libzt_quiet_lwip_debug`).
- `patches/` : Git patch files usable via `git apply` (fallback to script if already applied).
- `apply_libzt_patches.py` : Idempotent scripted patch for thread-local `zts_errno` etc.
- `patch_prometheus_stdexcept.py` / `prometheus_stdexcept.patch` : Ensures `<stdexcept>` include for prometheus-cpp-lite.
- `patch_quiet_lwip_debug.py` / `quiet_lwip_debug.patch` : Suppresses lwIP `SOCKETS_DEBUG` verbosity in Debug builds when option enabled.
- `sync_overrides.py` : Synchronizes local override source files (e.g. replacements placed beside wrapper) into the vendored tree.
- `UPSTREAM_VERSION` : Optional file pinning a specific upstream commit (`commit=<sha>`). Used to warn if checked out HEAD differs.
- Auxiliary sources (`NodeService.*`, `zts_errno.*`) : Local additions/overrides merged into the build.

Build Flow (meson.build)
------------------------
1. Pin Check: If `UPSTREAM_VERSION` exists, read pinned commit; compare to current `libzt` HEAD and emit a message.
2. Clone: If `libzt/` absent, clone with `--recursive` and update submodules.
3. Initial Patches: Run `apply_libzt_patches.py` (thread-local errno / other base adjustments).
4. Prometheus Patch: Attempt `git apply` of `prometheus_stdexcept.patch`; if already applied or fails, run scripted patch.
5. Overrides Sync: Always invoke `sync_overrides.py` to copy/update local override files (idempotent).
6. Quiet lwIP (conditional): If `-Dlibzt_quiet_lwip_debug=true`, apply or script patch for reduced Debug logging.
7. Build Type Mapping:
   - Meson `debug`          -> libzt `debug`
   - Meson `debugoptimized` -> libzt `relwithdebinfo`
   - Meson `release`        -> libzt `release`
8. Invoke platform build script:
   - Windows: `build-libzt.ps1` (PowerShell) with chosen build type.
   - Non-Windows: `build.sh host <type>` inside cloned repository.
9. Locate produced static library under `libzt/dist/native/lib` (Windows layout assumed by script).
10. Declare dependency `libzt_dep` with includes, Windows system libs (`ws2_32`, `iphlpapi`, `shlwapi`), and link arg suppression (`/IGNORE:4217`). Meson overrides as `libzt-wrapper`.

Meson Usage
-----------
Downstream subprojects obtain the dependency via:
```
libzt_dep = dependency('libzt-wrapper', fallback: ['libzt-wrapper','libzt_dep'])
```
or (after override) simply:
```
libzt_dep = dependency('libzt-wrapper')
```

Customization Points
--------------------
- Pin revision: Edit `UPSTREAM_VERSION` (e.g. `commit=abcdef123456...`) then re-run `meson setup` to see mismatch warnings.
- Toggle quiet lwIP: `meson setup builddir -Dlibzt_quiet_lwip_debug=true`.
- Add new patch: Place a `.patch` file in `patches/` and extend `meson.build` similarly to existing patch steps.
- Scripted transforms: Prefer Python scripts for idempotency when line-oriented patch may already be applied.

Idempotency Strategy
--------------------
Each patch step first attempts `git apply` (clean application), falls back to Python script when:
- Patch already applied.
- Source differs slightly (script performs targeted edits).
This avoids hard failures on reconfigure or multi-developer environments.

Updating libzt
--------------
1. Remove or stash local edits not covered by scripts.
2. `git -C subprojects/libzt-wrapper/libzt fetch --all` and checkout desired tag/commit.
3. Update `UPSTREAM_VERSION` to new commit hash.
4. Re-run `meson setup --wipe` with your native file so pkg-config & options reapply.
5. Verify that patches/scripts still succeed; update them if upstream changed target lines.

Windows Notes
-------------
- PowerShell build script ensures environment compatibility (VS toolchain, static linking).
- Suppression of warning 4217 prevents duplicate symbol import noise for static libs.
- System libraries discovered via `cc.find_library()`; if missing, ensure Windows SDK / Build Tools installed.

Troubleshooting
---------------
- Patch fails repeatedly: Delete partially applied changes (`git -C libzt reset --hard`) then rerun Meson.
- Library not found: Confirm `dist/native/lib/` path exists; inspect PowerShell script output.
- Commit mismatch warning: Update or revert working tree to match `UPSTREAM_VERSION` for reproducibility.

Rationale for Wrapper (Why Not Direct Wrap Only)
-----------------------------------------------
Maintaining a wrapper allows multiple scripted mutations, optional feature flags, and local overrides without polluting upstream history. It centralizes all custom logic and documents divergence from the original project.

Future Enhancements
-------------------
- Migrate patch steps to Meson `patch_directory` in a wrap for simpler maintenance.
- Add checksum or signature verification for pinned commit artifacts.
- Provide a CI script to assert patches still apply cleanly against upstream HEAD.

License Notice
--------------
`libzt` retains its original licensing. Wrapper scripts and patches should remain compatible; avoid embedding upstream license text redundantly.
