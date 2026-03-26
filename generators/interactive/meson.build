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
    output: 'zt-dll-gen-interactive.stamp',
    command: ['powershell', '-Command',
      'Copy-Item', '-Path', libzt_dll_file, '-Destination', '@OUTDIR@/zt-shared.dll', '-Force', ';',
      'New-Item', '-Path', '@OUTPUT@', '-ItemType', 'File', '-Force', '|', 'Out-Null'],
    build_by_default: true,
    install: false,
  )
  if enable_blas_skills
    custom_target('copy-openblas-dll-gen-interactive',
      output: 'openblas-gen-interactive.stamp',
      command: ['powershell', '-Command',
        'Copy-Item', '-Path', openblas_dll_file, '-Destination', '@OUTDIR@/libopenblas.dll', '-Force', ';',
        'New-Item', '-Path', '@OUTPUT@', '-ItemType', 'File', '-Force', '|', 'Out-Null'],
      build_by_default: true,
      install: false,
    )
  endif
endif
