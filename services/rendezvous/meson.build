# =============================================================================
# Rendezvous Service Binary Build Configuration
# =============================================================================

# Standalone rendezvous service: joins the virtual network, accepts registrations
# from generators, serves endpoint discovery to workers, and hosts the
# monitoring dashboard over regular HTTP.

tm_rendezvous_exe = executable(
  'tm-rendezvous',
  sources: [
    'main.cpp',
    'RendezvousServer.cpp',
    'RendezvousHttpHandler.cpp',
  ],
  dependencies: [
    rendezvous_dep,            # Shared rendezvous client/protocol library
    tm_transport_dep,          # Transport library (sockets, ZeroTier)
    shared_dep,                # Shared utilities (logger, options, etc.)
    libzt_dep,                 # ZeroTier networking library
    cli11_dep,                 # Command-line parsing
    json_dep,                  # JSON parsing
    httplib_dep,               # cpp-httplib for HTTP dashboard
  ],
  install: true,
  build_by_default: true,
)

# =============================================================================
# Development Build: Copy Shared Library to Build Directory
# =============================================================================

if host_machine.system() == 'windows'
  custom_target('copy-zt-dll-rendezvous',
    output: 'zt-shared.dll',
    command: ['powershell', '-Command', 'Copy-Item', '-Path', libzt_dll_file, '-Destination', '@OUTPUT@', '-Force'],
    build_by_default: true,
    install: false,
  )
elif host_machine.system() == 'linux'
  custom_target('copy-zt-so-rendezvous',
    output: 'libzt.so',
    command: ['cp', '-f', libzt_so_file, '@OUTPUT@'],
    build_by_default: true,
    install: false,
  )
elif host_machine.system() == 'darwin'
  custom_target('copy-zt-dylib-rendezvous',
    output: 'libzt.dylib',
    command: ['cp', '-f', libzt_dylib_file, '@OUTPUT@'],
    build_by_default: true,
    install: false,
  )
endif
