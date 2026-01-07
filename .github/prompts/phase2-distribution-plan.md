# Phase 2: Distribution Structure Implementation

## Context
This plan assumes Phase 1 (libzt shared library conversion) is complete and working. The project uses Meson build system with two main components (manager and worker) that should be distributed as separate packages for Linux and Windows.

## Plan: Distribution Packaging for Manager and Worker

Create meson install targets for executables and the libzt shared library with proper RPATH configuration, add version management from meson.build to C++ code, install configuration templates and ZeroTier identity files alongside executables, then implement platform-specific packaging scripts that generate separate distributable archives for manager and worker components.

### Steps

1. **Configure version and install paths** in `meson.build` by adding `version: '1.0.0'` to `project()` declaration, adding compile argument `-DPROJECT_VERSION="@0@".format(meson.project_version())` via `add_project_arguments()`, and verifying default install directories (`bindir`, `libdir`, `datadir`, `sysconfdir`)

2. **Install libzt shared library** in `subprojects/libzt-wrapper/meson.build` by adding `install_data()` to copy `zt-shared.dll` to `get_option('bindir')` on Windows (same directory as executables), or `libzt.so` to `get_option('libdir')` on Linux, using platform detection with `host_machine.system()` and joining paths from the CMake build output directory

3. **Replace hardcoded version** in `shared/Options.hpp` by changing `app.set_version_flag()` from hardcoded "task-messenger 0.1" string to use `PROJECT_VERSION` preprocessor macro that's defined via compiler arguments from meson

4. **Install manager identity files** by modifying `manager/meson.build` to add `install_data()` calls for `manager/.vn_manager_identity/identity.public` and `manager/.vn_manager_identity/identity.secret` with `install_dir: get_option('bindir')` so they're placed next to the manager executable as reusable defaults for all manager instances

5. **Install configuration templates and documentation** in `meson.build` by adding `install_data()` for `config-manager.json` and `config-worker.json` to `get_option('sysconfdir') / 'task-messenger'`, installing `README.md` and component READMEs to `get_option('datadir') / 'doc' / 'task-messenger'`, and creating then installing LICENSE file to doc directory

6. **Create Linux packaging script** at `extras/scripts/build_distribution.sh` that accepts component argument (manager/worker/all), runs `meson setup --prefix=/opt/task-messenger --buildtype=release`, compiles and installs to DESTDIR staging directory, creates component-specific tar.gz archives named `task-messenger-{component}-v{version}-linux-{arch}.tar.gz` including only relevant binaries and libzt.so, and generates SHA256 checksums

7. **Create Windows packaging script** at `extras/scripts/build_distribution.ps1` with equivalent functionality using `--prefix="C:\TaskMessenger"`, copying zt-shared.dll to bin/ directory alongside executables, generating component-specific ZIP archives, and creating SHA256 checksums

8. **Test distribution workflow** by running both packaging scripts for manager and worker separately, extracting archives to clean test environments, verifying executables display correct version with `--version`, confirming libzt library resolution with `ldd` on Linux or Dependency Walker on Windows, testing that configuration files and identity files are in expected locations, and validating complete runtime functionality including RPATH verification for the install layout

### Decisions Made

- **Separate packages**: Manager and worker distributed as separate packages (not combined)
- **Identity files**: Manager identity files (identity.public/identity.secret) placed next to manager executable for reuse across instances
- **Debug symbols**: .pdb files excluded from distribution packages
- **File permissions**: No special permission restrictions on identity files
- **Version naming**: Simple semantic versioning (no git commit hashes)
- **Configuration location**: Using sysconfdir (etc/) for config files, datadir for documentation only
- **Service integration**: Deferred to Phase 3

### Runtime Dependencies

Analysis confirms only **libzt** requires bundling:
- **Header-only**: CLI11, nlohmann_json (compiled into executables)
- **Static libraries**: FTXUI, shared_core (linked into executables)
- **Shared library**: libzt (zt-shared.dll / libzt.so) - **MUST BE BUNDLED**

### Distribution Structure

**Manager Package:**
```
bin/
  ├── manager[.exe]
  ├── identity.public      (ZeroTier identity - reusable default)
  ├── identity.secret      (ZeroTier identity - reusable default)
  └── zt-shared.dll        (Windows only - in same dir as exe)
lib/                       (Linux only)
  └── libzt.so
etc/task-messenger/
  └── config-manager.json
share/doc/task-messenger/
  ├── README.md
  ├── manager-README.md
  └── LICENSE
```

**Worker Package:**
```
bin/
  ├── worker[.exe]
  └── zt-shared.dll        (Windows only - in same dir as exe)
lib/                       (Linux only)
  └── libzt.so
etc/task-messenger/
  └── config-worker.json
share/doc/task-messenger/
  ├── README.md
  ├── worker-README.md
  └── LICENSE
```

## Implementation Status

### ✅ Phase 2 Complete

**Implementation Date:** January 6-7, 2026  
**Status:** Successfully implemented and tested on both Windows and Linux platforms

### Files Modified

1. **meson.build** - Added version 1.0.0, PROJECT_VERSION compile definition, installation rules for docs/configs
2. **subprojects/libzt-wrapper/meson.build** - Added install targets for shared library, architecture mapping fixes
3. **subprojects/shared/options/Options.cpp** - Replaced hardcoded version with PROJECT_VERSION macro
4. **subprojects/shared/meson.build** - Added project_version option handling and compile arguments
5. **manager/meson.build** - Added installation for manager identity files

### Files Created

6. **LICENSE** - MIT License
7. **subprojects/shared/meson_options.txt** - Version option for subproject
8. **extras/scripts/build_distribution.sh** - Linux packaging script
9. **extras/scripts/build_distribution.ps1** - Windows packaging script

### Issues Found and Fixed During Testing

#### 1. Architecture Naming Mismatch (Linux)

**Problem:** libzt uses `x64` for x86_64, but Meson returns `x86_64`, causing library path failures.

**Fix Applied:** Added architecture name mapping in `subprojects/libzt-wrapper/meson.build`:
```meson
arch_name = host_machine.cpu_family()
if arch_name == 'x86_64'
  arch_name = 'x64'
elif arch_name == 'aarch64'
  arch_name = 'arm64'
endif
```

#### 2. Library Installation Path (Linux)

**Problem:** `get_option('libdir')` returns platform-specific paths like `lib/x86_64-linux-gnu/`, breaking archive structure.

**Fix Applied:** Changed to use fixed path relative to prefix:
```meson
install_data(so_file, install_dir: join_paths(get_option('prefix'), 'lib'))
```

### Test Results

#### Windows Testing (January 6, 2026)
- ✅ Build and compile successful
- ✅ Version output: `task-messenger 1.0.0`
- ✅ Installation targets work correctly
- ✅ Packaging script creates valid ZIP archives
- ✅ All files present in correct structure
- ✅ Executables run with correct dependencies
- ✅ SHA256 checksums generated

**Artifacts:**
- `task-messenger-manager-v1.0.0-windows-x64.zip` (~790KB)
- `task-messenger-worker-v1.0.0-windows-x64.zip`

#### Linux Testing (January 7, 2026)
- ✅ Build and compile successful  
- ✅ Version output: `task-messenger 1.0.0`
- ✅ RPATH configured correctly: `[$ORIGIN/../lib]`
- ✅ No missing dependencies
- ✅ Packaging script creates valid tar.gz archives
- ✅ All files present in correct structure
- ✅ Executables run from extracted location
- ✅ SHA256 checksums verified

**Artifacts:**
- `task-messenger-manager-v1.0.0-linux-x86_64.tar.gz` (1.3MB)
- `task-messenger-worker-v1.0.0-linux-x86_64.tar.gz` (1.5MB)

### Distribution Workflow

Both packaging scripts support:
- Single component builds: `./build_distribution.sh manager`
- Multiple component builds: `./build_distribution.sh all`
- Release optimization: `--buildtype=release`
- Separate staging and output directories
- Automatic version extraction from meson.build
- SHA256 checksum generation

### Production Readiness

**Current Support:**
- ✅ Windows x64
- ✅ Linux x86_64
- ✅ Separate manager and worker packages
- ✅ Self-contained distributions
- ✅ Version management
- ✅ Documentation included

**Future Enhancements:**
- ARM64/aarch64 support (architecture mapping already in place)
- macOS support (requires testing)
- Systemd service files (Phase 3)
- Additional Linux distributions testing
- CI/CD pipeline integration
- Digital signatures for verification

### Next Steps

Phase 2 objectives achieved. Ready for:
1. Phase 3: Service integration (systemd, Windows services)
2. Production deployment
3. CI/CD pipeline setup
4. Multi-architecture builds
