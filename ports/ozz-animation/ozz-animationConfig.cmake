# ozz-animation installs its libraries but no CMake package: this file declares them as imported
# targets. ozz::animation_offline builds skeletons and animations at load time (from glTF data).
get_filename_component(_ozz_prefix "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

foreach(_ozz_lib base animation animation_offline geometry)
    if(NOT TARGET ozz::${_ozz_lib})
        add_library(ozz::${_ozz_lib} STATIC IMPORTED)
        set(_ozz_file "${CMAKE_STATIC_LIBRARY_PREFIX}ozz_${_ozz_lib}${CMAKE_STATIC_LIBRARY_SUFFIX}")
        set_target_properties(ozz::${_ozz_lib} PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${_ozz_prefix}/include"
            IMPORTED_CONFIGURATIONS "RELEASE;DEBUG"
            IMPORTED_LOCATION "${_ozz_prefix}/lib/${_ozz_file}"
            IMPORTED_LOCATION_RELEASE "${_ozz_prefix}/lib/${_ozz_file}"
            IMPORTED_LOCATION_DEBUG "${_ozz_prefix}/debug/lib/${_ozz_file}"
            MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
            MAP_IMPORTED_CONFIG_MINSIZEREL Release)
    endif()
endforeach()

set_property(TARGET ozz::animation APPEND PROPERTY INTERFACE_LINK_LIBRARIES ozz::base)
set_property(TARGET ozz::geometry APPEND PROPERTY INTERFACE_LINK_LIBRARIES ozz::base)
set_property(TARGET ozz::animation_offline APPEND PROPERTY INTERFACE_LINK_LIBRARIES ozz::animation)

unset(_ozz_prefix)
unset(_ozz_file)
unset(_ozz_lib)
set(ozz-animation_FOUND TRUE)
