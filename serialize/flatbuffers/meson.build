# FlatBuffers serialization test
#
# This builds a test executable demonstrating FlatBuffers usage for
# skill-based task messaging serialization.
#
# FlatBuffers dependency (flatbuffers_dep) and compiler (flatc_prog)
# are defined in root meson.build using native WrapDB wrap.

# Schema file
skill_task_fbs = files('schemas/skill_task.fbs')

# Generate C++ headers from schema
# The flatc compiler generates a _generated.h file from the .fbs schema
# --gen-mutable enables mutate_*() methods for scalar fields
flatbuffers_generated = custom_target(
  'flatbuffers_generated',
  input: skill_task_fbs,
  output: 'skill_task_generated.h',
  command: [flatc_prog, '--cpp', '--gen-mutable', '-o', '@OUTDIR@', '@INPUT@'],
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
