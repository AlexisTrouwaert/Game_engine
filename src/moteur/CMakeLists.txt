find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_path(STB_INCLUDE_DIRS "stb_image.h" REQUIRED)

add_library(moteur STATIC
    application.cpp
    atlas_builder.cpp
    camera.cpp
    fixed_timestep.cpp
    font.cpp
    frame_stats.cpp
    image.cpp
    iso.cpp
    paths.cpp
    renderer.cpp
    screenshot.cpp
    sprite_batcher.cpp
    sprite_region.cpp
    sprite_renderer.cpp
    text_layout.cpp
    texture_atlas.cpp
    utf8.cpp
    version.cpp
)

target_include_directories(moteur PUBLIC include)
target_include_directories(moteur SYSTEM PRIVATE ${STB_INCLUDE_DIRS})
target_link_libraries(moteur PUBLIC SDL3::SDL3 glm::glm PRIVATE nlohmann_json::nlohmann_json)

moteur_enable_warnings(moteur)
