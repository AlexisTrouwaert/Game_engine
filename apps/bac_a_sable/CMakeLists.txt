find_package(nlohmann_json CONFIG REQUIRED)

# The sandbox and the map editor (development tools; not in the shipping configuration). The
# playable slice's code they run (tranche_commun) is in apps/tranche.

# What the sandbox and the map editor share: the "Monde" scene (the editor's "Essayer") and the
# editor itself (milestone 7, parts 7 and 8).
# The 2D of milestone 2 (2D camera, isometric projection, frame animations), kept for the sandbox's
# 2D scenes and their reference capture; no longer part of the engine (review of milestones 1 to 7,
# 2.1). The engine's tests check it too (tests/test_camera.cpp, test_iso.cpp, test_animation.cpp).
add_library(bac_a_sable_2d STATIC camera2d.cpp iso.cpp sprite_animation.cpp)
target_link_libraries(bac_a_sable_2d PUBLIC moteur nlohmann_json::nlohmann_json)
target_include_directories(bac_a_sable_2d PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
moteur_enable_warnings(bac_a_sable_2d)

add_library(bac_a_sable_commun STATIC world_test.cpp map_editor.cpp)
target_link_libraries(bac_a_sable_commun PUBLIC tranche_commun)
moteur_enable_warnings(bac_a_sable_commun)

add_executable(bac_a_sable main.cpp blender_compare.cpp audio_test.cpp animation_test.cpp anim_compare.cpp)
target_link_libraries(bac_a_sable PRIVATE bac_a_sable_commun bac_a_sable_2d)
moteur_enable_warnings(bac_a_sable)

# The map editor as a program of its own.
add_executable(editeur editeur_main.cpp)
target_link_libraries(editeur PRIVATE bac_a_sable_commun)
moteur_enable_warnings(editeur)

# Shaders, assets (the engine's, then the sandbox's) and license texts next to the executable; the
# editor lands in the same folder and shares them.
moteur_add_game(bac_a_sable ASSETS "${PROJECT_SOURCE_DIR}/assets")
moteur_add_game(editeur SHARE_OUTPUT_OF bac_a_sable)

# Sprites are packed from the art sources at build time.
moteur_add_atlas(bac_a_sable NAME world SOURCES art/world)
moteur_add_atlas(bac_a_sable NAME test SOURCES art/test)
