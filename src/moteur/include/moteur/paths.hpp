#pragma once

#include <string>

namespace moteur {

// Directory of the executable, with a trailing separator. Assets and shaders are looked up
// relative to it, so the program works from any working directory.
std::string base_path();

// <base_path>/assets/<name>
std::string asset_path(const std::string& name);

}  // namespace moteur
