# =============================================================================
# Worker Build Configuration
# =============================================================================

# =============================================================================
# Optional UI Support
# =============================================================================

# FTXUI provides an interactive terminal UI for worker status monitoring
# When available, enables the --ui flag for real-time task/connection visualization
ftxui_dep = dependency('ftxui', fallback: ['ftxui-wrapper','ftxui_dep'], required: false)
ui_cargs = []
if ftxui_dep.found()
  ui_cargs += ['-DHAS_WORKER_UI=1']
endif

# =============================================================================
# Worker Executable
# =============================================================================

# Unified worker application supporting both blocking and async runtime modes
# Handles task processing, manager connection, and optional interactive UI
worker_exe = executable(
  'tm-worker',
  sources: [
    'workerMain.cpp',
    'runtime/IRuntimeMode.hpp',
    'runtime/BlockingRuntime.cpp',
    'runtime/BlockingRuntime.hpp',
    'runtime/AsyncRuntime.cpp',
    'runtime/AsyncRuntime.hpp',
    'session/WorkerSession.hpp',
    'session/WorkerSession.cpp',
    'processor/TaskProcessor.hpp',
    'processor/TaskProcessor.cpp',
    '../transport/coro/coroIoContext.cpp',
    '../transport/coro/coroSocketAdapter.cpp',
    '../transport/socket/zerotier/ZeroTierSocket.cpp',
    '../transport/socket/zerotier/ZeroTierNodeService.cpp',
    '../transport/socket/SocketFactory.cpp',
    '../transport/socket/SocketTypeOptions.cpp',
    'WorkerOptions.cpp',
  ],
  dependencies: [
    shared_dep,              # Shared utilities (logger, protocol, etc.)
    libzt_dep,               # ZeroTier networking library
    cli11_dep,               # Command-line parsing
    json_dep,                # JSON configuration parsing
    ftxui_dep,               # Optional terminal UI library
  ],
  cpp_args: ui_cargs,        # Define HAS_WORKER_UI if FTXUI is available
  install: true,
  build_by_default: true,
)
# =============================================================================
# Development Build: Copy Shared Library to Build Directory
# =============================================================================

# Copy shared library next to the worker executable for development builds
# This allows running the executable directly from the build directory without installation
# Note: When compiling specific targets (meson compile worker/worker), the DLL copy
# may not execute. Use `meson compile` without arguments to build everything.
if host_machine.system() == 'windows'
  # Windows: Copy zt-shared.dll
  custom_target('copy-zt-dll-worker',
    output: 'zt-shared.dll',
    command: ['powershell', '-Command', 'Copy-Item', '-Path', libzt_dll_file, '-Destination', '@OUTPUT@', '-Force'],
    build_by_default: true,
    install: false,
  )
elif host_machine.system() == 'linux'
  # Linux: Copy libzt.so
  custom_target('copy-zt-so-worker',
    output: 'libzt.so',
    command: ['cp', libzt_so_file, '@OUTPUT@'],
    build_by_default: true,
    install: false,
  )
elif host_machine.system() == 'darwin'
  # macOS: Copy libzt.dylib
  custom_target('copy-zt-dylib-worker',
    output: 'libzt.dylib',
    command: ['cp', libzt_dylib_file, '@OUTPUT@'],
    build_by_default: true,
    install: false,
  )
endif

if ftxui_dep.found()
  executable(
    'worker-ui-demo',
    sources: [
      '../extras/worker_ui/WorkerUIDemo.cpp',
    ],
    dependencies: [
      shared_dep,
      ftxui_dep,
    ],
    build_by_default: false,
    install: false,
  )
endif

