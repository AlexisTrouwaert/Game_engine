#include "moteur/bidi.hpp"

#include <algorithm>
#include <utility>

namespace moteur {

namespace {

using BC = unicode::BidiClass;

constexpr int kMaxDepth = 125;             // UAX #9, BD2
constexpr std::size_t kBracketStack = 63;  // BD16

bool is_isolate_initiator(BC c) {
    return c == BC::LRI || c == BC::RLI || c == BC::FSI;
}

bool is_removed_by_x9(BC c) {
    return c == BC::RLE || c == BC::LRE || c == BC::RLO || c == BC::LRO || c == BC::PDF || c == BC::BN;
}

bool is_neutral_or_isolate(BC c) {  // "NI" of the rules N1, N2
    return c == BC::B || c == BC::S || c == BC::WS || c == BC::ON || c == BC::LRI || c == BC::RLI || c == BC::FSI ||
           c == BC::PDI;
}

// For N0 and N1: L stays L; R, and the numbers EN and AN, count as R; anything else is not strong.
BC strong_direction(BC c) {
    if (c == BC::L) return BC::L;
    if (c == BC::R || c == BC::AL || c == BC::EN || c == BC::AN) return BC::R;
    return BC::ON;
}

// P2, P3 on [first, last), skipping isolates: 0, 1, or -1 without a strong character.
int first_strong(std::span<const BC> classes, std::size_t first, std::size_t last) {
    int isolates = 0;
    for (std::size_t i = first; i < last; ++i) {
        const BC c = classes[i];
        if (c == BC::B) {
            break;
        }
        if (is_isolate_initiator(c)) {
            ++isolates;
        } else if (c == BC::PDI) {
            if (isolates > 0) {
                --isolates;
            }
        } else if (isolates == 0) {
            if (c == BC::L) {
                return 0;
            }
            if (c == BC::R || c == BC::AL) {
                return 1;
            }
        }
    }
    return -1;
}

char32_t canonical_bracket(char32_t c) {  // BD16: U+2329/U+232A are canonically U+3008/U+3009
    if (c == 0x2329) return 0x3008;
    if (c == 0x232A) return 0x3009;
    return c;
}

// An isolating run sequence (BD13): its characters (none removed by X9) in logical order.
struct Sequence {
    std::vector<std::size_t> indices;
    BC sos = BC::L;
    BC eos = BC::L;
    std::uint8_t level = 0;
};

// N0 on one sequence (types already through W1 to W7).
void resolve_brackets(const Sequence& seq, std::vector<BC>& types, std::span<const BC> original,
                      std::span<const char32_t> text) {
    const std::vector<std::size_t>& ix = seq.indices;
    const std::size_t m = ix.size();

    // BD16: the pairs, as positions in the sequence.
    struct Open {
        char32_t closing;  // the canonical closing bracket that would match
        std::size_t position;
    };
    std::vector<Open> stack;
    std::vector<std::pair<std::size_t, std::size_t>> pairs;
    for (std::size_t k = 0; k < m; ++k) {
        const std::size_t i = ix[k];
        if (types[i] != BC::ON) {
            continue;
        }
        bool opening = false;
        const char32_t pair = unicode::paired_bracket(text[i], &opening);
        if (pair == 0) {
            continue;
        }
        if (opening) {
            if (stack.size() == kBracketStack) {
                pairs.clear();  // stack overflow: no pairs at all in this sequence
                stack.clear();
                break;
            }
            stack.push_back({canonical_bracket(pair), k});
        } else {
            const char32_t closing = canonical_bracket(text[i]);
            for (std::size_t s = stack.size(); s-- > 0;) {
                if (stack[s].closing == closing) {
                    pairs.emplace_back(stack[s].position, k);
                    stack.resize(s);
                    break;
                }
            }
        }
    }
    std::sort(pairs.begin(), pairs.end());

    const BC embedding = is_rtl_level(seq.level) ? BC::R : BC::L;
    const BC opposite = embedding == BC::L ? BC::R : BC::L;
    for (const auto& [open, close] : pairs) {
        bool found_embedding = false;
        bool found_opposite = false;
        for (std::size_t k = open + 1; k < close; ++k) {
            const BC d = strong_direction(types[ix[k]]);
            if (d == embedding) {
                found_embedding = true;
                break;
            }
            if (d == opposite) {
                found_opposite = true;
            }
        }
        BC resolved = BC::ON;
        if (found_embedding) {
            resolved = embedding;  // N0 b
        } else if (found_opposite) {
            BC context = seq.sos;  // N0 c: the strong type before the opening bracket
            for (std::size_t k = open; k-- > 0;) {
                const BC d = strong_direction(types[ix[k]]);
                if (d != BC::ON) {
                    context = d;
                    break;
                }
            }
            resolved = context == opposite ? opposite : embedding;
        }
        if (resolved == BC::ON) {
            continue;  // N0 d: no strong type inside, left to N1 and N2
        }
        for (const std::size_t bracket : {open, close}) {
            types[ix[bracket]] = resolved;
            // Nonspacing marks right after a bracket follow it.
            for (std::size_t k = bracket + 1; k < m && original[ix[k]] == BC::NSM; ++k) {
                types[ix[k]] = resolved;
            }
        }
    }
}

void resolve_sequence(const Sequence& seq, std::vector<BC>& types, std::span<const BC> original,
                      std::span<const char32_t> text, std::vector<std::uint8_t>& levels) {
    const std::vector<std::size_t>& ix = seq.indices;
    const std::size_t m = ix.size();
    const auto type = [&](std::size_t k) -> BC& { return types[ix[k]]; };

    // W1
    for (std::size_t k = 0; k < m; ++k) {
        if (type(k) == BC::NSM) {
            if (k == 0) {
                type(k) = seq.sos;
            } else {
                const BC prev = type(k - 1);
                type(k) = (is_isolate_initiator(prev) || prev == BC::PDI) ? BC::ON : prev;
            }
        }
    }
    // W2
    {
        BC last_strong = seq.sos;
        for (std::size_t k = 0; k < m; ++k) {
            const BC t = type(k);
            if (t == BC::L || t == BC::R || t == BC::AL) {
                last_strong = t;
            } else if (t == BC::EN && last_strong == BC::AL) {
                type(k) = BC::AN;
            }
        }
    }
    // W3
    for (std::size_t k = 0; k < m; ++k) {
        if (type(k) == BC::AL) {
            type(k) = BC::R;
        }
    }
    // W4
    for (std::size_t k = 1; k + 1 < m; ++k) {
        const BC t = type(k);
        const BC before = type(k - 1);
        const BC after = type(k + 1);
        if (t == BC::ES && before == BC::EN && after == BC::EN) {
            type(k) = BC::EN;
        } else if (t == BC::CS && before == after && (before == BC::EN || before == BC::AN)) {
            type(k) = before;
        }
    }
    // W5
    for (std::size_t k = 0; k < m;) {
        if (type(k) != BC::ET) {
            ++k;
            continue;
        }
        std::size_t end = k;
        while (end < m && type(end) == BC::ET) {
            ++end;
        }
        if ((k > 0 && type(k - 1) == BC::EN) || (end < m && type(end) == BC::EN)) {
            for (std::size_t j = k; j < end; ++j) {
                type(j) = BC::EN;
            }
        }
        k = end;
    }
    // W6
    for (std::size_t k = 0; k < m; ++k) {
        const BC t = type(k);
        if (t == BC::ES || t == BC::ET || t == BC::CS) {
            type(k) = BC::ON;
        }
    }
    // W7
    {
        BC last_strong = seq.sos;
        for (std::size_t k = 0; k < m; ++k) {
            const BC t = type(k);
            if (t == BC::L || t == BC::R) {
                last_strong = t;
            } else if (t == BC::EN && last_strong == BC::L) {
                type(k) = BC::L;
            }
        }
    }

    if (!text.empty()) {
        resolve_brackets(seq, types, original, text);  // N0
    }

    // N1, N2
    const BC embedding = is_rtl_level(seq.level) ? BC::R : BC::L;
    for (std::size_t k = 0; k < m;) {
        if (!is_neutral_or_isolate(type(k))) {
            ++k;
            continue;
        }
        std::size_t end = k;
        while (end < m && is_neutral_or_isolate(type(end))) {
            ++end;
        }
        const BC before = k == 0 ? seq.sos : strong_direction(type(k - 1));
        const BC after = end == m ? seq.eos : strong_direction(type(end));
        const BC resolved = (before == after && before != BC::ON) ? before : embedding;
        for (std::size_t j = k; j < end; ++j) {
            type(j) = resolved;
        }
        k = end;
    }

    // I1, I2
    for (std::size_t k = 0; k < m; ++k) {
        const std::size_t i = ix[k];
        const BC t = types[i];
        std::uint8_t& level = levels[i];
        if (!is_rtl_level(level)) {
            if (t == BC::R) {
                level = static_cast<std::uint8_t>(level + 1);
            } else if (t == BC::AN || t == BC::EN) {
                level = static_cast<std::uint8_t>(level + 2);
            }
        } else if (t == BC::L || t == BC::EN || t == BC::AN) {
            level = static_cast<std::uint8_t>(level + 1);
        }
    }
}

}  // namespace

int bidi_first_strong_level(std::u32string_view text) {
    std::vector<BC> classes(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        classes[i] = unicode::char_info(text[i]).bidi;
    }
    return first_strong(classes, 0, classes.size());
}

BidiParagraph bidi_resolve(std::span<const BC> classes, TextDirection direction, std::span<const char32_t> text) {
    const std::size_t n = classes.size();
    BidiParagraph result;
    if (direction == TextDirection::Auto) {
        result.base_level = static_cast<std::uint8_t>(std::max(first_strong(classes, 0, n), 0));  // P2, P3
    } else {
        result.base_level = direction == TextDirection::RightToLeft ? 1 : 0;  // HL1
    }
    result.levels.assign(n, result.base_level);
    result.removed.assign(n, false);
    if (n == 0) {
        return result;
    }

    // BD9: matching isolate initiators and PDIs.
    std::vector<std::size_t> pdi_of(n, n);
    std::vector<bool> matched_pdi(n, false);
    {
        std::vector<std::size_t> open;
        for (std::size_t i = 0; i < n; ++i) {
            if (is_isolate_initiator(classes[i])) {
                open.push_back(i);
            } else if (classes[i] == BC::PDI && !open.empty()) {
                pdi_of[open.back()] = i;
                matched_pdi[i] = true;
                open.pop_back();
            }
        }
    }

    // X1 to X8.
    std::vector<BC> types(classes.begin(), classes.end());
    std::vector<std::uint8_t>& levels = result.levels;
    struct Status {
        std::uint8_t level;
        BC override_type;  // ON: neutral; L or R: overriding
        bool isolate;
    };
    std::vector<Status> stack;
    stack.reserve(kMaxDepth + 2);
    stack.push_back({result.base_level, BC::ON, false});
    int overflow_isolates = 0;
    int overflow_embeddings = 0;
    int valid_isolates = 0;
    const auto least_greater = [](std::uint8_t level, bool odd) {
        const int next = level + 1;
        return static_cast<int>((next & 1) == (odd ? 1 : 0) ? next : next + 1);
    };

    for (std::size_t i = 0; i < n; ++i) {
        const BC c = classes[i];
        switch (c) {
            case BC::RLE:
            case BC::LRE:
            case BC::RLO:
            case BC::LRO: {  // X2 to X5
                const bool rtl = c == BC::RLE || c == BC::RLO;
                const int level = least_greater(stack.back().level, rtl);
                if (level <= kMaxDepth && overflow_isolates == 0 && overflow_embeddings == 0) {
                    const BC override_type = c == BC::RLO ? BC::R : (c == BC::LRO ? BC::L : BC::ON);
                    stack.push_back({static_cast<std::uint8_t>(level), override_type, false});
                } else if (overflow_isolates == 0) {
                    ++overflow_embeddings;
                }
                levels[i] = stack.back().level;
                break;
            }
            case BC::RLI:
            case BC::LRI:
            case BC::FSI: {  // X5a to X5c
                bool rtl = c == BC::RLI;
                if (c == BC::FSI) {
                    const std::size_t end = pdi_of[i];
                    rtl = first_strong(classes, i + 1, end) == 1;
                }
                levels[i] = stack.back().level;
                if (stack.back().override_type != BC::ON) {
                    types[i] = stack.back().override_type;
                }
                const int level = least_greater(stack.back().level, rtl);
                if (level <= kMaxDepth && overflow_isolates == 0 && overflow_embeddings == 0) {
                    ++valid_isolates;
                    stack.push_back({static_cast<std::uint8_t>(level), BC::ON, true});
                } else {
                    ++overflow_isolates;
                }
                break;
            }
            case BC::PDI: {  // X6a
                if (overflow_isolates > 0) {
                    --overflow_isolates;
                } else if (valid_isolates > 0) {
                    overflow_embeddings = 0;
                    while (!stack.back().isolate) {
                        stack.pop_back();
                    }
                    stack.pop_back();
                    --valid_isolates;
                }
                levels[i] = stack.back().level;
                if (stack.back().override_type != BC::ON) {
                    types[i] = stack.back().override_type;
                }
                break;
            }
            case BC::PDF: {  // X7
                if (overflow_isolates > 0) {
                } else if (overflow_embeddings > 0) {
                    --overflow_embeddings;
                } else if (!stack.back().isolate && stack.size() >= 2) {
                    stack.pop_back();
                }
                levels[i] = stack.back().level;
                break;
            }
            case BC::B:  // X8
                levels[i] = result.base_level;
                break;
            case BC::BN:
                levels[i] = stack.back().level;
                break;
            default:  // X6
                levels[i] = stack.back().level;
                if (stack.back().override_type != BC::ON) {
                    types[i] = stack.back().override_type;
                }
                break;
        }
    }

    // X9
    std::vector<std::size_t> kept;
    kept.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (is_removed_by_x9(classes[i])) {
            result.removed[i] = true;
        } else {
            kept.push_back(i);
        }
    }

    // X10: level runs over the kept characters, then isolating run sequences (BD13).
    struct Run {
        std::size_t first, last;  // positions in `kept`, inclusive
    };
    std::vector<Run> runs;
    for (std::size_t k = 0; k < kept.size(); ++k) {
        if (runs.empty() || levels[kept[k]] != levels[kept[runs.back().last]]) {
            runs.push_back({k, k});
        } else {
            runs.back().last = k;
        }
    }
    // The run that starts with each matched PDI, to continue the sequence of its initiator.
    std::vector<std::size_t> run_starting_at(n, runs.size());
    for (std::size_t r = 0; r < runs.size(); ++r) {
        run_starting_at[kept[runs[r].first]] = r;
    }

    std::vector<Sequence> sequences;
    for (std::size_t r = 0; r < runs.size(); ++r) {
        if (matched_pdi[kept[runs[r].first]]) {
            continue;  // continues the sequence of its isolate initiator
        }
        Sequence seq;
        std::size_t run = r;
        while (true) {
            for (std::size_t k = runs[run].first; k <= runs[run].last; ++k) {
                seq.indices.push_back(kept[k]);
            }
            const std::size_t last = kept[runs[run].last];
            if (!is_isolate_initiator(classes[last]) || pdi_of[last] == n) {
                break;
            }
            const std::size_t next = run_starting_at[pdi_of[last]];
            if (next == runs.size()) {
                break;
            }
            run = next;
        }
        sequences.push_back(std::move(seq));
    }

    // sos, eos (from the levels of X1 to X8, before any change).
    std::vector<std::size_t> position_in_kept(n, n);
    for (std::size_t k = 0; k < kept.size(); ++k) {
        position_in_kept[kept[k]] = k;
    }
    for (Sequence& seq : sequences) {
        const std::size_t first = seq.indices.front();
        const std::size_t last = seq.indices.back();
        seq.level = levels[first];
        const std::size_t first_k = position_in_kept[first];
        const std::size_t last_k = position_in_kept[last];
        const std::uint8_t before = first_k == 0 ? result.base_level : levels[kept[first_k - 1]];
        const bool open_isolate = is_isolate_initiator(classes[last]);
        const std::uint8_t after =
            (last_k + 1 >= kept.size() || open_isolate) ? result.base_level : levels[kept[last_k + 1]];
        seq.sos = is_rtl_level(std::max(seq.level, before)) ? BC::R : BC::L;
        seq.eos = is_rtl_level(std::max(levels[last], after)) ? BC::R : BC::L;
    }

    for (const Sequence& seq : sequences) {
        resolve_sequence(seq, types, classes, text, levels);
    }

    // The removed characters take the level of the character before them.
    for (std::size_t i = 0; i < n; ++i) {
        if (result.removed[i]) {
            levels[i] = i == 0 ? result.base_level : levels[i - 1];
        }
    }
    return result;
}

BidiParagraph bidi_paragraph(std::u32string_view paragraph, TextDirection direction) {
    std::vector<BC> classes(paragraph.size());
    for (std::size_t i = 0; i < paragraph.size(); ++i) {
        classes[i] = unicode::char_info(paragraph[i]).bidi;
    }
    return bidi_resolve(classes, direction, std::span<const char32_t>(paragraph.data(), paragraph.size()));
}

std::vector<std::uint8_t> bidi_line_levels(std::span<const BC> classes, const BidiParagraph& paragraph,
                                           std::size_t first, std::size_t last) {
    last = std::min(last, classes.size());
    first = std::min(first, last);
    std::vector<std::uint8_t> levels(paragraph.levels.begin() + static_cast<std::ptrdiff_t>(first),
                                     paragraph.levels.begin() + static_cast<std::ptrdiff_t>(last));
    // L1: separators, and the whitespace (with isolate controls and the X9-removed characters) before
    // them or at the end of the line, back to the paragraph level.
    const auto is_whitespace = [&](std::size_t i) {
        const BC c = classes[i];
        return c == BC::WS || is_isolate_initiator(c) || c == BC::PDI || paragraph.removed[i];
    };
    bool trailing = true;  // walking backwards: still in whitespace that reaches a separator or the end
    for (std::size_t i = last; i-- > first;) {
        const BC c = classes[i];
        if (c == BC::S || c == BC::B) {
            levels[i - first] = paragraph.base_level;
            trailing = true;
        } else if (trailing && is_whitespace(i)) {
            levels[i - first] = paragraph.base_level;
        } else {
            trailing = false;
        }
    }
    return levels;
}

std::vector<std::uint8_t> bidi_line_levels(std::u32string_view text, const BidiParagraph& paragraph, std::size_t first,
                                           std::size_t last) {
    std::vector<BC> classes(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        classes[i] = unicode::char_info(text[i]).bidi;
    }
    return bidi_line_levels(classes, paragraph, first, last);
}

std::vector<std::size_t> bidi_visual_order(std::span<const std::uint8_t> levels) {
    std::vector<std::size_t> order(levels.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    if (levels.empty()) {
        return order;
    }
    const std::uint8_t highest = *std::max_element(levels.begin(), levels.end());
    std::uint8_t lowest_odd = 255;
    for (const std::uint8_t level : levels) {
        if (is_rtl_level(level)) {
            lowest_odd = std::min(lowest_odd, level);
        }
    }
    if (lowest_odd == 255) {
        return order;  // nothing reads right to left
    }
    // `order` holds logical indices in visual positions; their levels move with them.
    std::vector<std::uint8_t> current(levels.begin(), levels.end());
    for (int level = highest; level >= lowest_odd; --level) {
        for (std::size_t i = 0; i < current.size();) {
            if (current[i] < level) {
                ++i;
                continue;
            }
            std::size_t end = i;
            while (end < current.size() && current[end] >= level) {
                ++end;
            }
            std::reverse(order.begin() + static_cast<std::ptrdiff_t>(i), order.begin() + static_cast<std::ptrdiff_t>(end));
            std::reverse(current.begin() + static_cast<std::ptrdiff_t>(i), current.begin() + static_cast<std::ptrdiff_t>(end));
            i = end;
        }
    }
    return order;
}

}  // namespace moteur
