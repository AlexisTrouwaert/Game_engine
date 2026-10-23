#pragma once

#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "moteur/font.hpp"
#include "moteur/font_face.hpp"
#include "moteur/glyph_atlas.hpp"

namespace moteur {

/// What the fonts of a program share (milestone 9 bis, part 4): the font files (each read once,
/// whichever stacks list it), the glyph atlas, and the laid-out texts of the last frames.
class FontLibrary {
public:
    explicit FontLibrary(Renderer* renderer) : renderer_(renderer), atlas_(renderer) {}

    /// The index of the face read from `path`, read now if it is not there yet. Thread-safe.
    /// Throws std::runtime_error like FontFace.
    std::uint32_t face_index(const std::string& path);
    /// A face by index. Thread-safe; the reference lives as long as the library.
    FontFace& face(std::uint32_t index);

    GlyphAtlas& atlas() { return atlas_; }
    Renderer* renderer() const { return renderer_; }
    /// Renderer::frame_index(), or a count of draws without renderer.
    std::uint64_t frame();

    /// The layout cache: a text and its style (the key), its layout. Thread-safe.
    std::shared_ptr<const TextLayout> cached(const std::string& key);
    void store(const std::string& key, std::shared_ptr<const TextLayout> layout);

    static constexpr std::size_t kCachedLayouts = 2048;

private:
    Renderer* renderer_;
    GlyphAtlas atlas_;
    std::mutex mutex_;
    std::vector<std::unique_ptr<FontFace>> faces_;
    std::unordered_map<std::string, std::uint32_t> face_by_path_;
    std::uint64_t own_frame_ = 0;

    std::mutex cache_mutex_;
    std::list<std::pair<std::string, std::shared_ptr<const TextLayout>>> recent_;  ///< most recent first
    std::unordered_map<std::string, decltype(recent_)::iterator> cache_;
};

}  // namespace moteur
