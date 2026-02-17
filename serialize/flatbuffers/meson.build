# FlatBuffers serialization test
#
# This builds a test executable demonstrating FlatBuffers usage for
# skill-based task messaging serialization.
#
# FlatBuffers is a CMake project, so we use Meson's CMake subproject support.

# Configure CMake options for FlatBuffers
flatbuffers_opts = cmake.subproject_options()
flatbuffers_opts.add_cmake_defines({
  'FLATBUFFERS_BUILD_TESTS': 'OFF',
  'FLATBUFFERS_BUILD_FLATC': 'ON',
  'FLATBUFFERS_BUILD_FLATHASH': 'OFF',
  'FLATBUFFERS_BUILD_GRPCTEST': 'OFF',
  'FLATBUFFERS_BUILD_SHAREDLIB': 'OFF',
})

# Use CMake subproject for FlatBuffers
flatbuffers_proj = cmake.subproject('flatbuffers', options: flatbuffers_opts)

flatbuffers_dep = flatbuffers_proj.dependency('flatbuffers')

# Get the flatc compiler from the CMake build
flatc = flatbuffers_proj.target('flatc')

# Schema file
skill_task_fbs = files('schemas/skill_task.fbs')

# Generate C++ headers from schema
# The flatc compiler generates a _generated.h file from the .fbs schema
# --gen-mutable enables mutate_*() methods for scalar fields
flatbuffers_generated = custom_target(
  'flatbuffers_generated',
  input: skill_task_fbs,
  output: 'skill_task_generated.h',
  command: [flatc, '--cpp', '--gen-mutable', '-o', '@OUTDIR@', '@INPUT@'],
)

# Build test executable
test_flatbuffers_exe = executable(
  'test_flatbuffers',
  sources: ['test_flatbuffers.cpp', flatbuffers_generated],
  dependencies: [flatbuffers_dep],
  include_directories: include_directories('.'),
  build_by_default: true,
)

# Register as a test
test('flatbuffers_skill_test', test_flatbuffers_exe)
