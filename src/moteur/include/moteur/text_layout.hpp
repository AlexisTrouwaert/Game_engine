#pragma once

#include <functional>
#include <string>
#include <vector>

namespace moteur {

enum class TextAlign { Left, Center, Right };

// One glyph on a line, already positioned. `x` includes the alignment offset; `y` is the top of
// its line (0 for the first line, line_height for the second, and so on). It does not include the
// glyph's own bearing or the font's ascent: that belongs to whoever actually draws the glyph,
// since layout only needs advances and kerning, not bitmaps.
struct GlyphPlacement {
    char32_t codepoint;
    float x;
    float y;
};

struct TextLine {
    std::vector<GlyphPlacement> glyphs;
    float width = 0.0f;  // natural width of this line, before any alignment offset
};

struct TextLayoutResult {
    std::vector<TextLine> lines;
};

using AdvanceFn = std::function<float(char32_t)>;
using KerningFn = std::function<float(char32_t, char32_t)>;

// Lays out already-decoded text: word wrap, alignment, explicit newlines. Pure position logic,
// with no font or GPU involved, so it can be tested with made-up metrics.
//
// advance_of(codepoint) gives how far the pen moves after that glyph, in pixels; an unknown
// codepoint should still return a sensible width (0 is fine, the glyph will just overlap the next
// one when drawn, since layout does not know which codepoints exist in a font).
// kerning_of(a, b) gives the extra spacing (possibly negative) applied between consecutive glyphs
// a then b.
//
// max_width <= 0 disables wrapping: the text is only cut at explicit '\n'. A single word longer
// than max_width is not split (it overflows its line) rather than broken mid-word.
//
// A space immediately followed by one of : ; ! ? stays attached to the word before it (a
// simplified version of the French non-breaking space rule), so a line never starts with one of
// these marks.
TextLayoutResult layout_text(const std::u32string& text, float line_height, float max_width, TextAlign align,
                             const AdvanceFn& advance_of, const KerningFn& kerning_of);

}  // namespace moteur
