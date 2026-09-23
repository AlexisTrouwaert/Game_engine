add_executable(bac_a_sable main.cpp)

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
)

moteur_add_assets(bac_a_sable)

# Shown by the "À propos" window; keep in step with assets/credits.json.
moteur_add_licenses(bac_a_sable PACKAGES sdl3 imgui glm stb nlohmann-json cgltf mikktspace)

# Sprites are packed from the art sources at build time.
moteur_add_atlas(bac_a_sable NAME world SOURCES art/world)
moteur_add_atlas(bac_a_sable NAME test SOURCES art/test)
