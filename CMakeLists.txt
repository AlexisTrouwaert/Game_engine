cmake_minimum_required(VERSION 3.25)

# Use vcpkg from VCPKG_ROOT when no toolchain file is given.
# CLion's integrated vcpkg injects its own toolchain file, which takes precedence.
# Otherwise fall back to the default location of CLion's vcpkg, which CLion
# only injects into the profiles that existed when vcpkg was added.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
    elseif(EXISTS "$ENV{HOME}/.vcpkg-clion/vcpkg/scripts/buildsystems/vcpkg.cmake")
        set(CMAKE_TOOLCHAIN_FILE "$ENV{HOME}/.vcpkg-clion/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
    endif()
endif()

project(moteur LANGUAGES C CXX)  # C: miniaudio's implementation

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
