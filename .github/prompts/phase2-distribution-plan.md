# Phase 2: Distribution Structure Implementation

## Context
This plan assumes Phase 1 (libzt shared library conversion) is complete and working. The project uses Meson build system with two main components (manager and worker) that should be distributed as separate packages for Linux and Windows.

## Plan: Implement Distribution Structure for Task Messenger

Add meson-native installation rules for binaries, shared libraries, configuration templates, and documentation, then create platform-specific build scripts to generate separate distributable packages for manager and worker components with proper versioning.

### Steps

1. **Create LICENSE.txt** at project root with project license text (MIT, Apache-2.0, or appropriate), then add static documentation files at `docs/README-manager.txt` and `docs/README-worker.txt` with quick start, configuration reference, ZeroTier setup instructions, and troubleshooting sections

2. **Add installation rules to root meson.build** using `install_data()` to install LICENSE.txt and README.md to `get_option('datadir') / 'doc' / 'task-messenger'`, install config-manager.json and config-worker.json as templates to `get_option('sysconfdir') / 'task-messenger'`, and install component-specific READMEs to respective doc directories

3. **Update subprojects/libzt-wrapper/meson.build** to add `install: true` parameter when declaring shared library dependency, ensuring libzt.dll/libzt.so installs to `get_option('libdir')` with proper RPATH configuration already set from Phase 1

4. **Modify manager/meson.build** to use `install_subdir()` for `.vn_manager_identity` template directory to `get_option('datadir') / 'task-messenger-manager' / 'identity'`, and add manager-specific documentation installation

5. **Add version output flag** by modifying `manager/managerMain.cpp` and `worker/workerMain.cpp` to add `--version` CLI flag using CLI11 that outputs version from meson project() via compile definition like `-DPROJECT_VERSION="1.0.0"`

6. **Create extras/scripts/build_distribution.sh** that accepts component argument (manager/worker/all), runs `meson setup` with `--prefix=/usr` and `--buildtype=release`, compiles with `meson compile`, installs to DESTDIR staging directory, creates component-specific tar.gz archives named `task-messenger-{component}-v{version}-linux-{arch}.tar.gz`, and generates SHA256 checksums

7. **Create extras/scripts/build_distribution.ps1** implementing Windows equivalent using `--prefix="C:\Program Files\TaskMessenger"`, creating ZIP archives named `task-messenger-{component}-v{version}-windows-{arch}.zip`, bundling libzt.dll in bin/ directory alongside executables, and generating SHA256 checksums

8. **Test distribution workflow** by running build scripts for both components, extracting archives to clean test environments, verifying directory structure (bin/, lib/, etc/, share/), confirming executables display version with `--version`, testing config file loading from installed locations, and validating shared library resolution (ldd on Linux, Dependency Walker on Windows)

### Further Considerations

1. **Version propagation mechanism**: Define version once in root meson.build `project()` call, pass to C++ via `add_project_arguments('-DPROJECT_VERSION="@0@"'.format(meson.project_version()))`, and use in archive filenames via meson.project_version() in build scripts

2. **Installation paths customization**: Consider adding meson options for custom config directory (e.g., `-Dsysconfdir=/opt/task-messenger/etc`) to support non-standard deployment scenarios

3. **Service integration**: Should distribution include systemd unit files for Linux (`extras/systemd/task-messenger-manager.service`) and NSSM install scripts for Windows service deployment?

4. **Dependency bundling**: Verify if other dependencies (CLI11, nlohmann_json) are header-only or if FTXUI shared library needs bundling for worker UI builds

5. **Separate build directories**: Current structure uses builddir-manager and builddir-worker separately - distribution scripts should handle both and create separate packages

6. **Identity directory**: Manager uses `.vn_manager_identity` directory - ensure installation preserves structure and documents that users may need to customize this per deployment
