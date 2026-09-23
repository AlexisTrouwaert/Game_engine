#include "moteur/font.hpp"

#include <SDL3/SDL.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>

#include "moteur/atlas_builder.hpp"
#include "moteur/image.hpp"
#include "moteur/sprite_renderer.hpp"
#include "moteur/utf8.hpp"

namespace moteur {

namespace {
constexpr int kMaxPageSize = 2048;
constexpr int kAtlasPadding = 1;
}  // namespace

std::vector<char32_t> Font::default_charset() {
    std::vector<char32_t> charset;
    for (char32_t c = 0x20; c <= 0x7E; ++c) {
        charset.push_back(c);
    }
    for (char32_t c = 0xA0; c <= 0xFF; ++c) {  // Latin-1 Supplement: accents, « »
        charset.push_back(c);
    }
    for (const std::uint32_t c : {0x152u, 0x153u, 0x2013u, 0x2014u, 0x2018u, 0x2019u, 0x201Cu, 0x201Du}) {
        charset.push_back(static_cast<char32_t>(c));  // Œ œ, dashes, curly quotes
    }
    return charset;
}

Font::~Font() = default;
Font::Font(Font&&) noexcept = default;
Font& Font::operator=(Font&&) noexcept = default;

Font Font::load(Renderer& renderer, const std::string& path, float pixel_height, std::vector<char32_t> charset) {
    Font font;

    std::size_t file_size = 0;
    void* file = SDL_LoadFile(path.c_str(), &file_size);
    if (file == nullptr) {
        throw std::runtime_error("Cannot read font '" + path + "': " + SDL_GetError());
    }
    font.ttf_buffer_.assign(static_cast<unsigned char*>(file), static_cast<unsigned char*>(file) + file_size);
    SDL_free(file);

    font.info_ = std::make_unique<stbtt_fontinfo>();
    const int offset = stbtt_GetFontOffsetForIndex(font.ttf_buffer_.data(), 0);
    if (offset < 0 || stbtt_InitFont(font.info_.get(), font.ttf_buffer_.data(), offset) == 0) {
        throw std::runtime_error("Font '" + path + "' could not be parsed (not a TrueType/OpenType file?)");
    }

    font.scale_ = stbtt_ScaleForPixelHeight(font.info_.get(), pixel_height);
    int ascent = 0, descent = 0, line_gap = 0;
    stbtt_GetFontVMetrics(font.info_.get(), &ascent, &descent, &line_gap);
    font.ascent_ = static_cast<float>(ascent) * font.scale_;
    font.line_height_ = static_cast<float>(ascent - descent + line_gap) * font.scale_;

    std::sort(charset.begin(), charset.end());
    charset.erase(std::unique(charset.begin(), charset.end()), charset.end());

    // Metrics needed for every codepoint, whether or not it ends up with a bitmap in the atlas.
    struct PendingGlyph {
        float advance;
        float bearing_x, bearing_y;  // offset from the pen to the bitmap's top-left, before packing
    };
    std::map<char32_t, PendingGlyph> pending;
    std::vector<AtlasInput> inputs;

    for (const char32_t cp : charset) {
        int advance_units = 0, left_bearing_units = 0;
        stbtt_GetCodepointHMetrics(font.info_.get(), static_cast<int>(cp), &advance_units, &left_bearing_units);
        const float advance = static_cast<float>(advance_units) * font.scale_;

        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetCodepointBitmapBox(font.info_.get(), static_cast<int>(cp), font.scale_, font.scale_, &x0, &y0, &x1, &y1);
        const int w = x1 - x0, h = y1 - y0;
        pending.emplace(cp, PendingGlyph{advance, static_cast<float>(x0), static_cast<float>(y0)});

        if (w <= 0 || h <= 0) {
            continue;  // no ink (space and the like): the metrics above are enough for layout
        }

        std::vector<unsigned char> gray(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
        stbtt_MakeCodepointBitmap(font.info_.get(), gray.data(), w, h, w, font.scale_, font.scale_, static_cast<int>(cp));

        // stb_truetype rasterizes in 8-bit grayscale (coverage). Stored as white with that
        // coverage as alpha, a glyph draws through the same premultiplied-alpha sprite pipeline
        // as any other texture: white * coverage (premultiplied) tinted by the text color.
        Image image;
        image.width = w;
        image.height = h;
        image.pixels.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
        for (std::size_t i = 0; i < gray.size(); ++i) {
            image.pixels[i * 4 + 0] = 255;
            image.pixels[i * 4 + 1] = 255;
            image.pixels[i * 4 + 2] = 255;
            image.pixels[i * 4 + 3] = gray[i];
        }

        AtlasInput input;
        input.name = std::to_string(static_cast<std::uint32_t>(cp));  // parsed back below
        input.image = std::move(image);
        inputs.push_back(std::move(input));
    }

    if (!inputs.empty()) {
        AtlasOptions options;
        options.max_page_size = kMaxPageSize;
        options.padding = kAtlasPadding;
        options.trim = true;  // stb's bitmap box can leave a near-empty antialiased edge row
        const AtlasResult atlas = build_atlas(std::move(inputs), options);

        font.pages_.reserve(atlas.pages.size());
        for (const AtlasPage& page : atlas.pages) {
            Image page_image;
            page_image.width = page.width;
            page_image.height = page.height;
            page_image.pixels = page.pixels;
            const std::string page_name = "font glyphs #" + std::to_string(font.pages_.size());
            font.pages_.push_back(renderer.create_texture(page_image, page_name.c_str()));
        }

        for (const AtlasFrame& frame : atlas.frames) {
            const auto cp = static_cast<char32_t>(std::stoul(frame.name));
            const PendingGlyph& base = pending.at(cp);
            const AtlasPage& page = atlas.pages[static_cast<std::size_t>(frame.page)];
            const glm::vec2 page_size(static_cast<float>(page.width), static_cast<float>(page.height));

            GlyphMetrics metrics;
            metrics.page = &font.pages_[static_cast<std::size_t>(frame.page)];
            metrics.uv_rect = {static_cast<float>(frame.x) / page_size.x, static_cast<float>(frame.y) / page_size.y,
                               static_cast<float>(frame.x + frame.w) / page_size.x,
                               static_cast<float>(frame.y + frame.h) / page_size.y};
            metrics.size = {static_cast<float>(frame.w), static_cast<float>(frame.h)};
            // The trim inside build_atlas moved the visible pixels; offset_x/y say by how much,
            // so adding it back to the original bearing keeps the glyph exactly where it was.
            metrics.bearing = {base.bearing_x + static_cast<float>(frame.offset_x),
                               base.bearing_y + static_cast<float>(frame.offset_y)};
            metrics.advance = base.advance;
            font.glyphs_.emplace(cp, metrics);
        }
    }

    for (const auto& [cp, base] : pending) {
        if (font.glyphs_.count(cp) != 0) {
            continue;
        }
        GlyphMetrics metrics;  // no bitmap: size stays {0, 0}, draw() skips it
        metrics.advance = base.advance;
        font.glyphs_.emplace(cp, metrics);
    }

    return font;
}

Font::Resolved Font::resolve(const std::u32string& text, const TextOptions& options) const {
    const auto advance_of = [this](char32_t cp) {
        const auto it = glyphs_.find(cp);
        return it != glyphs_.end() ? it->second.advance : 0.0f;
    };
    const auto kerning_of = [this](char32_t a, char32_t b) {
        return static_cast<float>(stbtt_GetCodepointKernAdvance(info_.get(), static_cast<int>(a), static_cast<int>(b))) *
               scale_;
    };

    const TextLayoutResult layout =
        layout_text(text, line_height_, options.max_width, options.align, advance_of, kerning_of);

    // Matches the alignment box that layout_text computes internally (see its header comment):
    // at least max_width, and at least as wide as the widest line.
    float box_width = std::max(options.max_width, 0.0f);
    for (const TextLine& line : layout.lines) {
        box_width = std::max(box_width, line.width);
    }

    Resolved resolved;
    resolved.size = {box_width, static_cast<float>(layout.lines.size()) * line_height_};
    for (const TextLine& line : layout.lines) {
        for (const GlyphPlacement& placement : line.glyphs) {
            const auto it = glyphs_.find(placement.codepoint);
            if (it == glyphs_.end() || it->second.size.x <= 0.0f || it->second.size.y <= 0.0f) {
                continue;  // whitespace, or a codepoint this font does not have
            }
            const glm::vec2 position(placement.x + it->second.bearing.x, placement.y + ascent_ + it->second.bearing.y);
            resolved.glyphs.push_back({&it->second, position});
        }
    }
    return resolved;
}

glm::vec2 Font::measure(std::string_view utf8_text, const TextOptions& options) const {
    return resolve(decode_utf8(utf8_text), options).size;
}

void Font::draw(SpriteRenderer& sprites, std::string_view utf8_text, glm::vec2 anchor, const TextOptions& options) const {
    for (const PlacedGlyph& placed : resolve(decode_utf8(utf8_text), options).glyphs) {
        SpriteOptions sprite_options;
        sprite_options.uv_rect = placed.glyph->uv_rect;
        sprite_options.tint = options.color;
        sprite_options.depth = options.depth;
        sprites.draw(*placed.glyph->page, anchor + placed.position, placed.glyph->size, sprite_options);
    }
}

}  // namespace moteur
