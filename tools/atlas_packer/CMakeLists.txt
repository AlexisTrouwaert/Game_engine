find_package(nlohmann_json CONFIG REQUIRED)
find_path(STB_INCLUDE_DIRS "stb_image_write.h" REQUIRED)

add_executable(atlas_packer main.cpp)

target_include_directories(atlas_packer SYSTEM PRIVATE ${STB_INCLUDE_DIRS})
target_link_libraries(atlas_packer PRIVATE moteur nlohmann_json::nlohmann_json)

moteur_enable_warnings(atlas_packer)
