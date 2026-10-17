add_executable(moteur_cook main.cpp)
target_link_libraries(moteur_cook PRIVATE moteur)
moteur_enable_warnings(moteur_cook)
