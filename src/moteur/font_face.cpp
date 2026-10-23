#include "moteur/font_face.hpp"

#include <hb-ot.h>
#include <hb.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace moteur {

namespace {

constexpr hb_tag_t kWeightAxis = HB_TAG('w', 'g', 'h', 't');

// The draw callbacks: HarfBuzz hands the outline over as pen moves (font units, y up).
void draw_move_to(hb_draw_funcs_t*, void* data, hb_draw_state_t*, float x, float y, void*) {
    static_cast<msdf::Outline*>(data)->move_to({x, y});
}
void draw_line_to(hb_draw_funcs_t*, void* data, hb_draw_state_t*, float x, float y, void*) {
    static_cast<msdf::Outline*>(data)->line_to({x, y});
}
void draw_quadratic_to(hb_draw_funcs_t*, void* data, hb_draw_state_t*, float cx, float cy, float x, float y, void*) {
    static_cast<msdf::Outline*>(data)->quadratic_to({cx, cy}, {x, y});
}
void draw_cubic_to(hb_draw_funcs_t*, void* data, hb_draw_state_t*, float c1x, float c1y, float c2x, float c2y, float x,
                   float y, void*) {
    static_cast<msdf::Outline*>(data)->cubic_to({c1x, c1y}, {c2x, c2y}, {x, y});
}
void draw_close_path(hb_draw_funcs_t*, void* data, hb_draw_state_t*, void*) {
    static_cast<msdf::Outline*>(data)->close();
}

hb_draw_funcs_t* outline_funcs() {
    static hb_draw_funcs_t* funcs = [] {
        hb_draw_funcs_t* f = hb_draw_funcs_create();
        hb_draw_funcs_set_move_to_func(f, draw_move_to, nullptr, nullptr);
        hb_draw_funcs_set_line_to_func(f, draw_line_to, nullptr, nullptr);
        hb_draw_funcs_set_quadratic_to_func(f, draw_quadratic_to, nullptr, nullptr);
        hb_draw_funcs_set_cubic_to_func(f, draw_cubic_to, nullptr, nullptr);
        hb_draw_funcs_set_close_path_func(f, draw_close_path, nullptr, nullptr);
        hb_draw_funcs_make_immutable(f);
        return f;
    }();
    return funcs;
}

}  // namespace

FontFace::FontFace(const std::string& path) : path_(path) {
    try {
        data_ = read_file(path);
    } catch (const std::runtime_error& e) {
        throw std::runtime_error("Cannot read font '" + path + "': " + e.what());
    }
    blob_ = hb_blob_create(static_cast<const char*>(data_.data()), static_cast<unsigned int>(data_.size()),
                           HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    face_ = hb_face_create(blob_, 0);
    units_per_em_ = static_cast<int>(hb_face_get_upem(face_));
    if (hb_face_get_glyph_count(face_) == 0) {
        hb_face_destroy(face_);
        hb_blob_destroy(blob_);
        throw std::runtime_error("Font '" + path + "' could not be parsed (not a TrueType/OpenType file?)");
    }
    base_ = hb_font_create(face_);  // OpenType functions, scale = units per em
    hb_font_make_immutable(base_);

    hb_ot_var_axis_info_t axis{};
    if (hb_ot_var_find_axis_info(face_, kWeightAxis, &axis)) {
        has_weight_axis_ = true;
        weight_min_ = axis.min_value;
        weight_default_ = axis.default_value;
        weight_max_ = axis.max_value;
    }

    hb_font_extents_t extents{};
    hb_font_get_h_extents(base_, &extents);
    extents_.ascender = static_cast<float>(extents.ascender);
    extents_.descender = static_cast<float>(extents.descender);
    extents_.line_gap = static_cast<float>(extents.line_gap);
    if (extents_.ascender <= extents_.descender) {  // a broken font: something readable
        extents_.ascender = 0.8f * static_cast<float>(units_per_em_);
        extents_.descender = -0.2f * static_cast<float>(units_per_em_);
    }
}

FontFace::~FontFace() {
    for (const auto& [weight, font] : instances_) {
        hb_font_destroy(font);
    }
    hb_font_destroy(base_);
    hb_face_destroy(face_);
    hb_blob_destroy(blob_);
}

int FontFace::instance_weight(float weight) const {
    if (!has_weight_axis_) {
        return 0;
    }
    return static_cast<int>(std::lround(std::clamp(weight, weight_min_, weight_max_)));
}

std::uint32_t FontFace::glyph_for(char32_t code_point) const {
    hb_codepoint_t glyph = 0;
    return hb_font_get_nominal_glyph(base_, code_point, &glyph) ? glyph : 0;
}

hb_font_t* FontFace::instance(int weight) {
    if (!has_weight_axis_ || weight == static_cast<int>(std::lround(weight_default_))) {
        return base_;
    }
    const std::lock_guard lock(mutex_);
    const auto it = instances_.find(weight);
    if (it != instances_.end()) {
        return it->second;
    }
    // A font of its own (not a sub-font: those ask their parent, which has no variations).
    hb_font_t* font = hb_font_create(face_);
    const hb_variation_t variation{kWeightAxis, static_cast<float>(weight)};
    hb_font_set_variations(font, &variation, 1);
    hb_font_make_immutable(font);
    instances_.emplace(weight, font);
    return font;
}

bool FontFace::outline(std::uint32_t glyph, int weight, msdf::Outline& out) {
    out = msdf::Outline{};
    hb_font_t* font = instance(weight);
    if (!hb_font_draw_glyph_or_fail(font, glyph, outline_funcs(), &out)) {
        return false;
    }
    out.close();
    return !out.empty();
}

}  // namespace moteur
