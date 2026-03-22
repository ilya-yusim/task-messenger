# =============================================================================
# Auto-Refill Generator Build Configuration
# =============================================================================

tm_generator_auto_refill = executable(
  'tm-generator-auto-refill',
  sources: [
    'autoRefillMain.cpp',
    '../common/TaskGenerator.cpp',
    '../common/GeneratorOptions.cpp',
  ],
  dependencies: [
    tm_manager_dep,
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
  custom_target('copy-zt-dll-gen-auto-refill',
    output: 'zt-shared-gen-auto-refill.dll',
    command: ['powershell', '-Command', 'Copy-Item', '-Path', libzt_dll_file, '-Destination', '@OUTPUT@', '-Force'],
    build_by_default: true,
    install: false,
  )
endif
