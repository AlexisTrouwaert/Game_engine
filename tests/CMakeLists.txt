find_package(doctest CONFIG REQUIRED)

add_executable(moteur_tests
    main.cpp
    test_animation.cpp
    test_atlas_builder.cpp
    test_camera.cpp
    test_fixed_timestep.cpp
    test_iso.cpp
    test_frame_stats.cpp
    test_image.cpp
    test_sprite_batcher.cpp
    test_sprite_region.cpp
    test_text_layout.cpp
    test_tilemap.cpp
    test_utf8.cpp
)

target_link_libraries(moteur_tests PRIVATE moteur doctest::doctest)

moteur_enable_warnings(moteur_tests)

add_test(NAME moteur_tests COMMAND moteur_tests)
