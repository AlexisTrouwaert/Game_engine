#include "moteur/paths.hpp"

#include <SDL3/SDL.h>

#include <stdexcept>

namespace moteur {

std::string base_path() {
    const char* path = SDL_GetBasePath();
    return path != nullptr ? path : "";
}

std::string asset_path(const std::string& name) {
    return base_path() + "assets/" + name;
}

std::string read_text_file(const std::string& path) {
    std::size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (data == nullptr) {
        throw std::runtime_error(std::string("cannot read the file: ") + SDL_GetError());
    }
    std::string text(static_cast<const char*>(data), size);
    SDL_free(data);
    return text;
}

}  // namespace moteur
