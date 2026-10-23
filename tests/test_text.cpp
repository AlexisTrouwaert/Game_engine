// The text system (milestone 9 bis, part 4): distance fields, fonts read through HarfBuzz, layout
// (fallback fonts, bidirectional text, line breaking), markup and the glyph atlas, without GPU.

#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

#include "moteur/font.hpp"
#include "moteur/font_face.hpp"
#include "moteur/font_library.hpp"
#include "moteur/msdf.hpp"

using moteur::Font;
using moteur::TextLayout;
using moteur::TextStyle;

namespace {

const std::string kInter = std::string(MOTEUR_SOURCE_ASSETS) + "../assets_moteur/fonts/Inter-Regular.ttf";
const std::string kArabic = std::string(MOTEUR_SOURCE_ASSETS) + "fonts/noto/NotoSansArabic.ttf";
const std::string kChinese = std::string(MOTEUR_SOURCE_ASSETS) + "fonts/noto/NotoSansSC.ttf";

bool have_noto() {
    if (std::filesystem::exists(kArabic) && std::filesystem::exists(kChinese)) {
        return true;
    }
    MESSAGE("Noto fonts missing (python tools/text/fetch_text_data.py): fallback and Arabic checks skipped");
    return false;
}

// An outline in a 0..10 square, y up.
moteur::msdf::Outline square(double lo, double hi) {
    moteur::msdf::Outline o;
    o.move_to({lo, lo});
    o.line_to({lo, hi});
    o.line_to({hi, hi});
    o.line_to({hi, lo});
    o.close();
    return o;
}

float median(const std::uint8_t* t) {
    const float r = t[0], g = t[1], b = t[2];
    return std::max(std::min(r, g), std::min(std::max(r, g), b)) / 255.0f;
}

// The font for these tests: a stack in a temporary file, Inter first (faces are relative to the
// stack's folder).
Font load_stack(bool with_noto) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "moteur_test.font.json";
    const std::filesystem::path folder = path.parent_path();
    const auto relative = [&](const std::string& file) {
        return std::filesystem::relative(file, folder).generic_string();
    };
    std::string faces = "\"" + relative(kInter) + "\"";
    if (with_noto) {
        faces += ", \"" + relative(kArabic) + "\", \"" + relative(kChinese) + "\"";
    }
    std::ofstream(path, std::ios::binary) << "{\"faces\": [" + faces + "], \"prepare\": \"none\"}";
    return Font::load(nullptr, path.generic_string());
}

}  // namespace

TEST_CASE("msdf: a square's field is inside in its middle, outside around, 0.5 on its edge") {
    moteur::msdf::Outline outline = square(2.0, 8.0);
    moteur::msdf::Placement t;
    t.scale = 4.0;  // 40 x 40 texels for 0..10
    t.range = 8.0;
    std::vector<std::uint8_t> rgba(40 * 40 * 4);
    moteur::msdf::generate(outline, t, 40, 40, rgba.data());
    const auto at = [&](int x, int y) { return &rgba[(static_cast<std::size_t>(y) * 40 + static_cast<std::size_t>(x)) * 4]; };
    CHECK(median(at(20, 20)) > 0.9f);      // deep inside (12 texels from the edge, range 8: saturated)
    CHECK(median(at(2, 2)) < 0.1f);        // outside
    CHECK(at(20, 20)[3] > 230);            // the true distance too
    // Texel 8's center is at x = 8.5 / 4 = 2.125, 0.125 inside the edge at x = 2: 0.5 + 0.5/8.
    CHECK(median(at(8, 20)) == doctest::Approx(0.5f + 0.5f / 8.0f).epsilon(0.03));
    CHECK(median(at(7, 20)) < 0.5f);
}

TEST_CASE("msdf: a circle's field has the right sign everywhere but on the edge") {
    // A circle of radius 4 around (5, 5) as eight quadratic arcs (within 0.3 % of a circle),
    // counter-clockwise: the generator turns it the other way itself.
    moteur::msdf::Outline o;
    const double c = 5.0, r = 4.0;
    const double step = 3.14159265358979 / 4.0;
    o.move_to({c + r, c});
    for (int i = 0; i < 8; ++i) {
        const double a0 = i * step, a1 = (i + 1) * step, mid = (a0 + a1) * 0.5;
        const double rc = r / std::cos(step * 0.5);
        o.quadratic_to({c + rc * std::cos(mid), c + rc * std::sin(mid)}, {c + r * std::cos(a1), c + r * std::sin(a1)});
    }
    o.close();
    moteur::msdf::Placement t;
    t.scale = 6.0;
    t.range = 6.0;
    const int n = 60;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(n) * n * 4);
    moteur::msdf::generate(o, t, n, n, rgba.data());
    // Compare the sign with a fine coverage test of the same outline: count disagreements away
    // from the edge (a texel or more from it).
    int wrong = 0;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const std::uint8_t* texel = &rgba[(static_cast<std::size_t>(y) * n + static_cast<std::size_t>(x)) * 4];
            const float m = median(texel);
            const float d = (m - 0.5f) * static_cast<float>(t.range);  // texels, positive inside
            if (std::fabs(d) > 1.0f) {
                // Texel centre in outline units (rows top to bottom).
                const double px = (x + 0.5) / t.scale, py = (n - y - 0.5) / t.scale;
                const double dist = std::hypot(px - c, py - c);
                const bool inside_round = dist < r - 0.3;
                const bool outside_round = dist > r + 0.3;
                if ((d > 0 && outside_round) || (d < 0 && inside_round)) {
                    ++wrong;
                }
            }
        }
    }
    CHECK(wrong == 0);
}

TEST_CASE("msdf: overlapping contours leave no seam inside the shape") {
    // Two squares drawn over each other (a variable font's strokes), and a bar crossing a ring
    // drawn as one contour: inside, every texel is inside, edges inside the shape are ignored.
    moteur::msdf::Outline o = square(1.0, 6.0);
    o.move_to({4.0, 4.0});  // the second square, same direction, overlapping the first
    o.line_to({4.0, 9.0});
    o.line_to({9.0, 9.0});
    o.line_to({9.0, 4.0});
    o.close();
    moteur::msdf::Placement t;
    t.scale = 6.0;
    t.range = 6.0;
    const int n = 60;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(n) * n * 4);
    moteur::msdf::generate(o, t, n, n, rgba.data());
    const auto at = [&](double x, double y) {  // outline coordinates
        const int tx = static_cast<int>(x * t.scale), ty = n - 1 - static_cast<int>(y * t.scale);
        return &rgba[(static_cast<std::size_t>(ty) * n + static_cast<std::size_t>(tx)) * 4];
    };
    // On the edges of each square lying inside the other (x = 4 between y 4 and 6, y = 6 between
    // x 4 and 6): fully inside, as deep as the range allows near there.
    CHECK(median(at(4.0, 5.0)) > 0.75f);
    CHECK(median(at(5.0, 6.0)) > 0.75f);
    CHECK(at(4.0, 5.0)[3] > 190);
    // The real corners stay outside where the shape is not.
    CHECK(median(at(8.0, 2.0)) < 0.25f);
    CHECK(median(at(2.0, 8.0)) < 0.25f);
}

TEST_CASE("text: Inter is read through HarfBuzz (metrics, weight axis, outlines)") {
    moteur::FontFace face(kInter);
    CHECK(face.units_per_em() == 2048);
    CHECK(face.variable_weight());
    CHECK(face.instance_weight(700.0f) == 700);
    CHECK(face.extents().ascender > 0.0f);
    CHECK(face.extents().descender < 0.0f);
    const std::uint32_t a = face.glyph_for(U'A');
    CHECK(a != 0);
    CHECK(face.glyph_for(0x4E2D) == 0);  // no Chinese in Inter
    moteur::msdf::Outline outline;
    CHECK(face.outline(a, 400, outline));
    CHECK_FALSE(outline.empty());
    moteur::msdf::Outline bold;
    CHECK(face.outline(a, 800, bold));
    glm::dvec2 lo, hi, blo, bhi;
    outline.bounds(lo, hi);
    bold.bounds(blo, bhi);
    CHECK((bhi.x - blo.x) > (hi.x - lo.x));  // the bold instance's A is wider
    CHECK_FALSE(face.outline(face.glyph_for(U' '), 400, outline));  // a space has no ink
}

TEST_CASE("text: layout of a Latin text (kerning, wrapping, newlines, alignment, cache)") {
    const Font font = load_stack(false);
    TextStyle style;
    style.size = 32.0f;
    const auto av = font.layout("AV", style);
    const auto aa = font.layout("AA", style);
    REQUIRE(av->glyphs.size() == 2);
    REQUIRE(aa->glyphs.size() == 2);
    CHECK(av->glyphs[1].position.x < aa->glyphs[1].position.x);  // kerned: V comes closer to A

    CHECK(font.layout("AV", style) == av);  // cached: the same layout

    const auto empty = font.layout("", style);
    CHECK(empty->lines.size() == 1);
    CHECK(empty->size.y == doctest::Approx(font.line_height(32.0f)));
    CHECK(font.layout("a\n", style)->lines.size() == 2);
    CHECK(font.layout("a\nb\nc", style)->lines.size() == 3);

    TextStyle wrapped = style;
    wrapped.max_width = 200.0f;
    const auto paragraph = font.layout("Où étaient les œufs d'été ? « Ici, sous le grand chêne. »", wrapped);
    CHECK(paragraph->lines.size() >= 3);
    for (const moteur::TextLine& line : paragraph->lines) {
        CHECK(line.width <= 200.0f + 0.01f);
    }
    CHECK(paragraph->size.x == doctest::Approx(200.0f));
    // The French "?" stays on the line of the word before it.
    for (const moteur::TextLine& line : paragraph->lines) {
        CHECK(paragraph->text[line.first_char] != U'?');
        CHECK(paragraph->text[line.first_char] != U'»');
    }

    TextStyle centered = wrapped;
    centered.align = moteur::TextAlign::Center;
    const auto c = font.layout("Centre", centered);
    CHECK(c->lines[0].x == doctest::Approx((200.0f - c->lines[0].width) * 0.5f));

    // Bold (variable font): wider.
    TextStyle bold = style;
    bold.weight = 700.0f;
    CHECK(font.measure("Épée", bold).x > font.measure("Épée", style).x);
}

TEST_CASE("text: fallback fonts, Arabic joining and bidirectional order") {
    if (!have_noto()) {
        return;
    }
    const Font font = load_stack(true);
    moteur::FontLibrary& library = font.library();
    TextStyle style;
    style.size = 24.0f;

    // A Chinese character is drawn with the third font of the stack.
    const auto chinese = font.layout("Bonjour 中文", style);
    REQUIRE(!chinese->glyphs.empty());
    const std::uint32_t inter = chinese->glyphs.front().face;
    bool found_chinese_face = false;
    for (const moteur::TextGlyph& g : chinese->glyphs) {
        if (chinese->text[g.cluster] == 0x4E2D) {
            CHECK(g.face != inter);
            CHECK(library.face(g.face).glyph_for(0x4E2D) == g.glyph);
            found_chinese_face = true;
        }
    }
    CHECK(found_chinese_face);

    // سلام: four letters, each in its joined form (not the isolated glyph of its code point), and
    // right to left: the first letter is the rightmost glyph.
    const auto arabic = font.layout("\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85", style);
    REQUIRE(arabic->glyphs.size() == 4);
    moteur::FontFace& face = library.face(arabic->glyphs[0].face);
    CHECK(face.path().find("Arabic") != std::string::npos);
    int joined = 0;
    for (const moteur::TextGlyph& g : arabic->glyphs) {
        if (g.glyph != face.glyph_for(arabic->text[g.cluster])) {
            ++joined;
        }
    }
    CHECK(joined >= 3);  // seen, lam: initial and medial forms; alef, meem: final forms
    CHECK(arabic->glyphs.front().cluster == 3);  // leftmost glyph: the last letter
    CHECK(arabic->glyphs.back().cluster == 0);
    CHECK(arabic->lines[0].rtl);

    // French with an Arabic word inside: the Latin reads left to right around the reversed word.
    const auto mixed = font.layout("Le mot \xD8\xB3\xD9\x84\xD8\xA7\xD9\x85 veut dire paix", style);
    CHECK_FALSE(mixed->lines[0].rtl);
    std::vector<std::uint32_t> clusters;
    for (const moteur::TextGlyph& g : mixed->glyphs) {
        clusters.push_back(g.cluster);
    }
    // Glyphs left to right: "Le mot " (clusters 0..5), then the Arabic word from its end (10) to
    // its start (7), then " veut dire paix".
    const auto where = [&](std::uint32_t cluster) {
        return std::find(clusters.begin(), clusters.end(), cluster) - clusters.begin();
    };
    CHECK(where(10) < where(7));
    CHECK(where(5) < where(10));
    CHECK(where(7) < where(12));

    // An Arabic paragraph aligned at its start is on the right of its box.
    TextStyle wide = style;
    wide.max_width = 600.0f;
    const auto right = font.layout("\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85", wide);
    CHECK(right->lines[0].x == doctest::Approx(600.0f - right->lines[0].width));
}

TEST_CASE("text: markup") {
    const moteur::TextMarkup m = moteur::parse_text_markup(
        "<b>\xC3\x89p\xC3\xA9\x65</b> <color=#ff0000>rouge</color> &lt;x&gt; <oops> <i>a<size=2>b</size></i><icon=coin>");
    // Épée rouge <x> <oops> ab￼
    CHECK(m.text == U"Épée rouge <x> <oops> ab￼");
    REQUIRE(m.span_of.size() == m.text.size());
    const moteur::TextSpan& bold = m.spans[m.span_of[0]];
    CHECK(bold.weight.value_or(0.0f) == 700.0f);
    const moteur::TextSpan& red = m.spans[m.span_of[5]];
    REQUIRE(red.color.has_value());
    CHECK(red.color->r == doctest::Approx(1.0f));
    CHECK(red.color->g == doctest::Approx(0.0f));
    CHECK(m.span_of[4] == 0);  // the space between: plain
    const moteur::TextSpan& big_italic = m.spans[m.span_of[m.text.size() - 2]];
    CHECK(big_italic.italic);
    CHECK(big_italic.scale == doctest::Approx(2.0f));
    const moteur::TextSpan& icon = m.spans[m.span_of.back()];
    CHECK(icon.icon == "coin");
    // An unmatched closing tag is dropped.
    CHECK(moteur::parse_text_markup("a</b>c").text == U"ac");
}

TEST_CASE("text: icons take their place in the line") {
    const Font font = load_stack(false);
    TextStyle style;
    style.markup = true;
    style.size = 20.0f;
    const auto layout = font.layout("Prix : 3<icon=coin>", style);
    REQUIRE(layout->icons.size() == 1);
    CHECK(layout->icons[0].name == "coin");
    CHECK(layout->icons[0].size.x == doctest::Approx(20.0f));
    CHECK(layout->size.x >= layout->icons[0].position.x + 20.0f - 0.01f);
}

TEST_CASE("text: the atlas makes glyphs as distance fields, in memory without GPU") {
    const Font font = load_stack(false);
    moteur::FontLibrary& library = font.library();
    moteur::GlyphAtlas& atlas = library.atlas();
    const std::uint32_t face_index = library.face_index(kInter);
    moteur::FontFace& face = library.face(face_index);
    const moteur::GlyphKey key{face_index, face.glyph_for(U'I'), face.instance_weight(400.0f)};
    const moteur::AtlasGlyph* glyph = atlas.find(face, key, 1, true);
    REQUIRE(glyph != nullptr);
    REQUIRE(glyph->cpu_page != nullptr);
    // The plane covers the ink with the range around: "I" stands on the baseline.
    CHECK(glyph->plane.y < 0.0f);
    CHECK(glyph->plane.w > 0.6f);
    // The texel in the middle of the quad is inside the stem.
    const moteur::Image& page = *glyph->cpu_page;
    const int x = static_cast<int>((glyph->uv.x + glyph->uv.z) * 0.5f * static_cast<float>(page.width));
    const int y = static_cast<int>((glyph->uv.y + glyph->uv.w) * 0.5f * static_cast<float>(page.height));
    const std::uint8_t* texel = &page.pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(page.width) +
                                              static_cast<std::size_t>(x)) * 4];
    CHECK(median(texel) > 0.5f);
    CHECK(texel[3] > 128);
    CHECK(atlas.find(face, key, 2) == glyph);  // found again, not made again
    const std::size_t made = atlas.made();
    font.prepare(U"Bonjour");
    CHECK(atlas.made() > made);
    CHECK(atlas.glyph_count() >= 7);
}
