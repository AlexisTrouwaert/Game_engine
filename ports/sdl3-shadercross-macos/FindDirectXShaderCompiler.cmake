# DXC for SDL_shadercross on macOS (overlay port sdl3-shadercross-macos): the LunarG Vulkan SDK's
# libdxcompiler.dylib and include/dxc/dxcapi.h under DirectXShaderCompiler_ROOT. libdxil is optional
# (shadercross loads it only to sign DXIL, which the Mac never makes).

find_path(DirectXShaderCompiler_INCLUDE_PATH NAMES "dxcapi.h" PATH_SUFFIXES "include/dxc" "include" HINTS ${DirectXShaderCompiler_ROOT} NO_DEFAULT_PATH)
find_library(DirectXShaderCompiler_dxcompiler_LIBRARY NAMES "dxcompiler" PATH_SUFFIXES "lib" HINTS ${DirectXShaderCompiler_ROOT} NO_DEFAULT_PATH)
find_library(DirectXShaderCompiler_dxil_LIBRARY NAMES "dxil" PATH_SUFFIXES "lib" HINTS ${DirectXShaderCompiler_ROOT} NO_DEFAULT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(DirectXShaderCompiler
    REQUIRED_VARS DirectXShaderCompiler_INCLUDE_PATH DirectXShaderCompiler_dxcompiler_LIBRARY)

if(DirectXShaderCompiler_FOUND)
    if(NOT TARGET DirectXShaderCompiler::dxcompiler)
        add_library(DirectXShaderCompiler::dxcompiler IMPORTED SHARED)
        set_property(TARGET DirectXShaderCompiler::dxcompiler PROPERTY IMPORTED_LOCATION "${DirectXShaderCompiler_dxcompiler_LIBRARY}")
        set_property(TARGET DirectXShaderCompiler::dxcompiler PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${DirectXShaderCompiler_INCLUDE_PATH}")
    endif()
    if(NOT TARGET DirectXShaderCompiler::dxil)
        add_library(DirectXShaderCompiler::dxil INTERFACE IMPORTED)
        if(DirectXShaderCompiler_dxil_LIBRARY)
            set_property(TARGET DirectXShaderCompiler::dxil PROPERTY INTERFACE_LINK_LIBRARIES "${DirectXShaderCompiler_dxil_LIBRARY}")
        endif()
    endif()
endif()
