# Shader build chain.
#
#   moteur_add_shaders(<target> [NAME <name>] SOURCE_DIR <folder> SOURCES sprite.vert.hlsl ...)
#
# Sources are HLSL files of <folder>, named <name>.<stage>.hlsl with stage in {vert, frag, comp}.
# They may #include other files of <folder> (a variant defines a macro and includes its base, such
# as mesh_skinned.vert.hlsl): every shader is rebuilt when any file of <folder> changes, which costs
# little and never leaves one stale. The result lands in <target output dir>/shaders/ where
# Renderer::load_shader() looks for it. NAME tells apart two sets of one target (the engine's and
# the game's, see moteur_add_game); its custom target is <target>_shaders, or <target>_shaders_<name>.
#
# Windows: compiled at build time to DXIL (Direct3D 12) with the `shadercross` tool from the
#          vcpkg port sdl3-shadercross.
# macOS:   the vcpkg port cannot be installed (its dependency directx-dxc does not exist on
#          macOS), so pre-generated MSL from <folder>/generated/msl/ is copied instead.
#          Regenerate those files on Windows with the `export_msl_shaders` target (checked by
#          tools/ci/verify.py).

# The engine's own shaders: every program of the engine gets them (moteur_add_game).
set(MOTEUR_ENGINE_SHADERS
    sprite.vert.hlsl
    sprite.frag.hlsl
    mesh.vert.hlsl
    mesh_skinned.vert.hlsl
    mesh.frag.hlsl
    mesh_id.vert.hlsl
    mesh_id_skinned.vert.hlsl
    mesh_id.frag.hlsl
    tonemap.vert.hlsl
    tonemap.frag.hlsl
    fxaa.frag.hlsl
    shadow.vert.hlsl
    shadow_skinned.vert.hlsl
    shadow.frag.hlsl
    shadow_masked.vert.hlsl
    shadow_masked_skinned.vert.hlsl
    shadow_masked.frag.hlsl
    shadow_clear.vert.hlsl
    point_shadow.vert.hlsl
    point_shadow_skinned.vert.hlsl
    point_shadow.frag.hlsl
    point_shadow_masked.vert.hlsl
    point_shadow_masked_skinned.vert.hlsl
    point_shadow_masked.frag.hlsl
    billboard.vert.hlsl
    billboard.frag.hlsl
    debug_line.vert.hlsl
    debug_line.frag.hlsl
    depth_view.frag.hlsl
    CACHE INTERNAL "The engine's shaders")

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
    set(_tool_dir "${PROJECT_BINARY_DIR}/shader_tools")
    file(COPY
        "${MOTEUR_SHADERCROSS}"
        "${_tools}/sdl3-shadercross/dxcompiler.dll"
        "${_tools}/directx-dxc/dxil.dll"
        DESTINATION "${_tool_dir}")
    set(MOTEUR_SHADERCROSS_EXE "${_tool_dir}/shadercross.exe" CACHE INTERNAL "shadercross, with its DLLs")

    add_custom_target(export_msl_shaders COMMENT "Pre-generated MSL shaders exported")
endif()

function(moteur_add_shaders target)
    cmake_parse_arguments(ARG "" "NAME;SOURCE_DIR" "SOURCES" ${ARGN})
    if(NOT ARG_SOURCE_DIR)
        message(FATAL_ERROR "moteur_add_shaders(${target}): SOURCE_DIR is required")
    endif()
    get_target_property(binary_dir ${target} BINARY_DIR)
    set(out_dir "${binary_dir}/shaders")
    set(msl_dir "${ARG_SOURCE_DIR}/generated/msl")
    set(suffix "")
    if(ARG_NAME)
        set(suffix "_${ARG_NAME}")
    endif()

    file(GLOB shader_files CONFIGURE_DEPENDS "${ARG_SOURCE_DIR}/*.hlsl" "${ARG_SOURCE_DIR}/*.hlsli")
    set(outputs "")
    foreach(source IN LISTS ARG_SOURCES)
        set(input "${ARG_SOURCE_DIR}/${source}")
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
                        -s HLSL -d DXIL -t ${stage} -e main -I "${ARG_SOURCE_DIR}" -o "${output}"
                DEPENDS ${shader_files}
                COMMENT "Compiling ${source} -> DXIL"
                VERBATIM)

            # Manual target (not part of the normal build): refresh the MSL files used on macOS. Once
            # per shader file, however many programs compile it.
            string(MAKE_C_IDENTIFIER "export_msl_${input}" export_target)
            if(NOT TARGET ${export_target})
                set(msl "${msl_dir}/${base}.msl")
                add_custom_target(${export_target}
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${msl_dir}"
                    COMMAND "${MOTEUR_SHADERCROSS_EXE}" "${input}"
                            -s HLSL -d MSL -t ${stage} -e main -I "${ARG_SOURCE_DIR}" -o "${msl}"
                    DEPENDS ${shader_files}
                    COMMENT "Exporting ${source} -> MSL"
                    VERBATIM)
                add_dependencies(export_msl_shaders ${export_target})
            endif()
        elseif(APPLE)
            set(msl "${msl_dir}/${base}.msl")
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

    add_custom_target(${target}_shaders${suffix} DEPENDS ${outputs})
    add_dependencies(${target} ${target}_shaders${suffix})
endfunction()
