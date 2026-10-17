# Compiler and linker settings of the shipping configuration (MOTEUR_SHIPPING, milestone 8, part 6),
# for every target of the build, the engine's and the game's (the engine may be a game's subfolder:
# what must reach the game's targets is either a cache variable or carried by the `moteur` target):
# - optimisation across files (LTO / LTCG);
# - debug information produced and kept apart from the program (Windows: a PDB next to the .exe;
#   macOS: -g, the dSYM is extracted when packaging), so that a crash report gives function names
#   while the shipped program stays lean;
# - Windows: the C++ runtime linked statically (/MT, preset windows-shipping with the triplet
#   x64-windows-static): nothing to install besides the game.
if(NOT MOTEUR_SHIPPING)
    function(moteur_shipping_options target)
    endfunction()
    return()
endif()

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON CACHE BOOL "Shipping: link-time optimisation" FORCE)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING "Shipping: static C++ runtime" FORCE)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "ProgramDatabase" CACHE STRING "Shipping: symbols in a PDB" FORCE)
endif()

# Called on the engine's library: its users (the game's programs) get the same options.
function(moteur_shipping_options target)
    if(MSVC)
        target_link_options(${target} INTERFACE /DEBUG:FULL /OPT:REF /OPT:ICF)
    else()
        target_compile_options(${target} PUBLIC -g)
    endif()
endfunction()
