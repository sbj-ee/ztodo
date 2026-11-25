cmake_minimum_required(VERSION 3.21)

# macOS: Apple Silicon only (set before project() so the compiler checks use it).
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Build architectures for macOS" FORCE)
endif()
# Minimum macOS for ztodo.app (Homebrew Qt bottles are built for macOS 14).
set(ZTODO_MACOS_MIN "14.0")
set(ZTODO_BUNDLE_ID "ee.sbj.ztodo")
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "${ZTODO_MACOS_MIN}" CACHE STRING "Minimum macOS version" FORCE)
endif()

# Single source of truth for the version: bump ONLY the VERSION here.
# configure_file() turns it into generated/version.hpp (window title, About,
# --version, update checks); CPack names the .deb / .dmg from PROJECT_VERSION.
project(ztodo VERSION 1.0.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(ZTODO_BUILD_TESTS "Build the Qt Test suite" ON)

# AddressSanitizer + UndefinedBehaviorSanitizer for ztodo's own code (CI runs
# the tests this way). Any UB report aborts, so a finding fails the test.
option(ZTODO_SANITIZE "Build with -fsanitize=address,undefined" OFF)
if(ZTODO_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all
                      -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# libFuzzer builds of the fuzz/ harnesses (clang only). ztodo_core gets
# coverage instrumentation; the harnesses link the fuzzer runtime.
option(ZTODO_FUZZ "Build fuzz/ as libFuzzer binaries (clang)" OFF)
if(ZTODO_FUZZ)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "ZTODO_FUZZ needs clang (libFuzzer)")
  endif()
  add_compile_options(-fsanitize=fuzzer-no-link)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network)
# libsodium: Ed25519 for the minisign signature on release SHA256SUMS.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium>=1.0.18)

include(cmake/CompilerWarnings.cmake)

set(ZTODO_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/version.hpp.in"
  "${ZTODO_GENERATED_DIR}/version.hpp"
  @ONLY)

add_subdirectory(core)
add_subdirectory(app)

if(APPLE)
  include(cmake/MacBundle.cmake)
endif()

if(ZTODO_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
  add_subdirectory(fuzz)
endif()

include(cmake/Packaging.cmake)
