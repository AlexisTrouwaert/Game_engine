#pragma once

#include <string>

namespace moteur {

// Directory of the executable, with a trailing separator. Assets and shaders are looked up
// relative to it, so the program works from any working directory.
std::string base_path();

// <base_path>/assets/<name>
std::string asset_path(const std::string& name);

// The whole content of a file. SDL_LoadFile handles UTF-8 paths on Windows, which fopen does not.
// Throws std::runtime_error with SDL's message if the file cannot be read.
std::string read_text_file(const std::string& path);

}  // namespace moteur
