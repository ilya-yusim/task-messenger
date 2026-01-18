# =============================================================================
# Manager Build Configuration
# =============================================================================

# =============================================================================
# Manager Executable
# =============================================================================

# Coroutine-based manager using CoroSocketAdapter architecture
# Handles task distribution, worker connections, and session management
manager_exe = executable(
  'tm-manager', 
sources: [
  'managerMain.cpp',
  'transport/AsyncTransportOptions.cpp',
  'transport/AsyncTransportServer.hpp',
  'transport/AsyncTransportServer.cpp',
  '../message/TaskMessagePool.cpp',
  'TaskGenerator.cpp',
  'session/Session.cpp',
  'session/SessionManager.cpp',
  '../transport/socket/SocketFactory.cpp',
  '../transport/coro/coroSocketAdapter.cpp',
  '../transport/socket/zerotier/ZeroTierSocket.cpp',
  '../transport/coro/coroIoContext.cpp',
  '../transport/socket/zerotier/ZeroTierNodeService.cpp',
  '../transport/socket/SocketTypeOptions.cpp',
],
dependencies: [
  shared_dep,      # Shared utilities (logger, protocol, etc.)
  libzt_dep,       # ZeroTier networking library
  cli11_dep,       # Command-line parsing
  json_dep,        # JSON configuration parsing
],
install: true,
build_by_default: true,
)

# =============================================================================
# Development Build: Copy Shared Library to Build Directory
# =============================================================================

# Copy shared library next to the manager executable for development builds
# This allows running the executable directly from the build directory without installation
# Note: When compiling specific targets (meson compile manager/manager), the DLL copy
# may not execute. Use `meson compile` without arguments to build everything.
if host_machine.system() == 'windows'
  # Windows: Copy zt-shared.dll
  custom_target('copy-zt-dll-manager',
    output: 'zt-shared.dll',
    command: ['powershell', '-Command', 'Copy-Item', '-Path', libzt_dll_file, '-Destination', '@OUTPUT@', '-Force'],
    build_by_default: true,
    install: false,
  )
elif host_machine.system() == 'linux'
  # Linux: Copy libzt.so
  custom_target('copy-zt-so-manager',
    output: 'libzt.so',
    command: ['cp', libzt_so_file, '@OUTPUT@'],
    build_by_default: true,
    install: false,
  )
endif
