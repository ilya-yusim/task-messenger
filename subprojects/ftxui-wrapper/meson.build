# ftxui-wrapper/meson.build - wrapper to build FTXUI via CMake inside wrapper layout
# Mirrors libzt-wrapper approach: nested original source at subprojects/ftxui
# Exposes combined dependency variable ftxui_dep for fallbacks.

project('ftxui-wrapper', 'cpp', version: '0')

cmake_mod = import('cmake')

# Configure CMake options for ftxui
cmake_opts = cmake_mod.subproject_options()
cmake_opts.add_cmake_defines({
  'FTXUI_BUILD_EXAMPLES': 'OFF',
  'FTXUI_BUILD_DOCS': 'OFF',
  'FTXUI_BUILD_TESTS': 'OFF',
  'FTXUI_ENABLE_INSTALL': 'ON',
})

ftxui_proj = cmake_mod.subproject('ftxui', options: cmake_opts)

ftxui_dep = declare_dependency(
  dependencies: [
    ftxui_proj.dependency('component'),
    ftxui_proj.dependency('dom'),
    ftxui_proj.dependency('screen'),
  ]
)
