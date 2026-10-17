find_package(EnTT CONFIG REQUIRED)
find_package(ozz-animation CONFIG REQUIRED)
find_package(efsw CONFIG REQUIRED)
find_package(Ktx CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(mikktspace CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(zstd CONFIG REQUIRED)
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
    animation_clock.cpp
    animation_data.cpp
    animation_debug.cpp
    animation_set.cpp
    animation_graph.cpp
    animator.cpp
    animator_graph.cpp
    animator_state.cpp
    application.cpp
    archive.cpp
    assert.cpp
    asset_cache.cpp
    assets.cpp
    atlas_builder.cpp
    audio.cpp
    audio_rules.cpp
    billboard_batcher.cpp
    billboard_renderer.cpp
    camera3d.cpp
    cbor_check.cpp
    collision.cpp
    color.cpp
    crash.cpp
    credits.cpp
    console.cpp
    cook.cpp
    data_reader.cpp
    data_table.cpp
    debug_lines.cpp
    debug_tools.cpp
    debug_ui.cpp
    display.cpp
    environment.cpp
    fixed_timestep.cpp
    input.cpp
    jobs.cpp
    file_io.cpp
    font.cpp
    frame_stats.cpp
    geometry_pool.cpp
    image.cpp
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

# Build information (cmake/BuildInfo.cmake): the commit and its date, rewritten only when they change.
set(_build_info "${CMAKE_CURRENT_BINARY_DIR}/build_info.cpp")
add_custom_target(moteur_build_info
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${MOTEUR_ROOT} -DOUTPUT=${_build_info}
            -P "${MOTEUR_ROOT}/cmake/BuildInfo.cmake"
    BYPRODUCTS "${_build_info}"
    COMMENT "Build information"
    VERBATIM)
target_sources(moteur PRIVATE "${_build_info}")
add_dependencies(moteur moteur_build_info)
target_compile_definitions(moteur PRIVATE MOTEUR_VERSION="${PROJECT_VERSION}"
                           MOTEUR_BUILD_TYPE="$<IF:$<CONFIG:>,${CMAKE_BUILD_TYPE},$<CONFIG>>")

# include/: the public interface (what a game includes); private/: the engine's own headers.
target_include_directories(moteur PUBLIC include PRIVATE private)
target_compile_features(moteur PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(moteur PUBLIC /utf-8)  # the games' sources too (see cmake/Warnings.cmake)
endif()
target_include_directories(moteur SYSTEM PRIVATE ${STB_INCLUDE_DIRS} ${CGLTF_INCLUDE_DIRS})
# nlohmann/json is public: the data tables (data_table.hpp) hand JSON rows to the game's readers.
target_link_libraries(moteur PUBLIC SDL3::SDL3 glm::glm EnTT::EnTT nlohmann_json::nlohmann_json
                      PRIVATE Threads::Threads zstd::libzstd mikktspace::mikktspace KTX::ktx moteur_miniaudio
                              ozz::animation_offline ozz::animation)
if(MOTEUR_SHIPPING)
    # The shipped game (milestone 8, part 6): no Dear ImGui (debug interface and tools), no efsw
    # (hot reload), no profiler zones (macros empty). Their code is not compiled at all.
    get_target_property(_sources moteur SOURCES)
    list(REMOVE_ITEM _sources debug_tools.cpp debug_ui.cpp)
    set_target_properties(moteur PROPERTIES SOURCES "${_sources}")
    target_compile_definitions(moteur PUBLIC MOTEUR_SHIPPING)
    moteur_shipping_options(moteur)
else()
    # ImGui is public: the games draw their debug windows with it.
    target_link_libraries(moteur PUBLIC imgui::imgui PRIVATE efsw::efsw)
endif()
if(WIN32)
    target_link_libraries(moteur PRIVATE psapi dbghelp)  # process_memory_bytes(), crash reports
endif()
if(MOTEUR_TRACY)
    find_package(Tracy CONFIG REQUIRED)
    target_link_libraries(moteur PUBLIC Tracy::TracyClient)
    target_compile_definitions(moteur PUBLIC MOTEUR_TRACY)
endif()

moteur_enable_warnings(moteur)

# The license texts every program of the engine carries (moteur_add_game); keep in step with
# assets_moteur/credits_moteur.json.
set(_licenses sdl3 glm stb nlohmann-json cgltf mikktspace entt ktx miniaudio ozz-animation zstd)
if(NOT MOTEUR_SHIPPING)  # development tools, not in the shipped game
    list(APPEND _licenses imgui efsw)
endif()
if(WIN32 AND NOT MOTEUR_SHIPPING)  # PIX's events: a development tool, not in the shipped game
    list(APPEND _licenses winpixevent)
endif()
if(MOTEUR_TRACY)
    list(APPEND _licenses tracy)
endif()
set(MOTEUR_LICENSE_PACKAGES ${_licenses} CACHE INTERNAL "vcpkg packages whose license texts the engine's programs carry")
