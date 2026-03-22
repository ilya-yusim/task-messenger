# =============================================================================
# Transport Library Build Configuration
# =============================================================================

# Shared transport library used by both manager and worker.
# Contains coroutine I/O context, socket adapters, and ZeroTier integration.

tm_transport_lib = static_library(
  'tm-transport',
  sources: [
    'coro/coroIoContext.cpp',
    'coro/coroSocketAdapter.cpp',
    'socket/SocketFactory.cpp',
    'socket/SocketTypeOptions.cpp',
    'socket/zerotier/ZeroTierSocket.cpp',
    'socket/zerotier/ZeroTierNodeService.cpp',
  ],
  dependencies: [
    shared_dep,
    libzt_dep,
    cli11_dep,
    json_dep,
  ],
  build_by_default: true,
)

tm_transport_dep = declare_dependency(
  link_with: tm_transport_lib,
  dependencies: [shared_dep, libzt_dep, cli11_dep, json_dep],
)
