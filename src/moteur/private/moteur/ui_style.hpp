#pragma once

#include <glm/glm.hpp>

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

/// The styles of the interface's screens (milestone 9 bis, part 6): a CSS subset read by the engine.
///
/// Selectors: type (`button`), `.class`, `#id`, `*`, the states `:hover`, `:focus`, `:active`,
/// `:disabled`, `:checked`, `:first-child`, `:last-child`; combined (`button.big:focus`), in
/// descendants (`panel button`) or children (`panel > button`), in lists (`a, b`). The cascade:
/// specificity (ids, then classes and states, then types), then the order of the rules; an
/// element's `style="..."` attribute wins. Variables: `--name: value` (inherited), `var(--name,
/// fallback)`. Comments `/* */`. FORMATS.md, section 23, lists the properties.
namespace moteur::ui {

struct StyleError : std::runtime_error {
    StyleError(int line, const std::string& message)
        : std::runtime_error("line " + std::to_string(line) + ": " + message), line(line) {}
    int line;
};

/// One compound selector: `button.big#ok:focus`.
struct Compound {
    std::string type;  ///< empty or "*": any
    std::string id;
    std::vector<std::string> classes;
    std::vector<std::string> states;
};

/// A complex selector: compounds joined by combinators (' ' descendant, '>' child), right to left
/// when matched.
struct Selector {
    std::vector<Compound> parts;
    std::vector<char> combinators;  ///< between parts[i] and parts[i + 1]
    int specificity = 0;            ///< ids * 10000 + (classes + states) * 100 + types
};

struct Declaration {
    std::string property;  ///< lower case
    std::string value;     ///< as written, trimmed
};

struct Rule {
    std::vector<Selector> selectors;
    std::vector<Declaration> declarations;
    int line = 0;
};

struct StyleSheet {
    std::string name;  ///< its file, for the messages
    std::vector<Rule> rules;
};

/// Reads a style sheet. Throws StyleError.
StyleSheet parse_style_sheet(std::string_view text, std::string name = {});
/// The declarations of a `style="..."` attribute (`a: b; c: d`). Throws StyleError.
std::vector<Declaration> parse_declarations(std::string_view text, int line = 1);
/// One selector list (`a, b > c`), for tests and tools. Throws StyleError.
std::vector<Selector> parse_selectors(std::string_view text, int line = 1);

/// What a selector needs to know of an element (the tree is the interface's).
struct MatchNode {
    std::string_view type;
    std::string_view id;
    const std::vector<std::string>* classes = nullptr;
    unsigned states = 0;  ///< kHover | kFocus...
    const MatchNode* parent = nullptr;
    bool first_child = false;
    bool last_child = false;
};
constexpr unsigned kHover = 1, kFocus = 2, kActive = 4, kDisabled = 8, kChecked = 16;

bool matches(const Selector& selector, const MatchNode& node);

// --- Computed values ------------------------------------------------------------------------------

struct Length {
    enum class Unit : unsigned char { Auto, Points, Percent };
    Unit unit = Unit::Auto;
    float value = 0;
    bool is_auto() const { return unit == Unit::Auto; }
    /// Points: the value; percent: of `basis`; auto: `fallback`.
    float resolve(float basis, float fallback = 0) const {
        return unit == Unit::Points ? value : unit == Unit::Percent ? value * 0.01f * basis : fallback;
    }
    bool operator==(const Length&) const = default;
};

enum class Easing : unsigned char { Linear, Ease, EaseIn, EaseOut, EaseInOut };

struct Transition {
    std::string property;  ///< "opacity", "background", "color", "translate", "scale", "border-color", "all"
    float duration = 0;    ///< seconds
    float delay = 0;
    Easing easing = Easing::Ease;
};

/// The style of an element once the cascade is done: every property typed.
struct ComputedStyle {
    enum class Display : unsigned char { Flex, None };
    enum class Position : unsigned char { Relative, Absolute };
    enum class Justify : unsigned char { Start, Center, End, SpaceBetween, SpaceAround, SpaceEvenly };
    enum class Align : unsigned char { Auto, Stretch, Start, Center, End };
    enum class Overflow : unsigned char { Visible, Hidden, Scroll };
    enum class TextAlign : unsigned char { Start, End, Left, Center, Right };

    Display display = Display::Flex;
    Position position = Position::Relative;
    bool row = false;  ///< flex-direction: row; column otherwise (the default of the interface)
    bool wrap = false;
    Justify justify = Justify::Start;
    Align align_items = Align::Stretch;
    Align align_self = Align::Auto;
    float grow = 0;
    float shrink = 1;
    Length basis;  ///< flex-basis: auto (its width or its content's), or a length
    float gap = 0;
    Length width, height, min_width, min_height, max_width, max_height;
    Length left, top, right, bottom;
    float padding[4] = {0, 0, 0, 0};  ///< top, right, bottom, left
    /// Auto margins take the free space of their line (centering); 0 by default.
    Length margin[4] = {{Length::Unit::Points, 0}, {Length::Unit::Points, 0}, {Length::Unit::Points, 0},
                        {Length::Unit::Points, 0}};
    Overflow overflow = Overflow::Visible;

    glm::vec4 background{0.0f};
    std::string background_image;  ///< asset path ("" none)
    float image_slice = 0;         ///< nine-slice border of the image, in its pixels
    glm::vec4 image_color{1.0f};   ///< tint of the image
    glm::vec4 border_color{0.0f};
    float border_width = 0;
    float border_radius = 0;
    glm::vec4 fill_color{0.95f, 0.75f, 0.3f, 1.0f};  ///< bars, sliders, checkboxes
    float opacity = 1;
    glm::vec2 translate{0.0f};
    float scale = 1;

    // Inherited.
    glm::vec4 color{1.0f};
    std::string font_family;  ///< a font or font stack ("" : the interface's)
    float font_size = 16;
    float font_weight = 400;
    bool italic = false;
    TextAlign text_align = TextAlign::Start;
    float line_spacing = 1;
    float text_outline_width = 0;
    glm::vec4 text_outline_color{0.0f, 0.0f, 0.0f, 1.0f};
    bool text_shadow = false;
    glm::vec2 text_shadow_offset{0.0f};
    float text_shadow_blur = 0;
    glm::vec4 text_shadow_color{0.0f, 0.0f, 0.0f, 0.6f};

    std::vector<Transition> transitions;
};

/// Parses a color (`#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa`, `rgb()`, `rgba()`, a few names:
/// white, black, transparent, red, green, blue, yellow, gray, orange). Linear-agnostic: the values
/// are used as the sprites use them.
std::optional<glm::vec4> parse_color(std::string_view text);
/// Parses a length (`12`, `12px`, `12pt`: points; `50%`; `auto`).
std::optional<Length> parse_length(std::string_view text);

/// The style of an element: `parent` (null for a root), the declarations that apply in cascade
/// order (later wins), the variables in scope (updated with the element's own `--*`). Unknown
/// properties and bad values are reported in `warnings` ("property: problem") and skipped.
ComputedStyle compute_style(const ComputedStyle* parent, const std::vector<const Declaration*>& declarations,
                            std::map<std::string, std::string, std::less<>>& variables,
                            std::vector<std::string>* warnings = nullptr);

/// Eases t in [0, 1].
float ease(Easing easing, float t);

}  // namespace moteur::ui
