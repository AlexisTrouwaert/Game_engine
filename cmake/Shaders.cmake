# Shader build chain.
#
#   moteur_add_shaders(<target> SOURCES sprite.vert.hlsl sprite.frag.hlsl ...)
#
# Sources are HLSL files under shaders/, named <name>.<stage>.hlsl with stage in
# {vert, frag, comp}. The result lands in <target output dir>/shaders/ where
# Renderer::load_shader() looks for it.
#
# Windows: compiled at build time to DXIL (Direct3D 12) with the `shadercross` tool from the
#          vcpkg port sdl3-shadercross.
# macOS:   the vcpkg port cannot be installed (its dependency directx-dxc does not exist on
#          macOS), so pre-generated MSL from shaders/generated/msl/ is copied instead.
#          Regenerate those files on Windows with the `export_msl_shaders` target.

set(MOTEUR_SHADER_SOURCE_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(MOTEUR_SHADER_MSL_DIR "${MOTEUR_SHADER_SOURCE_DIR}/generated/msl")

if(WIN32)
    set(_tools "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools")
    find_program(MOTEUR_SHADERCROSS shadercross
        PATHS "${_tools}/sdl3-shadercross"
        NO_DEFAULT_PATH)
    if(NOT MOTEUR_SHADERCROSS)
        message(FATAL_ERROR
            "shadercross not found under ${_tools}/sdl3-shadercross. "
            "vcpkg.json must list sdl3-shadercross for Windows.")
    endif()

    # Run the tool from a private folder that also holds the DXC libraries it needs.
    set(_tool_dir "${CMAKE_BINARY_DIR}/shader_tools")
    file(COPY
        "${MOTEUR_SHADERCROSS}"
        "${_tools}/sdl3-shadercross/dxcompiler.dll"
        "${_tools}/directx-dxc/dxil.dll"
        DESTINATION "${_tool_dir}")
    set(MOTEUR_SHADERCROSS_EXE "${_tool_dir}/shadercross.exe")

    add_custom_target(export_msl_shaders
        COMMENT "Pre-generated MSL shaders written to ${MOTEUR_SHADER_MSL_DIR}")
endif()

function(moteur_add_shaders target)
    cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})
    get_target_property(binary_dir ${target} BINARY_DIR)
    set(out_dir "${binary_dir}/shaders")

    set(outputs "")
    foreach(source IN LISTS ARG_SOURCES)
        set(input "${MOTEUR_SHADER_SOURCE_DIR}/${source}")
        string(REGEX REPLACE "\\.hlsl$" "" base "${source}")

        if(base MATCHES "\\.vert$")
            set(stage vertex)
        elseif(base MATCHES "\\.frag$")
            set(stage fragment)
        elseif(base MATCHES "\\.comp$")
            set(stage compute)
        else()
            message(FATAL_ERROR "Shader ${source}: name must be <name>.vert|frag|comp.hlsl")
        endif()

        if(WIN32)
            set(output "${out_dir}/${base}.dxil")
            add_custom_command(
                OUTPUT "${output}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${out_dir}"
                COMMAND "${MOTEUR_SHADERCROSS_EXE}" "${input}"
                        -s HLSL -d DXIL -t ${stage} -e main -o "${output}"
                DEPENDS "${input}"
                COMMENT "Compiling ${source} -> DXIL"
                VERBATIM)

            # Manual target (not part of the normal build): refresh the MSL files used on macOS.
            set(msl "${MOTEUR_SHADER_MSL_DIR}/${base}.msl")
            add_custom_target(export_msl_${base}
                COMMAND ${CMAKE_COMMAND} -E make_directory "${MOTEUR_SHADER_MSL_DIR}"
                COMMAND "${MOTEUR_SHADERCROSS_EXE}" "${input}"
                        -s HLSL -d MSL -t ${stage} -e main -o "${msl}"
                DEPENDS "${input}"
                COMMENT "Exporting ${source} -> MSL"
                VERBATIM)
            add_dependencies(export_msl_shaders export_msl_${base})
        elseif(APPLE)
            set(msl "${MOTEUR_SHADER_MSL_DIR}/${base}.msl")
            if(NOT EXISTS "${msl}")
                message(FATAL_ERROR
                    "Missing pre-generated shader ${msl}. "
                    "On Windows, build the export_msl_shaders target and commit the result.")
            endif()
            set(output "${out_dir}/${base}.msl")
            add_custom_command(
                OUTPUT "${output}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${out_dir}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${msl}" "${output}"
                DEPENDS "${msl}"
                COMMENT "Copying ${base}.msl"
                VERBATIM)
        else()
            message(FATAL_ERROR "No shader chain for this platform.")
        endif()

        list(APPEND outputs "${output}")
    endforeach()

    add_custom_target(${target}_shaders DEPENDS ${outputs})
    add_dependencies(${target} ${target}_shaders)
endfunction()
