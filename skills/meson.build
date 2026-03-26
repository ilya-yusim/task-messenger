# =============================================================================
# Skills Library Build Configuration
# =============================================================================
#
# Shared skills library used by both dispatcher and worker.
#
# Components:
# - builtins/  : Built-in skill implementations with FlatBuffers schemas
# - registry/  : Skill metadata, registration, and interface infrastructure
#
# Dependencies: flatbuffers_dep, flatc_prog (from root meson.build)

# =============================================================================
# Build Subdirectories
# =============================================================================

# Process builtins first - generates schema headers
subdir('builtins')

# Aggregate schema headers from builtins with FlatBuffers runtime
skills_schema_dep = declare_dependency(
  sources: skills_builtin_schema_gen,
  dependencies: [flatbuffers_dep],
)

subdir('registry')

# =============================================================================
# Skills Static Library
# =============================================================================

# Collect optional dependencies for skills library
skills_optional_deps = []
if enable_blas_skills
  skills_optional_deps += [blas_dep]
endif

skills_lib = static_library(
  'skills',
  sources: [
    skills_registry_sources,
    skills_builtin_sources,
  ],
  dependencies: [
    skills_schema_dep,
    shared_dep,  # For logger.hpp
    skills_optional_deps,
  ],
  include_directories: [
    include_directories('.'),
    skills_registry_inc,
    skills_builtin_inc,
  ],
)

# =============================================================================
# Skills Dependency
# =============================================================================

# Use this dependency in dispatcher and worker to access skills functionality
# NOTE: link_whole is required because builtin skills use static initialization
# for self-registration. Without it, the linker discards unreferenced object files.
skills_dep = declare_dependency(
  link_whole: skills_lib,
  dependencies: [skills_schema_dep],
  include_directories: [
    include_directories('.'),
    skills_registry_inc,
  ],
)
