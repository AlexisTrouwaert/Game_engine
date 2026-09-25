find_package(EnTT CONFIG REQUIRED)
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
    color.cpp
    debug_lines.cpp
    debug_tools.cpp
    debug_ui.cpp
    environment.cpp
    fixed_timestep.cpp
    input.cpp
    font.cpp
    frame_stats.cpp
    image.cpp
    iso.cpp
    ktx_texture.cpp
    mesh.cpp
    mesh_batcher.cpp
    mesh_renderer.cpp
    model.cpp
    paths.cpp
    process_memory.cpp
    renderer.cpp
    screenshot.cpp
    shadow.cpp
    sound.cpp
    sprite_batcher.cpp
    sprite_region.cpp
    sprite_renderer.cpp
    state_stack.cpp
    text_layout.cpp
    texture_atlas.cpp
    tone_mapper.cpp
    tilemap.cpp
    utf8.cpp
    version.cpp
    world.cpp
)

target_include_directories(moteur PUBLIC include)
target_include_directories(moteur SYSTEM PRIVATE ${STB_INCLUDE_DIRS} ${CGLTF_INCLUDE_DIRS})
target_link_libraries(moteur PUBLIC SDL3::SDL3 glm::glm imgui::imgui EnTT::EnTT
                      PRIVATE nlohmann_json::nlohmann_json mikktspace::mikktspace efsw::efsw KTX::ktx moteur_miniaudio)
if(WIN32)
    target_link_libraries(moteur PRIVATE psapi)  # process_memory_bytes()
endif()

moteur_enable_warnings(moteur)
