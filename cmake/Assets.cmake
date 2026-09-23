# Asset copy.
#
#   moteur_add_assets(<target>)
#
# Copies the assets/ folder next to the executable of <target>, so that asset_path()
# finds it. The copy runs at every build, which keeps edited assets up to date.

function(moteur_add_assets target)
    add_custom_target(${target}_assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:${target}>/assets"
        COMMENT "Copying assets"
        VERBATIM)
    add_dependencies(${target} ${target}_assets)
endfunction()
