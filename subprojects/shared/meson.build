project('shared', 'cpp', default_options : ['cpp_std=c++20'])

shared_include = include_directories('.')

# External dependencies needed by Options
cli11_dep = dependency('CLI11', required: false)
json_dep = dependency('nlohmann_json', required: false)

# Core shared utilities (renamed from processUtils) including Options
shared_core_lib = static_library(
  'shared_core',
  [
    'processUtils.cpp',
    'options/Options.cpp',
  ],
  dependencies: [cli11_dep, json_dep],
  install: false
)
# Export the include directory dependency and process_utils library for other subprojects
shared_dep = declare_dependency(
  include_directories: shared_include,
  link_with: shared_core_lib,
  dependencies: [cli11_dep, json_dep]
)


