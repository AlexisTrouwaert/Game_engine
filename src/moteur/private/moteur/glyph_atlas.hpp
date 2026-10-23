#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "moteur/image.hpp"
#include "moteur/msdf.hpp"
#include "moteur/renderer.hpp"

namespace moteur {

class FontFace;

/// Which glyph, of which face, at which instance (FontFace::instance_weight()).
struct GlyphKey {
    std::uint32_t face = 0;   ///< index in the font library
    std::uint32_t glyph = 0;  ///< glyph id in that face
    std::int32_t weight = 0;
    bool operator==(const GlyphKey&) const = default;
};

struct GlyphKeyHash {
    std::size_t operator()(const GlyphKey& k) const {
        return (static_cast<std::size_t>(k.face) * 0x9E3779B97F4A7C15ull) ^ (static_cast<std::size_t>(k.glyph) << 16) ^
               static_cast<std::size_t>(k.weight);
    }
};

/// A glyph of the atlas.
struct AtlasGlyph {
    /// The page holding it (null: a glyph without ink, a space: nothing to draw).
    const Texture* page = nullptr;
    /// Its CPU image (an atlas without renderer: tests).
    const Image* cpu_page = nullptr;
    glm::vec4 uv{0.0f};     ///< u0, v0, u1, v1 on the page
    /// The quad to draw, in em units around the pen on the baseline, y up: left, bottom, right, top.
    glm::vec4 plane{0.0f};
    std::uint64_t used_frame = 0;
};

/// The glyphs of every font as multi-channel distance fields (milestone 9 bis, part 4), in pages
/// of 1024 x 1024 RGBA8 texels added as needed (eight at most, 32 MiB): a glyph is made the first
/// time a text needs it, at 48 texels per em with a distance range of 12 texels, whatever size it
/// is drawn at. Making a glyph costs a fraction of a millisecond (a Chinese one more): a frame makes
/// the glyphs it misses over the job system's threads, 48 at most (a count, not a time: the same
/// run always shows the same glyphs at the same frame), the others wait for the next frames (not
/// drawn meanwhile). When the pages are full, the page used longest ago is emptied, unless the
/// frame being recorded uses it.
///
/// Main thread (the renderer's), except prepare() which spreads the work over the job system.
class GlyphAtlas {
public:
    static constexpr int kPageSize = 1024;
    static constexpr int kMaxPages = 8;
    static constexpr float kEmTexels = 48.0f;    ///< texels per em in the atlas
    static constexpr float kRangeTexels = 12.0f;  ///< total width of the distance range, in texels
    static constexpr std::size_t kGlyphsPerFrame = 48;

    /// `renderer` null: the pages stay in CPU memory (tests, tools).
    explicit GlyphAtlas(Renderer* renderer);
    ~GlyphAtlas();
    GlyphAtlas(const GlyphAtlas&) = delete;
    GlyphAtlas& operator=(const GlyphAtlas&) = delete;

    /// The glyph if it is made (null otherwise, or without room), marked used by `frame`
    /// (Renderer::frame_index(), any increasing count without renderer). `make`: made now if
    /// missing, whatever the budget. The pointer is valid until the next call.
    const AtlasGlyph* find(FontFace& face, const GlyphKey& key, std::uint64_t frame, bool make = false);

    /// A shape of the interface: a distance field of `outline` (texel coordinates of a `size`
    /// image, y up), made once under `id` (any number the caller owns; the glyphs use face 0xFFFFFFFF
    /// with this id). Same range as the glyphs (the glyph shader draws it). Null if no room.
    const AtlasGlyph* shape(std::uint32_t id, const msdf::Outline& outline, glm::ivec2 size, std::uint64_t frame);

    /// Makes the missing ones of these glyphs over the job system's threads, at most `limit`
    /// (counting those made earlier in the same frame when `frame_budget`). Loading passes no limit.
    void prepare(const std::vector<std::pair<FontFace*, GlyphKey>>& glyphs, std::uint64_t frame,
                 std::size_t limit = static_cast<std::size_t>(-1), bool frame_budget = false);
    /// Whether the glyph is made (or known to have no ink).
    bool contains(const GlyphKey& key) const { return glyphs_.count(key) != 0; }

    std::size_t glyph_count() const { return glyphs_.size(); }
    std::size_t page_count() const { return pages_.size(); }
    std::size_t gpu_bytes() const;
    /// Glyphs made since the atlas exists, and pages emptied to make room.
    std::size_t made() const { return made_; }
    std::size_t evictions() const { return evictions_; }
    /// The CPU copy of a page (renderer null only).
    const Image* cpu_page(std::size_t index) const;

private:
    struct Shelf {
        int y = 0, height = 0, x = 0;
    };
    struct Page {
        Texture texture;
        std::unique_ptr<Image> cpu;
        std::vector<Shelf> shelves;
        int next_y = 0;
        std::uint64_t used_frame = 0;
    };
    /// A glyph made but not yet placed.
    struct Made {
        int width = 0, height = 0;
        std::vector<std::uint8_t> rgba;
        glm::vec4 plane{0.0f};
        bool ink = false;
    };

    static Made make(FontFace& face, const GlyphKey& key);
    /// Places a made glyph (or records a glyph without ink); false when there was no room.
    bool place(const GlyphKey& key, Made& made, std::uint64_t frame);
    bool allocate(int width, int height, std::uint64_t frame, std::size_t& page, int& x, int& y);
    void add_page();
    void evict(std::size_t page);

    Renderer* renderer_;
    std::vector<Page> pages_;
    std::unordered_map<GlyphKey, AtlasGlyph, GlyphKeyHash> glyphs_;
    std::unordered_map<GlyphKey, std::size_t, GlyphKeyHash> page_of_;
    std::uint64_t budget_frame_ = 0;
    std::size_t budget_spent_ = 0;
    std::size_t made_ = 0;
    std::size_t evictions_ = 0;
};

}  // namespace moteur
