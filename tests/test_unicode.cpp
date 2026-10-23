// The engine's Unicode algorithms against the official conformance tests of the Unicode Character
// Database (milestone 9 bis, part 4): tests/data/unicode/*.txt.zst, written by
// tools/text/make_unicode_tables.py from the same UCD version as the tables.

#include <doctest/doctest.h>
#include <zstd.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "moteur/bidi.hpp"
#include "moteur/unicode.hpp"

using moteur::BidiParagraph;
using moteur::TextDirection;
namespace unicode = moteur::unicode;

namespace {

const std::string kUnicodeData = std::string(MOTEUR_TEST_DATA) + "unicode/";

// The two bidi files hold some 800 000 cases: all of them in an optimized build (the verifier's
// Release run), one in eight in a Debug one (checked iterators make it ten times slower), so that
// the Debug tests stay quick. Every case is still checked by each full run of the verifier.
#ifdef NDEBUG
constexpr int kBidiStride = 1;
#else
constexpr int kBidiStride = 8;
#endif

std::vector<std::string> read_lines(const std::string& name) {
    std::ifstream file(kUnicodeData + name, std::ios::binary);
    REQUIRE_MESSAGE(file, "missing " << kUnicodeData + name);
    const std::string packed((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const unsigned long long size = ZSTD_getFrameContentSize(packed.data(), packed.size());
    REQUIRE(size != ZSTD_CONTENTSIZE_ERROR);
    REQUIRE(size != ZSTD_CONTENTSIZE_UNKNOWN);
    std::string text(static_cast<std::size_t>(size), '\0');
    REQUIRE(ZSTD_decompress(text.data(), text.size(), packed.data(), packed.size()) == size);
    std::vector<std::string> lines;
    std::istringstream stream(text);
    for (std::string line; std::getline(stream, line);) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

// Plain loops rather than string streams: the bidi tests have some 800 000 cases, and streams are
// slow in a Debug build.
std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = text.find(separator, start);
        parts.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) {
            return parts;
        }
        start = end + 1;
    }
}

std::vector<std::string> words(const std::string& text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (text[i] == ' ' || text[i] == '	')) {
            ++i;
        }
        const std::size_t start = i;
        while (i < text.size() && text[i] != ' ' && text[i] != '	') {
            ++i;
        }
        if (i > start) {
            out.push_back(text.substr(start, i - start));
        }
    }
    return out;
}

unicode::BidiClass bidi_class_named(const std::string& name) {
    static const char* const kNames[] = {"L",   "R",   "AL",  "EN",  "ES",  "ET",  "AN",  "CS",
                                         "NSM", "BN",  "B",   "S",   "WS",  "ON",  "LRE", "LRO",
                                         "RLE", "RLO", "PDF", "LRI", "RLI", "FSI", "PDI"};
    for (std::size_t i = 0; i < std::size(kNames); ++i) {
        if (name == kNames[i]) {
            return static_cast<unicode::BidiClass>(i);
        }
    }
    FAIL("unknown bidi class " << name);
    return unicode::BidiClass::L;
}

// Levels after L1 for the whole text as one line, and the visual order without the removed characters.
struct BidiOutcome {
    std::vector<int> levels;  // -1 for removed
    std::vector<std::size_t> order;
    int paragraph_level = 0;
};

BidiOutcome run_bidi(std::span<const unicode::BidiClass> classes, TextDirection direction,
                     std::span<const char32_t> text) {
    const BidiParagraph paragraph = moteur::bidi_resolve(classes, direction, text);
    const std::vector<std::uint8_t> line = moteur::bidi_line_levels(classes, paragraph, 0, classes.size());
    BidiOutcome outcome;
    outcome.paragraph_level = paragraph.base_level;
    for (std::size_t i = 0; i < line.size(); ++i) {
        outcome.levels.push_back(paragraph.removed[i] ? -1 : line[i]);
    }
    for (const std::size_t i : moteur::bidi_visual_order(line)) {
        if (!paragraph.removed[i]) {
            outcome.order.push_back(i);
        }
    }
    return outcome;
}

std::vector<int> parse_levels(const std::string& text) {
    std::vector<int> levels;
    for (const std::string& word : words(text)) {
        levels.push_back(word == "x" ? -1 : std::stoi(word));
    }
    return levels;
}

std::vector<std::size_t> parse_order(const std::string& text) {
    std::vector<std::size_t> order;
    for (const std::string& word : words(text)) {
        order.push_back(static_cast<std::size_t>(std::stoul(word)));
    }
    return order;
}

std::u32string parse_code_points(const std::string& text) {
    std::u32string out;
    for (const std::string& word : words(text)) {
        out.push_back(static_cast<char32_t>(std::stoul(word, nullptr, 16)));
    }
    return out;
}

// LineBreakTest / GraphemeBreakTest: "× 0041 ÷ 0020 × 0042 ÷": the code points, and before each
// one and at the end whether there is a boundary.
struct BreakCase {
    std::u32string text;
    std::vector<bool> boundary;  // text.size() + 1
};

BreakCase parse_break_case(const std::string& line) {
    BreakCase c;
    for (const std::string& word : words(line)) {
        if (word == "\xC3\xB7") {  // ÷
            c.boundary.push_back(true);
        } else if (word == "\xC3\x97") {  // ×
            c.boundary.push_back(false);
        } else {
            c.text.push_back(static_cast<char32_t>(std::stoul(word, nullptr, 16)));
        }
    }
    return c;
}

std::string describe(const std::u32string& text) {
    std::ostringstream out;
    out << std::hex;
    for (const char32_t c : text) {
        out << static_cast<std::uint32_t>(c) << ' ';
    }
    return out.str();
}

}  // namespace

TEST_CASE("unicode: properties of a few code points") {
    CHECK(std::string(unicode::unicode_version()) == "18.0.0");
    CHECK(unicode::char_info(U'A').bidi == unicode::BidiClass::L);
    CHECK(unicode::char_info(0x05D0).bidi == unicode::BidiClass::R);   // Hebrew alef
    CHECK(unicode::char_info(0x0627).bidi == unicode::BidiClass::AL);  // Arabic alef
    CHECK(unicode::char_info(U'1').bidi == unicode::BidiClass::EN);
    CHECK(unicode::char_info(0x0661).bidi == unicode::BidiClass::AN);  // Arabic-Indic one
    CHECK(unicode::char_info(U' ').line == unicode::LineBreakClass::SP);
    CHECK(unicode::char_info(0x00A0).line == unicode::LineBreakClass::GL);
    CHECK(unicode::char_info(0x4E2D).line == unicode::LineBreakClass::ID);  // 中
    CHECK(unicode::char_info(0x4E2D).east_asian);
    CHECK(unicode::char_info(0x0301).category == unicode::GeneralCategory::Mn);
    CHECK(unicode::char_info(0x0301).grapheme == unicode::GraphemeBreak::Extend);
    CHECK(unicode::char_info(0x1F600).extended_pictographic);
    CHECK(std::string(unicode::script_tag(unicode::char_info(U'e').script)) == "Latn");
    CHECK(std::string(unicode::script_tag(unicode::char_info(0x0627).script)) == "Arab");
    CHECK(std::string(unicode::script_tag(unicode::char_info(0x4E2D).script)) == "Hani");
    CHECK(unicode::char_info(U'.').script == unicode::kScriptCommon);
    CHECK(unicode::char_info(0x0301).script == unicode::kScriptInherited);
    CHECK(unicode::char_info(0x10FFFF).category == unicode::GeneralCategory::Cn);
    CHECK(unicode::char_info(0x110000).category == unicode::GeneralCategory::Cn);  // out of range
    bool opening = false;
    CHECK(unicode::paired_bracket(U'(', &opening) == U')');
    CHECK(opening);
    CHECK(unicode::paired_bracket(U']', &opening) == U'[');
    CHECK_FALSE(opening);
    CHECK(unicode::paired_bracket(U'a') == 0);
}

TEST_CASE("unicode: grapheme clusters pass GraphemeBreakTest") {
    int failures = 0;
    int cases = 0;
    for (const std::string& line : read_lines("GraphemeBreakTest.txt.zst")) {
        const BreakCase c = parse_break_case(line);
        std::vector<bool> got(c.text.size() + 1, false);
        for (const std::size_t b : unicode::grapheme_boundaries(c.text)) {
            got[b] = true;
        }
        if (c.text.empty()) {
            continue;
        }
        ++cases;
        if (got != c.boundary) {
            if (++failures <= 10) {
                MESSAGE("grapheme: " << describe(c.text) << " (" << line << ")");
            }
        }
    }
    CHECK(cases > 500);
    CHECK(failures == 0);
}

TEST_CASE("unicode: line breaking passes LineBreakTest") {
    int failures = 0;
    int cases = 0;
    for (const std::string& line : read_lines("LineBreakTest.txt.zst")) {
        const BreakCase c = parse_break_case(line);
        const std::vector<unicode::LineBreak> breaks = unicode::line_breaks(c.text);
        std::vector<bool> got(breaks.size());
        for (std::size_t i = 0; i < breaks.size(); ++i) {
            got[i] = breaks[i] != unicode::LineBreak::None;
        }
        ++cases;
        if (got != c.boundary) {
            if (++failures <= 15) {
                std::string marks;
                for (std::size_t i = 0; i < got.size(); ++i) {
                    marks += got[i] ? "/ " : "x ";
                }
                MESSAGE("line break: " << line << "   got: " << marks);
            }
        }
    }
    CHECK(cases > 19000);
    CHECK(failures == 0);
}

TEST_CASE("unicode: the bidirectional algorithm passes BidiTest") {
    int failures = 0;
    int cases = 0;
    std::vector<int> levels;
    std::vector<std::size_t> order;
    int line_number = 0;
    for (const std::string& line : read_lines("BidiTest.txt.zst")) {
        if (line.rfind("@Levels:", 0) == 0) {
            levels = parse_levels(line.substr(8));
            continue;
        }
        if (line.rfind("@Reorder:", 0) == 0) {
            order = parse_order(line.substr(9));
            continue;
        }
        if (line[0] == '@' || (++line_number % kBidiStride) != 0) {
            continue;
        }
        const std::vector<std::string> fields = split(line, ';');
        if (fields.size() != 2) {
            FAIL("BidiTest: bad line " << line);
        }
        std::vector<unicode::BidiClass> classes;
        for (const std::string& name : words(fields[0])) {
            classes.push_back(bidi_class_named(name));
        }
        const int bitset = std::stoi(fields[1]);
        for (const auto& [bit, direction] : {std::pair{1, TextDirection::Auto}, std::pair{2, TextDirection::LeftToRight},
                                            std::pair{4, TextDirection::RightToLeft}}) {
            if ((bitset & bit) == 0) {
                continue;
            }
            ++cases;
            const BidiOutcome outcome = run_bidi(classes, direction, {});
            if (outcome.levels != levels || outcome.order != order) {
                if (++failures <= 10) {
                    std::ostringstream got;
                    for (const int l : outcome.levels) got << (l < 0 ? std::string("x") : std::to_string(l)) << ' ';
                    got << "| ";
                    for (const std::size_t o : outcome.order) got << o << ' ';
                    MESSAGE("BidiTest: " << fields[0] << " dir bit " << bit << "  got " << got.str());
                }
            }
        }
    }
    CHECK(cases > 700000 / kBidiStride);
    CHECK(failures == 0);
}

TEST_CASE("unicode: the bidirectional algorithm passes BidiCharacterTest") {
    int failures = 0;
    int cases = 0;
    int line_number = 0;
    for (const std::string& line : read_lines("BidiCharacterTest.txt.zst")) {
        if ((++line_number % kBidiStride) != 0) {
            continue;
        }
        const std::vector<std::string> fields = split(line, ';');
        if (fields.size() != 5) {
            FAIL("BidiCharacterTest: bad line " << line);
        }
        const std::u32string text = parse_code_points(fields[0]);
        const int dir = std::stoi(fields[1]);
        const TextDirection direction =
            dir == 0 ? TextDirection::LeftToRight : (dir == 1 ? TextDirection::RightToLeft : TextDirection::Auto);
        std::vector<unicode::BidiClass> classes;
        for (const char32_t c : text) {
            classes.push_back(unicode::char_info(c).bidi);
        }
        ++cases;
        const BidiOutcome outcome = run_bidi(classes, direction, text);
        const bool ok = outcome.paragraph_level == std::stoi(fields[2]) && outcome.levels == parse_levels(fields[3]) &&
                        outcome.order == parse_order(fields[4]);
        if (!ok) {
            if (++failures <= 10) {
                std::ostringstream got;
                got << outcome.paragraph_level << " | ";
                for (const int l : outcome.levels) got << (l < 0 ? std::string("x") : std::to_string(l)) << ' ';
                got << "| ";
                for (const std::size_t o : outcome.order) got << o << ' ';
                MESSAGE("BidiCharacterTest: " << line << "   got " << got.str());
            }
        }
    }
    CHECK(cases > 90000 / kBidiStride);
    CHECK(failures == 0);
}

TEST_CASE("unicode: French, Arabic and Chinese lines break where a reader expects") {
    // A French sentence: breaks after the spaces only, the no-break space before ':' holds.
    const std::u32string french = U"Vous avez trouvé : une épée";
    const std::vector<unicode::LineBreak> b = unicode::line_breaks(french);
    CHECK(b[5] == unicode::LineBreak::Allowed);  // "Vous |avez"
    CHECK(b[16] == unicode::LineBreak::None);     // "trouvé| "
    CHECK(b[17] == unicode::LineBreak::None);     // " |:"
    CHECK(b[2] == unicode::LineBreak::None);
    // Chinese: between ideographs, but not before the closing punctuation.
    const std::u32string chinese = U"中文。好";
    const std::vector<unicode::LineBreak> c = unicode::line_breaks(chinese);
    CHECK(c[1] == unicode::LineBreak::Allowed);
    CHECK(c[2] == unicode::LineBreak::None);  // before 。
    CHECK(c[3] == unicode::LineBreak::Allowed);
    // A newline is a mandatory break.
    const std::vector<unicode::LineBreak> n = unicode::line_breaks(U"a\nb");
    CHECK(n[2] == unicode::LineBreak::Mandatory);
}

TEST_CASE("bidi: a French sentence with an Arabic word reads in the right order") {
    // "Le mot سلام veut dire paix": the Arabic word is reversed in place, the rest stays.
    const std::u32string text = U"Le mot سلام veut dire paix";
    const BidiParagraph p = moteur::bidi_paragraph(text, TextDirection::Auto);
    CHECK(p.base_level == 0);
    const std::vector<std::size_t> order = moteur::bidi_visual_order(moteur::bidi_line_levels(text, p, 0, text.size()));
    REQUIRE(order.size() == text.size());
    CHECK(order[7] == 10);  // the last Arabic letter shows first, at the word's place
    CHECK(order[10] == 7);
    CHECK(order[0] == 0);
    CHECK(order.back() == text.size() - 1);
    // In an Arabic paragraph, a number keeps its digits left to right.
    const std::u32string arabic = U"العدد 123";
    const BidiParagraph q = moteur::bidi_paragraph(arabic, TextDirection::Auto);
    CHECK(q.base_level == 1);
    const std::vector<std::size_t> visual = moteur::bidi_visual_order(moteur::bidi_line_levels(arabic, q, 0, arabic.size()));
    CHECK(visual[0] == 6);  // "123" first, left to right
    CHECK(visual[1] == 7);
    CHECK(visual[2] == 8);
    CHECK(visual.back() == 0);
}
