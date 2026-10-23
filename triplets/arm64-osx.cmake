# Same as vcpkg's arm64-osx, plus the project's minimum macOS version:
# without it, dependencies target the build machine's macOS and the linker
# warns on every object file.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)
set(VCPKG_OSX_DEPLOYMENT_TARGET 13.0)

# The port sdl3-shadercross-macos (feature mac-shaders, milestone 9 bis, part 2) looks for the
# LunarG Vulkan SDK's DXC there.
set(VCPKG_ENV_PASSTHROUGH VULKAN_SDK HOME)
