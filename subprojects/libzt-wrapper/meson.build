# meson.build for libzt-wrapper
#
# This Meson build file automates fetching, building, and exposing the libzt library
# as a dependency for other Meson subprojects. It supports both Windows and Linux/macOS.
#
# Steps:
# 1. Clone the libzt repository if it does not exist.
# 2. Initialize git submodules for libzt.
# 3. Build libzt using the appropriate script for the host platform.
# 4. Find required system libraries (Windows only).
# 5. Find the built libzt static library.
# 6. Declare a dependency object for use by other subprojects.

project('libzt', 'cpp')

fs = import('fs')
libzt_dir = join_paths(meson.global_source_root(), 'subprojects','libzt-wrapper','libzt')
message('libzt_dir: ' + libzt_dir)

py = find_program('python3', 'python')

# Version pin check
upstream_version_file = join_paths(meson.current_source_dir(), 'UPSTREAM_VERSION')
if fs.exists(upstream_version_file)
  head_commit = ''
  # Fallback: use Python to print the file content (portable)
  ver_data = run_command(py, '-c', 'import sys, pathlib; p=pathlib.Path(sys.argv[1]); print(p.read_text())', upstream_version_file, check: false)
  if ver_data.returncode() == 0
    foreach l : ver_data.stdout().strip().split('\n')
      if l.startswith('commit=')
        head_commit = l.substring(7)
      endif
    endforeach
  endif
  git_head = run_command('git','-C', libzt_dir,'rev-parse','HEAD', check: false)
  if git_head.returncode() == 0 and head_commit != ''
    current = git_head.stdout().strip()
    if current != head_commit
      message('⚠ libzt HEAD ' + current + ' differs from pinned ' + head_commit)
    else
      message('✅ libzt HEAD matches pinned commit ' + head_commit)
    endif
  endif
endif

# Step 1: Clone libzt if not present
if not fs.exists(libzt_dir)
  message('📦 Cloning libzt repository...')
  run_command('git', 'clone', '--recursive', 'https://github.com/zerotier/libzt.git', libzt_dir, check: true)
  message('📦 Initializing libzt...')
  run_command('git', '-C', libzt_dir, 'submodule', 'update', '--init', check: true)

  # Step 1.5: Apply local patches (idempotent)
  message('🔧 Applying local libzt patches (thread-local zts_errno)...')
  run_command(py, join_paths(meson.current_source_dir(), 'apply_libzt_patches.py'), check: true)
endif

# Apply prometheus missing <stdexcept> fix idempotently using Python (more robust than patch)
family_header = join_paths(libzt_dir, 'ext','ZeroTierOne','ext','prometheus-cpp-lite-1.0','core','include','prometheus','family.h')
if fs.exists(family_header)
  message('🔧 Ensuring prometheus-cpp-lite headers include <stdexcept>...')
  stdexcept_patch = join_paths(meson.current_source_dir(), 'patches', 'prometheus_stdexcept.patch')
  if fs.exists(stdexcept_patch)
    rc_std = run_command('git','-C', libzt_dir,'apply', stdexcept_patch, check: false)
    if rc_std.returncode() == 0
      message('✅ Applied prometheus_stdexcept.patch via git apply')
    else
      message('ℹ git apply failed or already applied; falling back to script')
      run_command(py, join_paths(meson.current_source_dir(), 'patch_prometheus_stdexcept.py'), check: true)
    endif
  else
    run_command(py, join_paths(meson.current_source_dir(), 'patch_prometheus_stdexcept.py'), check: true)
  endif
endif

# Step 1.6: Always synchronize local override files into the vendored libzt tree
message('🔁 Synchronizing local libzt override files...')
run_command(py, join_paths(meson.current_source_dir(), 'sync_overrides.py'), libzt_dir, check: true)

# Optionally quiet lwIP SOCKETS_DEBUG in libzt's CMake flags when building Debug
libzt_quiet = get_option('libzt_quiet_lwip_debug')
if libzt_quiet
  message('🔇 Patching libzt CMake to suppress lwIP SOCKETS_DEBUG prints in Debug builds...')
  quiet_patch = join_paths(meson.current_source_dir(), 'patches', 'quiet_lwip_debug.patch')
  if fs.exists(quiet_patch)
    rc_q = run_command('git','-C', libzt_dir,'apply', quiet_patch, check: false)
    if rc_q.returncode() == 0
      message('✅ Applied quiet_lwip_debug.patch via git apply')
    else
      message('ℹ git apply failed or already applied; falling back to script')
      run_command(py, join_paths(meson.current_source_dir(), 'patch_quiet_lwip_debug.py'), libzt_dir, check: true)
    endif
  else
    run_command(py, join_paths(meson.current_source_dir(), 'patch_quiet_lwip_debug.py'), libzt_dir, check: true)
  endif
endif

# Step 2: Build libzt using the appropriate script

# Map Meson's build type to libzt/CMake build types expected by libzt scripts.
# Meson buildtypes include: 'debug', 'debugoptimized', 'release', etc.
# We want libzt to be built with:
#  - 'Debug' for Meson's 'debug'
#  - 'RelWithDebInfo' for Meson's 'debugoptimized' (optimized + debug symbols)
#  - 'Release' for Meson's 'release' (fully optimized, no debug info)
meson_buildtype = get_option('buildtype')
if meson_buildtype == 'debug'
  libzt_buildtype = 'debug'
elif meson_buildtype == 'debugoptimized'
  libzt_buildtype = 'relwithdebinfo'
else
  libzt_buildtype = 'release'
endif

if host_machine.system() == 'windows'
  message('📦 Building libzt (Windows)... buildtype=' + libzt_buildtype)
  run_command(
    'powershell', '-ExecutionPolicy', 'Bypass', '-File',
    join_paths(meson.current_source_dir(), 'build-libzt.ps1'),
    libzt_dir,
    libzt_buildtype,
    check: true
  )
else
  message('📦 Building libzt (Linux/macOS)... buildtype=' + libzt_buildtype)
  run_command(
    'sh', '-c',
    'cd "' + libzt_dir + '" && ./build.sh host ' + libzt_buildtype,
    check: true
  )
endif

# Step 3: Find system libraries (Windows only)
cc = meson.get_compiler('cpp')
ws2_32 = cc.find_library('ws2_32', required: host_machine.system() == 'windows')
iphlpapi = cc.find_library('iphlpapi', required: host_machine.system() == 'windows')
shlwapi = cc.find_library('shlwapi', required: host_machine.system() == 'windows')

# Step 4: Find the built libzt library
# Compute the actual distribution path based on platform and build type
# instead of relying on a junction/symlink
if host_machine.system() == 'windows'
  # Windows: dist/win-x64-host-{debug|release}
  libzt_dist_dir = 'win-x64-host-' + libzt_buildtype
  libzt_lib_path = join_paths(libzt_dir, 'dist', libzt_dist_dir, 'lib')
  # On Windows, use shared library (import library: zt-shared.lib for zt-shared.dll)
  zt_lib = cc.find_library('zt-shared', dirs: [libzt_lib_path], required: true, static: false)
else
  # Linux/macOS: dist/{linux|macos}-{arch}-host-{debug|release}
  # libzt uses 'x64' for x86_64, 'arm64' for aarch64
  arch_name = host_machine.cpu_family()
  if arch_name == 'x86_64'
    arch_name = 'x64'
  elif arch_name == 'aarch64'
    arch_name = 'arm64'
  endif
  libzt_dist_dir = host_machine.system() + '-' + arch_name + '-host-' + libzt_buildtype
  libzt_lib_path = join_paths(libzt_dir, 'dist', libzt_dist_dir, 'lib')
  # On Linux, use shared library (libzt.so - Meson searches as 'zt')
  zt_lib = cc.find_library('zt', dirs: [libzt_lib_path], required: true, static: false)
endif

# Step 5: Declare the dependency for use by other subprojects
# For shared library, no special link flags needed
link_args = []

extra_deps = []
if host_machine.system() == 'windows'
  extra_deps = [ws2_32, iphlpapi, shlwapi]
endif

# Configure RPATH for runtime library discovery on Linux
if host_machine.system() == 'linux'
  # Use $ORIGIN to allow executables to find the library relative to their location
  link_args += ['-Wl,-rpath,$ORIGIN/../lib']
endif

libzt_dep = declare_dependency(
  dependencies: [zt_lib] + extra_deps,
  include_directories: include_directories('libzt/include'),
  link_args: link_args,
)
meson.override_dependency('libzt-wrapper', libzt_dep)

# =============================================================================
# Installation
# =============================================================================

# Install the libzt shared library to the appropriate directory
if host_machine.system() == 'windows'
  # Windows: Install DLL to bindir (same directory as executables)
  dll_file = join_paths(libzt_lib_path, 'zt-shared.dll')
  install_data(dll_file, install_dir: get_option('bindir'))
  
  # Export buildtype and lib_path for parent project to construct DLL path
  # (Exporting the full dll_file path causes sandbox violations)
  set_variable('libzt_buildtype', libzt_buildtype)
  set_variable('libzt_lib_path', libzt_lib_path)
else
  # Linux/macOS: Install shared library to lib directory (relative to prefix)
  so_file = join_paths(libzt_lib_path, 'libzt.so')
  install_data(so_file, install_dir: join_paths(get_option('prefix'), 'lib'))
  
  # Export lib_path for parent project to construct SO path (for development builds)
  set_variable('libzt_lib_path', libzt_lib_path)
endif
