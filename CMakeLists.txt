cmake_minimum_required(VERSION 3.25)

# The engine is either the project itself (its sandbox, tools and tests) or a subfolder of a game,
# added with add_subdirectory() (milestone 8, part 3: see moteur_doc/GUIDE.md). In the second case
# the game's project owns the toolchain, vcpkg and the language settings.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
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
endif()

# Tracy (milestone 7, part 4): off by default; on, vcpkg builds it (manifest feature "tracy") and the
# engine's MOTEUR_PROFILE zones also go to Tracy's profiler application.
option(MOTEUR_TRACY "Send the profiler's zones to Tracy" OFF)
if(MOTEUR_TRACY)
    list(APPEND VCPKG_MANIFEST_FEATURES "tracy")
endif()

# The shipping configuration (milestone 8, part 6; presets *-shipping): the game as players get it,
# without Dear ImGui, hot reload, profiler nor test programs; optimised across files (LTO), with
# its debug symbols apart (PDB, dSYM) for the crash reports.
option(MOTEUR_SHIPPING "Build the shipped game: no debug tools, no hot reload, no profiler" OFF)
if(MOTEUR_SHIPPING AND MOTEUR_TRACY)
    message(FATAL_ERROR "MOTEUR_SHIPPING and MOTEUR_TRACY exclude each other: Tracy is a development tool")
endif()

project(moteur VERSION 0.8.0 LANGUAGES C CXX)  # C: miniaudio's implementation

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# Where the engine is, for its CMake functions, whichever project calls them.
set(MOTEUR_ROOT "${PROJECT_SOURCE_DIR}" CACHE INTERNAL "The engine's source folder")

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(Warnings)
include(Shipping)

find_package(SDL3 CONFIG REQUIRED)
include(Shaders)
include(Assets)
include(Atlas)
include(Game)
include(Package)

add_subdirectory(src/moteur)
if(NOT MOTEUR_FUZZ)  # the fuzzing build has only the fuzzing programs (the others lack libFuzzer)
    add_subdirectory(tools/atlas_packer)
    add_subdirectory(tools/cook)
endif()

# The engine's own programs: on when the engine is the project, off when it is a game's subfolder.
# The shipping configuration builds none of the tools (they need Dear ImGui), only the games.
set(_tools_default ${PROJECT_IS_TOP_LEVEL})
if(MOTEUR_SHIPPING)
    set(_tools_default OFF)
endif()
option(MOTEUR_BUILD_SANDBOX "Build the sandbox and the map editor" ${_tools_default})
option(MOTEUR_BUILD_SLICE "Build the playable slice (apps/tranche)" ${PROJECT_IS_TOP_LEVEL})
option(MOTEUR_BUILD_EXAMPLES "Build the examples (examples/)" ${PROJECT_IS_TOP_LEVEL})
option(MOTEUR_BUILD_TESTS "Build the unit tests" ${_tools_default})
if(MOTEUR_BUILD_SANDBOX OR MOTEUR_BUILD_SLICE)
    add_subdirectory(apps/tranche)  # the slice's code (tranche_commun), and the program if MOTEUR_BUILD_SLICE
endif()
if(MOTEUR_BUILD_SANDBOX)
    add_subdirectory(apps/bac_a_sable)
endif()
if(MOTEUR_BUILD_EXAMPLES)
    add_subdirectory(examples/jeu_minimal)
endif()
if(MOTEUR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(MOTEUR_FUZZ)
    add_subdirectory(tests/fuzz)
endif()
if(PROJECT_IS_TOP_LEVEL AND NOT MOTEUR_FUZZ)
    include(Docs)  # target moteur_docs
endif()
