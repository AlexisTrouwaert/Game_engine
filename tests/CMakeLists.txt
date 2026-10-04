find_package(doctest CONFIG REQUIRED)

add_executable(moteur_tests
    main.cpp
    test_animation.cpp
    test_animator.cpp
    test_animation_graph.cpp
    test_blending.cpp
    test_attachment.cpp
    test_animation_debug.cpp
    test_asset_cache.cpp
    test_anti_aliasing.cpp
    test_atlas_builder.cpp
    test_audio.cpp
    test_billboard_batcher.cpp
    test_camera.cpp
    test_camera3d.cpp
    test_color.cpp
    test_console.cpp
    test_data_table.cpp
    test_debug_lines.cpp
    test_debug_tools.cpp
    test_environment.cpp
    test_fixed_timestep.cpp
    test_iso.cpp
    test_map_data.cpp
    test_map_document.cpp
    test_ktx_texture.cpp
    test_frame_stats.cpp
    test_image.cpp
    test_input.cpp
    test_mesh.cpp
    test_mesh_batcher.cpp
    test_model.cpp
    test_ozz.cpp
    test_profiler.cpp
    test_particles.cpp
    test_save_file.cpp
    test_robustness.cpp
    test_shadow.cpp
    test_skeleton.cpp
    test_skinning.cpp
    test_sprite_batcher.cpp
    test_sprite_region.cpp
    test_state_stack.cpp
    test_text_layout.cpp
    test_tilemap.cpp
    test_utf8.cpp
    test_world.cpp
    test_world_grid.cpp
    test_world_save.cpp
)

find_package(Ktx CONFIG REQUIRED)
find_package(ozz-animation CONFIG REQUIRED)

target_link_libraries(moteur_tests PRIVATE moteur doctest::doctest KTX::ktx  # KTX: builds test files
                      ozz::animation_offline)                          # ozz: checks the library itself

# Tests that read files of the source tree (the skinned reference model, the downloaded test characters).
target_compile_definitions(moteur_tests PRIVATE MOTEUR_SOURCE_ASSETS="${CMAKE_SOURCE_DIR}/assets/")

moteur_enable_warnings(moteur_tests)

add_test(NAME moteur_tests COMMAND moteur_tests)
