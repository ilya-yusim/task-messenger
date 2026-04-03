# =============================================================================
# Dispatcher Library Build Configuration
# =============================================================================

# Dispatcher infrastructure library providing transport server, session management,
# task message pool, and the DispatcherApp harness. Generators link this library
# to get the full dispatcher runtime without duplicating plumbing code.

# Optional compile-time dashboard path override (runtime discovery used when empty).
_dashboard_dir = get_option('dashboard_dir')
_dispatcher_cpp_args = []
if _dashboard_dir != ''
  _dispatcher_cpp_args += ['-DDASHBOARD_DIR="' + _dashboard_dir + '"']
  message('dispatcher: DASHBOARD_DIR set to "' + _dashboard_dir + '"')
else
  message('dispatcher: DASHBOARD_DIR not set — runtime discovery enabled')
endif

tm_dispatcher_lib = static_library(
  'tm-dispatcher',
  sources: [
    'DispatcherApp.cpp',
    'monitoring/HttpRequestHandler.cpp',
    'monitoring/MonitoringOptions.cpp',
    'monitoring/MonitoringService.cpp',
    'monitoring/MonitoringSnapshotBuilder.cpp',
    'transport/AsyncTransportOptions.cpp',
    'transport/AsyncTransportServer.hpp',
    'transport/AsyncTransportServer.cpp',
    '../message/TaskMessagePool.cpp',
    'session/Session.cpp',
    'session/SessionManager.cpp',
  ],
  cpp_args: _dispatcher_cpp_args,
  dependencies: [
    tm_transport_dep,   # Shared transport library (coro IO, sockets, ZeroTier)
    shared_dep,         # Shared utilities (logger, protocol, etc.)
    libzt_dep,          # ZeroTier networking library
    cli11_dep,          # Command-line parsing
    json_dep,           # JSON configuration parsing
    httplib_dep,        # Browser-facing HTTP server abstraction
    flatbuffers_dep,    # FlatBuffers (transitively needed by PayloadBuffer.hpp)
  ],
  build_by_default: true,
)

# Dependency for generators and other consumers of the dispatcher library.
# Note: does NOT include skills_dep — generators must link skills_dep
# directly with link_whole to ensure static skill registration works.
tm_dispatcher_dep = declare_dependency(
  link_with: tm_dispatcher_lib,
  dependencies: [tm_transport_dep, shared_dep, libzt_dep, cli11_dep, json_dep, httplib_dep, flatbuffers_dep],
)

# Note: DLL/shared library copies are handled by generator executables,
# not by the dispatcher library.
