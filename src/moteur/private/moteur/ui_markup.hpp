#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// The markup of the interface's screens (milestone 9 bis, part 6): an XML subset read by the
/// engine. Elements, attributes ("..." or '...'), text, comments, the entities &lt; &gt; &amp;
/// &quot; &apos; &#NN; &#xNN;. No namespaces, no DTD, no processing instructions (an `<?xml ...?>`
/// line is skipped). Text between tags has its whitespace runs collapsed to one space and is
/// trimmed (a screen's layout is not its source's indentation).
namespace moteur::ui {

struct MarkupNode {
    std::string tag;  ///< empty for a text node
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<MarkupNode> children;
    std::string text;  ///< a text node's text
    int line = 0;

    /// The attribute's value, or `fallback`.
    std::string attribute(std::string_view name, std::string_view fallback = {}) const;
    bool has(std::string_view name) const;
};

/// A markup error: its line (1-based) and what is wrong.
struct MarkupError : std::runtime_error {
    MarkupError(int line, const std::string& message)
        : std::runtime_error("line " + std::to_string(line) + ": " + message), line(line) {}
    int line;
};

/// The single root element of `text`. Throws MarkupError.
MarkupNode parse_markup(std::string_view text);

/// `text` with the five entities and numeric references decoded (attributes and text).
std::string decode_entities(std::string_view text, int line);

}  // namespace moteur::ui
