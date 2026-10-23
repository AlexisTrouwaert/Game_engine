#include "moteur/ui_markup.hpp"

#include <cstdlib>

namespace moteur::ui {

namespace {

void append_utf8(std::string& out, char32_t c) {
    if (c < 0x80) {
        out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (c >> 6)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (c >> 12)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (c >> 18)));
        out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
}

bool is_name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
           c == ':' || c == '.';
}

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

class Reader {
public:
    explicit Reader(std::string_view text) : s_(text) {}

    MarkupNode document() {
        skip_misc();
        if (peek() != '<') {
            fail("expected the root element");
        }
        MarkupNode root = element();
        skip_misc();
        if (pos_ < s_.size()) {
            fail("text after the root element");
        }
        return root;
    }

private:
    [[noreturn]] void fail(const std::string& message) const { throw MarkupError(line_, message); }

    char peek(std::size_t ahead = 0) const { return pos_ + ahead < s_.size() ? s_[pos_ + ahead] : '\0'; }
    bool starts(std::string_view prefix) const { return s_.substr(pos_, prefix.size()) == prefix; }
    void advance(std::size_t n = 1) {
        for (std::size_t k = 0; k < n && pos_ < s_.size(); ++k) {
            if (s_[pos_] == '\n') {
                ++line_;
            }
            ++pos_;
        }
    }
    void skip_space() {
        while (pos_ < s_.size() && is_space(s_[pos_])) {
            advance();
        }
    }
    // Spaces, comments and an <?xml ...?> declaration.
    void skip_misc() {
        while (true) {
            skip_space();
            if (starts("<!--")) {
                comment();
            } else if (starts("<?")) {
                const std::size_t end = s_.find("?>", pos_);
                if (end == std::string_view::npos) {
                    fail("unterminated <?");
                }
                advance(end + 2 - pos_);
            } else if (starts("\xEF\xBB\xBF")) {
                advance(3);
            } else {
                return;
            }
        }
    }
    void comment() {
        const std::size_t end = s_.find("-->", pos_ + 4);
        if (end == std::string_view::npos) {
            fail("unterminated comment");
        }
        advance(end + 3 - pos_);
    }
    std::string name() {
        const std::size_t start = pos_;
        while (pos_ < s_.size() && is_name_char(s_[pos_])) {
            ++pos_;
        }
        if (pos_ == start) {
            fail("expected a name");
        }
        return std::string(s_.substr(start, pos_ - start));
    }

    MarkupNode element() {
        MarkupNode node;
        node.line = line_;
        advance();  // <
        node.tag = name();
        while (true) {
            skip_space();
            if (starts("/>")) {
                advance(2);
                return node;
            }
            if (peek() == '>') {
                advance();
                break;
            }
            if (pos_ >= s_.size()) {
                fail("unterminated tag <" + node.tag + ">");
            }
            const std::string key = name();
            skip_space();
            if (peek() != '=') {
                fail("attribute '" + key + "' without a value");
            }
            advance();
            skip_space();
            const char quote = peek();
            if (quote != '"' && quote != '\'') {
                fail("attribute '" + key + "': the value must be quoted");
            }
            advance();
            const std::size_t end = s_.find(quote, pos_);
            if (end == std::string_view::npos) {
                fail("unterminated value of '" + key + "'");
            }
            const int value_line = line_;
            const std::string_view raw = s_.substr(pos_, end - pos_);
            advance(end + 1 - pos_);
            for (const auto& [existing, value] : node.attributes) {
                if (existing == key) {
                    fail("attribute '" + key + "' given twice");
                }
            }
            node.attributes.emplace_back(key, decode_entities(raw, value_line));
        }
        // Content.
        std::string text;
        int text_line = line_;
        const auto flush = [&] {
            // Whitespace collapsed and trimmed.
            std::string collapsed;
            bool space = false;
            for (const char c : text) {
                if (is_space(c)) {
                    space = true;
                } else {
                    if (space && !collapsed.empty()) {
                        collapsed.push_back(' ');
                    }
                    space = false;
                    collapsed.push_back(c);
                }
            }
            if (!collapsed.empty()) {
                MarkupNode t;
                t.text = decode_entities(collapsed, text_line);
                t.line = text_line;
                node.children.push_back(std::move(t));
            }
            text.clear();
        };
        while (true) {
            if (pos_ >= s_.size()) {
                fail("<" + node.tag + "> is not closed");
            }
            if (starts("<!--")) {
                comment();
                continue;
            }
            if (starts("</")) {
                flush();
                advance(2);
                const std::string closing = name();
                skip_space();
                if (peek() != '>') {
                    fail("expected '>'");
                }
                advance();
                if (closing != node.tag) {
                    fail("</" + closing + "> closes <" + node.tag + ">");
                }
                return node;
            }
            if (peek() == '<') {
                flush();
                node.children.push_back(element());
                text_line = line_;
                continue;
            }
            if (text.empty()) {
                text_line = line_;
            }
            text.push_back(peek());
            advance();
        }
    }

    std::string_view s_;
    std::size_t pos_ = 0;
    int line_ = 1;
};

}  // namespace

std::string MarkupNode::attribute(std::string_view name, std::string_view fallback) const {
    for (const auto& [key, value] : attributes) {
        if (key == name) {
            return value;
        }
    }
    return std::string(fallback);
}

bool MarkupNode::has(std::string_view name) const {
    for (const auto& [key, value] : attributes) {
        if (key == name) {
            return true;
        }
    }
    return false;
}

std::string decode_entities(std::string_view text, int line) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            out.push_back(text[i]);
            continue;
        }
        const std::size_t end = text.find(';', i);
        if (end == std::string_view::npos || end - i > 10) {
            throw MarkupError(line, "'&' not starting an entity (write &amp;)");
        }
        const std::string_view entity = text.substr(i + 1, end - i - 1);
        if (entity == "lt") out.push_back('<');
        else if (entity == "gt") out.push_back('>');
        else if (entity == "amp") out.push_back('&');
        else if (entity == "quot") out.push_back('"');
        else if (entity == "apos") out.push_back('\'');
        else if (!entity.empty() && entity[0] == '#') {
            const bool hex = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X');
            const std::string digits(entity.substr(hex ? 2 : 1));
            char* stop = nullptr;
            const unsigned long code = std::strtoul(digits.c_str(), &stop, hex ? 16 : 10);
            if (digits.empty() || *stop != '\0' || code == 0 || code > 0x10FFFF) {
                throw MarkupError(line, "bad character reference &" + std::string(entity) + ";");
            }
            append_utf8(out, static_cast<char32_t>(code));
        } else {
            throw MarkupError(line, "unknown entity &" + std::string(entity) + ";");
        }
        i = end;
    }
    return out;
}

MarkupNode parse_markup(std::string_view text) {
    return Reader(text).document();
}

}  // namespace moteur::ui
