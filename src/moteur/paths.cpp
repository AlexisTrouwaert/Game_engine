#include "moteur/paths.hpp"

#include <SDL3/SDL.h>

#include <stdexcept>
#include <utility>

namespace moteur {

std::string base_path() {
    const char* path = SDL_GetBasePath();
    return path != nullptr ? path : "";
}

std::string asset_path(const std::string& name) {
    return base_path() + "assets/" + name;
}

FileData::~FileData() {
    SDL_free(data_);
}

FileData::FileData(FileData&& other) noexcept
    : data_(std::exchange(other.data_, nullptr)), size_(std::exchange(other.size_, 0)) {}

FileData& FileData::operator=(FileData&& other) noexcept {
    if (this != &other) {
        SDL_free(data_);
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
    }
    return *this;
}

void* FileData::release() {
    size_ = 0;
    return std::exchange(data_, nullptr);
}

FileData read_file(const std::string& path) {
    std::size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (data == nullptr) {
        throw std::runtime_error(std::string("cannot read the file: ") + SDL_GetError());
    }
    return FileData(data, size);
}

std::string read_text_file(const std::string& path) {
    const FileData file = read_file(path);
    return std::string(static_cast<const char*>(file.data()), file.size());
}

}  // namespace moteur
