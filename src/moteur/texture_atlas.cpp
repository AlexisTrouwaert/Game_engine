#include "moteur/texture_atlas.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <stdexcept>

#include "moteur/image.hpp"
#include "moteur/paths.hpp"

namespace moteur {

namespace {

constexpr int kSupportedVersion = 1;

std::string directory_of(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

}  // namespace

TextureAtlas TextureAtlas::load(Renderer& renderer, const std::string& json_path) {
    TextureAtlas atlas;
    atlas.source_ = json_path;

    try {
        const nlohmann::json doc = nlohmann::json::parse(read_text_file(json_path));

        const int version = doc.at("version").get<int>();
        if (version != kSupportedVersion) {
            throw std::runtime_error("format version " + std::to_string(version) + " is not supported (expected " +
                                     std::to_string(kSupportedVersion) + ")");
        }

        const std::string directory = directory_of(json_path);
        std::vector<glm::vec2> page_sizes;
        for (const nlohmann::json& page : doc.at("pages")) {
            const std::string file = page.at("file").get<std::string>();
            const int width = page.at("width").get<int>();
            const int height = page.at("height").get<int>();

            const Image image = load_image(directory + file);
            if (image.width != width || image.height != height) {
                throw std::runtime_error("page '" + file + "' is " + std::to_string(image.width) + "x" +
                                         std::to_string(image.height) + " but the atlas declares " +
                                         std::to_string(width) + "x" + std::to_string(height));
            }
            atlas.pages_.push_back(std::make_unique<Texture>(renderer.create_texture(image, file.c_str())));
            page_sizes.emplace_back(static_cast<float>(width), static_cast<float>(height));
        }

        for (const auto& [name, frame] : doc.at("frames").items()) {
            const int page = frame.at("page").get<int>();
            if (page < 0 || static_cast<std::size_t>(page) >= atlas.pages_.size()) {
                throw std::runtime_error("sprite '" + name + "' refers to page " + std::to_string(page) +
                                         ", which does not exist");
            }
            const int x = frame.at("x").get<int>(), y = frame.at("y").get<int>();
            const int w = frame.at("w").get<int>(), h = frame.at("h").get<int>();
            const glm::vec2 page_size = page_sizes[static_cast<std::size_t>(page)];
            if (x < 0 || y < 0 || w <= 0 || h <= 0 || static_cast<float>(x + w) > page_size.x ||
                static_cast<float>(y + h) > page_size.y) {
                throw std::runtime_error("sprite '" + name + "' lies outside its page");
            }

            SpriteRegion region;
            region.texture = atlas.pages_[static_cast<std::size_t>(page)].get();
            // Texel edges, not centers: the region covers exactly its own texels.
            region.uv_rect = {static_cast<float>(x) / page_size.x, static_cast<float>(y) / page_size.y,
                              static_cast<float>(x + w) / page_size.x, static_cast<float>(y + h) / page_size.y};
            region.size = {static_cast<float>(w), static_cast<float>(h)};
            region.source_size = {frame.at("source_w").get<float>(), frame.at("source_h").get<float>()};
            region.offset = {frame.at("offset_x").get<float>(), frame.at("offset_y").get<float>()};
            region.pivot = {frame.at("pivot_x").get<float>(), frame.at("pivot_y").get<float>()};
            atlas.regions_.emplace(name, region);
        }
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error("Atlas '" + json_path + "' is not valid: " + e.what());
    } catch (const std::runtime_error& e) {
        throw std::runtime_error("Atlas '" + json_path + "': " + e.what());
    }
    return atlas;
}

const SpriteRegion& TextureAtlas::region(const std::string& name) const {
    const auto found = regions_.find(name);
    if (found == regions_.end()) {
        throw std::runtime_error("Atlas '" + source_ + "' has no sprite named '" + name + "' (it holds " +
                                 std::to_string(regions_.size()) + " sprites)");
    }
    return found->second;
}

std::vector<std::string> TextureAtlas::names() const {
    std::vector<std::string> result;
    result.reserve(regions_.size());
    for (const auto& entry : regions_) {
        result.push_back(entry.first);
    }
    return result;  // std::map keeps them sorted
}

}  // namespace moteur
