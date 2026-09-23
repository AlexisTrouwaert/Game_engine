cmake_minimum_required(VERSION 3.25)

# Use vcpkg from VCPKG_ROOT when no toolchain file is given.
# CLion's integrated vcpkg injects its own toolchain file, which takes precedence.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

project(moteur LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(Warnings)

find_package(SDL3 CONFIG REQUIRED)
include(Shaders)
include(Assets)
include(Atlas)

add_subdirectory(src/moteur)
add_subdirectory(tools/atlas_packer)
add_subdirectory(apps/bac_a_sable)

option(MOTEUR_BUILD_TESTS "Build the unit tests" ON)
if(MOTEUR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
