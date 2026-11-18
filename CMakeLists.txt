cmake_minimum_required(VERSION 3.20)

# Some fetched dependencies still declare very old minimum versions, which
# CMake 4.x rejects. This lets them configure without patching them.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(Spillway VERSION 0.1.0 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SPILLWAY_BUILD_GAME  "Build the raylib game"  ON)
option(SPILLWAY_BUILD_TESTS "Build the Catch2 tests" ON)

# Warnings for our own targets only (not for fetched dependencies).
add_library(spillway_warnings INTERFACE)
if(MSVC)
  target_compile_options(spillway_warnings INTERFACE /W4)
else()
  target_compile_options(spillway_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow)
endif()

include(FetchContent)

add_subdirectory(engine)

if(SPILLWAY_BUILD_GAME)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(raylib
    URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(raylib)
  add_subdirectory(game)
endif()

if(SPILLWAY_BUILD_TESTS)
  enable_testing()
  FetchContent_Declare(Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.7.1.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  add_subdirectory(tests)
endif()
