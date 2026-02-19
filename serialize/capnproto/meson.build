# Cap'n Proto serialization tests
#
# This builds a test executable demonstrating Cap'n Proto usage for
# skill-based task messaging serialization, including async/promise patterns.
#
# Cap'n Proto is a CMake project, so we use our capnproto-wrapper.

capnproto_sp = subproject('capnproto-wrapper')

# Get dependencies with async support (includes kj, kj-async, capnp)
capnproto_async_dep = capnproto_sp.get_variable('capnproto_async_dep')

# On Windows, kj-async needs ws2_32 for socket functions
if host_machine.system() == 'windows'
  ws2_32_dep = meson.get_compiler('cpp').find_library('ws2_32')
  capnproto_async_dep = declare_dependency(
    dependencies: [capnproto_async_dep, ws2_32_dep]
  )
endif

# Get compiler tools
capnp_tool = capnproto_sp.get_variable('capnp_tool')
capnpc_cpp = capnproto_sp.get_variable('capnpc_cpp')

# Schema file
skill_task_capnp = files('schemas/skill_task.capnp')

# The capnp include paths (needed for imports like /capnp/c++.capnp)
capnp_include = meson.project_source_root() / 'subprojects/capnproto/c++/src'

# Generate C++ from schema
# The capnp compiler generates .h and .c++ files
# We use a helper script to rename .c++ to .cpp for MSVC compatibility
python = find_program('python3', 'python')
capnp_compile_helper = files('capnp_compile_helper.py')

capnproto_generated = custom_target(
  'capnproto_generated',
  input: skill_task_capnp,
  output: ['skill_task.capnp.h', 'skill_task.capnp.cpp'],
  command: [
    python,
    capnp_compile_helper,
    capnp_tool.full_path(),
    capnpc_cpp.full_path(),
    capnp_include,
    meson.current_source_dir() / 'schemas',
    '@OUTDIR@',
    '@INPUT@'
  ],
  depends: [capnp_tool, capnpc_cpp],
)

# Build test executable
test_capnproto_exe = executable(
  'test_capnproto',
  sources: ['test_capnproto.cpp', capnproto_generated],
  dependencies: [capnproto_async_dep],
  include_directories: include_directories('.'),
  build_by_default: true,
)

# Register as a test
test('capnproto_skill_test', test_capnproto_exe)
