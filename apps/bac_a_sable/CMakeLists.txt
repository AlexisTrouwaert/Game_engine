add_executable(bac_a_sable main.cpp)

target_link_libraries(bac_a_sable PRIVATE moteur)

moteur_enable_warnings(bac_a_sable)

moteur_add_shaders(bac_a_sable
    SOURCES
        sprite.vert.hlsl
        sprite.frag.hlsl
)

moteur_add_assets(bac_a_sable)

# Sprites are packed from the art sources at build time.
moteur_add_atlas(bac_a_sable NAME world SOURCES art/world)
moteur_add_atlas(bac_a_sable NAME test SOURCES art/test)
