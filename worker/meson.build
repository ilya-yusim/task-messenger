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
executable(
  'worker',
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

