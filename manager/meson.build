# =============================================================================
# Manager Library Build Configuration
# =============================================================================

# Manager infrastructure library providing transport server, session management,
# task message pool, and the ManagerApp harness. Generators link this library
# to get the full manager runtime without duplicating plumbing code.

tm_manager_lib = static_library(
  'tm-manager',
  sources: [
    'ManagerApp.cpp',
    'transport/AsyncTransportOptions.cpp',
    'transport/AsyncTransportServer.hpp',
    'transport/AsyncTransportServer.cpp',
    '../message/TaskMessagePool.cpp',
    'session/Session.cpp',
    'session/SessionManager.cpp',
  ],
  dependencies: [
    tm_transport_dep,   # Shared transport library (coro IO, sockets, ZeroTier)
    shared_dep,         # Shared utilities (logger, protocol, etc.)
    libzt_dep,          # ZeroTier networking library
    cli11_dep,          # Command-line parsing
    json_dep,           # JSON configuration parsing
    flatbuffers_dep,    # FlatBuffers (transitively needed by PayloadBuffer.hpp)
  ],
  build_by_default: true,
)

# Dependency for generators and other consumers of the manager library.
# Note: does NOT include skills_dep — generators must link skills_dep
# directly with link_whole to ensure static skill registration works.
tm_manager_dep = declare_dependency(
  link_with: tm_manager_lib,
  dependencies: [tm_transport_dep, shared_dep, libzt_dep, cli11_dep, json_dep, flatbuffers_dep],
)

# Note: DLL/shared library copies are handled by generator executables,
# not by the manager library.
