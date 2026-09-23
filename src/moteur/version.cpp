#include "moteur/version.hpp"

#include <SDL3/SDL.h>

namespace moteur {

std::string sdl_version() {
    const int v = SDL_GetVersion();
    return std::to_string(SDL_VERSIONNUM_MAJOR(v)) + "." +
           std::to_string(SDL_VERSIONNUM_MINOR(v)) + "." +
           std::to_string(SDL_VERSIONNUM_MICRO(v));
}

}  // namespace moteur
