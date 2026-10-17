find_package(nlohmann_json CONFIG REQUIRED)

# The playable slice's code (milestone 8, part 12): its data tables and map objects, the world
# (Demo3D), the screens (StatesDemo) and their interface (SliceUi). Shared by the program `tranche`
# and by the sandbox (its "states" scene and the reference captures); built in every configuration,
# the shipping one included (Dear ImGui only in its debug panels, compiled out there).
add_library(tranche_commun STATIC sandbox_data.cpp map_objects.cpp demo3d.cpp states_demo.cpp slice_ui.cpp)
target_link_libraries(tranche_commun PUBLIC moteur nlohmann_json::nlohmann_json)
target_include_directories(tranche_commun PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
moteur_enable_warnings(tranche_commun)
if(NOT MOTEUR_SHIPPING)
    # Hot reload of the assets while developing (see Assets::enable_hot_reload; --no-hot-reload), and
    # where the editor writes the maps.
    target_compile_definitions(tranche_commun PUBLIC MOTEUR_ASSETS_SOURCE_DIR="${PROJECT_SOURCE_DIR}/assets")
endif()

if(NOT MOTEUR_BUILD_SLICE)
    return()  # only the library, for the sandbox
endif()

# The playable slice as a program of its own (milestone 8, part 12), and its cooking tool.
add_executable(tranche main.cpp)
target_link_libraries(tranche PRIVATE tranche_commun)
moteur_enable_warnings(tranche)
moteur_add_game(tranche NAME "Tranche jouable" PUBLISHER "moteur" ICON "${PROJECT_SOURCE_DIR}/art/icons/tranche"
                ASSETS "${PROJECT_SOURCE_DIR}/assets")

# Prepares the slice's assets for delivery (moteur/cook.hpp): its manifest is assets/livraison_tranche.json.
add_executable(tranche_cook cook_main.cpp)
target_link_libraries(tranche_cook PRIVATE tranche_commun)
moteur_enable_warnings(tranche_cook)

# The players' packages (shipping configuration, preset windows-shipping / macos-shipping):
# cmake --build build/windows-shipping --target tranche_package (cmake/Package.cmake).
moteur_package(tranche MANIFEST "${PROJECT_SOURCE_DIR}/assets/livraison_tranche.json" COOK_TOOL tranche_cook
               IDENTIFIER org.moteur.tranche)
