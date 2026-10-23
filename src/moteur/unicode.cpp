#include "moteur/unicode.hpp"

#include <algorithm>
#include <array>

namespace moteur::unicode {

namespace {

#include "moteur/unicode_tables.inc"

constexpr char32_t kMaxCodePoint = 0x10FFFF;
constexpr char32_t kDottedCircle = 0x25CC;

const UnicodeRecord& record_of(char32_t code_point) {
    if (code_point > kMaxCodePoint) {
        code_point = 0xE0080;  // an unassigned code point of plane 14: the defaults of the tables
    }
    const std::uint32_t block = kStage1[code_point >> kBlockShift];
    return kRecords[kStage2[(block << kBlockShift) | (code_point & ((1u << kBlockShift) - 1u))]];
}

}  // namespace

CharInfo char_info(char32_t code_point) {
    const UnicodeRecord& r = record_of(code_point);
    CharInfo info;
    info.bidi = static_cast<BidiClass>(r.bidi);
    info.line = static_cast<LineBreakClass>(r.line);
    info.category = static_cast<GeneralCategory>(r.category);
    info.grapheme = static_cast<GraphemeBreak>(r.grapheme);
    info.conjunct = static_cast<ConjunctBreak>(r.flags & 3u);
    info.east_asian = (r.flags & 4u) != 0;
    info.extended_pictographic = (r.flags & 8u) != 0;
    info.script = r.script;
    return info;
}

const char* script_tag(std::uint8_t script) {
    return script < std::size(kScriptTags) ? kScriptTags[script] : "Zzzz";
}

const char* unicode_version() {
    return kUnicodeVersion;
}

char32_t paired_bracket(char32_t code_point, bool* opening) {
    const auto* end = std::end(kBrackets);
    const auto* it = std::lower_bound(std::begin(kBrackets), end, code_point,
                                      [](const BracketPair& pair, char32_t cp) { return pair.code_point < cp; });
    if (it == end || it->code_point != code_point) {
        return 0;
    }
    if (opening != nullptr) {
        *opening = it->opening;
    }
    return it->pair;
}

// --- Grapheme clusters (UAX #29, section 3.1.1) --------------------------------------------------

std::vector<std::size_t> grapheme_boundaries(std::u32string_view text) {
    std::vector<std::size_t> boundaries;
    boundaries.push_back(0);
    if (text.empty()) {
        return boundaries;
    }

    // State carried along the text for the rules that look further back than one character.
    int regional_run = 0;          // GB12/13: regional indicators in a row just before
    bool pictographic_extend = false;  // GB11: Extended_Pictographic Extend* just before
    bool pictographic_zwj = false;     // GB11: ... followed by a ZWJ just before
    // GB9c (the form of Unicode 18): a Linker, then only Extend characters, just before.
    bool linker_extend = false;

    CharInfo previous = char_info(text[0]);
    const auto update_state = [&](const CharInfo& info) {
        regional_run = info.grapheme == GraphemeBreak::RegionalIndicator ? regional_run + 1 : 0;
        if (info.extended_pictographic) {
            pictographic_extend = true;
            pictographic_zwj = false;
        } else if (info.grapheme == GraphemeBreak::Extend && pictographic_extend && !pictographic_zwj) {
            // stays: Extended_Pictographic Extend*
        } else if (info.grapheme == GraphemeBreak::ZWJ && pictographic_extend && !pictographic_zwj) {
            pictographic_zwj = true;
        } else {
            pictographic_extend = false;
            pictographic_zwj = false;
        }
        if (info.conjunct == ConjunctBreak::Linker) {
            linker_extend = true;
        } else if (info.conjunct != ConjunctBreak::Extend) {
            linker_extend = false;
        }
    };
    update_state(previous);

    for (std::size_t i = 1; i < text.size(); ++i) {
        const CharInfo current = char_info(text[i]);
        const GraphemeBreak a = previous.grapheme;
        const GraphemeBreak b = current.grapheme;
        bool join = false;
        if (a == GraphemeBreak::CR && b == GraphemeBreak::LF) {
            join = true;  // GB3
        } else if (a == GraphemeBreak::Control || a == GraphemeBreak::CR || a == GraphemeBreak::LF ||
                   b == GraphemeBreak::Control || b == GraphemeBreak::CR || b == GraphemeBreak::LF) {
            join = false;  // GB4, GB5
        } else if (a == GraphemeBreak::L &&
                   (b == GraphemeBreak::L || b == GraphemeBreak::V || b == GraphemeBreak::LV || b == GraphemeBreak::LVT)) {
            join = true;  // GB6
        } else if ((a == GraphemeBreak::LV || a == GraphemeBreak::V) && (b == GraphemeBreak::V || b == GraphemeBreak::T)) {
            join = true;  // GB7
        } else if ((a == GraphemeBreak::LVT || a == GraphemeBreak::T) && b == GraphemeBreak::T) {
            join = true;  // GB8
        } else if (b == GraphemeBreak::Extend || b == GraphemeBreak::ZWJ) {
            join = true;  // GB9
        } else if (b == GraphemeBreak::SpacingMark) {
            join = true;  // GB9a
        } else if (a == GraphemeBreak::Prepend) {
            join = true;  // GB9b
        } else if (linker_extend && current.conjunct == ConjunctBreak::Consonant) {
            join = true;  // GB9c
        } else if (pictographic_zwj && current.extended_pictographic) {
            join = true;  // GB11
        } else if (a == GraphemeBreak::RegionalIndicator && b == GraphemeBreak::RegionalIndicator) {
            join = (regional_run % 2) == 1;  // GB12, GB13
        }
        if (!join) {
            boundaries.push_back(i);
        }
        update_state(current);
        previous = current;
    }
    boundaries.push_back(text.size());
    return boundaries;
}

// --- Line breaking (UAX #14, section 6) ----------------------------------------------------------

namespace {

using LB = LineBreakClass;

// LB1: the classes the default rules resolve before anything else.
LB resolve_class(const CharInfo& info) {
    switch (info.line) {
        case LB::AI:
        case LB::SG:
        case LB::XX: return LB::AL;
        case LB::SA:
            return info.category == GeneralCategory::Mn || info.category == GeneralCategory::Mc ? LB::CM : LB::AL;
        case LB::CJ: return LB::NS;
        default: return info.line;
    }
}

bool is_hard_break(LB c) {
    return c == LB::BK || c == LB::CR || c == LB::LF || c == LB::NL;
}

// What the rules after LB9 see: a base character with the combining marks (and ZWJ) it took.
struct Item {
    LB cls;
    GeneralCategory category;
    bool east_asian;
    bool pictographic;
    char32_t base;
};

template <typename... Classes>
bool any_of(LB c, Classes... classes) {
    return ((c == classes) || ...);
}

}  // namespace

std::vector<LineBreak> line_breaks(std::u32string_view text) {
    const std::size_t n = text.size();
    std::vector<LineBreak> breaks(n + 1, LineBreak::None);
    if (n == 0) {
        breaks[0] = LineBreak::Mandatory;  // LB3 wins for an empty text: one (empty) line
        return breaks;
    }
    breaks[n] = LineBreak::Mandatory;  // LB3

    std::vector<LB> classes(n);
    std::vector<CharInfo> infos(n);
    for (std::size_t i = 0; i < n; ++i) {
        infos[i] = char_info(text[i]);
        classes[i] = resolve_class(infos[i]);
    }

    // LB9 and LB10: group each base with its marks. item_of[i]: the item character i belongs to;
    // items[k]: what the later rules see of it.
    std::vector<Item> items;
    std::vector<std::size_t> item_start;
    std::vector<std::size_t> item_of(n);
    std::vector<bool> absorbed(n, false);
    for (std::size_t i = 0; i < n; ++i) {
        const LB c = classes[i];
        const bool mark = c == LB::CM || c == LB::ZWJ;
        if (mark && !items.empty()) {
            const LB base = classes[item_start.back()];
            const bool base_takes_marks = !(is_hard_break(base) || base == LB::SP || base == LB::ZW);
            if (base_takes_marks) {
                absorbed[i] = true;
                item_of[i] = items.size() - 1;
                continue;
            }
        }
        Item item;
        if (mark) {  // LB10: a mark left alone is a letter A
            item = {LB::AL, GeneralCategory::Lu, false, false, U'A'};
        } else {
            item = {c, infos[i].category, infos[i].east_asian, infos[i].extended_pictographic, text[i]};
        }
        item_of[i] = items.size();
        items.push_back(item);
        item_start.push_back(i);
    }

    const auto cls = [&](std::ptrdiff_t k) { return items[static_cast<std::size_t>(k)].cls; };
    const auto count = static_cast<std::ptrdiff_t>(items.size());
    const auto is_aksara = [&](std::ptrdiff_t k) {
        return k >= 0 && k < count && (any_of(cls(k), LB::AK, LB::AS) || items[static_cast<std::size_t>(k)].base == kDottedCircle);
    };
    // The item before k skipping spaces (LB14, LB15a, LB16, LB17), or -1.
    const auto before_spaces = [&](std::ptrdiff_t k) {
        std::ptrdiff_t j = k - 1;
        while (j >= 0 && cls(j) == LB::SP) {
            --j;
        }
        return j;
    };

    for (std::size_t i = 1; i < n; ++i) {
        const LB a = classes[i - 1];
        const LB b = classes[i];
        LineBreak result = LineBreak::Allowed;

        // Non-tailorable rules, on characters.
        if (a == LB::BK) {
            breaks[i] = LineBreak::Mandatory;  // LB4
            continue;
        }
        if (a == LB::CR && b == LB::LF) {
            breaks[i] = LineBreak::None;  // LB5
            continue;
        }
        if (a == LB::CR || a == LB::LF || a == LB::NL) {
            breaks[i] = LineBreak::Mandatory;  // LB5
            continue;
        }
        if (is_hard_break(b) || b == LB::SP || b == LB::ZW) {
            breaks[i] = LineBreak::None;  // LB6, LB7
            continue;
        }
        {
            std::ptrdiff_t j = static_cast<std::ptrdiff_t>(i) - 1;
            while (j >= 0 && classes[static_cast<std::size_t>(j)] == LB::SP) {
                --j;
            }
            if (j >= 0 && classes[static_cast<std::size_t>(j)] == LB::ZW) {
                breaks[i] = LineBreak::Allowed;  // LB8
                continue;
            }
        }
        if (a == LB::ZWJ) {
            breaks[i] = LineBreak::None;  // LB8a
            continue;
        }
        if (absorbed[i]) {
            breaks[i] = LineBreak::None;  // LB9
            continue;
        }

        // The other rules, on items: x before the boundary, y after it.
        const auto k = static_cast<std::ptrdiff_t>(item_of[i]);
        const LB x = cls(k - 1);
        const LB y = cls(k);
        const Item& xi = items[static_cast<std::size_t>(k - 1)];
        const Item& yi = items[static_cast<std::size_t>(k)];
        const LB next = k + 1 < count ? cls(k + 1) : LB::XX;
        const bool has_next = k + 1 < count;
        const auto none = [&] { result = LineBreak::None; };
        const std::ptrdiff_t ns = before_spaces(k);  // the item before the spaces before the boundary

        if (x == LB::WJ || y == LB::WJ) {
            none();  // LB11
        } else if (x == LB::GL) {
            none();  // LB12
        } else if (y == LB::GL && !any_of(x, LB::SP, LB::HY, LB::HH)) {
            none();  // LB12a
        } else if (any_of(y, LB::CL, LB::CP, LB::EX, LB::SY)) {
            none();  // LB13
        } else if (ns >= 0 && cls(ns) == LB::OP) {
            none();  // LB14
        } else if (ns >= 0 && cls(ns) == LB::QU && items[static_cast<std::size_t>(ns)].category == GeneralCategory::Pi &&
                   (ns == 0 || any_of(cls(ns - 1), LB::BK, LB::CR, LB::LF, LB::NL, LB::OP, LB::QU, LB::GL, LB::SP, LB::ZW))) {
            none();  // LB15a
        } else if (y == LB::QU && yi.category == GeneralCategory::Pf &&
                   (!has_next || any_of(next, LB::SP, LB::GL, LB::WJ, LB::CL, LB::QU, LB::CP, LB::EX, LB::IS, LB::SY,
                                        LB::BK, LB::CR, LB::LF, LB::NL, LB::ZW))) {
            none();  // LB15b
        } else if (x == LB::SP && y == LB::IS && has_next && next == LB::NU) {
            result = LineBreak::Allowed;  // LB15c
        } else if (y == LB::IS) {
            none();  // LB15d
        } else if (y == LB::NS && ns >= 0 && any_of(cls(ns), LB::CL, LB::CP)) {
            none();  // LB16
        } else if (y == LB::B2 && ns >= 0 && cls(ns) == LB::B2) {
            none();  // LB17
        } else if (x == LB::SP) {
            result = LineBreak::Allowed;  // LB18
        } else if ((y == LB::QU && yi.category != GeneralCategory::Pi) ||
                   (x == LB::QU && xi.category != GeneralCategory::Pf)) {
            none();  // LB19
        } else if ((y == LB::QU && !xi.east_asian) ||
                   (y == LB::QU && (!has_next || !items[static_cast<std::size_t>(k + 1)].east_asian)) ||
                   (x == LB::QU && !yi.east_asian) ||
                   (x == LB::QU && (k - 1 == 0 || !items[static_cast<std::size_t>(k - 2)].east_asian))) {
            none();  // LB19a
        } else if (x == LB::CB || y == LB::CB) {
            result = LineBreak::Allowed;  // LB20
        } else if (any_of(x, LB::HY, LB::HH) && any_of(y, LB::AL, LB::HL) &&
                   (k - 1 == 0 || any_of(cls(k - 2), LB::BK, LB::CR, LB::LF, LB::NL, LB::SP, LB::ZW, LB::CB, LB::GL))) {
            none();  // LB20a
        } else if (any_of(y, LB::BA, LB::HH, LB::HY, LB::NS) || x == LB::BB) {
            none();  // LB21
        } else if (any_of(x, LB::HY, LB::HH) && k >= 2 && cls(k - 2) == LB::HL && y != LB::HL) {
            none();  // LB21a
        } else if (x == LB::SY && y == LB::HL) {
            none();  // LB21b
        } else if (y == LB::IN_) {
            none();  // LB22
        } else if ((any_of(x, LB::AL, LB::HL) && y == LB::NU) || (x == LB::NU && any_of(y, LB::AL, LB::HL))) {
            none();  // LB23
        } else if ((x == LB::PR && any_of(y, LB::ID, LB::EB, LB::EM)) || (any_of(x, LB::ID, LB::EB, LB::EM) && y == LB::PO)) {
            none();  // LB23a
        } else if ((any_of(x, LB::PR, LB::PO) && any_of(y, LB::AL, LB::HL)) ||
                   (any_of(x, LB::AL, LB::HL) && any_of(y, LB::PR, LB::PO))) {
            none();  // LB24
        } else if ([&] {  // LB25
                       if (any_of(y, LB::PO, LB::PR) || y == LB::NU) {
                           std::ptrdiff_t p = k - 1;
                           if (y != LB::NU && any_of(cls(p), LB::CL, LB::CP)) {
                               --p;
                           }
                           while (p >= 0 && any_of(cls(p), LB::SY, LB::IS)) {
                               --p;
                           }
                           if (p >= 0 && cls(p) == LB::NU) {
                               return true;
                           }
                       }
                       if (any_of(x, LB::PO, LB::PR) && y == LB::OP && has_next &&
                           (next == LB::NU || (next == LB::IS && k + 2 < count && cls(k + 2) == LB::NU))) {
                           return true;
                       }
                       return any_of(x, LB::PO, LB::PR, LB::HY, LB::IS) && y == LB::NU;
                   }()) {
            none();
        } else if ((x == LB::JL && any_of(y, LB::JL, LB::JV, LB::H2, LB::H3)) ||
                   (any_of(x, LB::JV, LB::H2) && any_of(y, LB::JV, LB::JT)) || (any_of(x, LB::JT, LB::H3) && y == LB::JT)) {
            none();  // LB26
        } else if ((any_of(x, LB::JL, LB::JV, LB::JT, LB::H2, LB::H3) && y == LB::PO) ||
                   (x == LB::PR && any_of(y, LB::JL, LB::JV, LB::JT, LB::H2, LB::H3))) {
            none();  // LB27
        } else if (any_of(x, LB::AL, LB::HL) && any_of(y, LB::AL, LB::HL)) {
            none();  // LB28
        } else if ((x == LB::AP && (any_of(y, LB::AK, LB::AS) || yi.base == kDottedCircle)) ||
                   (is_aksara(k - 1) && any_of(y, LB::VF, LB::VI)) ||
                   (x == LB::VI && is_aksara(k - 2) && (y == LB::AK || yi.base == kDottedCircle)) ||
                   (is_aksara(k - 1) && is_aksara(k) && has_next && next == LB::VF)) {
            none();  // LB28a
        } else if (x == LB::IS && any_of(y, LB::AL, LB::HL)) {
            none();  // LB29
        } else if ((any_of(x, LB::AL, LB::HL, LB::NU) && y == LB::OP && !yi.east_asian) ||
                   (x == LB::CP && !xi.east_asian && any_of(y, LB::AL, LB::HL, LB::NU))) {
            none();  // LB30
        } else if (x == LB::RI && y == LB::RI) {
            std::ptrdiff_t run = 0;  // LB30a: regional indicators in a row before the boundary
            for (std::ptrdiff_t r = k - 1; r >= 0 && cls(r) == LB::RI; --r) {
                ++run;
            }
            if (run % 2 == 1) {
                none();
            }
        } else if (y == LB::EM && (x == LB::EB || (xi.pictographic && xi.category == GeneralCategory::Cn))) {
            none();  // LB30b
        }
        breaks[i] = result;  // LB31 otherwise
    }
    return breaks;
}

}  // namespace moteur::unicode
