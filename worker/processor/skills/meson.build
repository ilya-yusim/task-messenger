# Task processing using FlatBuffers serialization
#
# This directory contains the task handler infrastructure for processing
# typed task requests using FlatBuffers for efficient serialization.

# =============================================================================
# FlatBuffers Configuration
# =============================================================================

# Import CMake module for FlatBuffers subproject
cmake = import('cmake')

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

# =============================================================================
# Schema Code Generation
# =============================================================================

# Schema file location (shared with serialize tests)
skill_task_fbs = files('../../../serialize/flatbuffers/schemas/skill_task.fbs')

# Generate C++ headers from schema
# --gen-mutable enables mutate_*() methods for scalar fields
skill_task_generated = custom_target(
  'skill_task_generated_worker',
  input: skill_task_fbs,
  output: 'skill_task_generated.h',
  command: [flatc, '--cpp', '--gen-mutable', '-o', '@OUTDIR@', '@INPUT@'],
)

# =============================================================================
# Skills Library
# =============================================================================

# Include directory for generated headers
skills_inc = include_directories('.')

# Source files for skill processing
skills_sources = files(
  'SkillProcessor.cpp',
)

# Declare skills dependency for use by worker
skills_dep = declare_dependency(
  sources: [skill_task_generated, skills_sources],
  include_directories: skills_inc,
  dependencies: [flatbuffers_dep],
)
