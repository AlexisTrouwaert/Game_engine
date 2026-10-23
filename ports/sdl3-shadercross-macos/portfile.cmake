# Overlay port (milestone 9 bis, part 2): SDL_shadercross's command-line tool on macOS, so that the
# Mac compiles the engine's HLSL to MSL itself instead of copying the MSL generated on Windows.
#
# The registry's sdl3-shadercross needs directx-dxc, which has no macOS build. HLSL still needs DXC:
# it is taken from the LunarG Vulkan SDK (its macOS installer ships libdxcompiler.dylib and
# include/dxc/dxcapi.h), found through VULKAN_SDK, else under ~/VulkanSDK/<version>/macOS. The dylib
# is copied next to the tool. libdxil is not needed: shadercross only signs DXIL, never made here.
#
# NOT TRIED YET (written on Windows): to check at the next Mac session, see moteur_doc/TEST_MAC.md.
# The same source as the registry's port (same REF and hash).

set(dxc_root "")
set(candidates "$ENV{VULKAN_SDK}")
file(GLOB sdk_versions LIST_DIRECTORIES true "$ENV{HOME}/VulkanSDK/*/macOS")
list(SORT sdk_versions COMPARE NATURAL ORDER DESCENDING)  # the newest first
list(APPEND candidates ${sdk_versions})
foreach(candidate IN LISTS candidates)
    if(candidate AND EXISTS "${candidate}/lib/libdxcompiler.dylib" AND EXISTS "${candidate}/include/dxc/dxcapi.h")
        set(dxc_root "${candidate}")
        break()
    endif()
endforeach()
if(NOT dxc_root)
    message(FATAL_ERROR "sdl3-shadercross-macos: DXC not found. Install the LunarG Vulkan SDK for macOS "
                        "(https://vulkan.lunarg.com/sdk/home), and set VULKAN_SDK to its macOS folder, or leave it "
                        "under ~/VulkanSDK/<version>/macOS. Looked in: ${candidates}")
endif()
message(STATUS "sdl3-shadercross-macos: DXC from ${dxc_root}")

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO libsdl-org/SDL_shadercross
    REF 1ca46e0ef7a9e50c706e7be6ef73ce467bac3b2e
    SHA512 61feb70137b1a8e9037b0f9113a28b1f59578d6604423ef2a66173d9772b15a001f661cb4b53b0f3be941c0178d3562b0ae4d7a7f35404b137204bdce95f537f
    HEAD_REF main
)
# Our finder: the SDK's layout, dxil optional, the headers' folder given to the target.
file(COPY "${CMAKE_CURRENT_LIST_DIR}/FindDirectXShaderCompiler.cmake" DESTINATION "${SOURCE_PATH}/cmake")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DSDLSHADERCROSS_INSTALL=ON
        -DSDLSHADERCROSS_INSTALL_CMAKEDIR_ROOT=share/sdl3_shadercross
        -DSDLSHADERCROSS_INSTALL_RUNTIME=OFF
        -DSDLSHADERCROSS_SPIRVCROSS_SHARED=OFF
        -DSDLSHADERCROSS_VENDORED=OFF
        -DSDLSHADERCROSS_DXC=ON
        "-DDirectXShaderCompiler_ROOT=${dxc_root}"
        "-DCMAKE_INSTALL_RPATH=@loader_path"
        -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME "sdl3_shadercross" CONFIG_PATH "share/sdl3_shadercross/SDL3_shadercross")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_fixup_pkgconfig()

vcpkg_copy_tools(TOOL_NAMES shadercross AUTO_CLEAN)
# The tool finds DXC next to itself (@loader_path).
file(COPY "${dxc_root}/lib/libdxcompiler.dylib" DESTINATION "${CURRENT_PACKAGES_DIR}/tools/${PORT}")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")
set(VCPKG_POLICY_SKIP_ABSOLUTE_PATHS_CHECK enabled)  # the SDK's path in the build's notes
