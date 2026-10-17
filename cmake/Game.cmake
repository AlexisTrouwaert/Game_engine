# A program built on the engine (milestone 8, part 3).
#
#   moteur_add_game(<target>
#       [NAME <text>]                 # the game's name as players see it (default: <target>)
#       [VERSION <x.y.z>]             # default: the calling project's version
#       [PUBLISHER <text>]            # who makes the game (version information, installer)
#       [ICON <path without extension>]  # <path>.ico on Windows, <path>.icns on macOS
#       [ASSETS <folder>...]          # the game's assets, copied after the engine's into assets/
#       [SHADER_DIR <folder> SHADERS <file>...]   # the game's own shaders, if any
#       [LICENSES <vcpkg package>...] # license texts of the game's own libraries
#       [SHARE_OUTPUT_OF <target>])   # a second program of the same folder (an editor next to its game)
#
# Gives <target> what every program of the engine needs next to its executable:
# - the engine's shaders (compiled for this OS) and the game's, in shaders/;
# - the engine's assets (assets_moteur/) then the game's folders, merged into assets/ (a file of the
#   game with the same path as one of the engine's replaces it);
# - the license texts of the engine's libraries and of the game's, in licenses/;
# - on Windows, its resources: the icon, the version information (name, publisher, version) and a
#   manifest (Windows 10 / 11, per-monitor density, long paths, UTF-8 code page, no elevation);
# - on Windows outside the shipping configuration, WinPixEventRuntime.dll (names of the render
#   passes in RenderDoc and PIX).
#
# With SHARE_OUTPUT_OF, <target> lands in the same folder as that other program and only depends on
# its copies (the two share shaders, assets and licenses).
#
# The engine may be the project itself or a subfolder of a game (add_subdirectory): paths come from
# MOTEUR_ROOT, never from CMAKE_SOURCE_DIR.

function(moteur_add_game target)
    cmake_parse_arguments(ARG "" "SHADER_DIR;SHARE_OUTPUT_OF;NAME;VERSION;PUBLISHER;ICON" "ASSETS;SHADERS;LICENSES" ${ARGN})
    target_link_libraries(${target} PRIVATE moteur)

    # Who the game is: kept on the target for moteur_package (installer, bundle).
    if(NOT ARG_NAME)
        set(ARG_NAME "${target}")
    endif()
    if(NOT ARG_VERSION)
        set(ARG_VERSION "${PROJECT_VERSION}")
    endif()
    if(NOT ARG_VERSION MATCHES "^[0-9]+(\\.[0-9]+)?(\\.[0-9]+)?$")
        message(FATAL_ERROR "moteur_add_game(${target}): VERSION '${ARG_VERSION}' is not x.y.z")
    endif()
    if(ARG_ICON)
        cmake_path(ABSOLUTE_PATH ARG_ICON BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    set_target_properties(${target} PROPERTIES MOTEUR_GAME_NAME "${ARG_NAME}" MOTEUR_GAME_VERSION "${ARG_VERSION}"
                          MOTEUR_GAME_PUBLISHER "${ARG_PUBLISHER}" MOTEUR_GAME_ICON "${ARG_ICON}")
    if(WIN32)
        _moteur_windows_resources(${target} "${ARG_NAME}" "${ARG_VERSION}" "${ARG_PUBLISHER}" "${ARG_ICON}")
    endif()

    if(ARG_SHARE_OUTPUT_OF)
        get_target_property(_dir ${ARG_SHARE_OUTPUT_OF} BINARY_DIR)
        set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${_dir}")
        add_dependencies(${target} ${ARG_SHARE_OUTPUT_OF}_shaders ${ARG_SHARE_OUTPUT_OF}_assets
                         ${ARG_SHARE_OUTPUT_OF}_licenses)
        return()
    endif()

    moteur_add_shaders(${target} SOURCE_DIR "${MOTEUR_ROOT}/shaders" SOURCES ${MOTEUR_ENGINE_SHADERS})
    if(ARG_SHADERS)
        if(NOT ARG_SHADER_DIR)
            message(FATAL_ERROR "moteur_add_game(${target}): SHADERS needs SHADER_DIR")
        endif()
        moteur_add_shaders(${target} NAME game SOURCE_DIR "${ARG_SHADER_DIR}" SOURCES ${ARG_SHADERS})
    endif()

    moteur_add_assets(${target} FOLDERS "${MOTEUR_ROOT}/assets_moteur" ${ARG_ASSETS})
    moteur_add_licenses(${target} PACKAGES ${MOTEUR_LICENSE_PACKAGES} ${ARG_LICENSES})

    if(WIN32 AND NOT MOTEUR_SHIPPING)
        set(_pix_dll "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/WinPixEventRuntime.dll")
        if(EXISTS "${_pix_dll}")
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_pix_dll}" "$<TARGET_FILE_DIR:${target}>"
                VERBATIM)
        endif()
    endif()
endfunction()

# The icon, version information and manifest of a game's executable (moteur_add_game, Windows).
function(_moteur_windows_resources target name version publisher icon)
    string(REPLACE "." ";" _parts "${version}")
    list(LENGTH _parts _count)
    while(_count LESS 4)
        list(APPEND _parts 0)
        math(EXPR _count "${_count} + 1")
    endwhile()
    string(REPLACE ";" "," MOTEUR_RC_VERSION_COMMAS "${_parts}")
    set(MOTEUR_RC_VERSION "${version}")
    set(MOTEUR_RC_NAME "${name}")
    set(MOTEUR_RC_TARGET "${target}")
    set(MOTEUR_RC_PUBLISHER "${publisher}")
    string(TIMESTAMP _year "%Y")
    if(publisher)
        set(MOTEUR_RC_COPYRIGHT "Copyright (C) ${_year} ${publisher}")
    else()
        set(MOTEUR_RC_COPYRIGHT "")
    endif()
    set(MOTEUR_RC_ICON "")
    if(icon)
        if(NOT EXISTS "${icon}.ico")
            message(FATAL_ERROR "moteur_add_game(${target}): ICON '${icon}.ico' does not exist")
        endif()
        file(TO_CMAKE_PATH "${icon}.ico" _ico)
        set(MOTEUR_RC_ICON "1 ICON \"${_ico}\"")  # the first icon: the Explorer's and the window's
    endif()
    set(_rc "${CMAKE_CURRENT_BINARY_DIR}/${target}_resources.rc")
    configure_file("${MOTEUR_ROOT}/cmake/windows/game.rc.in" "${_rc}" @ONLY)
    target_sources(${target} PRIVATE "${_rc}")
    if(MSVC)
        target_sources(${target} PRIVATE "${MOTEUR_ROOT}/cmake/windows/game.manifest")
    endif()
endfunction()
