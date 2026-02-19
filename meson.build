project('task-messenger', 'cpp', version: '0.0.1', default_options : ['cpp_std=c++20', 'warning_level=3'])

# =============================================================================
# Compiler Arguments Configuration
# =============================================================================

cpp_args = []

# Enable detailed debug logging (controlled by meson option)
debug_logging = get_option('debug_logging')
if debug_logging
  cpp_args += ['-DDEBUG_LOGGING']
endif

# Uncomment to enable trace-level logging in logger implementation
#cpp_args += ['-DLOGGER_ENABLE_TRACE']

# Enable profiling-friendly compilation for perf/Hotspot analysis
profiling_unwind = get_option('profiling_unwind')
if profiling_unwind
  # Improve stack unwinding fidelity: preserve frame pointers, unwind tables,
  # and avoid sibling call optimizations that obscure call stacks
  cpp_args += [
    '-fno-omit-frame-pointer',
    '-fasynchronous-unwind-tables',
    '-fno-optimize-sibling-calls',
    '-gdwarf-5',  # Use DWARF-5 debug format if supported (improves profiler accuracy)
  ]
  message('profiling_unwind enabled: consider configuring with -Db_lto=false and --buildtype=debugoptimized')
endif

# Apply all C++ arguments project-wide
add_project_arguments(cpp_args, language: 'cpp')

# Add version definition for C++ code
add_project_arguments('-DPROJECT_VERSION="' + meson.project_version() + '"', language: 'cpp')

# =============================================================================
# Include Directories
# =============================================================================

# Add project root to include path so all code can use project-relative includes
# (e.g., #include "transport/coro/coroIoContext.hpp")
add_project_arguments('-I' + meson.project_source_root(), language: 'cpp')

# =============================================================================
# Third-Party Dependencies
# =============================================================================

# Command-line parsing (CLI11) and JSON parsing (nlohmann_json)
# Override CLI11's warning_level to match ours and prevent D9025 warnings
cli11_sp = subproject('CLI11', required : true, default_options: ['warning_level=3'])
json_sp = subproject('nlohmann_json', required : true)

# Make CLI11 and nlohmann_json available to all subdirectories
cli11_dep = declare_dependency()
if cli11_sp.found()
  cli11_dep = dependency('CLI11', fallback: ['CLI11', 'CLI11_dep'])
endif

json_dep = declare_dependency()
if json_sp.found()
  json_dep = dependency('nlohmann_json', fallback: ['nlohmann_json', 'nlohmann_json_dep'])
endif

# ZeroTier networking library (via wrapper for CMake integration)
libzt_sp = subproject('libzt-wrapper')
libzt_dep = libzt_sp.get_variable('libzt_dep')

# Get shared library path for development builds
# Reconstruct the path here to avoid Meson sandbox violations
if host_machine.system() == 'windows'
  libzt_lib_path = libzt_sp.get_variable('libzt_lib_path')
  libzt_dll_file = join_paths(libzt_lib_path, 'zt-shared.dll')
elif host_machine.system() == 'linux'
  libzt_lib_path = libzt_sp.get_variable('libzt_lib_path')
  libzt_so_file = join_paths(libzt_lib_path, 'libzt.so')
elif host_machine.system() == 'darwin'
  libzt_lib_path = libzt_sp.get_variable('libzt_lib_path')
  libzt_dylib_file = join_paths(libzt_lib_path, 'libzt.dylib')
endif

# Local shared utilities (logger, protocol definitions, etc.)
# Pass the project version to the shared subproject via default_project_options
shared_sp = subproject('shared', default_options: ['project_version=' + meson.project_version()])
shared_dep = shared_sp.get_variable('shared_dep')

# =============================================================================
# Build Targets
# =============================================================================

if get_option('build_manager')
  subdir('manager')
endif

if get_option('build_worker')
  subdir('worker')
endif

# Serialization tests (optional, for evaluating FlatBuffers/Cap'n Proto)
if get_option('build_serialize_tests')
  subdir('serialize')
endif

# =============================================================================
# Installation
# =============================================================================

# Install documentation
doc_dir = get_option('datadir') / 'doc' / 'task-messenger'
install_data('LICENSE', install_dir: doc_dir)

# Install configuration templates
config_dir = get_option('sysconfdir') / 'task-messenger'
install_data('config/config-manager.json', 'config/config-worker.json', install_dir: config_dir)

# Install manager's ZeroTier identity files (public/secret keys only)
install_data(
  'config/vn-manager-identity/identity.public',
  'config/vn-manager-identity/identity.secret',
  install_dir: config_dir / 'vn-manager-identity'
)

# =============================================================================
# Documentation Generation (Optional)
# =============================================================================

doxygen = find_program('doxygen', required : false)
if doxygen.found()
  # Configure Doxyfile with absolute paths for flexible builddir location
  conf = configuration_data()
  conf.set('SRC_DIR', meson.project_source_root())
  conf.set('OUT_DIR', join_paths(meson.project_build_root(), 'doxygen'))
  
  doxyfile_cfg = configure_file(
    input : join_paths(meson.project_source_root(), 'docs', 'Doxyfile.in'),
    output : 'Doxyfile',
    configuration : conf
  )

  custom_target(
    'docs',
    command : [doxygen, doxyfile_cfg],
    output : 'doxygen.log',
    build_by_default : false,
    capture : true
  )
else
  message('Doxygen not found: documentation generation unavailable')
endif