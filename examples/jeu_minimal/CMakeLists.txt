# The smallest program built on the engine (milestone 8, part 3), and the model of how a game
# consumes it (moteur_doc/GUIDE.md, "Démarrer un jeu").
#
# Two ways to build it:
# - inside the engine's project (option MOTEUR_BUILD_EXAMPLES, on by default): nothing to do;
# - on its own, from the outside, exactly as a game does with the engine in a git submodule:
#       cmake --preset windows-release      (in this folder; see CMakePresets.json)
#   MOTEUR_DIR points to the engine (here ../..; in a game, the submodule's folder).
cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(MOTEUR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "The engine's folder (a git submodule in a game)")
    # vcpkg: this example has no library of its own, so it reads the engine's manifest (and its
    # overlay ports). A game with libraries of its own keeps its vcpkg.json, which must also list
    # the engine's dependencies (see the guide).
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg.json")
        set(VCPKG_MANIFEST_DIR "${MOTEUR_DIR}" CACHE PATH "")
    endif()
    # The engine's own vcpkg triplets (macOS: arm64-osx with the engine's deployment target).
    if(NOT DEFINED VCPKG_OVERLAY_TRIPLETS)
        set(VCPKG_OVERLAY_TRIPLETS "${MOTEUR_DIR}/triplets" CACHE PATH "")
    endif()
    if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
        if(DEFINED ENV{VCPKG_ROOT})
            set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
        elseif(EXISTS "$ENV{HOME}/.vcpkg-clion/vcpkg/scripts/buildsystems/vcpkg.cmake")
            set(CMAKE_TOOLCHAIN_FILE "$ENV{HOME}/.vcpkg-clion/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
        endif()
    endif()

    project(jeu_minimal LANGUAGES C CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    add_subdirectory("${MOTEUR_DIR}" moteur)
endif()

add_executable(jeu_minimal main.cpp)
moteur_enable_warnings(jeu_minimal)
# Who the game is, for players: the version information and the packages. A game also gives its
# ICON (<path without extension>: .ico on Windows, .icns on macOS; tools/package/make_icon.py).
moteur_add_game(jeu_minimal NAME "Jeu minimal" VERSION 1.0.0 PUBLISHER "moteur"
                ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/assets")

# The players' packages (shipping configuration, preset windows-shipping / macos-shipping):
#   cmake --build build/windows-shipping --target jeu_minimal_package   (cmake/Package.cmake)
# The example has no tables of its own: the engine's cooking tool (moteur_cook, the default) does.
moteur_package(jeu_minimal MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/assets/livraison.json"
               IDENTIFIER org.moteur.jeu_minimal)
