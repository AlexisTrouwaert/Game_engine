#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "moteur/unicode.hpp"

/// The Unicode Bidirectional Algorithm (UAX #9, milestone 9 bis part 4): Arabic or Hebrew mixed
/// with Latin text, numbers inside right-to-left text, explicit embeddings and isolates, paired
/// brackets. Written in the engine and checked against the official conformance tests
/// (tests/test_unicode.cpp).
///
/// ```
/// const BidiParagraph p = bidi_paragraph(text, TextDirection::Auto);  // levels, rules up to I2
/// // ... break the paragraph into lines (line_breaks(), widths) ...
/// const std::vector<std::uint8_t> levels = bidi_line_levels(text, p, first, last);  // rule L1
/// const std::vector<std::size_t> order = bidi_visual_order(levels);                  // rule L2
/// ```
///
/// Pure computation, callable from any thread.
namespace moteur {

/// The direction of a paragraph.
enum class TextDirection : std::uint8_t {
    Auto,         ///< from its first strong character (rules P2, P3), left to right if it has none
    LeftToRight,  ///< forced (rule HL1): an interface in a left-to-right language
    RightToLeft,  ///< forced (rule HL1): an interface in Arabic or Hebrew
};

/// The resolved levels of one paragraph: even levels read left to right, odd ones right to left.
struct BidiParagraph {
    std::uint8_t base_level = 0;  ///< 0 or 1: the paragraph's direction
    /// One level per character (rules X1 to I2). The characters rule X9 removes (embedding
    /// controls, BN) get the level of the character before them (or the base level), so that
    /// they stay with their neighbours; `removed` marks them.
    std::vector<std::uint8_t> levels;
    std::vector<bool> removed;  ///< per character: removed by rule X9 (zero width, never shown)
};

/// Rules P2 to I2 on one paragraph given by its Bidi classes. `text` (optional, same length) gives
/// the code points, for the paired brackets of rule N0 (unicode::paired_bracket()); without it no
/// bracket pairs are made. The text must not contain a paragraph separator except as its last
/// character (rule P1 is the caller's job).
BidiParagraph bidi_resolve(std::span<const unicode::BidiClass> classes, TextDirection direction,
                           std::span<const char32_t> text = {});

/// Rules P2 to I2 on one paragraph of text (its classes and brackets from the Unicode tables).
BidiParagraph bidi_paragraph(std::u32string_view paragraph, TextDirection direction);

/// The paragraph level rules P2 and P3 give (0 or 1), or -1 if the text has no strong character.
int bidi_first_strong_level(std::u32string_view text);

/// Rule L1 for one line [first, last) of a resolved paragraph: the levels of that line, segment
/// separators, trailing whitespace and isolate controls (and the X9-removed characters among them)
/// put back to the paragraph level. `text` gives the original classes (the same text as given to
/// bidi_paragraph()).
std::vector<std::uint8_t> bidi_line_levels(std::u32string_view text, const BidiParagraph& paragraph, std::size_t first,
                                           std::size_t last);
/// Rule L1 from classes (the conformance tests' form).
std::vector<std::uint8_t> bidi_line_levels(std::span<const unicode::BidiClass> classes, const BidiParagraph& paragraph,
                                           std::size_t first, std::size_t last);

/// Rule L2: the visual order of a line, left to right, as indices into `levels` (each run of
/// level >= k reversed, from the highest level down to the lowest odd one).
std::vector<std::size_t> bidi_visual_order(std::span<const std::uint8_t> levels);

/// true if `level` reads right to left (odd).
constexpr bool is_rtl_level(std::uint8_t level) {
    return (level & 1u) != 0;
}

}  // namespace moteur
