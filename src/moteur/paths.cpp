#include "moteur/paths.hpp"

#include <SDL3/SDL.h>

namespace moteur {

std::string base_path() {
    const char* path = SDL_GetBasePath();
    return path != nullptr ? path : "";
}

std::string asset_path(const std::string& name) {
    return base_path() + "assets/" + name;
}

}  // namespace moteur
