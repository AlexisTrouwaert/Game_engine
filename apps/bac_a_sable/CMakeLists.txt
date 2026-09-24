add_executable(bac_a_sable main.cpp demo3d.cpp blender_compare.cpp)

find_package(nlohmann_json CONFIG REQUIRED)

target_link_libraries(bac_a_sable PRIVATE moteur nlohmann_json::nlohmann_json)

moteur_enable_warnings(bac_a_sable)

moteur_add_shaders(bac_a_sable
    SOURCES
        sprite.vert.hlsl
        sprite.frag.hlsl
        mesh.vert.hlsl
        mesh.frag.hlsl
        tonemap.vert.hlsl
        tonemap.frag.hlsl
        fxaa.frag.hlsl
        shadow.vert.hlsl
        shadow.frag.hlsl
        shadow_clear.vert.hlsl
        point_shadow.vert.hlsl
        point_shadow.frag.hlsl
        billboard.vert.hlsl
        billboard.frag.hlsl
        debug_line.vert.hlsl
        debug_line.frag.hlsl
        depth_view.frag.hlsl
)

moteur_add_assets(bac_a_sable)

# Shown by the "À propos" window; keep in step with assets/credits.json.
set(_licenses sdl3 imgui glm stb nlohmann-json cgltf mikktspace)
if(WIN32)
    list(APPEND _licenses winpixevent)
endif()
moteur_add_licenses(bac_a_sable PACKAGES ${_licenses})

# Windows: SDL loads WinPixEventRuntime.dll, if it sits next to the executable, to name the render
# passes in RenderDoc and PIX captures under Direct3D 12 (SDL_PushGPUDebugGroup). Without it the
# groups are silently dropped; nothing else changes.
if(WIN32)
    set(_pix_dll "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/WinPixEventRuntime.dll")
    if(NOT EXISTS "${_pix_dll}")
        message(FATAL_ERROR "WinPixEventRuntime.dll not found (${_pix_dll}): vcpkg.json must list winpixevent.")
    endif()
    add_custom_command(TARGET bac_a_sable POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_pix_dll}" "$<TARGET_FILE_DIR:bac_a_sable>"
        VERBATIM)
endif()

# Sprites are packed from the art sources at build time.
moteur_add_atlas(bac_a_sable NAME world SOURCES art/world)
moteur_add_atlas(bac_a_sable NAME test SOURCES art/test)
