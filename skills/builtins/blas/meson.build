# =============================================================================
# BLAS Skills
# =============================================================================
#
# Skills wrapping CBLAS routines (OpenBLAS or Apple Accelerate).
# Each skill maps one BLAS/LAPACK routine to the Skill<Derived> pattern.
#
# Adding a new BLAS skill (generated):
# 1. Create SkillName.skill.toml with schema + metadata
# 2. Create SkillName_impl.hpp with compute() + test data
# 3. Add a skill_gen + schema_gen target pair below

# =============================================================================
# Code Generator
# =============================================================================

py = find_program('python3', 'python')
skill_gen_script = meson.project_source_root() / 'skills' / 'codegen' / 'skill_generator.py'

# Prevent Python from writing __pycache__/ into the source tree
skill_gen_env = environment()
skill_gen_env.set('PYTHONDONTWRITEBYTECODE', '1')

# =============================================================================
# DgemmSkill — Generated from .skill.toml
# =============================================================================

# Python generator → .fbs + _gen.hpp + .cpp
dgemm_skill_gen = custom_target(
  'DgemmSkill_codegen',
  input: 'DgemmSkill.skill.toml',
  output: ['DgemmSkill.fbs', 'DgemmSkill_gen.hpp', 'DgemmSkill.cpp'],
  command: [
    py, skill_gen_script,
    '@INPUT@',
    '--outdir', '@OUTDIR@',
    '--srcdir', meson.current_source_dir(),
  ],
  env: skill_gen_env,
)

# flatc → _generated.h (from the generated .fbs)
dgemm_schema_gen = custom_target(
  'DgemmSkill_generated',
  input: dgemm_skill_gen[0],  # DgemmSkill.fbs
  output: 'DgemmSkill_generated.h',
  command: [flatc_prog, '--cpp', '--gen-mutable', '-o', '@OUTDIR@', '@INPUT@'],
)

# All BLAS schema gen targets (appended by parent meson.build)
skills_blas_schema_gen = [
  dgemm_schema_gen,
  dgemm_skill_gen[1],  # DgemmSkill_gen.hpp (needed as source dep)
]

# =============================================================================
# BLAS Skill Sources
# =============================================================================

skills_blas_sources = [
  dgemm_skill_gen[2],  # DgemmSkill.cpp (generated)
]

skills_blas_inc = include_directories('.')
