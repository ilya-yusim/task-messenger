# =============================================================================
# BLAS Skills
# =============================================================================
#
# Skills wrapping CBLAS routines (OpenBLAS or Apple Accelerate).
# Each skill maps one BLAS/LAPACK routine to the Skill<Derived> pattern.
#
# Adding a new BLAS skill:
# 1. Create SkillName.fbs with FlatBuffers schema
# 2. Create SkillName.hpp with Skill<Derived> implementation
# 3. Create SkillName.cpp with REGISTER_SKILL_CLASS macro
# 4. Add schema gen target and .cpp to lists below

# =============================================================================
# Schema Code Generation
# =============================================================================

dgemm_schema_gen = custom_target(
  'DgemmSkill_generated',
  input: 'DgemmSkill.fbs',
  output: 'DgemmSkill_generated.h',
  command: [flatc_prog, '--cpp', '--gen-mutable', '-o', '@OUTDIR@', '@INPUT@'],
)

# All BLAS schema gen targets (appended by parent meson.build)
skills_blas_schema_gen = [
  dgemm_schema_gen,
]

# =============================================================================
# BLAS Skill Sources
# =============================================================================

skills_blas_sources = files(
  'DgemmSkill.cpp',
)

skills_blas_inc = include_directories('.')
