# Atlas build chain.
#
#   moteur_add_atlas(<target> NAME <name> SOURCES <folder>)
#
# Packs the PNG images of <folder> (relative to the project root) into an atlas named <name>, with
# the atlas_packer tool, at every build where an image (or pivots.json) has changed. The result,
# <name>.json and <name>_0.png, <name>_1.png..., lands in the assets/ folder next to the
# executable of <target>, where TextureAtlas::load() finds it.
#
# Unlike the shaders, the packer is our own program, so the same commands run on Windows and macOS.

function(moteur_add_atlas target)
    cmake_parse_arguments(ARG "" "NAME;SOURCES" "" ${ARGN})
    get_target_property(binary_dir ${target} BINARY_DIR)
    set(out_dir "${binary_dir}/assets")
    set(src_dir "${CMAKE_SOURCE_DIR}/${ARG_SOURCES}")

    # CONFIGURE_DEPENDS makes CMake notice added or removed images without being asked to reconfigure.
    file(GLOB_RECURSE inputs CONFIGURE_DEPENDS "${src_dir}/*.png" "${src_dir}/pivots.json")
    list(SORT inputs)

    # A removed image is a dependency that vanishes, and nothing newer than the output remains, so
    # the packing would not run again and the atlas would keep the deleted image. This file lists the
    # inputs and is rewritten only when that list changes: depending on it covers that case.
    string(REPLACE ";" "\n" inputs_text "${inputs}")
    set(inputs_list "${binary_dir}/atlas_${ARG_NAME}.inputs")
    file(CONFIGURE OUTPUT "${inputs_list}" CONTENT "@inputs_text@\n" @ONLY)

    add_custom_command(
        OUTPUT "${out_dir}/${ARG_NAME}.json"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${out_dir}"
        COMMAND atlas_packer --input "${src_dir}" --output "${out_dir}" --name ${ARG_NAME}
        DEPENDS atlas_packer ${inputs} "${inputs_list}"
        COMMENT "Packing atlas ${ARG_NAME}"
        VERBATIM)

    add_custom_target(${target}_atlas_${ARG_NAME} DEPENDS "${out_dir}/${ARG_NAME}.json")
    add_dependencies(${target} ${target}_atlas_${ARG_NAME})
endfunction()
