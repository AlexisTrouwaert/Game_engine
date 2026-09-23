find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(mikktspace CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_path(STB_INCLUDE_DIRS "stb_image.h" REQUIRED)
find_path(CGLTF_INCLUDE_DIRS "cgltf.h" REQUIRED)

add_library(moteur STATIC
    animation.cpp
    application.cpp
    atlas_builder.cpp
    camera.cpp
    camera3d.cpp
    color.cpp
    debug_ui.cpp
    environment.cpp
    fixed_timestep.cpp
    font.cpp
    frame_stats.cpp
    image.cpp
    iso.cpp
    mesh.cpp
    mesh_renderer.cpp
    model.cpp
    paths.cpp
    renderer.cpp
    screenshot.cpp
    sprite_batcher.cpp
    sprite_region.cpp
    sprite_renderer.cpp
    text_layout.cpp
    texture_atlas.cpp
    tone_mapper.cpp
    tilemap.cpp
    utf8.cpp
    version.cpp
)

target_include_directories(moteur PUBLIC include)
target_include_directories(moteur SYSTEM PRIVATE ${STB_INCLUDE_DIRS} ${CGLTF_INCLUDE_DIRS})
target_link_libraries(moteur PUBLIC SDL3::SDL3 glm::glm imgui::imgui PRIVATE nlohmann_json::nlohmann_json mikktspace::mikktspace)

moteur_enable_warnings(moteur)
