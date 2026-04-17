# =============================================================================
# Rendezvous Client Library Build Configuration
# =============================================================================

# Shared rendezvous client library used by generators, workers, and dispatchers
# to register/discover endpoints via the rendezvous service.

tm_rendezvous_lib = static_library(
  'tm-rendezvous',
  sources: [
    'RendezvousOptions.cpp',
    'RendezvousClient.cpp',
  ],
  dependencies: [
    tm_transport_dep,
    shared_dep,
    json_dep,
    cli11_dep,
  ],
  build_by_default: true,
)

rendezvous_dep = declare_dependency(
  link_with: tm_rendezvous_lib,
  dependencies: [tm_transport_dep, shared_dep, json_dep, cli11_dep],
)
