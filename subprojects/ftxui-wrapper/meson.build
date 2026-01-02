# ftxui-wrapper/meson.build - wrapper to build FTXUI via CMake inside wrapper layout
# Mirrors libzt-wrapper approach: nested original source at subprojects/ftxui
# Exposes combined dependency variable ftxui_dep for fallbacks.

project('ftxui-wrapper', 'cpp', version: '0')

cmake_mod = import('cmake')
ftxui_proj = cmake_mod.subproject('ftxui')

ftxui_dep = declare_dependency(
  dependencies: [
    ftxui_proj.dependency('component'),
    ftxui_proj.dependency('dom'),
    ftxui_proj.dependency('screen'),
  ]
)
