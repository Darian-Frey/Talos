# Talos — top-level build
#
# Builds the Talos Qt6 client (and, later, the validation harness).
# It does NOT build the Hatari fork: that is a separate GPLv2 upstream with its
# own CMake build, cloned into external/ by scripts/bootstrap-hatari.sh and run
# as a separate process (D-004, two-process architecture).

cmake_minimum_required(VERSION 3.21)

project(Talos
  VERSION 0.0.0
  DESCRIPTION "Cycle-accurate Atari ST/STE hardware visualiser (drives a patched Hatari over a socket)"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for clangd / tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(TALOS_BUILD_HARNESS "Build the per-scanline validation/diff harness (F-213)" OFF)

add_subdirectory(client)

if(TALOS_BUILD_HARNESS)
  add_subdirectory(harness)
endif()
