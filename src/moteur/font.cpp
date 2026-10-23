#include "moteur/font.hpp"

#include <SDL3/SDL.h>
#include <hb.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <stdexcept>

#include "moteur/document.hpp"
#include "moteur/font_library.hpp"
#include "moteur/paths.hpp"
#include "moteur/sprite_renderer.hpp"
#include "moteur/unicode.hpp"
#include "moteur/utf8.hpp"

namespace moteur {

namespace {

constexpr char32_t kObjectReplacement = 0xFFFC;  // an inline icon's place in the text
constexpr float kSlant = 0.2f;                   // synthetic italic: x shift per unit of height
constexpr float kBoldDilationEm = 0.025f;        // synthetic bold at 700, in em

bool is_default_ignorable(char32_t c, const unicode::CharInfo& info) {
    return info.category == unicode::GeneralCategory::Cf || (c >= 0xFE00 && c <= 0xFE0F) ||
           (c >= 0xE0100 && c <= 0xE01EF) || c == 0x034F || info.category == unicode::GeneralCategory::Cc;
}

bool is_space(const unicode::CharInfo& info) {
    return info.category == unicode::GeneralCategory::Zs || info.bidi == unicode::BidiClass::WS ||
           info.bidi == unicode::BidiClass::S || info.category == unicode::GeneralCategory::Zl ||
           info.category == unicode::GeneralCategory::Cc;
}

hb_script_t hb_script_of(std::uint8_t script) {
    const char* tag = unicode::script_tag(script);
    return hb_script_from_string(tag, 4);
}

}  // namespace

// --- Markup ---------------------------------------------------------------------------------------

TextMarkup parse_text_markup(std::string_view utf8) {
    const std::u32string source = decode_utf8(utf8);
    TextMarkup out;
    out.spans.emplace_back();
    std::vector<TextSpan> stack{TextSpan{}};
    std::vector<std::string> open_tags;

    const auto span_index = [&](const TextSpan& span) {
        for (std::size_t i = 0; i < out.spans.size(); ++i) {
            if (out.spans[i] == span) {
                return static_cast<std::uint16_t>(i);
            }
        }
        out.spans.push_back(span);
        return static_cast<std::uint16_t>(out.spans.size() - 1);
    };
    const auto emit = [&](char32_t c, std::uint16_t span) {
        out.text.push_back(c);
        out.span_of.push_back(span);
    };
    const auto to_ascii = [](std::u32string_view s) {
        std::string a;
        for (const char32_t c : s) {
            a.push_back(c < 128 ? static_cast<char>(c) : '?');
        }
        return a;
    };
    const auto parse_color = [](const std::string& v, glm::vec4& color) {
        if ((v.size() != 7 && v.size() != 9) || v[0] != '#') {
            return false;
        }
        unsigned int value = 0;
        for (std::size_t i = 1; i < v.size(); ++i) {
            const char c = v[i];
            const int digit = (c >= '0' && c <= '9') ? c - '0'
                              : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                              : (c >= 'A' && c <= 'F') ? c - 'A' + 10
                                                       : -1;
            if (digit < 0) {
                return false;
            }
            value = value * 16 + static_cast<unsigned int>(digit);
        }
        if (v.size() == 7) {
            value = (value << 8) | 0xFFu;
        }
        color = {static_cast<float>((value >> 24) & 0xFF) / 255.0f, static_cast<float>((value >> 16) & 0xFF) / 255.0f,
                 static_cast<float>((value >> 8) & 0xFF) / 255.0f, static_cast<float>(value & 0xFF) / 255.0f};
        return true;
    };
    const auto parse_number = [](const std::string& v, float& number) {
        if (v.empty()) {
            return false;
        }
        char* end = nullptr;
        number = std::strtof(v.c_str(), &end);
        return end == v.c_str() + v.size() && std::isfinite(number);
    };

    for (std::size_t i = 0; i < source.size(); ++i) {
        const char32_t c = source[i];
        if (c == U'&') {
            static constexpr std::pair<std::u32string_view, char32_t> kEntities[] = {
                {U"&lt;", U'<'}, {U"&gt;", U'>'}, {U"&amp;", U'&'}};
            bool matched = false;
            for (const auto& [entity, value] : kEntities) {
                if (std::u32string_view(source).substr(i, entity.size()) == entity) {
                    emit(value, span_index(stack.back()));
                    i += entity.size() - 1;
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                emit(c, span_index(stack.back()));
            }
            continue;
        }
        if (c != U'<') {
            emit(c, span_index(stack.back()));
            continue;
        }
        const std::size_t close = source.find(U'>', i);
        if (close == std::u32string::npos) {
            emit(c, span_index(stack.back()));
            continue;
        }
        const std::string tag = to_ascii(std::u32string_view(source).substr(i + 1, close - i - 1));
        const bool closing = !tag.empty() && tag[0] == '/';
        const std::string body = closing ? tag.substr(1) : tag;
        const std::size_t equal = body.find('=');
        const std::string name = body.substr(0, equal);
        const std::string value = equal == std::string::npos ? std::string() : body.substr(equal + 1);

        bool known = true;
        if (closing) {
            const auto it = std::find(open_tags.rbegin(), open_tags.rend(), name);
            if (it != open_tags.rend()) {
                const auto depth = static_cast<std::size_t>(std::distance(it, open_tags.rend()));  // 1-based
                open_tags.resize(depth - 1);
                stack.resize(depth);
            }
            i = close;  // a closing tag without its opening one is dropped
            continue;
        }
        TextSpan span = stack.back();
        span.icon.clear();
        if (name == "b" && value.empty()) {
            span.weight = 700.0f;
        } else if (name == "i" && value.empty()) {
            span.italic = true;
        } else if (name == "color") {
            glm::vec4 color;
            known = parse_color(value, color);
            span.color = color;
        } else if (name == "size") {
            float scale = 1.0f;
            known = parse_number(value, scale) && scale > 0.0f && scale <= 16.0f;
            span.scale *= scale;
        } else if (name == "weight") {
            float weight = 400.0f;
            known = parse_number(value, weight) && weight >= 1.0f && weight <= 1000.0f;
            span.weight = weight;
        } else if (name == "icon" && !value.empty()) {
            TextSpan icon = stack.back();
            icon.icon = value.back() == '/' ? value.substr(0, value.size() - 1) : value;
            emit(kObjectReplacement, span_index(icon));
            i = close;
            continue;
        } else {
            known = false;
        }
        if (!known) {
            emit(c, span_index(stack.back()));  // shown as text: a typo is visible
            continue;
        }
        stack.push_back(span);
        open_tags.push_back(name);
        i = close;
    }
    return out;
}

// --- Library --------------------------------------------------------------------------------------

std::uint32_t FontLibrary::face_index(const std::string& path) {
    const std::lock_guard lock(mutex_);
    const auto it = face_by_path_.find(path);
    if (it != face_by_path_.end()) {
        return it->second;
    }
    faces_.push_back(std::make_unique<FontFace>(path));
    const auto index = static_cast<std::uint32_t>(faces_.size() - 1);
    face_by_path_.emplace(path, index);
    return index;
}

FontFace& FontLibrary::face(std::uint32_t index) {
    const std::lock_guard lock(mutex_);
    return *faces_.at(index);
}

std::uint64_t FontLibrary::frame() {
    if (renderer_ != nullptr) {
        return renderer_->frame_index();
    }
    return ++own_frame_;
}

std::shared_ptr<const TextLayout> FontLibrary::cached(const std::string& key) {
    const std::lock_guard lock(cache_mutex_);
    const auto it = cache_.find(key);
    if (it == cache_.end()) {
        return nullptr;
    }
    recent_.splice(recent_.begin(), recent_, it->second);
    return it->second->second;
}

void FontLibrary::store(const std::string& key, std::shared_ptr<const TextLayout> layout) {
    const std::lock_guard lock(cache_mutex_);
    if (cache_.count(key) != 0) {
        return;
    }
    recent_.emplace_front(key, std::move(layout));
    cache_.emplace(key, recent_.begin());
    while (recent_.size() > kCachedLayouts) {
        cache_.erase(recent_.back().first);
        recent_.pop_back();
    }
}

std::shared_ptr<FontLibrary> Font::make_library(Renderer* renderer) {
    return std::make_shared<FontLibrary>(renderer);
}

// --- Loading --------------------------------------------------------------------------------------

Font::~Font() = default;
Font::Font(Font&&) noexcept = default;
Font& Font::operator=(Font&&) noexcept = default;

Font Font::load(Renderer* renderer, const std::string& path, std::shared_ptr<FontLibrary> library) {
    Font font;
    font.library_ = library ? std::move(library) : make_library(renderer);
    std::string prepare = "latin";

    const std::string lower = [&] {
        std::string l = path;
        std::transform(l.begin(), l.end(), l.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return l;
    }();
    const bool stack = lower.size() > 10 && lower.compare(lower.size() - 10, 10, ".font.json") == 0;
    if (!stack) {
        font.faces_.push_back(font.library_->face_index(path));
        font.files_.push_back(path);
    } else {
        nlohmann::json document;
        try {
            const FileData file = read_file(path);
            document = parse_document(std::string_view(static_cast<const char*>(file.data()), file.size()));
        } catch (const std::exception& e) {
            throw std::runtime_error("Cannot read font stack '" + path + "': " + e.what());
        }
        if (!document.is_object() || !document.contains("faces") || !document["faces"].is_array() ||
            document["faces"].empty()) {
            throw std::runtime_error("Font stack '" + path + "': \"faces\" must list at least one font file");
        }
        if (document.contains("prepare") && document["prepare"].is_string()) {
            prepare = document["prepare"].get<std::string>();
        }
        const std::string folder = std::filesystem::path(path).parent_path().generic_string() + "/";
        bool first = true;
        for (const nlohmann::json& entry : document["faces"]) {
            if (!entry.is_string()) {
                throw std::runtime_error("Font stack '" + path + "': each face is a file name");
            }
            const std::string file = folder + entry.get<std::string>();
            if (!first && !file_exists(file)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Font stack '%s': fallback '%s' missing, skipped", path.c_str(),
                            file.c_str());
                continue;
            }
            font.faces_.push_back(font.library_->face_index(file));
            font.files_.push_back(file);
            first = false;
        }
    }

    if (prepare == "latin") {
        std::u32string latin;
        for (char32_t c = 0x21; c <= 0x7E; ++c) latin.push_back(c);
        for (char32_t c = 0xA1; c <= 0xFF; ++c) latin.push_back(c);
        for (const char32_t c : {0x152u, 0x153u, 0x2013u, 0x2014u, 0x2018u, 0x2019u, 0x201Cu, 0x201Du, 0x2026u, 0x20ACu}) {
            latin.push_back(static_cast<char32_t>(c));
        }
        const auto start = std::chrono::steady_clock::now();
        const std::size_t before = font.library_->atlas().made();
        font.prepare(latin);
        SDL_Log("Font '%s': %zu face(s), %zu glyphs made in %.1f ms", path.c_str(), font.faces_.size(),
                font.library_->atlas().made() - before,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    return font;
}

void Font::prepare(std::u32string_view characters, float weight) const {
    std::vector<std::pair<FontFace*, GlyphKey>> glyphs;
    for (const char32_t c : characters) {
        for (const std::uint32_t index : faces_) {
            FontFace& face = library_->face(index);
            const std::uint32_t glyph = face.glyph_for(c);
            if (glyph != 0) {
                glyphs.push_back({&face, GlyphKey{index, glyph, face.instance_weight(weight)}});
                break;
            }
        }
    }
    library_->atlas().prepare(glyphs, library_->frame());
}

std::size_t Font::gpu_bytes() const {
    return library_ ? library_->atlas().gpu_bytes() : 0;
}

float Font::line_height(float size) const {
    FontFace& face = library_->face(faces_.front());
    const FontFace::Extents& e = face.extents();
    return (e.ascender - e.descender + e.line_gap) * size / static_cast<float>(face.units_per_em());
}

float Font::ascent(float size) const {
    FontFace& face = library_->face(faces_.front());
    return face.extents().ascender * size / static_cast<float>(face.units_per_em());
}

// --- Layout ---------------------------------------------------------------------------------------

namespace {

struct LayoutContext {
    FontLibrary& library;
    const std::vector<std::uint32_t>& stack;
    const TextStyle& style;
    TextLayout& out;
    std::vector<std::uint16_t> span_of;   // per character: index in out.spans
    std::vector<std::uint8_t> face_of;    // per character: index in `stack`
    std::vector<std::uint8_t> script_of;  // per character: resolved script
    hb_buffer_t* buffer = nullptr;
    float y = 0;  // top of the next line
};

float span_size(const LayoutContext& ctx, std::uint16_t span) {
    return ctx.style.size * ctx.out.spans[span].scale;
}

float span_weight(const LayoutContext& ctx, std::uint16_t span) {
    return ctx.out.spans[span].weight.value_or(ctx.style.weight);
}

bool is_icon(const LayoutContext& ctx, std::size_t i) {
    return ctx.out.text[i] == kObjectReplacement && !ctx.out.spans[ctx.span_of[i]].icon.empty();
}

// Picks the font of each grapheme cluster: the first of the stack that has all its characters;
// spaces and punctuation stay in the font of the text before them when it has them.
void choose_faces(LayoutContext& ctx) {
    const std::u32string& text = ctx.out.text;
    ctx.face_of.assign(text.size(), 0);
    const std::vector<std::size_t> clusters = unicode::grapheme_boundaries(text);
    std::uint8_t previous = 0;
    std::vector<char32_t> needed;
    for (std::size_t k = 0; k + 1 < clusters.size(); ++k) {
        const std::size_t a = clusters[k], b = clusters[k + 1];
        needed.clear();
        bool common = true;
        for (std::size_t i = a; i < b; ++i) {
            const unicode::CharInfo info = unicode::char_info(text[i]);
            if (info.script != unicode::kScriptCommon && info.script != unicode::kScriptInherited) {
                common = false;
            }
            if (!is_default_ignorable(text[i], info) && text[i] != kObjectReplacement) {
                needed.push_back(text[i]);
            }
        }
        const auto has_all = [&](std::size_t f) {
            FontFace& face = ctx.library.face(ctx.stack[f]);
            return std::all_of(needed.begin(), needed.end(), [&](char32_t c) { return face.glyph_for(c) != 0; });
        };
        std::uint8_t chosen = previous;
        if (!needed.empty() && !(common && has_all(previous))) {
            chosen = 0;  // nobody has it: the main font's missing glyph
            for (std::size_t f = 0; f < ctx.stack.size(); ++f) {
                if (has_all(f)) {
                    chosen = static_cast<std::uint8_t>(f);
                    break;
                }
            }
        }
        std::fill(ctx.face_of.begin() + static_cast<std::ptrdiff_t>(a), ctx.face_of.begin() + static_cast<std::ptrdiff_t>(b),
                  chosen);
        previous = chosen;
    }
}

// The script of each character; Common and Inherited ones take the script before them (or, at
// the start, the first one after).
void resolve_scripts(LayoutContext& ctx) {
    const std::u32string& text = ctx.out.text;
    ctx.script_of.assign(text.size(), unicode::kScriptCommon);
    std::uint8_t last = unicode::kScriptCommon;
    std::size_t first_real = text.size();
    for (std::size_t i = 0; i < text.size(); ++i) {
        const std::uint8_t s = unicode::char_info(text[i]).script;
        if (s != unicode::kScriptCommon && s != unicode::kScriptInherited && s != unicode::kScriptUnknown) {
            last = s;
            if (first_real == text.size()) {
                first_real = i;
            }
        }
        ctx.script_of[i] = last;
    }
    for (std::size_t i = 0; i < first_real && first_real < text.size(); ++i) {
        ctx.script_of[i] = ctx.script_of[first_real];
    }
}

struct ShapedGlyph {
    std::uint32_t glyph;
    std::uint32_t cluster;              // character index in the whole text
    float advance, x_offset, y_offset;  // pixels
};

struct Run {
    std::size_t first = 0, end = 0;  // characters [first, end) of the whole text
    std::uint8_t face = 0;
    std::uint8_t script = 0;
    std::uint8_t level = 0;
    std::uint16_t span = 0;
    bool icon = false;
    int instance = 0;
    float dilation = 0;
    std::vector<ShapedGlyph> glyphs;  // left to right
    float width = 0;
};

// Shapes one run with HarfBuzz, the paragraph given as context (Arabic letters join across runs).
void shape(LayoutContext& ctx, Run& run, std::size_t paragraph_start, std::size_t paragraph_end) {
    run.glyphs.clear();
    run.width = 0;
    const float size = span_size(ctx, run.span);
    if (run.icon) {
        for (std::size_t i = run.first; i < run.end; ++i) {
            run.glyphs.push_back({0, static_cast<std::uint32_t>(i), size, 0, 0});
            run.width += size;
        }
        return;
    }
    FontFace& face = ctx.library.face(ctx.stack[run.face]);
    const float weight = span_weight(ctx, run.span);
    run.instance = face.instance_weight(weight);
    hb_font_t* font = face.instance(run.instance);
    const float scale = size / static_cast<float>(face.units_per_em());
    const bool synthetic_bold = !face.variable_weight() && weight >= 550.0f;
    run.dilation = synthetic_bold ? std::clamp((weight - 400.0f) / 300.0f, 0.0f, 1.7f) * kBoldDilationEm * size : 0.0f;

    hb_buffer_t* buffer = ctx.buffer;
    hb_buffer_clear_contents(buffer);
    const std::u32string& text = ctx.out.text;
    static_assert(sizeof(char32_t) == sizeof(std::uint32_t));
    hb_buffer_add_utf32(buffer, reinterpret_cast<const std::uint32_t*>(text.data() + paragraph_start),
                        static_cast<int>(paragraph_end - paragraph_start),
                        static_cast<unsigned int>(run.first - paragraph_start), static_cast<int>(run.end - run.first));
    hb_buffer_set_direction(buffer, is_rtl_level(run.level) ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    hb_buffer_set_script(buffer, hb_script_of(run.script));
    hb_buffer_set_language(buffer, ctx.style.language.empty() ? hb_language_get_default()
                                                              : hb_language_from_string(ctx.style.language.c_str(), -1));
    hb_buffer_set_cluster_level(buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);
    hb_shape(font, buffer, nullptr, 0);

    unsigned int count = 0;
    const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &count);
    const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &count);
    run.glyphs.reserve(count);
    for (unsigned int g = 0; g < count; ++g) {
        ShapedGlyph shaped;
        shaped.glyph = infos[g].codepoint;
        shaped.cluster = static_cast<std::uint32_t>(paragraph_start + infos[g].cluster);
        shaped.advance = static_cast<float>(positions[g].x_advance) * scale + 2.0f * run.dilation;
        shaped.x_offset = static_cast<float>(positions[g].x_offset) * scale + run.dilation;
        shaped.y_offset = static_cast<float>(positions[g].y_offset) * scale;
        run.width += shaped.advance;
        run.glyphs.push_back(shaped);
    }
}

// [first, end) of the text cut into runs of one font, script, level and span; `levels[i -
// origin]` is the level of character i.
std::vector<Run> make_runs(const LayoutContext& ctx, std::size_t first, std::size_t end,
                           const std::vector<std::uint8_t>& levels, std::size_t origin) {
    std::vector<Run> runs;
    for (std::size_t i = first; i < end; ++i) {
        const bool icon = is_icon(ctx, i);
        const std::uint8_t level = levels[i - origin];
        if (!runs.empty() && !icon && !runs.back().icon && runs.back().face == ctx.face_of[i] &&
            runs.back().script == ctx.script_of[i] && runs.back().level == level && runs.back().span == ctx.span_of[i]) {
            runs.back().end = i + 1;
            continue;
        }
        Run run;
        run.first = i;
        run.end = i + 1;
        run.face = ctx.face_of[i];
        run.script = ctx.script_of[i];
        run.level = level;
        run.span = ctx.span_of[i];
        run.icon = icon;
        runs.push_back(std::move(run));
    }
    return runs;
}

// The main font's metrics at a size: ascent, descent (positive), gap, in pixels.
struct Metrics {
    float ascent, descent, gap;
};

Metrics metrics_at(LayoutContext& ctx, float size) {
    FontFace& face = ctx.library.face(ctx.stack.front());
    const float scale = size / static_cast<float>(face.units_per_em());
    const FontFace::Extents& e = face.extents();
    return {e.ascender * scale, -e.descender * scale, e.line_gap * scale};
}

// Lays out the line [first, end) of a paragraph and appends it.
void emit_line(LayoutContext& ctx, std::size_t first, std::size_t end, std::size_t paragraph_start,
               std::size_t paragraph_end, const BidiParagraph& bidi, std::u32string_view paragraph) {
    const std::u32string& text = ctx.out.text;
    // Trailing spaces take no room (rule L1 puts them at the paragraph's level anyway).
    std::size_t content_end = end;
    while (content_end > first && is_space(unicode::char_info(text[content_end - 1]))) {
        --content_end;
    }
    const std::vector<std::uint8_t> line_levels =
        bidi_line_levels(paragraph, bidi, first - paragraph_start, end - paragraph_start);
    std::vector<Run> runs = make_runs(ctx, first, content_end, line_levels, first);
    float scale_max = 0.0f;
    for (Run& run : runs) {
        shape(ctx, run, paragraph_start, paragraph_end);
        scale_max = std::max(scale_max, ctx.out.spans[run.span].scale);
    }
    if (scale_max == 0.0f) {
        scale_max = 1.0f;
    }

    TextLine line;
    line.first_char = static_cast<std::uint32_t>(first);
    line.end_char = static_cast<std::uint32_t>(end);
    line.rtl = is_rtl_level(bidi.base_level);
    const Metrics m = metrics_at(ctx, ctx.style.size * scale_max);
    line.top = ctx.y;
    line.height = (m.ascent + m.descent + m.gap) * ctx.style.line_spacing;
    line.baseline = ctx.y + (line.height - (m.ascent + m.descent)) * 0.5f + m.ascent;
    line.first_glyph = static_cast<std::uint32_t>(ctx.out.glyphs.size());

    // Rule L2 on the runs (each has one level): their order left to right.
    std::vector<std::uint8_t> run_levels(runs.size());
    for (std::size_t r = 0; r < runs.size(); ++r) {
        run_levels[r] = runs[r].level;
    }
    float pen = 0.0f;
    for (const std::size_t r : bidi_visual_order(run_levels)) {
        const Run& run = runs[r];
        const TextSpan& span = ctx.out.spans[run.span];
        const float size = span_size(ctx, run.span);
        for (const ShapedGlyph& g : run.glyphs) {
            if (run.icon) {
                TextIcon icon;
                icon.name = span.icon;
                icon.size = glm::vec2(size);
                icon.position = glm::vec2(pen, line.baseline - 0.8f * size);
                icon.span = run.span;
                icon.line = static_cast<std::uint32_t>(ctx.out.lines.size());
                ctx.out.icons.push_back(icon);
            } else {
                TextGlyph glyph;
                glyph.face = ctx.stack[run.face];
                glyph.glyph = g.glyph;
                glyph.weight = run.instance;
                glyph.span = run.span;
                glyph.size = size;
                glyph.dilate = run.dilation;
                glyph.italic = ctx.style.italic || span.italic;
                glyph.position = glm::vec2(pen + g.x_offset, line.baseline - g.y_offset);
                glyph.cluster = g.cluster;
                ctx.out.glyphs.push_back(glyph);
            }
            pen += g.advance;
        }
    }
    line.width = pen;
    line.glyph_count = static_cast<std::uint32_t>(ctx.out.glyphs.size()) - line.first_glyph;
    ctx.out.lines.push_back(line);
    ctx.y += line.height;
}

// One paragraph [start, end): bidirectional levels, line breaks, lines.
void layout_paragraph(LayoutContext& ctx, std::size_t start, std::size_t end) {
    const std::u32string& text = ctx.out.text;
    const std::u32string_view paragraph(text.data() + start, end - start);
    const BidiParagraph bidi = bidi_paragraph(paragraph, ctx.style.direction);
    if (paragraph.empty()) {
        emit_line(ctx, start, end, start, end, bidi, paragraph);
        return;
    }

    // The width of each character: the paragraph shaped once, each glyph's advance given to the
    // character its cluster starts at.
    std::vector<float> advance(paragraph.size(), 0.0f);
    for (Run& run : make_runs(ctx, start, end, bidi.levels, start)) {
        shape(ctx, run, start, end);
        for (const ShapedGlyph& g : run.glyphs) {
            advance[g.cluster - start] += g.advance;
        }
    }

    // Greedy: as many segments (text between two break opportunities) as fit.
    const std::vector<unicode::LineBreak> breaks = unicode::line_breaks(paragraph);
    const float max_width = ctx.style.max_width;
    std::size_t line_start = 0;
    float line_width = 0.0f;
    std::size_t segment_start = 0;
    for (std::size_t i = 1; i <= paragraph.size(); ++i) {
        if (breaks[i] == unicode::LineBreak::None) {
            continue;
        }
        float width = 0.0f;
        float trailing = 0.0f;
        for (std::size_t k = segment_start; k < i; ++k) {
            width += advance[k];
        }
        for (std::size_t k = i; k > segment_start && is_space(unicode::char_info(paragraph[k - 1])); --k) {
            trailing += advance[k - 1];
        }
        if (max_width > 0.0f && segment_start > line_start && line_width + width - trailing > max_width) {
            emit_line(ctx, start + line_start, start + segment_start, start, end, bidi, paragraph);
            line_start = segment_start;
            line_width = 0.0f;
        }
        line_width += width;
        if (breaks[i] == unicode::LineBreak::Mandatory && i < paragraph.size()) {
            emit_line(ctx, start + line_start, start + i, start, end, bidi, paragraph);
            line_start = i;
            line_width = 0.0f;
        }
        segment_start = i;
    }
    emit_line(ctx, start + line_start, end, start, end, bidi, paragraph);
}

TextLayout build_layout(FontLibrary& library, const std::vector<std::uint32_t>& stack, std::string_view utf8,
                        const TextStyle& style) {
    TextLayout out;
    LayoutContext ctx{library, stack, style, out};
    if (style.markup) {
        TextMarkup markup = parse_text_markup(utf8);
        out.text = std::move(markup.text);
        out.spans = std::move(markup.spans);
        ctx.span_of = std::move(markup.span_of);
    } else {
        out.text = decode_utf8(utf8);
        out.spans.emplace_back();
        ctx.span_of.assign(out.text.size(), 0);
    }
    choose_faces(ctx);
    resolve_scripts(ctx);
    ctx.buffer = hb_buffer_create();

    // Paragraphs end at the separators of Bidi class B (newlines); a separator belongs to its
    // paragraph but takes no room. "a\n" is two lines, the second empty.
    const std::u32string& text = out.text;
    std::size_t start = 0;
    while (true) {
        std::size_t end = start;
        while (end < text.size() && unicode::char_info(text[end]).bidi != unicode::BidiClass::B) {
            ++end;
        }
        layout_paragraph(ctx, start, end);
        if (end >= text.size()) {
            break;
        }
        start = end + ((text[end] == U'\r' && end + 1 < text.size() && text[end + 1] == U'\n') ? 2 : 1);
    }
    hb_buffer_destroy(ctx.buffer);

    // The box, then each line aligned in it.
    float box = std::max(style.max_width, 0.0f);
    for (const TextLine& line : out.lines) {
        box = std::max(box, line.width);
    }
    out.size = glm::vec2(box, ctx.y);
    for (TextLine& line : out.lines) {
        TextAlign align = style.align;
        if (align == TextAlign::Start) {
            align = line.rtl ? TextAlign::Right : TextAlign::Left;
        } else if (align == TextAlign::End) {
            align = line.rtl ? TextAlign::Left : TextAlign::Right;
        }
        line.x = align == TextAlign::Center ? (box - line.width) * 0.5f : (align == TextAlign::Right ? box - line.width : 0.0f);
        for (std::uint32_t g = line.first_glyph; g < line.first_glyph + line.glyph_count; ++g) {
            out.glyphs[g].position.x += line.x;
        }
    }
    for (TextIcon& icon : out.icons) {
        icon.position.x += out.lines[icon.line].x;
    }
    return out;
}

// The cache key: the stack, the text and every field of the style that changes the layout.
std::string layout_key(const std::vector<std::uint32_t>& stack, std::string_view utf8, const TextStyle& style) {
    std::string key;
    key.reserve(utf8.size() + 64);
    const auto add = [&](const auto& value) {
        key.append(reinterpret_cast<const char*>(&value), sizeof(value));
    };
    add(stack.size());
    for (const std::uint32_t f : stack) {
        add(f);
    }
    add(style.size);
    add(style.weight);
    add(style.italic);
    add(style.align);
    add(style.max_width);
    add(style.line_spacing);
    add(style.direction);
    add(style.markup);
    key += style.language;
    key.push_back('\0');
    key.append(utf8);
    return key;
}

}  // namespace

std::shared_ptr<const TextLayout> Font::layout(std::string_view utf8_text, const TextStyle& style) const {
    const std::string key = layout_key(faces_, utf8_text, style);
    if (std::shared_ptr<const TextLayout> cached = library_->cached(key)) {
        return cached;
    }
    auto layout = std::make_shared<const TextLayout>(build_layout(*library_, faces_, utf8_text, style));
    library_->store(key, layout);
    return layout;
}

glm::vec2 Font::measure(std::string_view utf8_text, const TextStyle& style) const {
    return layout(utf8_text, style)->size;
}

void Font::draw(SpriteRenderer& sprites, std::string_view utf8_text, glm::vec2 anchor, const TextStyle& style) const {
    draw(sprites, *layout(utf8_text, style), anchor, style);
}

void Font::draw(SpriteRenderer& sprites, const TextLayout& layout, glm::vec2 anchor, const TextStyle& style) const {
    GlyphAtlas& atlas = library_->atlas();
    const std::uint64_t frame = library_->frame();
    // The distance field reaches half its range around the ink: outlines and shadows stay inside.
    const auto reach = [](float size) { return GlyphAtlas::kRangeTexels * 0.5f * size / GlyphAtlas::kEmTexels; };

    // The glyphs this text misses, made now within the frame's budget (over the job system).
    std::vector<std::pair<FontFace*, GlyphKey>> missing;
    for (const TextGlyph& g : layout.glyphs) {
        const GlyphKey key{g.face, g.glyph, g.weight};
        if (!atlas.contains(key)) {
            missing.push_back({&library_->face(g.face), key});
        }
    }
    if (!missing.empty()) {
        atlas.prepare(missing, frame, static_cast<std::size_t>(-1), true);
    }

    const auto record = [&](const TextGlyph& g, glm::vec2 offset, glm::vec4 color, glm::vec4 outline_color, float outline,
                            float softness) {
        if (color.a <= 0.0f && outline_color.a <= 0.0f) {
            return;
        }
        const AtlasGlyph* a = atlas.find(library_->face(g.face), GlyphKey{g.face, g.glyph, g.weight}, frame);
        if (a == nullptr || a->page == nullptr) {
            return;  // not made yet, or no ink
        }
        const float s = g.size;
        const glm::vec2 pen = anchor + offset + g.position;
        const glm::vec2 top_left(pen.x + a->plane.x * s, pen.y - a->plane.w * s);
        const glm::vec2 size((a->plane.z - a->plane.x) * s, (a->plane.w - a->plane.y) * s);
        GlyphLook look;
        look.color = color;
        look.outline_color = outline_color;
        const float limit = std::max(reach(s) - 1.0f, 0.0f);
        look.dilate = std::min(g.dilate, limit);
        look.outline = std::clamp(outline, 0.0f, std::max(limit - look.dilate, 0.0f));
        look.softness = std::clamp(softness, 1.0f, std::max(limit - look.dilate - look.outline, 1.0f));
        float top_shift = 0.0f, bottom_shift = 0.0f;
        if (g.italic) {
            top_shift = a->plane.w * s * kSlant;
            bottom_shift = a->plane.y * s * kSlant;
        }
        sprites.draw_glyph(*a->page, top_left, size, a->uv, look, style.depth, top_shift, bottom_shift, style.clip);
    };

    if (style.shadow) {
        const TextShadow& shadow = *style.shadow;
        for (const TextGlyph& g : layout.glyphs) {
            record(g, shadow.offset, shadow.color, style.outline_width > 0.0f ? shadow.color : glm::vec4(0.0f),
                   style.outline_width, shadow.softness);
        }
    }
    for (const TextGlyph& g : layout.glyphs) {
        const glm::vec4 color = layout.spans[g.span].color.value_or(style.color);
        record(g, glm::vec2(0.0f), color, style.outline_width > 0.0f ? style.outline_color : glm::vec4(0.0f),
               style.outline_width, 1.0f);
    }
    if (icons_) {
        for (const TextIcon& icon : layout.icons) {
            const SpriteRegion* region = icons_(icon.name);
            if (region != nullptr && region->texture != nullptr) {
                SpriteOptions options;
                options.uv_rect = region->uv_rect;
                options.depth = style.depth;
                options.tint = layout.spans[icon.span].color.value_or(glm::vec4(1.0f));
                options.clip = style.clip;
                sprites.draw(*region->texture, anchor + icon.position, icon.size, options);
            }
        }
    }
}

glm::vec3 text_caret(const TextLayout& layout, std::size_t index) {
    if (layout.lines.empty()) {
        return glm::vec3(0.0f);
    }
    // The line holding the index (its end belongs to it), else the last one.
    const TextLine* line = &layout.lines.back();
    for (const TextLine& l : layout.lines) {
        if (index >= l.first_char && index < l.end_char) {
            line = &l;
            break;
        }
    }
    const float top = line->top;
    const float bottom = line->top + line->height;
    // The glyph of that character (the first of its cluster), else the line's end.
    float x = line->x + (line->rtl ? 0.0f : line->width);
    bool found = false;
    for (std::uint32_t g = line->first_glyph; g < line->first_glyph + line->glyph_count; ++g) {
        if (layout.glyphs[g].cluster == index) {
            x = layout.glyphs[g].position.x;
            found = true;
            break;
        }
    }
    if (!found) {
        // Past its last glyph: after the glyph of the nearest smaller cluster.
        std::uint32_t best = line->first_glyph + line->glyph_count;
        std::uint32_t best_cluster = 0;
        for (std::uint32_t g = line->first_glyph; g < line->first_glyph + line->glyph_count; ++g) {
            const std::uint32_t c = layout.glyphs[g].cluster;
            if (c < index && (best == line->first_glyph + line->glyph_count || c >= best_cluster)) {
                best = g;
                best_cluster = c;
            }
        }
        if (best < line->first_glyph + line->glyph_count) {
            const TextGlyph& glyph = layout.glyphs[best];
            // The next glyph's position on the line (or the line's end) is the end of this one.
            const std::uint32_t next = best + 1;
            x = next < line->first_glyph + line->glyph_count ? layout.glyphs[next].position.x : line->x + line->width;
            (void)glyph;
        }
    }
    return glm::vec3(x, top, bottom);
}

std::size_t text_hit(const TextLayout& layout, glm::vec2 point) {
    if (layout.lines.empty()) {
        return 0;
    }
    const TextLine* line = &layout.lines.back();
    for (const TextLine& l : layout.lines) {
        if (point.y < l.top + l.height) {
            line = &l;
            break;
        }
    }
    std::size_t best = line->first_char;
    float best_distance = std::abs(text_caret(layout, best).x - point.x);
    for (std::size_t i = line->first_char + 1; i <= line->end_char; ++i) {
        const float d = std::abs(text_caret(layout, i).x - point.x);
        if (d < best_distance) {
            best_distance = d;
            best = i;
        }
    }
    return std::min(best, layout.text.size());
}

}  // namespace moteur

