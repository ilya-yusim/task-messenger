# =============================================================================
# Built-in Skills
# =============================================================================
#
# Self-registering skill implementations.
# Each skill file contains both the handler, registration code, and schema.
#
# Adding a new skill:
# 1. Create SkillName.fbs with FlatBuffers schema
# 2. Create SkillName.cpp with handler class and REGISTER_SKILL macro
# 3. Add .fbs to skill_schemas list and .cpp to skills_builtin_sources below
# 4. Run meson setup --reconfigure

# =============================================================================
# Schema Code Generation
# =============================================================================

# Common schema (no dependencies)
common_schema_gen = custom_target(
  'Common_generated',
  input: 'Common.fbs',
  output: 'Common_generated.h',
  command: [flatc_prog, '--cpp', '--gen-mutable', '-o', '@OUTDIR@', '@INPUT@'],
)

# String reversal (no dependencies on Common)
string_reversal_schema_gen = custom_target(
  'StringReversalSkill_generated',
  input: 'StringReversalSkill.fbs',
  output: 'StringReversalSkill_generated.h',
  command: [flatc_prog, '--cpp', '--gen-mutable', '-o', '@OUTDIR@', '@INPUT@'],
)

# Math operation (depends on Common for MathOperation enum)
math_operation_schema_gen = custom_target(
  'MathOperationSkill_generated',
  input: 'MathOperationSkill.fbs',
  output: 'MathOperationSkill_generated.h',
  command: [flatc_prog, '--cpp', '--gen-mutable', '-I', meson.current_source_dir(), '-o', '@OUTDIR@', '@INPUT@'],
  depends: [common_schema_gen],
)

# Vector math (depends on Common for MathOperation enum)
vector_math_schema_gen = custom_target(
  'VectorMathSkill_generated',
  input: 'VectorMathSkill.fbs',
  output: 'VectorMathSkill_generated.h',
  command: [flatc_prog, '--cpp', '--gen-mutable', '-I', meson.current_source_dir(), '-o', '@OUTDIR@', '@INPUT@'],
  depends: [common_schema_gen],
)

# Fused multiply-add (no dependencies on Common)
fused_multiply_add_schema_gen = custom_target(
  'FusedMultiplyAddSkill_generated',
  input: 'FusedMultiplyAddSkill.fbs',
  output: 'FusedMultiplyAddSkill_generated.h',
  command: [flatc_prog, '--cpp', '--gen-mutable', '-o', '@OUTDIR@', '@INPUT@'],
)

# All generated schema headers
skills_builtin_schema_gen = [
  common_schema_gen,
  string_reversal_schema_gen,
  math_operation_schema_gen,
  vector_math_schema_gen,
  fused_multiply_add_schema_gen,
]

# =============================================================================
# Skill Implementation Sources
# =============================================================================

skills_builtin_sources = files(
  'StringReversalSkill.cpp',
  'MathOperationSkill.cpp',
  'VectorMathSkill.cpp',
  'FusedMultiplyAddSkill.cpp',
)

skills_builtin_inc = include_directories('.')
