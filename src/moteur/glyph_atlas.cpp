#include "moteur/glyph_atlas.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "moteur/font_face.hpp"
#include "moteur/jobs.hpp"

namespace moteur {

namespace {

constexpr int kGap = 1;  // texels between glyphs, so that filtering never reads a neighbour

}  // namespace

GlyphAtlas::GlyphAtlas(Renderer* renderer) : renderer_(renderer) {
    pages_.reserve(kMaxPages);  // never moves: the entries point at the pages' textures
}

GlyphAtlas::~GlyphAtlas() = default;

std::size_t GlyphAtlas::gpu_bytes() const {
    std::size_t bytes = 0;
    for (const Page& page : pages_) {
        bytes += page.texture.gpu_bytes;
    }
    return bytes;
}

const Image* GlyphAtlas::cpu_page(std::size_t index) const {
    return index < pages_.size() ? pages_[index].cpu.get() : nullptr;
}

GlyphAtlas::Made GlyphAtlas::make(FontFace& face, const GlyphKey& key) {
    Made made;
    msdf::Outline outline;
    if (!face.outline(key.glyph, key.weight, outline)) {
        return made;  // no ink
    }
    glm::dvec2 lo, hi;
    outline.bounds(lo, hi);
    const double scale = static_cast<double>(kEmTexels) / face.units_per_em();  // texels per font unit
    const double pad = kRangeTexels * 0.5 + 1.0;                                // texels around the ink
    made.width = static_cast<int>(std::ceil((hi.x - lo.x) * scale + 2 * pad));
    made.height = static_cast<int>(std::ceil((hi.y - lo.y) * scale + 2 * pad));
    msdf::Placement transform;
    transform.scale = scale;
    transform.translate = glm::dvec2(pad / scale) - lo;
    transform.range = kRangeTexels;
    made.rgba.resize(static_cast<std::size_t>(made.width) * static_cast<std::size_t>(made.height) * 4);
    msdf::generate(outline, transform, made.width, made.height, made.rgba.data());
    // Texel (0, 0)'s corner is the outline point -translate; in em units around the pen:
    const double upem = face.units_per_em();
    const glm::dvec2 origin = -transform.translate;
    made.plane = glm::vec4(static_cast<float>(origin.x / upem), static_cast<float>(origin.y / upem),
                           static_cast<float>((origin.x + made.width / scale) / upem),
                           static_cast<float>((origin.y + made.height / scale) / upem));
    made.ink = true;
    return made;
}

void GlyphAtlas::add_page() {
    Page page;
    Image image;
    image.width = kPageSize;
    image.height = kPageSize;
    image.pixels.assign(static_cast<std::size_t>(kPageSize) * kPageSize * 4, 0);
    if (renderer_ != nullptr) {
        TextureSettings settings;
        settings.premultiply = false;  // distances, not colors
        const std::string name = "glyph atlas #" + std::to_string(pages_.size());
        page.texture = renderer_->create_texture(image, settings, name.c_str());
    } else {
        page.cpu = std::make_unique<Image>(std::move(image));
        page.texture.width = kPageSize;
        page.texture.height = kPageSize;
    }
    pages_.push_back(std::move(page));
}

void GlyphAtlas::evict(std::size_t index) {
    Page& page = pages_[index];
    page.shelves.clear();
    page.next_y = 0;
    for (auto it = page_of_.begin(); it != page_of_.end();) {
        if (it->second == index) {
            glyphs_.erase(it->first);
            it = page_of_.erase(it);
        } else {
            ++it;
        }
    }
    ++evictions_;
    SDL_Log("Glyph atlas: page %zu emptied to make room (%zu glyphs kept)", index, glyphs_.size());
}

bool GlyphAtlas::allocate(int width, int height, std::uint64_t frame, std::size_t& page_index, int& x, int& y) {
    const int w = width + kGap;
    const int h = height + kGap;
    if (w > kPageSize || h > kPageSize) {
        return false;
    }
    const auto try_page = [&](std::size_t index) {
        Page& page = pages_[index];
        // The shelf of the closest height that still has room, else a new shelf.
        Shelf* best = nullptr;
        for (Shelf& shelf : page.shelves) {
            if (shelf.height >= h && shelf.height <= h + h / 2 + 2 && shelf.x + w <= kPageSize &&
                (best == nullptr || shelf.height < best->height)) {
                best = &shelf;
            }
        }
        if (best == nullptr) {
            if (page.next_y + h > kPageSize) {
                return false;
            }
            page.shelves.push_back({page.next_y, h, 0});
            page.next_y += h;
            best = &page.shelves.back();
        }
        page_index = index;
        x = best->x;
        y = best->y;
        best->x += w;
        page.used_frame = std::max(page.used_frame, frame);
        return true;
    };
    for (std::size_t i = 0; i < pages_.size(); ++i) {
        if (try_page(i)) {
            return true;
        }
    }
    if (pages_.size() < static_cast<std::size_t>(kMaxPages)) {
        add_page();
        return try_page(pages_.size() - 1);
    }
    // Full: empty the page used longest ago, if the frame being recorded does not use it.
    std::size_t oldest = 0;
    for (std::size_t i = 1; i < pages_.size(); ++i) {
        if (pages_[i].used_frame < pages_[oldest].used_frame) {
            oldest = i;
        }
    }
    if (pages_[oldest].used_frame >= frame) {
        return false;
    }
    evict(oldest);
    return try_page(oldest);
}

bool GlyphAtlas::place(const GlyphKey& key, Made& made, std::uint64_t frame) {
    AtlasGlyph glyph;
    glyph.used_frame = frame;
    if (!made.ink) {
        glyphs_[key] = glyph;  // remembered, so that it is not asked again
        return true;
    }
    std::size_t page = 0;
    int x = 0, y = 0;
    if (!allocate(made.width, made.height, frame, page, x, y)) {
        return false;
    }
    Page& p = pages_[page];
    if (renderer_ != nullptr) {
        renderer_->update_texture(p.texture, x, y, made.width, made.height, made.rgba.data());
    } else {
        for (int row = 0; row < made.height; ++row) {
            std::memcpy(&p.cpu->pixels[(static_cast<std::size_t>(y + row) * kPageSize + static_cast<std::size_t>(x)) * 4],
                        &made.rgba[static_cast<std::size_t>(row) * static_cast<std::size_t>(made.width) * 4],
                        static_cast<std::size_t>(made.width) * 4);
        }
    }
    constexpr float kSize = static_cast<float>(kPageSize);
    glyph.page = &p.texture;
    glyph.cpu_page = p.cpu.get();
    glyph.uv = {static_cast<float>(x) / kSize, static_cast<float>(y) / kSize,
                static_cast<float>(x + made.width) / kSize, static_cast<float>(y + made.height) / kSize};
    glyph.plane = made.plane;
    glyphs_[key] = glyph;
    page_of_[key] = page;
    return true;
}

const AtlasGlyph* GlyphAtlas::find(FontFace& face, const GlyphKey& key, std::uint64_t frame, bool make_it) {
    auto it = glyphs_.find(key);
    if (it == glyphs_.end()) {
        if (!make_it) {
            return nullptr;
        }
        Made made = make(face, key);
        ++made_;
        if (!place(key, made, frame)) {
            return nullptr;
        }
        it = glyphs_.find(key);
    }
    it->second.used_frame = frame;
    const auto page = page_of_.find(key);
    if (page != page_of_.end()) {
        pages_[page->second].used_frame = std::max(pages_[page->second].used_frame, frame);
    }
    return &it->second;
}

void GlyphAtlas::prepare(const std::vector<std::pair<FontFace*, GlyphKey>>& glyphs, std::uint64_t frame,
                         std::size_t limit, bool frame_budget) {
    if (frame_budget) {
        if (frame != budget_frame_) {
            budget_frame_ = frame;
            budget_spent_ = 0;
        }
        limit = std::min(limit, kGlyphsPerFrame > budget_spent_ ? kGlyphsPerFrame - budget_spent_ : 0);
    }
    std::vector<std::pair<FontFace*, GlyphKey>> missing;
    for (const auto& entry : glyphs) {
        if (missing.size() >= limit) {
            break;
        }
        if (glyphs_.count(entry.second) == 0 &&
            std::none_of(missing.begin(), missing.end(), [&](const auto& m) { return m.second == entry.second; })) {
            missing.push_back(entry);
        }
    }
    if (missing.empty()) {
        return;
    }
    std::vector<Made> made(missing.size());
    jobs().parallel_for(missing.size(), [&](std::size_t i) { made[i] = make(*missing[i].first, missing[i].second); });
    for (std::size_t i = 0; i < missing.size(); ++i) {
        ++made_;
        place(missing[i].second, made[i], frame);
    }
    if (frame_budget) {
        budget_spent_ += missing.size();
    }
}

const AtlasGlyph* GlyphAtlas::shape(std::uint32_t id, const msdf::Outline& outline, glm::ivec2 size, std::uint64_t frame) {
    const GlyphKey key{0xFFFFFFFFu, id, 0};
    const auto it = glyphs_.find(key);
    if (it != glyphs_.end()) {
        it->second.used_frame = frame;
        return &it->second;
    }
    Made made;
    made.width = size.x;
    made.height = size.y;
    made.rgba.resize(static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.y) * 4);
    msdf::Outline copy = outline;
    msdf::Placement transform;
    transform.range = kRangeTexels;
    msdf::generate(copy, transform, size.x, size.y, made.rgba.data());
    made.ink = true;
    if (!place(key, made, frame)) {
        return nullptr;
    }
    return &glyphs_.find(key)->second;
}

}  // namespace moteur

