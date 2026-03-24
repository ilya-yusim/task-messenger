# =============================================================================
# Interactive Generator Build Configuration
# =============================================================================

tm_generator_interactive = executable(
  'tm-generator-interactive',
  sources: [
    'interactiveMain.cpp',
    'InteractiveGenerator.cpp',
    '../common/TaskGenerator.cpp',
    '../common/SkillTestIterator.cpp',
    '../common/VerificationHelper.cpp',
    '../common/GeneratorOptions.cpp',
  ],
  dependencies: [
    tm_dispatcher_dep,
    skills_dep,       # link_whole for static skill registration
    shared_dep,
    cli11_dep,
    json_dep,
  ],
  build_by_default: true,
  install: true,
)

# Copy shared library next to the executable for development builds
if host_machine.system() == 'windows'
  custom_target('copy-zt-dll-gen-interactive',
    output: 'zt-shared-gen-interactive.dll',
    command: ['powershell', '-Command', 'Copy-Item', '-Path', libzt_dll_file, '-Destination', '@OUTPUT@', '-Force'],
    build_by_default: true,
    install: false,
  )
endif
