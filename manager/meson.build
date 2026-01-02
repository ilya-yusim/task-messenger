# =============================================================================
# Manager Build Configuration
# =============================================================================

# =============================================================================
# ZeroTier Identity Setup
# =============================================================================

# Copy the manager's ZeroTier identity directory to build directory
# This ensures the manager uses a consistent network identity across builds
src_identity = meson.current_source_dir() / '.vn_manager_identity'
dst_identity = meson.current_build_dir() / '.vn_manager_identity'

python = import('python').find_installation()
run_command(
  python, '-c',
  '''
import shutil, sys, os
src, dst = sys.argv[1], sys.argv[2]
if os.path.exists(src):
    if os.path.exists(dst):
        shutil.rmtree(dst)
    shutil.copytree(src, dst)
''',
  src_identity, dst_identity,
  check: true
)

# =============================================================================
# Manager Executable
# =============================================================================

# Coroutine-based manager using CoroSocketAdapter architecture
# Handles task distribution, worker connections, and session management
manager_exe = executable(
  'manager', 
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