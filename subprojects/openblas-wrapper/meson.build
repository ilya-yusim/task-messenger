# =============================================================================
# openblas-wrapper — Meson subproject for OpenBLAS / Apple Accelerate
# =============================================================================
#
# Provides a single dependency 'openblas-wrapper' that encapsulates all
# platform logic:
#
#   macOS       → Apple Accelerate framework (always, zero install)
#   Windows     → Downloads prebuilt OpenBLAS from GitHub Releases,
#                 generates MSVC import library via dumpbin + lib.exe
#   Linux       → Uses system-installed OpenBLAS (apt/dnf/pacman)
#
# The user can override with -Dopenblas-wrapper:blas_use_system=true to
# force use of a system-installed OpenBLAS (e.g. from vcpkg or Homebrew).
#
# Downstream usage (root meson.build):
#   blas_dep = dependency('openblas-wrapper',
#                          fallback: ['openblas-wrapper', 'openblas_dep'],
#                          required: false)
# =============================================================================

project('openblas-wrapper', 'c')

fs = import('fs')
py = find_program('python3', 'python')
cc = meson.get_compiler('c')

blas_use_system = get_option('blas_use_system')

# =============================================================================
# macOS — always Apple Accelerate
# =============================================================================
if host_machine.system() == 'darwin'
  openblas_dep = dependency('appleframeworks', modules: ['Accelerate'],
                            required: false)
  if openblas_dep.found()
    message('✅ Using Apple Accelerate for BLAS')
  else
    warning('Apple Accelerate not found (unexpected on macOS)')
  endif

# =============================================================================
# Explicit system dependency (all platforms, user opt-in)
# =============================================================================
elif blas_use_system
  openblas_dep = dependency('openblas', required: false)
  if openblas_dep.found()
    message('✅ Using system-installed OpenBLAS')
  else
    warning('System OpenBLAS not found. Install with:')
    warning('  Ubuntu/Debian : apt install libopenblas-dev')
    warning('  Fedora        : dnf install openblas-devel')
    warning('  Arch          : pacman -S openblas')
    warning('  vcpkg         : vcpkg install openblas')
    warning('  Homebrew      : brew install openblas')
  endif

# =============================================================================
# Windows — download prebuilt from GitHub Releases
# =============================================================================
elif host_machine.system() == 'windows'
  # Read pinned version
  version_file = join_paths(meson.current_source_dir(), 'UPSTREAM_VERSION')
  ver_data = run_command(py, '-c',
    'import pathlib,sys; print(pathlib.Path(sys.argv[1]).read_text().strip())',
    version_file, check: true)
  openblas_version = ver_data.stdout().strip()

  dist_dir = join_paths(meson.current_source_dir(), 'dist', 'win-x64')

  # Download if not already present (idempotent — checks for cblas.h)
  if not fs.exists(join_paths(dist_dir, 'include', 'cblas.h'))
    message('📦 Downloading OpenBLAS ' + openblas_version + ' for Windows x64 …')
    dl = run_command(py,
      join_paths(meson.current_source_dir(), 'download_openblas.py'),
      '--version', openblas_version,
      '--outdir', dist_dir,
      '--checksums', join_paths(meson.current_source_dir(), 'checksums.json'),
      check: false)
    if dl.returncode() != 0
      warning('OpenBLAS download failed:')
      warning(dl.stderr())
    else
      message(dl.stdout())
    endif
  else
    message('✅ OpenBLAS ' + openblas_version + ' already present in dist/')
  endif

  # Try to find the downloaded library
  if fs.exists(join_paths(dist_dir, 'include', 'cblas.h'))
    lib_path = dist_dir
    openblas_lib = cc.find_library('openblas', dirs: [lib_path], required: false)
    if openblas_lib.found()
      openblas_dep = declare_dependency(
        dependencies: [openblas_lib],
        include_directories: include_directories('dist' / 'win-x64' / 'include'),
      )
      message('✅ Using downloaded OpenBLAS ' + openblas_version)
    else
      warning('Downloaded OpenBLAS not linkable (libopenblas.lib missing?) — trying system fallback')
      openblas_dep = dependency('openblas', required: false)
    endif
  else
    warning('Download appears to have failed — trying system fallback')
    openblas_dep = dependency('openblas', required: false)
  endif

# =============================================================================
# Linux / other — system package
# =============================================================================
else
  openblas_dep = dependency('openblas', required: false)
  if openblas_dep.found()
    message('✅ Using system-installed OpenBLAS')
  else
    warning('OpenBLAS not found. Install with:')
    warning('  Ubuntu/Debian : apt install libopenblas-dev')
    warning('  Fedora        : dnf install openblas-devel')
    warning('  Arch          : pacman -S openblas')
  endif
endif

# =============================================================================
# Override for downstream consumers
# =============================================================================
if openblas_dep.found()
  meson.override_dependency('openblas-wrapper', openblas_dep)
endif

# =============================================================================
# DLL installation (Windows)
# =============================================================================
if host_machine.system() == 'windows' and openblas_dep.found() and not blas_use_system
  dist_bin = join_paths(meson.current_source_dir(), 'dist', 'win-x64', 'bin')
  if fs.exists(dist_bin)
    # Find the DLL name dynamically via Python (avoid hardcoding)
    dll_find = run_command(py, '-c',
      'from pathlib import Path; import sys;'
      + 'dlls=sorted(Path(sys.argv[1]).glob("*.dll"));'
      + 'print(dlls[0] if dlls else "")',
      dist_bin, check: false)
    dll_name = dll_find.stdout().strip()
    if dll_name != ''
      install_data(dll_name, install_dir: get_option('bindir'))
      # Export DLL path for parent project (development builds)
      openblas_dll_path = dll_name
    endif
  endif
endif
