# meson.build for capnproto-wrapper
#
# This wraps the Cap'n Proto cmake build and exposes it as a meson dependency.
# Cap'n Proto provides zero-copy, schema-based serialization.
#
# Usage from main project:
#   capnp_sp = subproject('capnproto-wrapper')
#   capnp_dep = capnp_sp.get_variable('capnproto_dep')

project('capnproto-wrapper', 'cpp',
  version: '1.1.0',
  default_options: ['cpp_std=c++17']
)

cmake = import('cmake')

# Configure cmake options for capnproto
capnp_opts = cmake.subproject_options()

# Workaround for CMake 4.0+ compatibility (capnproto still uses cmake_minimum_required 3.4)
capnp_opts.add_cmake_defines({
  'CMAKE_POLICY_VERSION_MINIMUM': '3.5',
  'BUILD_TESTING': false,
  'WITH_OPENSSL': 'OFF',
  'WITH_ZLIB': 'OFF',
  'WITH_FIBERS': 'OFF',
  # Force MSVC to treat .c++ files as C++ (workaround for non-standard extension)
  'CMAKE_CXX_FLAGS': '/TP',
})

# Build capnproto as a cmake subproject
# The capnproto source should exist at ../capnproto/ (downloaded via capnproto.wrap)
capnp_proj = cmake.subproject('capnproto', options: capnp_opts)

# Get the library dependencies
kj_dep = capnp_proj.dependency('kj')
kj_async_dep = capnp_proj.dependency('kj-async')
capnp_dep = capnp_proj.dependency('capnp')

# Export combined dependency for convenience
capnproto_dep = declare_dependency(
  dependencies: [kj_dep, capnp_dep]
)

# Full async dependency (includes kj-async for promise/async patterns)
capnproto_async_dep = declare_dependency(
  dependencies: [kj_dep, kj_async_dep, capnp_dep]
)

# Also export individual deps
kj_lib_dep = kj_dep
kj_async_lib_dep = kj_async_dep
capnp_lib_dep = capnp_dep

# Export compiler tools for schema compilation
capnp_tool = capnp_proj.target('capnp_tool')
capnpc_cpp = capnp_proj.target('capnpc_cpp')
