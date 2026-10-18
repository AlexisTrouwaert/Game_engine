#include "moteur/paths.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <utility>

#include "moteur/archive.hpp"

namespace moteur {

namespace {

struct Mounted {
    std::shared_mutex mutex;
    std::vector<std::unique_ptr<Archive>> archives;  // the last one first when looking
    std::vector<std::string> folders;                // mounted asset folders, the first one first
    bool only = false;
};

Mounted& mounted() {
    static Mounted state;
    return state;
}

// `path` relative to the program's folder, with '/' separators, if it is inside it.
std::optional<std::string> program_relative(const std::string& path) {
    static const std::string base = [] {
        std::string text = base_path();
        std::replace(text.begin(), text.end(), '\\', '/');
        return text;
    }();
    std::string normal = path;
    std::replace(normal.begin(), normal.end(), '\\', '/');
    if (base.empty() || normal.size() <= base.size() || normal.compare(0, base.size(), base) != 0) {
        return std::nullopt;
    }
    // "a/./b", "a/b/../c": as the archive's keys are written.
    const std::filesystem::path relative(std::u8string(normal.begin() + static_cast<std::ptrdiff_t>(base.size()), normal.end()));
    const std::u8string key = relative.lexically_normal().generic_u8string();
    return std::string(key.begin(), key.end());
}

// The archive entry standing for `path`: its archive and entry, or nulls.
std::pair<const Archive*, const Archive::Entry*> find_in_archives(const std::string& path, bool& inside_program) {
    inside_program = false;
    Mounted& state = mounted();
    if (state.archives.empty()) {
        return {nullptr, nullptr};
    }
    const std::optional<std::string> key = program_relative(path);
    if (!key) {
        return {nullptr, nullptr};
    }
    inside_program = true;
    for (auto it = state.archives.rbegin(); it != state.archives.rend(); ++it) {
        if (const Archive::Entry* entry = (*it)->find(*key)) {
            return {it->get(), entry};
        }
    }
    return {nullptr, nullptr};
}

// The asset key of `path` when it is inside the program's assets folder.
std::optional<std::string> asset_key_of(const std::string& path) {
    const std::optional<std::string> relative = program_relative(path);
    if (!relative || !relative->starts_with("assets/")) {
        return std::nullopt;
    }
    return relative->substr(7);
}

// The file of a mounted asset folder standing for `path`, if one has it. Under the lock.
std::optional<std::string> in_asset_folders(const std::string& path) {
    Mounted& state = mounted();
    if (state.folders.empty()) {
        return std::nullopt;
    }
    const std::optional<std::string> key = asset_key_of(path);
    if (!key) {
        return std::nullopt;
    }
    for (const std::string& folder : state.folders) {
        std::string candidate = folder + *key;
        SDL_PathInfo info;
        if (SDL_GetPathInfo(candidate.c_str(), &info) && info.type == SDL_PATHTYPE_FILE) {
            return candidate;
        }
    }
    return std::nullopt;
}

}  // namespace

void mount_asset_folder(const std::string& folder) {
    Mounted& state = mounted();
    const std::unique_lock lock(state.mutex);
    std::string normal = folder;
    std::replace(normal.begin(), normal.end(), '\\', '/');
    if (!normal.empty() && normal.back() != '/') {
        normal += '/';
    }
    if (std::find(state.folders.begin(), state.folders.end(), normal) == state.folders.end()) {
        state.folders.push_back(normal);
    }
}

void unmount_asset_folders() {
    Mounted& state = mounted();
    const std::unique_lock lock(state.mutex);
    state.folders.clear();
}

std::vector<std::string> mounted_asset_folders() {
    Mounted& state = mounted();
    const std::shared_lock lock(state.mutex);
    return state.folders;
}

std::string disk_path(const std::string& path) {
    SDL_PathInfo info;
    if (SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE) {
        return path;
    }
    Mounted& state = mounted();
    const std::shared_lock lock(state.mutex);
    return in_asset_folders(path).value_or(path);
}

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
    {
        Mounted& state = mounted();
        const std::shared_lock lock(state.mutex);
        bool inside_program = false;
        const auto [archive, entry] = find_in_archives(path, inside_program);
        if (entry != nullptr) {
            const std::vector<std::uint8_t> bytes = archive->read(*entry);
            auto* data = static_cast<std::uint8_t*>(SDL_malloc(bytes.size() + 1));
            if (data == nullptr) {
                throw std::runtime_error("out of memory reading " + path);
            }
            std::memcpy(data, bytes.data(), bytes.size());
            data[bytes.size()] = 0;
            return FileData(data, bytes.size());
        }
        if (inside_program && state.only) {
            throw std::runtime_error("cannot read the file: not in the game's archives (" + path + ")");
        }
    }
    std::size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (data == nullptr) {
        const std::string error = SDL_GetError();
        std::optional<std::string> mounted_file;
        {
            const std::shared_lock lock(mounted().mutex);
            mounted_file = in_asset_folders(path);
        }
        if (mounted_file) {
            data = SDL_LoadFile(mounted_file->c_str(), &size);
            if (data == nullptr) {
                throw std::runtime_error("cannot read the file: " + std::string(SDL_GetError()));
            }
        }
        if (data == nullptr) {
            throw std::runtime_error("cannot read the file: " + error);
        }
    }
    return FileData(data, size);
}

bool file_exists(const std::string& path) {
    {
        Mounted& state = mounted();
        const std::shared_lock lock(state.mutex);
        bool inside_program = false;
        if (find_in_archives(path, inside_program).second != nullptr) {
            return true;
        }
        if (inside_program && state.only) {
            return false;
        }
    }
    SDL_PathInfo info;
    if (SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE) {
        return true;
    }
    const std::shared_lock lock(mounted().mutex);
    return in_asset_folders(path).has_value();
}

bool mount_archive(const std::string& path, std::string& error) {
    std::unique_ptr<Archive> archive = Archive::open(path, error);
    if (!archive) {
        return false;
    }
    Mounted& state = mounted();
    const std::unique_lock lock(state.mutex);
    SDL_Log("Archive: '%s' mounted (%zu files)", path.c_str(), archive->entries().size());
    state.archives.push_back(std::move(archive));
    return true;
}

std::size_t mount_archives(const std::string& directory, std::vector<std::string>& errors) {
    std::vector<std::string> names;
    std::error_code error;
    const std::filesystem::path folder(std::u8string(directory.begin(), directory.end()));
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
        const std::u8string name = entry.path().filename().u8string();
        if (entry.is_regular_file() && entry.path().extension() == ".pak") {
            names.emplace_back(name.begin(), name.end());
        }
    }
    std::sort(names.begin(), names.end());
    std::size_t count = 0;
    for (const std::string& name : names) {
        std::string message;
        std::string path = directory;
        if (!path.empty() && path.back() != '/' && path.back() != '\\') {
            path += '/';
        }
        if (mount_archive(path + name, message)) {
            ++count;
        } else {
            errors.push_back(message);
        }
    }
    return count;
}

void unmount_archives() {
    Mounted& state = mounted();
    const std::unique_lock lock(state.mutex);
    state.archives.clear();
    state.only = false;
}

std::size_t mounted_archives() {
    Mounted& state = mounted();
    const std::shared_lock lock(state.mutex);
    return state.archives.size();
}

void set_archives_only(bool only) {
    Mounted& state = mounted();
    const std::unique_lock lock(state.mutex);
    state.only = only;
}

bool archives_only() {
    Mounted& state = mounted();
    const std::shared_lock lock(state.mutex);
    return state.only;
}

std::string read_text_file(const std::string& path) {
    const FileData file = read_file(path);
    return std::string(static_cast<const char*>(file.data()), file.size());
}

}  // namespace moteur
