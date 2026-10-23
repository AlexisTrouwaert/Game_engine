#include "moteur/ui_style.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace moteur::ui {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// Words separated by spaces, parentheses kept together ("rgba(0, 0, 0, 0.5) 2" -> two words).
std::vector<std::string> words(std::string_view s) {
    std::vector<std::string> out;
    std::string current;
    int depth = 0;
    bool quoted = false;
    for (const char c : s) {
        if (c == '"') quoted = !quoted;
        if (c == '(') ++depth;
        if (c == ')') --depth;
        if (std::isspace(static_cast<unsigned char>(c)) && depth == 0 && !quoted) {
            if (!current.empty()) {
                out.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        out.push_back(current);
    }
    return out;
}

// Splits at top-level commas.
std::vector<std::string> commas(std::string_view s) {
    std::vector<std::string> out;
    std::string current;
    int depth = 0;
    for (const char c : s) {
        if (c == '(') ++depth;
        if (c == ')') --depth;
        if (c == ',' && depth == 0) {
            out.emplace_back(trim(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    out.emplace_back(trim(current));
    return out;
}

std::optional<float> parse_number(std::string_view s) {
    s = trim(s);
    if (s.empty()) {
        return std::nullopt;
    }
    const std::string text(s);
    char* end = nullptr;
    const float v = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || !std::isfinite(v)) {
        return std::nullopt;
    }
    return v;
}

std::optional<float> parse_seconds(std::string_view s) {
    s = trim(s);
    if (s.size() > 2 && s.substr(s.size() - 2) == "ms") {
        if (const auto v = parse_number(s.substr(0, s.size() - 2))) return *v / 1000.0f;
        return std::nullopt;
    }
    if (s.size() > 1 && s.back() == 's') {
        return parse_number(s.substr(0, s.size() - 1));
    }
    return std::nullopt;
}

int line_of(std::string_view text, std::size_t pos) {
    return 1 + static_cast<int>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(std::min(pos, text.size())), '\n'));
}

Compound parse_compound(std::string_view s, int line) {
    Compound c;
    std::size_t i = 0;
    const auto ident = [&] {
        const std::size_t start = i;
        while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '-' || s[i] == '_')) {
            ++i;
        }
        if (i == start) {
            throw StyleError(line, "bad selector '" + std::string(s) + "'");
        }
        return std::string(s.substr(start, i - start));
    };
    if (i < s.size() && s[i] == '*') {
        c.type = "*";
        ++i;
    } else if (i < s.size() && (std::isalpha(static_cast<unsigned char>(s[i])) || s[i] == '_')) {
        c.type = lower(ident());
    }
    while (i < s.size()) {
        const char k = s[i++];
        if (k == '.') {
            c.classes.push_back(ident());
        } else if (k == '#') {
            c.id = ident();
        } else if (k == ':') {
            const std::string state = lower(ident());
            static const char* const kStates[] = {"hover", "focus", "active", "disabled", "checked", "first-child", "last-child"};
            if (std::find(std::begin(kStates), std::end(kStates), state) == std::end(kStates)) {
                throw StyleError(line, "unknown state ':" + state + "'");
            }
            c.states.push_back(state);
        } else {
            throw StyleError(line, "bad selector '" + std::string(s) + "'");
        }
    }
    return c;
}

Selector parse_selector(std::string_view text, int line) {
    Selector sel;
    // Tokens: compounds and '>' combinators; spaces between compounds are descendants.
    std::string current;
    char pending = 0;
    const auto push = [&] {
        if (current.empty()) {
            return;
        }
        if (!sel.parts.empty()) {
            sel.combinators.push_back(pending == '>' ? '>' : ' ');
        }
        sel.parts.push_back(parse_compound(current, line));
        current.clear();
        pending = 0;
    };
    for (const char c : text) {
        if (c == '>') {
            push();
            pending = '>';
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            push();
        } else {
            current.push_back(c);
        }
    }
    push();
    if (sel.parts.empty() || pending == '>') {
        throw StyleError(line, "empty selector");
    }
    int ids = 0, classes = 0, types = 0;
    for (const Compound& c : sel.parts) {
        ids += c.id.empty() ? 0 : 1;
        classes += static_cast<int>(c.classes.size() + c.states.size());
        types += (!c.type.empty() && c.type != "*") ? 1 : 0;
    }
    sel.specificity = ids * 10000 + classes * 100 + types;
    return sel;
}

bool matches_compound(const Compound& c, const MatchNode& n) {
    if (!c.type.empty() && c.type != "*" && c.type != n.type) return false;
    if (!c.id.empty() && c.id != n.id) return false;
    for (const std::string& cls : c.classes) {
        if (n.classes == nullptr || std::find(n.classes->begin(), n.classes->end(), cls) == n.classes->end()) {
            return false;
        }
    }
    for (const std::string& s : c.states) {
        const bool ok = (s == "hover" && (n.states & kHover)) || (s == "focus" && (n.states & kFocus)) ||
                        (s == "active" && (n.states & kActive)) || (s == "disabled" && (n.states & kDisabled)) ||
                        (s == "checked" && (n.states & kChecked)) || (s == "first-child" && n.first_child) ||
                        (s == "last-child" && n.last_child);
        if (!ok) return false;
    }
    return true;
}

bool matches_from(const Selector& sel, int index, const MatchNode* node) {
    if (node == nullptr || !matches_compound(sel.parts[static_cast<std::size_t>(index)], *node)) {
        return false;
    }
    if (index == 0) {
        return true;
    }
    const char combinator = sel.combinators[static_cast<std::size_t>(index - 1)];
    if (combinator == '>') {
        return matches_from(sel, index - 1, node->parent);
    }
    for (const MatchNode* p = node->parent; p != nullptr; p = p->parent) {
        if (matches_from(sel, index - 1, p)) {
            return true;
        }
    }
    return false;
}

// var(--x, fallback) replaced, recursively (depth limited).
std::string substitute(std::string_view value, const std::map<std::string, std::string, std::less<>>& vars, int depth = 0) {
    std::string out;
    std::size_t i = 0;
    while (i < value.size()) {
        const std::size_t at = value.find("var(", i);
        if (at == std::string_view::npos) {
            out.append(value.substr(i));
            break;
        }
        out.append(value.substr(i, at - i));
        int level = 1;
        std::size_t j = at + 4;
        while (j < value.size() && level > 0) {
            if (value[j] == '(') ++level;
            if (value[j] == ')') --level;
            ++j;
        }
        const std::string_view inside = value.substr(at + 4, j - at - 5);
        const std::size_t comma = inside.find(',');
        const std::string name(trim(inside.substr(0, comma)));
        const auto it = vars.find(name);
        std::string replacement;
        if (it != vars.end()) {
            replacement = it->second;
        } else if (comma != std::string_view::npos) {
            replacement = std::string(trim(inside.substr(comma + 1)));
        }
        out += depth < 8 ? substitute(replacement, vars, depth + 1) : replacement;
        i = j;
    }
    return out;
}

ComputedStyle::Align parse_align(std::string_view v, bool& ok) {
    ok = true;
    if (v == "stretch") return ComputedStyle::Align::Stretch;
    if (v == "flex-start" || v == "start") return ComputedStyle::Align::Start;
    if (v == "center") return ComputedStyle::Align::Center;
    if (v == "flex-end" || v == "end") return ComputedStyle::Align::End;
    if (v == "auto") return ComputedStyle::Align::Auto;
    ok = false;
    return ComputedStyle::Align::Auto;
}

std::optional<std::string> parse_url(std::string_view v) {
    v = trim(v);
    if (v.substr(0, 4) != "url(" || v.back() != ')') {
        return std::nullopt;
    }
    std::string_view inside = trim(v.substr(4, v.size() - 5));
    if (inside.size() >= 2 && (inside.front() == '"' || inside.front() == '\'')) {
        inside = inside.substr(1, inside.size() - 2);
    }
    return std::string(inside);
}

std::optional<Easing> parse_easing(std::string_view v) {
    if (v == "linear") return Easing::Linear;
    if (v == "ease") return Easing::Ease;
    if (v == "ease-in") return Easing::EaseIn;
    if (v == "ease-out") return Easing::EaseOut;
    if (v == "ease-in-out") return Easing::EaseInOut;
    return std::nullopt;
}

// Applies one declaration; false (with `problem`) when the value is not understood.
bool apply(ComputedStyle& s, const std::string& property, const std::string& raw, std::string& problem) {
    const std::string v = lower(trim(raw));
    const std::vector<std::string> w = words(trim(raw));
    const auto length = [&](Length& out) {
        const auto l = parse_length(v);
        if (!l) return false;
        out = *l;
        return true;
    };
    const auto points = [&](std::string_view text, float& out) {
        const auto l = parse_length(text);
        if (!l || l->unit != Length::Unit::Points) return false;
        out = l->value;
        return true;
    };
    const auto color = [&](glm::vec4& out) {
        const auto c = parse_color(v);
        if (!c) return false;
        out = *c;
        return true;
    };
    const auto number = [&](float& out) {
        const auto n = parse_number(v);
        if (!n) return false;
        out = *n;
        return true;
    };
    const auto four = [&](auto* sides, auto convert) -> bool {
        using T = std::remove_reference_t<decltype(sides[0])>;
        if (w.empty() || w.size() > 4) return false;
        std::vector<T> parsed;
        for (const std::string& word : w) {
            T one{};
            if (!convert(word, one)) return false;
            parsed.push_back(one);
        }
        const std::size_t n = parsed.size();
        sides[0] = parsed[0];
        sides[1] = parsed[n > 1 ? 1 : 0];
        sides[2] = parsed[n > 2 ? 2 : 0];
        sides[3] = parsed[n > 3 ? 3 : (n > 1 ? 1 : 0)];
        return true;
    };
    bool ok = true;
    if (property == "display") {
        if (v == "flex" || v == "block") s.display = ComputedStyle::Display::Flex;
        else if (v == "none") s.display = ComputedStyle::Display::None;
        else ok = false;
    } else if (property == "position") {
        if (v == "relative") s.position = ComputedStyle::Position::Relative;
        else if (v == "absolute") s.position = ComputedStyle::Position::Absolute;
        else ok = false;
    } else if (property == "flex-direction") {
        if (v == "row") s.row = true;
        else if (v == "column") s.row = false;
        else ok = false;
    } else if (property == "flex-wrap") {
        if (v == "wrap") s.wrap = true;
        else if (v == "nowrap") s.wrap = false;
        else ok = false;
    } else if (property == "justify-content") {
        if (v == "flex-start" || v == "start") s.justify = ComputedStyle::Justify::Start;
        else if (v == "center") s.justify = ComputedStyle::Justify::Center;
        else if (v == "flex-end" || v == "end") s.justify = ComputedStyle::Justify::End;
        else if (v == "space-between") s.justify = ComputedStyle::Justify::SpaceBetween;
        else if (v == "space-around") s.justify = ComputedStyle::Justify::SpaceAround;
        else if (v == "space-evenly") s.justify = ComputedStyle::Justify::SpaceEvenly;
        else ok = false;
    } else if (property == "align-items") {
        s.align_items = parse_align(v, ok);
    } else if (property == "align-self") {
        s.align_self = parse_align(v, ok);
    } else if (property == "flex-grow") {
        ok = number(s.grow);
    } else if (property == "flex-shrink") {
        ok = number(s.shrink);
    } else if (property == "flex-basis") {
        ok = length(s.basis);
    } else if (property == "flex") {
        if (v == "none") {
            s.grow = 0;
            s.shrink = 0;
            s.basis = {};
        } else if (const auto n = parse_number(w.empty() ? "" : w[0])) {
            // As in CSS: `flex: 1` shares the whole room (basis 0), `flex: 1 1 auto` the free room.
            s.grow = *n;
            s.shrink = 1;
            s.basis = Length{Length::Unit::Points, 0};
            if (w.size() > 1) {
                const auto shrink = parse_number(w[1]);
                ok = shrink.has_value();
                s.shrink = shrink.value_or(1);
            }
            if (ok && w.size() > 2) {
                const auto basis = parse_length(w[2]);
                ok = basis.has_value();
                s.basis = basis.value_or(Length{});
            }
        } else {
            ok = false;
        }
    } else if (property == "gap") {
        ok = points(v, s.gap);
    } else if (property == "width") ok = length(s.width);
    else if (property == "height") ok = length(s.height);
    else if (property == "min-width") ok = length(s.min_width);
    else if (property == "min-height") ok = length(s.min_height);
    else if (property == "max-width") ok = length(s.max_width);
    else if (property == "max-height") ok = length(s.max_height);
    else if (property == "left") ok = length(s.left);
    else if (property == "top") ok = length(s.top);
    else if (property == "right") ok = length(s.right);
    else if (property == "bottom") ok = length(s.bottom);
    else if (property == "padding") {
        ok = four(s.padding, [&](const std::string& word, float& out) { return points(word, out); });
    } else if (property == "padding-top") ok = points(v, s.padding[0]);
    else if (property == "padding-right") ok = points(v, s.padding[1]);
    else if (property == "padding-bottom") ok = points(v, s.padding[2]);
    else if (property == "padding-left") ok = points(v, s.padding[3]);
    else if (property == "margin") {
        ok = four(s.margin, [&](const std::string& word, Length& out) {
            const auto l = parse_length(word);
            if (!l || l->unit == Length::Unit::Percent) return false;
            out = *l;
            return true;
        });
    } else if (property == "margin-top") ok = length(s.margin[0]);
    else if (property == "margin-right") ok = length(s.margin[1]);
    else if (property == "margin-bottom") ok = length(s.margin[2]);
    else if (property == "margin-left") ok = length(s.margin[3]);
    else if (property == "overflow") {
        if (v == "visible") s.overflow = ComputedStyle::Overflow::Visible;
        else if (v == "hidden") s.overflow = ComputedStyle::Overflow::Hidden;
        else if (v == "scroll" || v == "auto") s.overflow = ComputedStyle::Overflow::Scroll;
        else ok = false;
    } else if (property == "background" || property == "background-color") {
        if (v == "none") {
            s.background = glm::vec4(0.0f);
            if (property == "background") s.background_image.clear();
        } else if (const auto url = parse_url(raw); url && property == "background") {
            s.background_image = *url;
        } else {
            ok = color(s.background);
        }
    } else if (property == "background-image") {
        if (v == "none") s.background_image.clear();
        else if (const auto url = parse_url(raw)) s.background_image = *url;
        else ok = false;
    } else if (property == "image-slice") ok = points(v, s.image_slice) || number(s.image_slice);
    else if (property == "image-color") ok = color(s.image_color);
    else if (property == "border") {
        if (v == "none") {
            s.border_width = 0;
        } else {
            ok = w.size() == 2 && points(w[0], s.border_width);
            if (ok) {
                const auto c = parse_color(lower(w[1]));
                ok = c.has_value();
                s.border_color = c.value_or(s.border_color);
            }
        }
    } else if (property == "border-width") ok = points(v, s.border_width);
    else if (property == "border-color") ok = color(s.border_color);
    else if (property == "border-radius") ok = points(v, s.border_radius);
    else if (property == "fill-color") ok = color(s.fill_color);
    else if (property == "opacity") ok = number(s.opacity);
    else if (property == "translate") {
        float x = 0, y = 0;
        ok = !w.empty() && w.size() <= 2 && points(w[0], x) && (w.size() < 2 || points(w[1], y));
        if (ok) s.translate = {x, y};
    } else if (property == "scale") ok = number(s.scale);
    else if (property == "color") ok = color(s.color);
    else if (property == "font-family") {
        std::string_view f = trim(raw);
        if (f.size() >= 2 && (f.front() == '"' || f.front() == '\'')) f = f.substr(1, f.size() - 2);
        s.font_family = std::string(f);
    } else if (property == "font-size") ok = points(v, s.font_size);
    else if (property == "font-weight") {
        if (v == "normal") s.font_weight = 400;
        else if (v == "bold") s.font_weight = 700;
        else ok = number(s.font_weight);
    } else if (property == "font-style") {
        if (v == "italic" || v == "oblique") s.italic = true;
        else if (v == "normal") s.italic = false;
        else ok = false;
    } else if (property == "text-align") {
        if (v == "start") s.text_align = ComputedStyle::TextAlign::Start;
        else if (v == "end") s.text_align = ComputedStyle::TextAlign::End;
        else if (v == "left") s.text_align = ComputedStyle::TextAlign::Left;
        else if (v == "center") s.text_align = ComputedStyle::TextAlign::Center;
        else if (v == "right") s.text_align = ComputedStyle::TextAlign::Right;
        else ok = false;
    } else if (property == "line-spacing" || property == "line-height") ok = number(s.line_spacing);
    else if (property == "text-outline") {
        if (v == "none") {
            s.text_outline_width = 0;
        } else {
            ok = w.size() == 2 && points(w[0], s.text_outline_width);
            if (ok) {
                const auto c = parse_color(lower(w[1]));
                ok = c.has_value();
                s.text_outline_color = c.value_or(s.text_outline_color);
            }
        }
    } else if (property == "text-shadow") {
        if (v == "none") {
            s.text_shadow = false;
        } else {
            float x = 0, y = 0, blur = 0;
            ok = w.size() == 4 && points(w[0], x) && points(w[1], y) && points(w[2], blur);
            const auto c = ok ? parse_color(lower(w[3])) : std::nullopt;
            ok = ok && c.has_value();
            if (ok) {
                s.text_shadow = true;
                s.text_shadow_offset = {x, y};
                s.text_shadow_blur = blur;
                s.text_shadow_color = *c;
            }
        }
    } else if (property == "transition") {
        s.transitions.clear();
        if (v != "none") {
            for (const std::string& item : commas(v)) {
                const std::vector<std::string> parts = words(item);
                Transition t;
                if (parts.size() < 2) {
                    ok = false;
                    break;
                }
                t.property = parts[0];
                const auto duration = parse_seconds(parts[1]);
                ok = duration.has_value();
                t.duration = duration.value_or(0);
                for (std::size_t k = 2; k < parts.size() && ok; ++k) {
                    if (const auto e = parse_easing(parts[k])) t.easing = *e;
                    else if (const auto d = parse_seconds(parts[k])) t.delay = *d;
                    else ok = false;
                }
                if (!ok) break;
                s.transitions.push_back(t);
            }
        }
    } else {
        problem = "unknown property";
        return false;
    }
    if (!ok) {
        problem = "bad value '" + std::string(trim(raw)) + "'";
    }
    return ok;
}

}  // namespace

std::optional<glm::vec4> parse_color(std::string_view text) {
    const std::string v = lower(trim(text));
    if (v.size() > 1 && v[0] == '#') {
        const std::string hex = v.substr(1);
        if (hex.find_first_not_of("0123456789abcdef") != std::string::npos) return std::nullopt;
        const auto nibble = [&](std::size_t i) { return static_cast<float>(std::strtoul(hex.substr(i, 1).c_str(), nullptr, 16)) / 15.0f; };
        const auto byte = [&](std::size_t i) { return static_cast<float>(std::strtoul(hex.substr(i, 2).c_str(), nullptr, 16)) / 255.0f; };
        if (hex.size() == 3) return glm::vec4(nibble(0), nibble(1), nibble(2), 1.0f);
        if (hex.size() == 4) return glm::vec4(nibble(0), nibble(1), nibble(2), nibble(3));
        if (hex.size() == 6) return glm::vec4(byte(0), byte(2), byte(4), 1.0f);
        if (hex.size() == 8) return glm::vec4(byte(0), byte(2), byte(4), byte(6));
        return std::nullopt;
    }
    if (v.rfind("rgb", 0) == 0) {
        const std::size_t open = v.find('(');
        const std::size_t close = v.rfind(')');
        if (open == std::string::npos || close == std::string::npos) return std::nullopt;
        const std::vector<std::string> parts = commas(v.substr(open + 1, close - open - 1));
        if (parts.size() != 3 && parts.size() != 4) return std::nullopt;
        glm::vec4 c(1.0f);
        for (std::size_t i = 0; i < parts.size(); ++i) {
            const auto n = parse_number(parts[i]);
            if (!n) return std::nullopt;
            c[static_cast<int>(i)] = i < 3 ? *n / 255.0f : *n;
        }
        return c;
    }
    static const std::pair<const char*, glm::vec4> kNames[] = {
        {"white", {1, 1, 1, 1}},     {"black", {0, 0, 0, 1}},       {"transparent", {0, 0, 0, 0}},
        {"red", {1, 0, 0, 1}},       {"green", {0, 0.5f, 0, 1}},    {"blue", {0, 0, 1, 1}},
        {"yellow", {1, 1, 0, 1}},    {"gray", {0.5f, 0.5f, 0.5f, 1}}, {"grey", {0.5f, 0.5f, 0.5f, 1}},
        {"orange", {1, 0.65f, 0, 1}}};
    for (const auto& [name, c] : kNames) {
        if (v == name) return c;
    }
    return std::nullopt;
}

std::optional<Length> parse_length(std::string_view text) {
    const std::string v = lower(trim(text));
    if (v == "auto") return Length{};
    if (!v.empty() && v.back() == '%') {
        if (const auto n = parse_number(std::string_view(v).substr(0, v.size() - 1))) return Length{Length::Unit::Percent, *n};
        return std::nullopt;
    }
    std::string_view number = v;
    if (number.size() > 2 && (number.substr(number.size() - 2) == "px" || number.substr(number.size() - 2) == "pt")) {
        number.remove_suffix(2);
    }
    if (const auto n = parse_number(number)) return Length{Length::Unit::Points, *n};
    return std::nullopt;
}

std::vector<Declaration> parse_declarations(std::string_view text, int line) {
    std::vector<Declaration> out;
    std::string current;
    bool quoted = false;
    int depth = 0;
    const auto push = [&] {
        const std::string_view d = trim(current);
        if (!d.empty()) {
            const std::size_t colon = d.find(':');
            if (colon == std::string_view::npos) {
                throw StyleError(line, "declaration without ':' (" + std::string(d) + ")");
            }
            const std::string_view name = trim(d.substr(0, colon));
            Declaration decl;
            decl.property = name.substr(0, 2) == "--" ? std::string(name) : lower(name);
            decl.value = std::string(trim(d.substr(colon + 1)));
            out.push_back(std::move(decl));
        }
        current.clear();
    };
    for (const char c : text) {
        if (c == '"') quoted = !quoted;
        if (c == '(') ++depth;
        if (c == ')') --depth;
        if (c == ';' && !quoted && depth == 0) {
            push();
        } else {
            if (c == '\n') ++line;
            current.push_back(c);
        }
    }
    push();
    return out;
}

std::vector<Selector> parse_selectors(std::string_view text, int line) {
    std::vector<Selector> out;
    for (const std::string& one : commas(text)) {
        out.push_back(parse_selector(one, line));
    }
    return out;
}

StyleSheet parse_style_sheet(std::string_view input, std::string name) {
    // Comments become spaces (lines kept for the messages).
    std::string text(input);
    for (std::size_t i = text.find("/*"); i != std::string::npos; i = text.find("/*", i)) {
        const std::size_t end = text.find("*/", i + 2);
        if (end == std::string::npos) {
            throw StyleError(line_of(text, i), "unterminated comment");
        }
        for (std::size_t k = i; k < end + 2; ++k) {
            if (text[k] != '\n') text[k] = ' ';
        }
        i = end + 2;
    }
    StyleSheet sheet;
    sheet.name = std::move(name);
    std::size_t pos = 0;
    while (true) {
        const std::size_t open = text.find('{', pos);
        if (open == std::string::npos) {
            if (!trim(std::string_view(text).substr(pos)).empty()) {
                throw StyleError(line_of(text, pos), "text after the last rule");
            }
            break;
        }
        const std::size_t close = text.find('}', open);
        if (close == std::string::npos) {
            throw StyleError(line_of(text, open), "'{' not closed");
        }
        if (text.find('{', open + 1) < close) {
            throw StyleError(line_of(text, open), "'{' inside a rule");
        }
        Rule rule;
        rule.line = line_of(text, open);
        const std::string_view selectors = trim(std::string_view(text).substr(pos, open - pos));
        rule.selectors = parse_selectors(selectors, rule.line);
        rule.declarations = parse_declarations(std::string_view(text).substr(open + 1, close - open - 1), rule.line);
        sheet.rules.push_back(std::move(rule));
        pos = close + 1;
    }
    return sheet;
}

bool matches(const Selector& selector, const MatchNode& node) {
    return matches_from(selector, static_cast<int>(selector.parts.size()) - 1, &node);
}

ComputedStyle compute_style(const ComputedStyle* parent, const std::vector<const Declaration*>& declarations,
                            std::map<std::string, std::string, std::less<>>& variables, std::vector<std::string>* warnings) {
    ComputedStyle s;
    if (parent != nullptr) {
        s.color = parent->color;
        s.font_family = parent->font_family;
        s.font_size = parent->font_size;
        s.font_weight = parent->font_weight;
        s.italic = parent->italic;
        s.text_align = parent->text_align;
        s.line_spacing = parent->line_spacing;
        s.text_outline_width = parent->text_outline_width;
        s.text_outline_color = parent->text_outline_color;
        s.text_shadow = parent->text_shadow;
        s.text_shadow_offset = parent->text_shadow_offset;
        s.text_shadow_blur = parent->text_shadow_blur;
        s.text_shadow_color = parent->text_shadow_color;
    }
    for (const Declaration* d : declarations) {
        if (d->property.rfind("--", 0) == 0) {
            variables[d->property] = substitute(d->value, variables);
        }
    }
    for (const Declaration* d : declarations) {
        if (d->property.rfind("--", 0) == 0) {
            continue;
        }
        std::string problem;
        const std::string value = substitute(d->value, variables);
        if (lower(trim(value)) == "inherit") {
            continue;  // the inherited ones are there already; the others keep their default
        }
        if (!apply(s, d->property, value, problem) && warnings != nullptr) {
            warnings->push_back(d->property + ": " + problem);
        }
    }
    return s;
}

float ease(Easing easing, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (easing) {
        case Easing::Linear: return t;
        case Easing::EaseIn: return t * t;
        case Easing::EaseOut: return 1.0f - (1.0f - t) * (1.0f - t);
        case Easing::EaseInOut: return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
        case Easing::Ease:
        default: return t * t * (3.0f - 2.0f * t);  // smoothstep, close to CSS's ease
    }
}

}  // namespace moteur::ui
