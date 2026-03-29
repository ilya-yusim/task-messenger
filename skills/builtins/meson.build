# =============================================================================
# Built-in Skills
# =============================================================================
#
# Self-registering skill implementations generated from .skill.toml files.
#
# Adding a new skill:
# 1. Create SkillName.skill.toml with schema + metadata
# 2. Create SkillName_impl.hpp with compute() + test data
# 3. Add a skill_gen + schema_gen target pair below
# 4. Run meson setup --reconfigure

# =============================================================================
# Code Generator
# =============================================================================

py = find_program('python3', 'python')
skill_gen_script = meson.project_source_root() / 'skills' / 'codegen' / 'skill_generator.py'

skill_gen_env = environment()
skill_gen_env.set('PYTHONDONTWRITEBYTECODE', '1')

# =============================================================================
# FusedMultiplyAddSkill
# =============================================================================

fma_skill_gen = custom_target(
  'FusedMultiplyAddSkill_codegen',
  input: 'FusedMultiplyAddSkill.skill.toml',
  output: ['FusedMultiplyAddSkill.fbs', 'FusedMultiplyAddSkill_gen.hpp', 'FusedMultiplyAddSkill.cpp'],
  command: [
    py, skill_gen_script,
    '@INPUT@',
    '--outdir', '@OUTDIR@',
    '--srcdir', meson.current_source_dir(),
  ],
  env: skill_gen_env,
)

fma_schema_gen = custom_target(
  'FusedMultiplyAddSkill_generated',
  input: fma_skill_gen[0],
  output: 'FusedMultiplyAddSkill_generated.h',
  command: [flatc_prog, '--cpp', '--gen-mutable', '-o', '@OUTDIR@', '@INPUT@'],
)

# =============================================================================
# VectorMathSkill
# =============================================================================

vector_math_skill_gen = custom_target(
  'VectorMathSkill_codegen',
  input: 'VectorMathSkill.skill.toml',
  output: ['VectorMathSkill.fbs', 'VectorMathSkill_gen.hpp', 'VectorMathSkill.cpp'],
  command: [
    py, skill_gen_script,
    '@INPUT@',
    '--outdir', '@OUTDIR@',
    '--srcdir', meson.current_source_dir(),
  ],
  env: skill_gen_env,
)

vector_math_schema_gen = custom_target(
  'VectorMathSkill_generated',
  input: vector_math_skill_gen[0],
  output: 'VectorMathSkill_generated.h',
  command: [flatc_prog, '--cpp', '--gen-mutable', '-o', '@OUTDIR@', '@INPUT@'],
)

# =============================================================================
# MathOperationSkill
# =============================================================================

math_op_skill_gen = custom_target(
  'MathOperationSkill_codegen',
  input: 'MathOperationSkill.skill.toml',
  output: ['MathOperationSkill.fbs', 'MathOperationSkill_gen.hpp', 'MathOperationSkill.cpp'],
  command: [
    py, skill_gen_script,
    '@INPUT@',
    '--outdir', '@OUTDIR@',
    '--srcdir', meson.current_source_dir(),
  ],
  env: skill_gen_env,
)

math_op_schema_gen = custom_target(
  'MathOperationSkill_generated',
  input: math_op_skill_gen[0],
  output: 'MathOperationSkill_generated.h',
  command: [flatc_prog, '--cpp', '--gen-mutable', '-o', '@OUTDIR@', '@INPUT@'],
)

# =============================================================================
# StringReversalSkill
# =============================================================================

string_rev_skill_gen = custom_target(
  'StringReversalSkill_codegen',
  input: 'StringReversalSkill.skill.toml',
  output: ['StringReversalSkill.fbs', 'StringReversalSkill_gen.hpp', 'StringReversalSkill.cpp'],
  command: [
    py, skill_gen_script,
    '@INPUT@',
    '--outdir', '@OUTDIR@',
    '--srcdir', meson.current_source_dir(),
  ],
  env: skill_gen_env,
)

string_rev_schema_gen = custom_target(
  'StringReversalSkill_generated',
  input: string_rev_skill_gen[0],
  output: 'StringReversalSkill_generated.h',
  command: [flatc_prog, '--cpp', '--gen-mutable', '-o', '@OUTDIR@', '@INPUT@'],
)

# =============================================================================
# Aggregate targets
# =============================================================================

skills_builtin_schema_gen = [
  fma_schema_gen,
  fma_skill_gen[1],
  vector_math_schema_gen,
  vector_math_skill_gen[1],
  math_op_schema_gen,
  math_op_skill_gen[1],
  string_rev_schema_gen,
  string_rev_skill_gen[1],
]

skills_builtin_sources = [
  fma_skill_gen[2],
  vector_math_skill_gen[2],
  math_op_skill_gen[2],
  string_rev_skill_gen[2],
]

skills_builtin_inc = include_directories('.')

# =============================================================================
# BLAS Skills (conditional on enable_blas_skills option)
# =============================================================================

if enable_blas_skills
  subdir('blas')
  skills_builtin_schema_gen += skills_blas_schema_gen
  skills_builtin_sources += skills_blas_sources
  skills_builtin_inc = [skills_builtin_inc, skills_blas_inc]
endif
