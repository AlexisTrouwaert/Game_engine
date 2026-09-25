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

struct TextOptions {
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    TextAlign align = TextAlign::Left;
    float max_width = 0.0f;  // <= 0 disables word wrap
    float depth = 0.0f;

    // Deliberately no per-character color or embedded markup yet (an ARPG's item tooltips will
    // want both). The option is left free for that: a future revision can add a `spans` list here
    // without touching draw()'s signature, since callers already pass one TextOptions value.
};

// A TrueType font, rasterized once into one or more GPU atlas pages at a fixed pixel size.
// To stay sharp on a high-density screen or with a UI scale factor, load it at the final physical
// pixel size rather than scaling a smaller rendering: stb_truetype (like most font rasterizers)
// produces a blurry result when its bitmap is stretched.
class Font {
public:
    // Covers ASCII, Latin-1 (French accents and the « » guillemets), plus a few characters
    // Latin-1 leaves out: the œ/Œ ligature, en/em dashes, and curly quotes.
    static std::vector<char32_t> default_charset();

    // Reads `path` (a TrueType or OpenType file) and rasterizes `charset` at `pixel_height`
    // pixels. Throws std::runtime_error, naming the file, if it cannot be read or parsed.
    // A codepoint the font does not have is silently skipped (draw() then skips it too).
    static Font load(Renderer& renderer, const std::string& path, float pixel_height,
                     std::vector<char32_t> charset = default_charset());

    ~Font();
    Font(Font&&) noexcept;
    Font& operator=(Font&&) noexcept;
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    float line_height() const { return line_height_; }
    std::size_t gpu_bytes() const;  // of the glyph pages
    float ascent() const { return ascent_; }

    // The size of the box the text would occupy, in pixels. Uses the exact same layout as draw(),
    // so measuring and drawing never disagree.
    glm::vec2 measure(std::string_view utf8_text, const TextOptions& options = {}) const;

    // Records the text's glyphs into `sprites`, `anchor` being the top-left corner of the box
    // measure() describes for the same text and options.
    void draw(SpriteRenderer& sprites, std::string_view utf8_text, glm::vec2 anchor,
             const TextOptions& options = {}) const;

private:
    struct GlyphMetrics {
        const Texture* page = nullptr;
        glm::vec4 uv_rect{0.0f};
        glm::vec2 size{0.0f};     // 0 for glyphs with no ink (space): draw() then skips them
        glm::vec2 bearing{0.0f};  // offset from the pen position to the bitmap's top-left corner
        float advance = 0.0f;
    };

    Font() = default;

    struct PlacedGlyph {
        const GlyphMetrics* glyph;
        glm::vec2 position;  // top-left corner, ready to draw
    };

    // Shared by measure() and draw(): the box size, and where each visible glyph belongs in it.
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
