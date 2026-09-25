#pragma once

#include <cstddef>
#include <string>

namespace moteur {

// Directory of the executable, with a trailing separator. Assets and shaders are looked up
// relative to it, so the program works from any working directory.
std::string base_path();

// <base_path>/assets/<name>
std::string asset_path(const std::string& name);

// The bytes of a file read by read_file(). Moves, does not copy.
class FileData {
public:
    FileData() = default;
    FileData(void* data, std::size_t size) : data_(data), size_(size) {}
    ~FileData();
    FileData(FileData&& other) noexcept;
    FileData& operator=(FileData&& other) noexcept;
    FileData(const FileData&) = delete;
    FileData& operator=(const FileData&) = delete;

    const void* data() const { return data_; }
    std::size_t size() const { return size_; }
    // Hands the bytes over: the caller frees them with SDL_free.
    void* release();

private:
    void* data_ = nullptr;
    std::size_t size_ = 0;
};

// The whole content of a file. Every file the engine loads (assets, not shaders) is read through
// here, so that an archive can replace the directory when the game is packaged. SDL_LoadFile
// handles UTF-8 paths on Windows, which fopen does not.
// Throws std::runtime_error with SDL's message if the file cannot be read.
FileData read_file(const std::string& path);

// read_file() as a string (text files, JSON).
std::string read_text_file(const std::string& path);

}  // namespace moteur
