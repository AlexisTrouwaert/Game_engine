#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "moteur/renderer.hpp"
#include "moteur/sprite_region.hpp"

namespace moteur {

/// A set of sprites packed into a few textures (pages) by the `atlas_packer` tool (or by
/// build_atlas()): a JSON file that describes each sprite, and one PNG per page next to it.
/// Sprites are named after their path in the source folder, without extension
/// (`monsters/skeleton/walk_00`).
///
/// ```
/// auto atlas = TextureAtlas::load(renderer, asset_path("world.json"));
/// sprites.draw(atlas.region("tile_a"), anchor);
/// ```
///
/// Sprites of one page can be drawn with one draw call, which is the point of an atlas.
class TextureAtlas {
public:
    /// Reads the JSON file (`version` 1, `pages`, `frames`) and its pages, whose PNG files are
    /// looked up in the JSON file's folder, and uploads each page as a sprite texture
    /// (Renderer::create_texture(), premultiplied, no mipmaps). Blocks on file reads and GPU
    /// uploads: call it on the thread that owns `renderer`, at load time. Throws
    /// std::runtime_error, with the file name, if a file is missing, is not valid, or describes
    /// something impossible (a sprite outside its page or on a page that does not exist, a page
    /// whose real size differs from the declared one, an unknown format version).
    static TextureAtlas load(Renderer& renderer, const std::string& json_path);

    /// noexcept: the members only hand over their storage. MSVC's std::map alone allocates a new
    /// sentinel node when moved from, whose failure (out of memory) is fatal anyway. Moving keeps
    /// every SpriteRegion reference and page pointer valid (the pages are heap-allocated).
    ///@{
    TextureAtlas(TextureAtlas&&) noexcept = default;
    TextureAtlas& operator=(TextureAtlas&&) noexcept = default;
    ///@}
    /// Not copyable: the atlas owns its GPU pages.
    ///@{
    TextureAtlas(const TextureAtlas&) = delete;
    TextureAtlas& operator=(const TextureAtlas&) = delete;
    ///@}

    /// The region of a sprite. The reference stays valid as long as the atlas lives. Throws
    /// std::runtime_error naming the atlas and the sprite if there is no such sprite. O(log n).
    const SpriteRegion& region(const std::string& name) const;
    /// True if the atlas holds a sprite named `name` (region() would not throw). O(log n).
    bool contains(const std::string& name) const { return regions_.count(name) != 0; }

    /// All sprite names, sorted (byte order). Allocates a new vector at each call.
    std::vector<std::string> names() const;
    /// Number of textures (pages) the sprites are spread over: the minimum number of draw calls.
    std::size_t page_count() const { return pages_.size(); }
    /// GPU memory used by all the pages, in bytes.
    std::size_t gpu_bytes() const;

private:
    TextureAtlas() = default;

    std::string source_;  ///< the JSON path, for error messages
    std::vector<std::unique_ptr<Texture>> pages_;
    std::map<std::string, SpriteRegion> regions_;
};

}  // namespace moteur
