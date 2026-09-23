# Asset copy.
#
#   moteur_add_assets(<target>)
#
# Copies the assets/ folder next to the executable of <target>, so that asset_path()
# finds it. The copy runs at every build, which keeps edited assets up to date.

function(moteur_add_assets target)
    add_custom_target(${target}_assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:${target}>/assets"
        COMMENT "Copying assets"
        VERBATIM)
    add_dependencies(${target} ${target}_assets)
endfunction()

# License texts of the third-party libraries.
#
#   moteur_add_licenses(<target> PACKAGES sdl3 imgui ...)
#
# Copies the license file vcpkg installs with each package (share/<package>/copyright) next to the
# executable of <target>, as licenses/<package>.txt. Most of these licenses (MIT, zlib) require their
# notice to travel with the program; the sandbox shows them in its "À propos" window.

function(moteur_add_licenses target)
    cmake_parse_arguments(ARG "" "" "PACKAGES" ${ARGN})
    set(commands "")
    foreach(package IN LISTS ARG_PACKAGES)
        set(source "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/${package}/copyright")
        if(NOT EXISTS "${source}")
            message(FATAL_ERROR "No license file for vcpkg package '${package}' (${source})")
        endif()
        list(APPEND commands COMMAND ${CMAKE_COMMAND} -E copy_if_different
             "${source}" "$<TARGET_FILE_DIR:${target}>/licenses/${package}.txt")
    endforeach()
    add_custom_target(${target}_licenses
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/licenses"
        ${commands}
        COMMENT "Copying license texts"
        VERBATIM)
    add_dependencies(${target} ${target}_licenses)
endfunction()
