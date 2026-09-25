#include "moteur/asset_cache.hpp"

#include <filesystem>
#include <stdexcept>
#include <system_error>

namespace moteur {

namespace {

std::filesystem::path utf8_path(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::string utf8_string(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

}  // namespace

std::string normalize_asset_path(std::string_view path) {
    if (path.empty()) {
        throw std::invalid_argument("Empty asset path");
    }
    if (path.front() == '/' || path.front() == '\\' || path.find(':') != std::string_view::npos) {
        throw std::invalid_argument("Asset path '" + std::string(path) + "' is absolute: give it relative to the assets directory");
    }
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t end = path.find_first_of("/\\", start);
        if (end == std::string_view::npos) {
            end = path.size();
        }
        const std::string_view part = path.substr(start, end - start);
        if (part == "..") {
            if (parts.empty()) {
                throw std::invalid_argument("Asset path '" + std::string(path) + "' goes out of the assets directory");
            }
            parts.pop_back();
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        start = end + 1;
    }
    if (parts.empty()) {
        throw std::invalid_argument("Asset path '" + std::string(path) + "' names no file");
    }
    std::string key;
    for (const std::string_view part : parts) {
        if (!key.empty()) {
            key += '/';
        }
        key += part;
    }
    return key;
}

void check_asset_case(const std::string& root, const std::string& key) {
    std::filesystem::path directory = utf8_path(root);
    std::size_t start = 0;
    while (start < key.size()) {
        std::size_t end = key.find('/', start);
        if (end == std::string::npos) {
            end = key.size();
        }
        const std::string part = key.substr(start, end - start);
        std::error_code error;
        std::filesystem::directory_iterator entries(directory, error);
        if (error) {
            return;  // no such directory: the loader says so
        }
        bool exact = false;
        std::string on_disk;
        for (const auto& entry : entries) {
            const std::string name = utf8_string(entry.path().filename());
            if (name == part) {
                exact = true;
                break;
            }
            if (name.size() == part.size() && on_disk.empty()) {
                bool same = true;
                for (std::size_t i = 0; i < name.size() && same; ++i) {
                    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
                    same = lower(name[i]) == lower(part[i]);
                }
                if (same) {
                    on_disk = name;
                }
            }
        }
        if (!exact) {
            if (!on_disk.empty()) {
                throw std::runtime_error("Asset '" + key + "': '" + part + "' is written '" + on_disk +
                                         "' on disk (the case must match: other systems and archives are case-sensitive)");
            }
            return;  // missing: the loader says so
        }
        directory /= utf8_path(part);
        start = end + 1;
    }
}

}  // namespace moteur
