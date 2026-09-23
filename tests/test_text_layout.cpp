#include <doctest/doctest.h>

#include "moteur/text_layout.hpp"

using moteur::layout_text;
using moteur::TextAlign;
using moteur::TextLayoutResult;

namespace {

// A fake monospace font: every code point, including the space, advances by 10 pixels, and there
// is no kerning. This makes line widths and wrap points exact round numbers, easy to reason about
// without needing a real font.
constexpr float kAdvance = 10.0f;
float advance_of(char32_t) { return kAdvance; }
float kerning_of(char32_t, char32_t) { return 0.0f; }

TextLayoutResult layout(const std::u32string& text, float line_height = 10.0f, float max_width = 0.0f,
                       TextAlign align = TextAlign::Left) {
    return layout_text(text, line_height, max_width, align, advance_of, kerning_of);
}

}  // namespace

TEST_CASE("a short text with no wrapping is a single line") {
    const TextLayoutResult result = layout(U"abc");
    REQUIRE(result.lines.size() == 1);
    REQUIRE(result.lines[0].glyphs.size() == 3);
    CHECK(result.lines[0].width == 30.0f);
    CHECK((result.lines[0].glyphs[0].x == 0.0f && result.lines[0].glyphs[1].x == 10.0f &&
          result.lines[0].glyphs[2].x == 20.0f));
}

TEST_CASE("empty text still produces one, empty line") {
    const TextLayoutResult result = layout(U"");
    REQUIRE(result.lines.size() == 1);
    CHECK(result.lines[0].glyphs.empty());
    CHECK(result.lines[0].width == 0.0f);
}

TEST_CASE("an explicit newline starts a new line") {
    const TextLayoutResult result = layout(U"ab\ncd", 10.0f);
    REQUIRE(result.lines.size() == 2);
    CHECK(result.lines[0].width == 20.0f);
    CHECK(result.lines[1].width == 20.0f);
    for (const auto& g : result.lines[0].glyphs) CHECK(g.y == 0.0f);
    for (const auto& g : result.lines[1].glyphs) CHECK(g.y == 10.0f);
}

TEST_CASE("consecutive spaces collapse to a single separator") {
    const TextLayoutResult result = layout(U"a    b");  // 4 spaces
    REQUIRE(result.lines.size() == 1);
    // "a" (10) + one space (10) + "b" (10) = 30, not 10 + 40 + 10.
    CHECK(result.lines[0].width == 30.0f);
}

TEST_CASE("word wrap breaks a line once the next word would overflow it") {
    // Three words of width 20 each, one space (10) between: "aa"(20) + " "(10) + "bb"(20) = 50.
    const TextLayoutResult result = layout(U"aa bb cc", 10.0f, 45.0f);
    REQUIRE(result.lines.size() == 3);
    CHECK((result.lines[0].width == 20.0f && result.lines[1].width == 20.0f && result.lines[2].width == 20.0f));
}

TEST_CASE("a word is kept on the current line as long as it still fits exactly") {
    // "aa bb" is exactly 50 wide; max_width 50 must not force a wrap.
    const TextLayoutResult result = layout(U"aa bb cc", 10.0f, 50.0f);
    REQUIRE(result.lines.size() == 2);
    CHECK(result.lines[0].width == 50.0f);  // "aa bb"
    CHECK(result.lines[1].width == 20.0f);  // "cc"
}

TEST_CASE("a lone punctuation mark never starts a line: it stays glued to the word before it") {
    // Without the rule, a naive wrapper would put "mot" (30) on one line and ":" alone on the
    // next once "suite" no longer fits; the rule merges "mot" and ":" into a single unbreakable
    // token first, so that split can no longer happen.
    const TextLayoutResult result = layout(U"mot : suite", 10.0f, 35.0f);
    REQUIRE(result.lines.size() == 2);
    REQUIRE(result.lines[0].glyphs.size() == 5);  // 'm' 'o' 't' ' ' ':'
    const std::u32string first_line = {result.lines[0].glyphs[0].codepoint, result.lines[0].glyphs[1].codepoint,
                                       result.lines[0].glyphs[2].codepoint, result.lines[0].glyphs[3].codepoint,
                                       result.lines[0].glyphs[4].codepoint};
    CHECK(first_line == U"mot :");
    REQUIRE(result.lines[1].glyphs.size() == 5);
    CHECK(result.lines[1].glyphs[0].codepoint == U's');
}

TEST_CASE("other punctuation is not glued: a line may still start with a plain word") {
    const TextLayoutResult result = layout(U"aa, bb", 10.0f, 25.0f);
    REQUIRE(result.lines.size() == 2);
    CHECK(result.lines[0].glyphs.size() == 3);  // "aa,"
    CHECK(result.lines[1].glyphs.size() == 2);  // "bb"
}

TEST_CASE("a single word longer than max_width overflows its own line instead of being cut") {
    const TextLayoutResult result = layout(U"aaaaaaaaaa", 10.0f, 30.0f);  // 10 letters, width 100
    REQUIRE(result.lines.size() == 1);
    CHECK(result.lines[0].width == 100.0f);
}

TEST_CASE("center alignment splits the leftover space evenly") {
    const TextLayoutResult result = layout(U"ab", 10.0f, 100.0f, TextAlign::Center);
    REQUIRE(result.lines[0].glyphs.size() == 2);
    CHECK(result.lines[0].glyphs[0].x == 40.0f);
    CHECK(result.lines[0].glyphs[1].x == 50.0f);
}

TEST_CASE("right alignment pushes the line to the far edge") {
    const TextLayoutResult result = layout(U"ab", 10.0f, 100.0f, TextAlign::Right);
    CHECK(result.lines[0].glyphs[0].x == 80.0f);
    CHECK(result.lines[0].glyphs[1].x == 90.0f);
}

TEST_CASE("left alignment (the default) does not move anything") {
    const TextLayoutResult result = layout(U"ab", 10.0f, 100.0f, TextAlign::Left);
    CHECK(result.lines[0].glyphs[0].x == 0.0f);
}

TEST_CASE("without a max width, alignment uses the widest line as the box") {
    const TextLayoutResult result = layout(U"a\nabc", 10.0f, 0.0f, TextAlign::Right);
    REQUIRE(result.lines.size() == 2);
    // Box width = 30 (the widest line, "abc"). Line "a" (width 10) is pushed by 20.
    CHECK(result.lines[0].glyphs[0].x == 20.0f);
    CHECK(result.lines[1].glyphs[0].x == 0.0f);
}

TEST_CASE("reported line width is the natural width, unaffected by alignment") {
    const TextLayoutResult result = layout(U"ab", 10.0f, 100.0f, TextAlign::Center);
    CHECK(result.lines[0].width == 20.0f);
}
