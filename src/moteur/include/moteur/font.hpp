#pragma once

#include <glm/glm.hpp>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "moteur/renderer.hpp"
#include "moteur/text_layout.hpp"

struct stbtt_fontinfo;  // kept out of this header; only font.cpp needs stb_truetype.h

namespace moteur {

class SpriteRenderer;

/// How Font::measure() and Font::draw() lay out and color a text.
struct TextOptions {
    /// The text's color, components in [0, 1], straight (non-premultiplied) alpha: it is the
    /// SpriteOptions::tint of every glyph, so alpha fades the text.
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    TextAlign align = TextAlign::Left;  ///< alignment of each line inside the text's box
    /// Word-wrap width, in pixels; <= 0 disables word wrap (lines end only at `'\n'`). When > 0,
    /// the text's box is at least this wide, even for a short text.
    float max_width = 0.0f;
    float depth = 0.0f;  ///< SpriteOptions::depth of every glyph: larger is drawn later, so on top

    // Deliberately no per-character color or embedded markup yet (an ARPG's item tooltips will
    // want both). The option is left free for that: a future revision can add a `spans` list here
    // without touching draw()'s signature, since callers already pass one TextOptions value.
};

/// A TrueType font, rasterized once into one or more GPU atlas pages at a fixed pixel size.
/// To stay sharp on a high-density screen or with a UI scale factor, load it at the final physical
/// pixel size rather than scaling a smaller rendering: stb_truetype (like most font rasterizers)
/// produces a blurry result when its bitmap is stretched.
///
/// Glyphs are drawn as sprites (one per visible character) through a SpriteRenderer, usually
/// Renderer::screen_sprites() for the interface. Text is laid out by layout_text(), with the
/// font's advances and kerning.
class Font {
public:
    /// Covers ASCII, Latin-1 (French accents and the « » guillemets), plus a few characters
    /// Latin-1 leaves out: the œ/Œ ligature, en/em dashes, and curly quotes.
    static std::vector<char32_t> default_charset();

    /// Reads `path` (a TrueType or OpenType file; the first font of a collection) and rasterizes
    /// `charset` at `pixel_height` pixels (the distance from the highest ascender to the lowest
    /// descender), packing the glyphs with build_atlas() into pages of at most 2048 x 2048 that
    /// are uploaded to the GPU. Blocks on the file read and the uploads: call it on the thread
    /// that owns `renderer`, at load time. Throws std::runtime_error, naming the file, if it
    /// cannot be read or parsed. Duplicates in `charset` are ignored.
    ///
    /// Codepoints outside `charset` are skipped when drawing and take no room. A codepoint of
    /// `charset` the font does not have gets the font's "missing glyph" (its `.notdef`, often an
    /// empty box), as stb_truetype reports it.
    static Font load(Renderer& renderer, const std::string& path, float pixel_height,
                     std::vector<char32_t> charset = default_charset());

    /// Releases the glyph pages (GPU textures) and the font data.
    ~Font();
    /// Moving keeps the font's glyph pages where they are, so text already recorded this frame
    /// with the moved-from font still draws.
    ///@{
    Font(Font&&) noexcept;
    Font& operator=(Font&&) noexcept;
    ///@}
    /// Not copyable: the font owns its GPU pages.
    ///@{
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;
    ///@}

    /// Distance between the tops of two consecutive lines, in pixels (ascent - descent + line
    /// gap, as the font declares them).
    float line_height() const { return line_height_; }
    std::size_t gpu_bytes() const;  ///< GPU memory used by the glyph pages, in bytes
    /// Distance from the top of a line to its baseline, in pixels.
    float ascent() const { return ascent_; }

    /// The size of the box the text would occupy, in pixels: as wide as the widest line (and at
    /// least TextOptions::max_width when wrapping), `line_height()` times the number of lines tall.
    /// `utf8_text` is decoded with decode_utf8() (invalid bytes become U+FFFD). Uses the exact same layout as draw(), so measuring and
    /// drawing never disagree. Allocates; O(length of the text).
    glm::vec2 measure(std::string_view utf8_text, const TextOptions& options = {}) const;

    /// Records the text's glyphs into `sprites`, `anchor` being the top-left corner of the box
    /// measure() describes for the same text and options, in the sprite renderer's units (window
    /// pixels unless it has a view-projection). Spaces and characters without ink record nothing.
    /// The font must stay alive until the end of the frame (its pages are the sprites' textures).
    void draw(SpriteRenderer& sprites, std::string_view utf8_text, glm::vec2 anchor,
             const TextOptions& options = {}) const;

private:
    struct GlyphMetrics {
        const Texture* page = nullptr;
        glm::vec4 uv_rect{0.0f};
        glm::vec2 size{0.0f};     ///< 0 for glyphs with no ink (space): draw() then skips them
        glm::vec2 bearing{0.0f};  ///< offset from the pen position to the bitmap's top-left corner
        float advance = 0.0f;
    };

    Font() = default;

    struct PlacedGlyph {
        const GlyphMetrics* glyph;
        glm::vec2 position;  ///< top-left corner, ready to draw
    };

    /// Shared by measure() and draw(): the box size, and where each visible glyph belongs in it.
    struct Resolved {
        std::vector<PlacedGlyph> glyphs;
        glm::vec2 size;
    };
    Resolved resolve(const std::u32string& text, const TextOptions& options) const;

    std::vector<unsigned char> ttf_buffer_;
    std::unique_ptr<stbtt_fontinfo> info_;
    float scale_ = 1.0f;
    float ascent_ = 0.0f;
    float line_height_ = 0.0f;
    std::vector<Texture> pages_;
    std::map<char32_t, GlyphMetrics> glyphs_;
};

}  // namespace moteur
