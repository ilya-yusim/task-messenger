# Task processing using FlatBuffers serialization
#
# This directory contains the task handler infrastructure for processing
# typed task requests using FlatBuffers for efficient serialization.
#
# FlatBuffers dependency (flatbuffers_dep) and compiler (flatc_prog)
# are defined in root meson.build using native WrapDB wrap.

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
  command: [flatc_prog, '--cpp', '--gen-mutable', '-o', '@OUTDIR@', '@INPUT@'],
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
