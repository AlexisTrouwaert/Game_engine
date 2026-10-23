#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

#include "moteur/msdf.hpp"
#include "moteur/paths.hpp"

struct hb_blob_t;
struct hb_face_t;
struct hb_font_t;

namespace moteur {

/// One font file (the first face of a collection), read through HarfBuzz (milestone 9 bis, part
/// 4): its metrics, which characters it has, its glyph outlines (TrueType, CFF, variable fonts at
/// any weight) and the HarfBuzz font objects the shaper uses. Thread-safe: shaping and outlines
/// may run on the job system's threads.
class FontFace {
public:
    /// Reads `path` (read_file(): archives first). Throws std::runtime_error naming the file if it
    /// cannot be read or is not a font HarfBuzz can open.
    explicit FontFace(const std::string& path);
    ~FontFace();
    FontFace(const FontFace&) = delete;
    FontFace& operator=(const FontFace&) = delete;

    const std::string& path() const { return path_; }
    /// Font units per em (2048 for Inter, 1000 for most CFF fonts).
    int units_per_em() const { return units_per_em_; }

    /// Whether the font has a weight axis ("wght"): bold is then a real instance, not a dilation.
    bool variable_weight() const { return has_weight_axis_; }
    /// The weight the instance for `weight` uses: clamped to the axis, or the font's own weight
    /// (0 for a font without the axis: one instance).
    int instance_weight(float weight) const;

    /// The glyph of `code_point` in this font (its cmap), 0 when it has none (.notdef).
    std::uint32_t glyph_for(char32_t code_point) const;

    /// Ascender (positive), descender (negative) and line gap, in font units, of the default instance.
    struct Extents {
        float ascender = 0;
        float descender = 0;
        float line_gap = 0;
    };
    const Extents& extents() const { return extents_; }

    /// The HarfBuzz font of an instance (instance_weight()), its scale in font units. Made on first
    /// use and kept; valid as long as the face. Thread-safe.
    hb_font_t* instance(int weight);

    /// The glyph's outline at that instance, in font units (y up). false for a glyph without one
    /// (a space) or out of range.
    bool outline(std::uint32_t glyph, int weight, msdf::Outline& out);

private:
    std::string path_;
    FileData data_;
    hb_blob_t* blob_ = nullptr;
    hb_face_t* face_ = nullptr;
    hb_font_t* base_ = nullptr;  ///< the default instance
    int units_per_em_ = 1000;
    bool has_weight_axis_ = false;
    float weight_min_ = 400, weight_default_ = 400, weight_max_ = 400;
    Extents extents_;
    std::mutex mutex_;
    std::map<int, hb_font_t*> instances_;
};

}  // namespace moteur
