#pragma once

#include <functional>
#include <string>
#include <vector>

namespace moteur {

/// Horizontal alignment of each line inside the text's box. The box is `max_width` wide, or as
/// wide as the widest line if that is wider (or if wrapping is off).
enum class TextAlign {
    Left,    ///< lines start at x = 0
    Center,  ///< lines are centered in the box
    Right,   ///< lines end at the right edge of the box
};

/// One glyph on a line, already positioned. `x` includes the alignment offset; `y` is the top of
/// its line (0 for the first line, line_height for the second, and so on). It does not include the
/// glyph's own bearing or the font's ascent: that belongs to whoever actually draws the glyph,
/// since layout only needs advances and kerning, not bitmaps.
struct GlyphPlacement {
    char32_t codepoint;  ///< the character (spaces between words are not listed, only skipped)
    float x;             ///< pen position, in pixels from the left of the box
    float y;             ///< top of the line, in pixels from the top of the box
};

/// One line of laid-out text.
struct TextLine {
    std::vector<GlyphPlacement> glyphs;  ///< in reading order; empty for a blank line
    float width = 0.0f;  ///< natural width of this line, before any alignment offset
};

/// The output of layout_text(): always at least one line, even for an empty text.
struct TextLayoutResult {
    std::vector<TextLine> lines;  ///< top to bottom; the text's height is `lines.size() * line_height`
};

/// How far the pen moves after a glyph, in pixels (see layout_text()).
using AdvanceFn = std::function<float(char32_t)>;
/// Extra spacing between two consecutive glyphs, in pixels, possibly negative (see layout_text()).
using KerningFn = std::function<float(char32_t, char32_t)>;

/// Lays out already-decoded text: word wrap, alignment, explicit newlines. Pure position logic,
/// with no font or GPU involved, so it can be tested with made-up metrics.
///
/// advance_of(codepoint) gives how far the pen moves after that glyph, in pixels; an unknown
/// codepoint should still return a sensible width (0 is fine, the glyph will just overlap the next
/// one when drawn, since layout does not know which codepoints exist in a font).
/// kerning_of(a, b) gives the extra spacing (possibly negative) applied between consecutive glyphs
/// a then b.
///
/// max_width <= 0 disables wrapping: the text is only cut at explicit `'\n'`. A single word longer
/// than max_width is not split (it overflows its line) rather than broken mid-word.
///
/// Words are separated by U+0020 spaces only; a run of spaces counts as one (the advance of
/// `U' '`), and spaces at the start or end of a line take no room. Other whitespace (tabs,
/// no-break spaces) is treated as part of a word.
///
/// A space immediately followed by one of : ; ! ? stays attached to the word before it (a
/// simplified version of the French non-breaking space rule), so a line never starts with one of
/// these marks.
///
/// Pure function, callable from any thread as long as the two callbacks are; it allocates the
/// result and is O(length of the text).
TextLayoutResult layout_text(const std::u32string& text, float line_height, float max_width, TextAlign align,
                             const AdvanceFn& advance_of, const KerningFn& kerning_of);

}  // namespace moteur
