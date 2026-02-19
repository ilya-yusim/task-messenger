# Serialization tests for skill-based task messaging
#
# This directory contains standalone test cases demonstrating serialization
# libraries for the "skill" concept where each skill type has associated
# request and response data structures.
#
# Usage:
#   meson setup builddir -Dbuild_serialize_tests=true
#   meson compile -C builddir
#   meson test -C builddir

# Import cmake module for CMake-based subprojects
cmake = import('cmake')

subdir('flatbuffers')

# Cap'n Proto tests subdir handles its own dependencies via capnproto-wrapper
subdir('capnproto')
