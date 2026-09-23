#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "moteur/renderer.hpp"
#include "moteur/sprite_region.hpp"

namespace moteur {

// A set of sprites packed into a few textures (pages) by the atlas_packer tool: a JSON file
// that describes each sprite, and one PNG per page next to it.
//
//   auto atlas = TextureAtlas::load(renderer, asset_path("world.json"));
//   sprites.draw(atlas.region("tile_a"), anchor);
//
// Sprites of one page can be drawn with one draw call, which is the point of an atlas.
class TextureAtlas {
public:
    // Reads the JSON file and its pages. Throws std::runtime_error, with the file name, if a file
    // is missing, is not valid, or describes something impossible (a sprite outside its page, a
    // page whose real size differs from the declared one, an unknown format version).
    static TextureAtlas load(Renderer& renderer, const std::string& json_path);

    TextureAtlas(TextureAtlas&&) = default;
    TextureAtlas& operator=(TextureAtlas&&) = default;
    TextureAtlas(const TextureAtlas&) = delete;
    TextureAtlas& operator=(const TextureAtlas&) = delete;

    // The region of a sprite. The reference stays valid as long as the atlas lives. Throws
    // std::runtime_error naming the atlas and the sprite if there is no such sprite.
    const SpriteRegion& region(const std::string& name) const;
    bool contains(const std::string& name) const { return regions_.count(name) != 0; }

    // All sprite names, sorted.
    std::vector<std::string> names() const;
    std::size_t page_count() const { return pages_.size(); }

private:
    TextureAtlas() = default;

    std::string source_;  // the JSON path, for error messages
    std::vector<std::unique_ptr<Texture>> pages_;
    std::map<std::string, SpriteRegion> regions_;
};

}  // namespace moteur
