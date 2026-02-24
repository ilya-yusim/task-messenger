# =============================================================================
# Skills Library Build Configuration
# =============================================================================
#
# Shared skills library used by both manager and worker.
#
# Components:
# - builtins/  : Built-in skill implementations with FlatBuffers schemas
# - schemas/   : Schema dependency aggregation
# - registry/  : Skill metadata and registration infrastructure  
# - handlers/  : ISkillHandler interface
#
# Dependencies: flatbuffers_dep, flatc_prog (from root meson.build)

# =============================================================================
# Build Subdirectories
# =============================================================================

# Process builtins first - generates schema headers
subdir('builtins')
# Process schemas - aggregates schema dependencies
subdir('schemas')
subdir('registry')
subdir('handlers')

# =============================================================================
# Skills Static Library
# =============================================================================

skills_lib = static_library(
  'skills',
  sources: [
    skills_registry_sources,
    skills_handler_sources,
    skills_builtin_sources,
  ],
  dependencies: [
    skills_schema_dep,
    shared_dep,  # For logger.hpp
  ],
  include_directories: [
    include_directories('.'),
    skills_registry_inc,
    skills_handler_inc,
    skills_builtin_inc,
  ],
)

# =============================================================================
# Skills Dependency
# =============================================================================

# Use this dependency in manager and worker to access skills functionality
# NOTE: link_whole is required because builtin skills use static initialization
# for self-registration. Without it, the linker discards unreferenced object files.
skills_dep = declare_dependency(
  link_whole: skills_lib,
  dependencies: [skills_schema_dep],
  include_directories: [
    include_directories('.'),
    skills_registry_inc,
    skills_handler_inc,
  ],
)
