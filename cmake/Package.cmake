# A game's packages for players (milestone 8, parts 10 and 11).
#
#   moteur_package(<target>
#       MANIFEST <file>               # the delivery manifest (moteur/cook.hpp)
#       [COOK_TOOL <target>]          # the game's cooking tool (cook_main); default moteur_cook
#       [IDENTIFIER <reverse DNS>]    # com.publisher.game: the macOS bundle identifier
#       [WINDOWS_APP_ID <GUID>])      # the installer's identity (default: derived from publisher + name)
#
# Makes the target <target>_package, in the shipping configuration only (MOTEUR_SHIPPING): the
# shipped game is the only one players get. It
# 1. cooks the game's assets with the manifest (incremental) into <build>/package/<target>/cook;
# 2. lays out what is installed: the executable, archives/ (the cooked assets and shaders, packed),
#    licenses/ (the libraries' license texts);
# 3. Windows: signs the executable if MOTEUR_SIGN_COMMAND is set, writes the portable archive
#    (<target>-<version>-windows-x64.zip) and the installer (<target>-<version>-windows-x64-setup.exe,
#    Inno Setup, signed with the same command);
#    macOS: builds <name>.app (Info.plist, icon, resources), signs it (MOTEUR_MACOS_SIGN_IDENTITY,
#    ad hoc by default), puts it into <target>-<version>-macos-arm64.dmg, and has both notarized
#    and stapled when MOTEUR_MACOS_NOTARY_PROFILE names a notarytool keychain profile;
# 4. keeps the program and its debug symbols (PDB, dSYM) of that version in symbols/, for the crash
#    reports (tools/crash/read_report.py).
# Everything lands in <build>/packages/. The name, version, publisher and icon are those given to
# moteur_add_game.

set(MOTEUR_SIGN_COMMAND "" CACHE STRING
    "Windows: command that signs a file, the file's path appended (e.g. signtool sign /fd sha256 /tr <timestamp URL> /td sha256 /a); empty: not signed")
set(MOTEUR_MACOS_SIGN_IDENTITY "-" CACHE STRING
    "macOS: codesign identity (\"Developer ID Application: ...\"); '-' signs ad hoc")
set(MOTEUR_MACOS_NOTARY_PROFILE "" CACHE STRING
    "macOS: keychain profile of xcrun notarytool (store-credentials); empty: not notarized")

function(moteur_package target)
    cmake_parse_arguments(ARG "" "MANIFEST;COOK_TOOL;IDENTIFIER;WINDOWS_APP_ID" "" ${ARGN})
    if(NOT MOTEUR_SHIPPING)
        return()  # packages come from the shipping configuration (presets *-shipping)
    endif()
    if(NOT ARG_MANIFEST)
        message(FATAL_ERROR "moteur_package(${target}): MANIFEST is required")
    endif()
    cmake_path(ABSOLUTE_PATH ARG_MANIFEST BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT ARG_COOK_TOOL)
        set(ARG_COOK_TOOL moteur_cook)
    endif()
    get_target_property(_name ${target} MOTEUR_GAME_NAME)
    get_target_property(_version ${target} MOTEUR_GAME_VERSION)
    get_target_property(_publisher ${target} MOTEUR_GAME_PUBLISHER)
    get_target_property(_icon ${target} MOTEUR_GAME_ICON)
    if(NOT _name)
        message(FATAL_ERROR "moteur_package(${target}): not a game (moteur_add_game)")
    endif()
    if(NOT _publisher)
        set(_publisher "")
    endif()
    if(NOT _icon)
        set(_icon "")
    endif()

    set(_work "${CMAKE_BINARY_DIR}/package/${target}")
    set(_output "${CMAKE_BINARY_DIR}/packages")
    set(_exe_dir "$<TARGET_FILE_DIR:${target}>")
    set(_common
        -DTARGET=${target} "-DNAME=${_name}" -DVERSION=${_version} "-DPUBLISHER=${_publisher}" "-DICON=${_icon}"
        "-DEXE=$<TARGET_FILE:${target}>" "-DLICENSES=${_exe_dir}/licenses" "-DCOOK=${_work}/cook"
        "-DWORK=${_work}" "-DOUTPUT=${_output}")

    if(WIN32)
        if(ARG_WINDOWS_APP_ID)
            set(_app_id "${ARG_WINDOWS_APP_ID}")
        else()
            # Stable across versions (an installer updates the previous installation), unique per game.
            string(UUID _app_id NAMESPACE 6ba7b811-9dad-11d1-80b4-00c04fd430c8 NAME "moteur:${_publisher}:${_name}"
                   TYPE SHA1 UPPER)
        endif()
        find_program(MOTEUR_ISCC ISCC
            HINTS "$ENV{LOCALAPPDATA}/Programs/Inno Setup 7" "$ENV{ProgramFiles}/Inno Setup 7"
                  "$ENV{LOCALAPPDATA}/Programs/Inno Setup 6" "$ENV{ProgramFiles\(x86\)}/Inno Setup 6"
            DOC "Inno Setup's compiler (installers of the games)")
        if(NOT MOTEUR_ISCC)
            message(WARNING "moteur_package(${target}): Inno Setup not found: the package will have no installer "
                            "(https://jrsoftware.org, or winget install JRSoftware.InnoSetup.7)")
            set(MOTEUR_ISCC "")
        endif()
        set(_platform_args "-DAPP_ID=${_app_id}" "-DISCC=${MOTEUR_ISCC}" "-DSIGN_COMMAND=${MOTEUR_SIGN_COMMAND}"
                           "-DPDB=$<TARGET_PDB_FILE:${target}>")
        set(_script "${MOTEUR_ROOT}/cmake/PackageWindows.cmake")
    elseif(APPLE)
        if(NOT ARG_IDENTIFIER)
            string(MAKE_C_IDENTIFIER "${target}" _id)
            set(ARG_IDENTIFIER "org.moteur.${_id}")
        endif()
        set(_platform_args "-DIDENTIFIER=${ARG_IDENTIFIER}" "-DSIGN_IDENTITY=${MOTEUR_MACOS_SIGN_IDENTITY}"
                           "-DNOTARY_PROFILE=${MOTEUR_MACOS_NOTARY_PROFILE}"
                           "-DMINIMUM_MACOS=${CMAKE_OSX_DEPLOYMENT_TARGET}" "-DPLIST=${MOTEUR_ROOT}/cmake/macos/Info.plist.in")
        set(_script "${MOTEUR_ROOT}/cmake/PackageMacos.cmake")
    else()
        message(STATUS "moteur_package(${target}): no package for this system")
        return()
    endif()

    add_custom_target(${target}_package
        COMMAND "$<TARGET_FILE:${ARG_COOK_TOOL}>" --sources "${_exe_dir}/assets" --manifest "${ARG_MANIFEST}"
                --output "${_work}/cook" --shaders "${_exe_dir}/shaders"
        COMMAND "${CMAKE_COMMAND}" ${_common} ${_platform_args} -P "${_script}"
        COMMENT "Package of ${target} (${_name} ${_version})"
        VERBATIM)
    add_dependencies(${target}_package ${target} ${ARG_COOK_TOOL})
endfunction()
