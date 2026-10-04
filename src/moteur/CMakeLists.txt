find_package(EnTT CONFIG REQUIRED)
find_package(ozz-animation CONFIG REQUIRED)
find_package(efsw CONFIG REQUIRED)
find_package(Ktx CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(mikktspace CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_path(STB_INCLUDE_DIRS "stb_image.h" REQUIRED)
find_path(CGLTF_INCLUDE_DIRS "cgltf.h" REQUIRED)
find_path(MINIAUDIO_INCLUDE_DIRS "miniaudio.h" REQUIRED)

# miniaudio (and stb_vorbis for OGG) in a library of its own: C, third-party code, built without the
# project's warnings.
add_library(moteur_miniaudio STATIC miniaudio.c)
target_include_directories(moteur_miniaudio SYSTEM PUBLIC ${MINIAUDIO_INCLUDE_DIRS} ${STB_INCLUDE_DIRS})
if(MSVC)
    target_compile_options(moteur_miniaudio PRIVATE /W0)
else()
    target_compile_options(moteur_miniaudio PRIVATE -w)
endif()
if(APPLE)
    target_link_libraries(moteur_miniaudio PRIVATE "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
elseif(UNIX)
    target_link_libraries(moteur_miniaudio PRIVATE ${CMAKE_DL_LIBS} m pthread)
endif()

add_library(moteur STATIC
    animation.cpp
    animation_clock.cpp
    animation_data.cpp
    animation_debug.cpp
    animation_set.cpp
    animation_graph.cpp
    animator.cpp
    animator_graph.cpp
    animator_state.cpp
    application.cpp
    asset_cache.cpp
    assets.cpp
    atlas_builder.cpp
    audio.cpp
    audio_rules.cpp
    billboard_batcher.cpp
    billboard_renderer.cpp
    camera.cpp
    camera3d.cpp
    collision.cpp
    color.cpp
    console.cpp
    data_reader.cpp
    data_table.cpp
    debug_lines.cpp
    debug_tools.cpp
    debug_ui.cpp
    environment.cpp
    fixed_timestep.cpp
    input.cpp
    file_io.cpp
    font.cpp
    frame_stats.cpp
    image.cpp
    iso.cpp
    map_data.cpp
    map_document.cpp
    ktx_texture.cpp
    mesh.cpp
    mesh_batcher.cpp
    mesh_renderer.cpp
    log_file.cpp
    model.cpp
    movement.cpp
    nav_grid.cpp
    particles.cpp
    paths.cpp
    profiler.cpp
    pathfinding.cpp
    process_memory.cpp
    renderer.cpp
    save_file.cpp
    screenshot.cpp
    shadow.cpp
    skeleton.cpp
    sound.cpp
    spatial_hash.cpp
    sprite_batcher.cpp
    sprite_region.cpp
    sprite_renderer.cpp
    state_stack.cpp
    text_layout.cpp
    texture_atlas.cpp
    tone_mapper.cpp
    tilemap.cpp
    utf8.cpp
    variables.cpp
    visibility.cpp
    version.cpp
    world.cpp
    world_debug.cpp
    world_save.cpp
)

target_include_directories(moteur PUBLIC include)
target_include_directories(moteur SYSTEM PRIVATE ${STB_INCLUDE_DIRS} ${CGLTF_INCLUDE_DIRS})
# nlohmann/json is public: the data tables (data_table.hpp) hand JSON rows to the game's readers.
target_link_libraries(moteur PUBLIC SDL3::SDL3 glm::glm imgui::imgui EnTT::EnTT nlohmann_json::nlohmann_json
                      PRIVATE mikktspace::mikktspace efsw::efsw KTX::ktx moteur_miniaudio
                              ozz::animation_offline ozz::animation)
if(WIN32)
    target_link_libraries(moteur PRIVATE psapi)  # process_memory_bytes()
endif()
if(MOTEUR_TRACY)
    find_package(Tracy CONFIG REQUIRED)
    target_link_libraries(moteur PUBLIC Tracy::TracyClient)
    target_compile_definitions(moteur PUBLIC MOTEUR_TRACY)
endif()

moteur_enable_warnings(moteur)
