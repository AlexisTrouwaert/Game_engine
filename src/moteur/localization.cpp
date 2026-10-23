#include "moteur/localization.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

#include "moteur/document.hpp"
#include "moteur/paths.hpp"
#include "moteur/utf8.hpp"

namespace moteur {

namespace {

// --- The syntax tree --------------------------------------------------------------------------------

struct Expr;
using ExprPtr = std::shared_ptr<const Expr>;

struct Element {
    std::string text;  // when expr is null
    ExprPtr expr;
};

struct Pattern {
    std::vector<Element> elements;
};

struct Variant {
    std::string key;
    bool numeric = false;
    double number = 0;
    Pattern value;
};

struct Expr {
    enum class Kind { String, Number, Variable, Message, Term, Function, Select };
    Kind kind = Kind::String;
    std::string name;       // literal text, variable, message, term or function name
    std::string attribute;  // message or term attribute
    double number = 0;
    int digits = 0;         // fraction digits written in a number literal
    std::vector<ExprPtr> positional;
    std::vector<std::pair<std::string, ExprPtr>> named;
    ExprPtr selector;
    std::vector<Variant> variants;
    std::size_t default_variant = 0;
};

struct Entry {
    Pattern value;
    bool has_value = false;
    std::map<std::string, Pattern, std::less<>> attributes;
    std::string file;
    int line = 0;
};

// --- The parser -------------------------------------------------------------------------------------

bool is_identifier_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool is_identifier_char(char c) {
    return is_identifier_start(c) || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

struct ParseError {
    std::string message;
};

class Parser {
public:
    Parser(std::string_view text, std::string_view file, std::string_view language, std::vector<LocalizationIssue>& issues)
        : src_(text), file_(file), language_(language), issues_(issues) {}

    // Reads every entry into `messages` and `terms`; false if there were errors.
    bool parse(std::map<std::string, Entry, std::less<>>& messages, std::map<std::string, Entry, std::less<>>& terms) {
        bool ok = true;
        if (src_.substr(0, 3) == "\xEF\xBB\xBF") {
            pos_ = 3;  // a UTF-8 byte order mark
        }
        while (pos_ < src_.size()) {
            const char c = src_[pos_];
            if (c == '\n' || c == '\r') {
                next_line();
                continue;
            }
            if (c == '#') {
                skip_line();
                continue;
            }
            const int entry_line = line_;
            const std::size_t entry_start = pos_;
            try {
                if (c == '-' || is_identifier_start(c)) {
                    const bool term = c == '-';
                    if (term) {
                        ++pos_;
                    }
                    const std::string id = identifier();
                    skip_inline_blank();
                    expect('=');
                    skip_inline_blank();
                    Entry entry;
                    entry.file = std::string(file_);
                    entry.line = entry_line;
                    entry.value = pattern();
                    entry.has_value = !entry.value.elements.empty();
                    // Attributes: indented lines starting with '.'.
                    while (true) {
                        const std::size_t save = pos_;
                        const int save_line = line_;
                        if (!at_newline()) {
                            break;
                        }
                        next_line();
                        skip_blank_lines();
                        if (pos_ >= src_.size() || (src_[pos_] != ' ' && src_[pos_] != '\t')) {
                            pos_ = save;
                            line_ = save_line;
                            break;
                        }
                        skip_inline_blank();
                        if (pos_ >= src_.size() || src_[pos_] != '.') {
                            throw ParseError{"an indented line that continues nothing"};
                        }
                        ++pos_;
                        const std::string attribute = identifier();
                        skip_inline_blank();
                        expect('=');
                        skip_inline_blank();
                        entry.attributes[attribute] = pattern();
                    }
                    if (!entry.has_value && entry.attributes.empty()) {
                        throw ParseError{"'" + id + "' has no value"};
                    }
                    if (term && !entry.has_value) {
                        throw ParseError{"the term '-" + id + "' has no value"};
                    }
                    (term ? terms : messages)[id] = std::move(entry);
                    if (pos_ < src_.size() && !at_newline()) {
                        throw ParseError{"unexpected text after the entry"};
                    }
                } else {
                    throw ParseError{"an entry must start with an identifier at the start of the line"};
                }
            } catch (const ParseError& error) {
                LocalizationIssue issue;
                issue.kind = LocalizationIssue::Kind::Syntax;
                issue.language = std::string(language_);
                issue.file = std::string(file_);
                issue.line = entry_line;
                issue.message = error.message + " (line " + std::to_string(line_) + ")";
                issues_.push_back(issue);
                ok = false;
                // The entry is dropped: from its first line to the next line that starts an entry
                // (an identifier, a term or a comment at the start of the line).
                pos_ = entry_start;
                line_ = entry_line;
                skip_line();
                while (pos_ < src_.size() && !(is_identifier_start(src_[pos_]) || src_[pos_] == '-' || src_[pos_] == '#')) {
                    skip_line();
                }
            }
        }
        return ok;
    }

private:
    char peek(std::size_t ahead = 0) const {
        return pos_ + ahead < src_.size() ? src_[pos_ + ahead] : '\0';
    }
    bool at_newline() const {
        return pos_ < src_.size() && (src_[pos_] == '\n' || src_[pos_] == '\r');
    }
    void next_line() {
        if (peek() == '\r') {
            ++pos_;
        }
        if (peek() == '\n') {
            ++pos_;
        }
        ++line_;
    }
    void skip_line() {
        while (pos_ < src_.size() && !at_newline()) {
            ++pos_;
        }
        if (pos_ < src_.size()) {
            next_line();
        }
    }
    void skip_inline_blank() {
        while (peek() == ' ' || peek() == '\t') {
            ++pos_;
        }
    }
    // Spaces, tabs and newlines.
    void skip_blank() {
        while (true) {
            if (peek() == ' ' || peek() == '\t') {
                ++pos_;
            } else if (at_newline()) {
                next_line();
            } else {
                return;
            }
        }
    }
    // Whole lines of spaces only.
    void skip_blank_lines() {
        while (true) {
            std::size_t p = pos_;
            while (p < src_.size() && (src_[p] == ' ' || src_[p] == '\t')) {
                ++p;
            }
            if (p < src_.size() && (src_[p] == '\n' || src_[p] == '\r')) {
                pos_ = p;
                next_line();
            } else {
                return;
            }
        }
    }
    void expect(char c) {
        if (peek() != c) {
            throw ParseError{std::string("expected '") + c + "'"};
        }
        ++pos_;
    }
    std::string identifier() {
        if (!is_identifier_start(peek())) {
            throw ParseError{"expected an identifier"};
        }
        const std::size_t start = pos_;
        while (is_identifier_char(peek())) {
            ++pos_;
        }
        return std::string(src_.substr(start, pos_ - start));
    }

    // Whether the line after the current newline continues the pattern: indented, and not starting
    // an attribute, a variant or the end of a placeable. Blank lines in between count (`blank`).
    bool continues(int& blank_lines, std::size_t& content) const {
        std::size_t p = pos_;
        blank_lines = 0;
        bool first = true;
        while (p < src_.size()) {
            // p is on a newline: go past it.
            if (src_[p] == '\r') ++p;
            if (p < src_.size() && src_[p] == '\n') ++p;
            if (!first) {
                ++blank_lines;
            }
            first = false;
            std::size_t q = p;
            while (q < src_.size() && (src_[q] == ' ' || src_[q] == '\t')) {
                ++q;
            }
            if (q >= src_.size()) {
                return false;
            }
            if (src_[q] == '\n' || src_[q] == '\r') {
                p = q;
                continue;  // a blank line
            }
            if (q == p) {
                return false;  // not indented: a new entry
            }
            const char c = src_[q];
            if (c == '.' || c == '[' || c == '*' || c == '}') {
                return false;
            }
            content = q;
            return true;
        }
        return false;
    }

    Pattern pattern() {
        Pattern out;
        std::string text;
        const auto flush = [&] {
            if (!text.empty()) {
                out.elements.push_back({text, nullptr});
                text.clear();
            }
        };
        while (pos_ < src_.size()) {
            const char c = src_[pos_];
            if (c == '{') {
                flush();
                out.elements.push_back({{}, placeable()});
            } else if (c == '}') {
                throw ParseError{"'}' without '{' (write {\"}\"} for the character)"};
            } else if (c == '\n' || c == '\r') {
                int blank = 0;
                std::size_t content = 0;
                if (!continues(blank, content)) {
                    break;
                }
                // Count the lines skipped for the line numbers of later errors.
                for (std::size_t p = pos_; p < content; ++p) {
                    if (src_[p] == '\n') {
                        ++line_;
                    }
                }
                if (!out.elements.empty() || !text.empty()) {
                    // Trailing spaces of a line go.
                    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
                        text.pop_back();
                    }
                    text.append(static_cast<std::size_t>(blank) + 1, '\n');
                }
                pos_ = content;  // the common indentation (all of it) is removed
            } else {
                text.push_back(c);
                ++pos_;
            }
        }
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
            text.pop_back();
        }
        flush();
        return out;
    }

    ExprPtr placeable() {
        expect('{');
        skip_blank();
        ExprPtr expr = inline_expression();
        skip_blank();
        if (peek() == '-' && peek(1) == '>') {
            pos_ += 2;
            auto select = std::make_shared<Expr>();
            select->kind = Expr::Kind::Select;
            select->selector = expr;
            skip_inline_blank();
            bool has_default = false;
            while (true) {
                skip_blank();
                if (peek() == '}') {
                    break;
                }
                bool is_default = false;
                if (peek() == '*') {
                    ++pos_;
                    is_default = true;
                }
                expect('[');
                skip_inline_blank();
                Variant variant;
                if ((peek() >= '0' && peek() <= '9') || peek() == '-') {
                    const std::size_t start = pos_;
                    while ((peek() >= '0' && peek() <= '9') || peek() == '.' || peek() == '-') {
                        ++pos_;
                    }
                    variant.key = std::string(src_.substr(start, pos_ - start));
                    variant.numeric = true;
                    variant.number = std::strtod(variant.key.c_str(), nullptr);
                } else {
                    variant.key = identifier();
                }
                skip_inline_blank();
                expect(']');
                skip_inline_blank();
                variant.value = pattern();
                if (is_default) {
                    if (has_default) {
                        throw ParseError{"two default variants (*)"};
                    }
                    has_default = true;
                    select->default_variant = select->variants.size();
                }
                select->variants.push_back(std::move(variant));
            }
            if (!has_default) {
                throw ParseError{"a selector needs one default variant (*[...])"};
            }
            expect('}');
            return select;
        }
        expect('}');
        return expr;
    }

    std::vector<std::pair<std::string, ExprPtr>> named_args_of(std::vector<ExprPtr>& positional) {
        std::vector<std::pair<std::string, ExprPtr>> named;
        expect('(');
        while (true) {
            skip_blank();
            if (peek() == ')') {
                ++pos_;
                break;
            }
            // name: literal, or an expression.
            const std::size_t save = pos_;
            if (is_identifier_start(peek())) {
                const std::string name = identifier();
                skip_blank();
                if (peek() == ':') {
                    ++pos_;
                    skip_blank();
                    ExprPtr value = inline_expression();
                    if (value->kind != Expr::Kind::String && value->kind != Expr::Kind::Number) {
                        throw ParseError{"a named argument takes a literal (\"text\" or a number)"};
                    }
                    named.emplace_back(name, value);
                } else {
                    pos_ = save;
                    positional.push_back(inline_expression());
                }
            } else {
                positional.push_back(inline_expression());
            }
            skip_blank();
            if (peek() == ',') {
                ++pos_;
            } else if (peek() != ')') {
                throw ParseError{"expected ',' or ')'"};
            }
        }
        return named;
    }

    ExprPtr inline_expression() {
        auto expr = std::make_shared<Expr>();
        const char c = peek();
        if (c == '"') {
            ++pos_;
            expr->kind = Expr::Kind::String;
            while (true) {
                const char d = peek();
                if (d == '\0' || d == '\n' || d == '\r') {
                    throw ParseError{"unterminated string"};
                }
                ++pos_;
                if (d == '"') {
                    break;
                }
                if (d == '\\') {
                    const char e = peek();
                    ++pos_;
                    if (e == '"' || e == '\\') {
                        expr->name.push_back(e);
                    } else if (e == 'u' || e == 'U') {
                        const int count = e == 'u' ? 4 : 6;
                        if (pos_ + static_cast<std::size_t>(count) > src_.size()) {
                            throw ParseError{"bad \\u escape"};
                        }
                        const std::string hex(src_.substr(pos_, static_cast<std::size_t>(count)));
                        pos_ += static_cast<std::size_t>(count);
                        const auto code = static_cast<char32_t>(std::strtoul(hex.c_str(), nullptr, 16));
                        // UTF-8 of the code point.
                        if (code < 0x80) {
                            expr->name.push_back(static_cast<char>(code));
                        } else if (code < 0x800) {
                            expr->name.push_back(static_cast<char>(0xC0 | (code >> 6)));
                            expr->name.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        } else if (code < 0x10000) {
                            expr->name.push_back(static_cast<char>(0xE0 | (code >> 12)));
                            expr->name.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                            expr->name.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        } else {
                            expr->name.push_back(static_cast<char>(0xF0 | (code >> 18)));
                            expr->name.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
                            expr->name.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                            expr->name.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        }
                    } else {
                        throw ParseError{"unknown escape in a string"};
                    }
                } else {
                    expr->name.push_back(d);
                }
            }
            return expr;
        }
        if ((c >= '0' && c <= '9') || (c == '-' && peek(1) >= '0' && peek(1) <= '9')) {
            const std::size_t start = pos_;
            ++pos_;
            while ((peek() >= '0' && peek() <= '9') || peek() == '.') {
                ++pos_;
            }
            const std::string text(src_.substr(start, pos_ - start));
            expr->kind = Expr::Kind::Number;
            expr->number = std::strtod(text.c_str(), nullptr);
            const std::size_t point = text.find('.');
            expr->digits = point == std::string::npos ? 0 : static_cast<int>(text.size() - point - 1);
            return expr;
        }
        if (c == '$') {
            ++pos_;
            expr->kind = Expr::Kind::Variable;
            expr->name = identifier();
            return expr;
        }
        if (c == '{') {
            return placeable();
        }
        if (c == '-') {
            ++pos_;
            expr->kind = Expr::Kind::Term;
            expr->name = identifier();
            if (peek() == '.') {
                ++pos_;
                expr->attribute = identifier();
            }
            if (peek() == '(') {
                std::vector<ExprPtr> positional;
                expr->named = named_args_of(positional);
            }
            return expr;
        }
        if (is_identifier_start(c)) {
            const std::string id = identifier();
            if (peek() == '(') {
                expr->kind = Expr::Kind::Function;
                expr->name = id;
                expr->named = named_args_of(expr->positional);
                return expr;
            }
            expr->kind = Expr::Kind::Message;
            expr->name = id;
            if (peek() == '.') {
                ++pos_;
                expr->attribute = identifier();
            }
            return expr;
        }
        throw ParseError{"expected an expression"};
    }

    std::string_view src_;
    std::string_view file_;
    std::string_view language_;
    std::vector<LocalizationIssue>& issues_;
    std::size_t pos_ = 0;
    int line_ = 1;
};

// --- Helpers ----------------------------------------------------------------------------------------

bool is_rtl_language(std::string_view language) {
    const std::string_view base = language.substr(0, language.find('-'));
    return base == "ar" || base == "he" || base == "fa" || base == "ur" || base == "ps" || base == "sd" ||
           base == "yi" || base == "dv" || base == "ug" || base == "ckb";
}

// The variables a pattern uses itself (not through references).
void collect_variables(const Pattern& pattern, std::set<std::string>& out);

void collect_expr_variables(const Expr& expr, std::set<std::string>& out) {
    if (expr.kind == Expr::Kind::Variable) {
        out.insert(expr.name);
    }
    for (const ExprPtr& p : expr.positional) {
        collect_expr_variables(*p, out);
    }
    if (expr.selector) {
        collect_expr_variables(*expr.selector, out);
    }
    for (const Variant& v : expr.variants) {
        collect_variables(v.value, out);
    }
}

void collect_variables(const Pattern& pattern, std::set<std::string>& out) {
    for (const Element& e : pattern.elements) {
        if (e.expr) {
            collect_expr_variables(*e.expr, out);
        }
    }
}

void collect_references(const Pattern& pattern, std::vector<const Expr*>& out);

void collect_expr_references(const Expr& expr, std::vector<const Expr*>& out) {
    if (expr.kind == Expr::Kind::Message || expr.kind == Expr::Kind::Term) {
        out.push_back(&expr);
    }
    for (const ExprPtr& p : expr.positional) {
        collect_expr_references(*p, out);
    }
    if (expr.selector) {
        collect_expr_references(*expr.selector, out);
    }
    for (const Variant& v : expr.variants) {
        collect_references(v.value, out);
    }
}

void collect_references(const Pattern& pattern, std::vector<const Expr*>& out) {
    for (const Element& e : pattern.elements) {
        if (e.expr) {
            collect_expr_references(*e.expr, out);
        }
    }
}

// The text of a pattern with its placeables as "{}" (to compare translations).
std::string flat_text(const Pattern& pattern) {
    std::string out;
    for (const Element& e : pattern.elements) {
        out += e.expr ? "{}" : e.text;
    }
    return out;
}

// Pseudo-localization of a text: accented look-alikes (in Inter's and Latin-1's reach).
std::string pseudo_text(std::string_view text) {
    static const std::pair<char, const char*> kAccents[] = {
        {'a', "\xC3\xA1"}, {'c', "\xC3\xA7"}, {'e', "\xC3\xA9"}, {'i', "\xC3\xAD"}, {'o', "\xC3\xB3"},
        {'u', "\xC3\xBA"}, {'n', "\xC3\xB1"}, {'y', "\xC3\xBD"}, {'s', "\xC5\xA1"}, {'z', "\xC5\xBE"},
        {'A', "\xC3\x81"}, {'C', "\xC3\x87"}, {'E', "\xC3\x89"}, {'I', "\xC3\x8D"}, {'O', "\xC3\x93"},
        {'U', "\xC3\x9A"}, {'N', "\xC3\x91"}, {'Y', "\xC3\x9D"}, {'S', "\xC5\xA0"}, {'Z', "\xC5\xBD"}};
    std::string out;
    for (const char c : text) {
        const char* replacement = nullptr;
        for (const auto& [from, to] : kAccents) {
            if (from == c) {
                replacement = to;
                break;
            }
        }
        if (replacement != nullptr) {
            out += replacement;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::size_t letters_in(const Pattern& pattern) {
    std::size_t count = 0;
    for (const Element& e : pattern.elements) {
        count += e.expr ? 2 : decode_utf8(e.text).size();
    }
    return count;
}

Pattern pseudo_pattern(const Pattern& pattern, bool top_level);

ExprPtr pseudo_expr(const ExprPtr& expr) {
    if (expr->kind != Expr::Kind::Select) {
        return expr;
    }
    auto copy = std::make_shared<Expr>(*expr);
    for (Variant& v : copy->variants) {
        v.value = pseudo_pattern(v.value, false);
    }
    return copy;
}

Pattern pseudo_pattern(const Pattern& pattern, bool top_level) {
    Pattern out;
    if (top_level) {
        out.elements.push_back({"[", nullptr});
    }
    for (const Element& e : pattern.elements) {
        if (e.expr) {
            out.elements.push_back({{}, pseudo_expr(e.expr)});
        } else {
            out.elements.push_back({pseudo_text(e.text), nullptr});
        }
    }
    if (top_level) {
        // 40 % longer: German and Russian texts are about a third longer than English ones.
        const std::size_t extra = (letters_in(pattern) * 2 + 4) / 5;
        std::string pad;
        for (std::size_t k = 0; k < extra; ++k) {
            pad += "\xC2\xB7";  // ·
        }
        out.elements.push_back({pad + "]", nullptr});
    }
    return out;
}

}  // namespace

// --- Bundles ----------------------------------------------------------------------------------------

struct Localization::Bundle {
    std::map<std::string, Entry, std::less<>> messages;
    std::map<std::string, Entry, std::less<>> terms;
};

namespace {

// --- Resolution -------------------------------------------------------------------------------------

struct Value {
    LocalizedValue value;
    NumberFormatOptions options;
    bool ordinal = false;
    bool is_number() const { return !std::holds_alternative<std::string>(value); }
    double number() const {
        if (const auto* i = std::get_if<std::int64_t>(&value)) return static_cast<double>(*i);
        if (const auto* d = std::get_if<double>(&value)) return *d;
        return 0.0;
    }
};

using Variables = std::vector<std::pair<std::string_view, Value>>;

struct Scope {
    const Localization& localization;
    const Localization::Bundle* bundle;       // the current language's
    const Localization::Bundle* fallback;     // the base language's
    std::string_view language;
    const Variables* variables;
    const std::map<std::string, Localization::Function, std::less<>>* functions;
    int depth = 0;
    bool isolate = false;
};

std::string to_text(const Value& v, std::string_view language) {
    if (const auto* s = std::get_if<std::string>(&v.value)) {
        return *s;
    }
    return format_number(language, v.number(), v.options);
}

// The fraction digits a number shows once formatted (its plural operands depend on them).
int visible_digits(double value, const NumberFormatOptions& options) {
    const std::string text = format_number("en", value, NumberFormatOptions{options.min_fraction_digits,
                                                                             options.max_fraction_digits, false,
                                                                             options.percent});
    const std::size_t point = text.find('.');
    if (point == std::string::npos) {
        return 0;
    }
    std::size_t end = point + 1;
    while (end < text.size() && text[end] >= '0' && text[end] <= '9') {
        ++end;
    }
    return static_cast<int>(end - point - 1);
}

const Entry* find_entry(const Scope& scope, bool term, std::string_view id) {
    for (const Localization::Bundle* b : {scope.bundle, scope.fallback}) {
        if (b == nullptr) {
            continue;
        }
        const auto& table = term ? b->terms : b->messages;
        const auto it = table.find(id);
        if (it != table.end()) {
            return &it->second;
        }
    }
    return nullptr;
}

std::string resolve_pattern(const Pattern& pattern, Scope& scope);

Value resolve(const Expr& expr, Scope& scope) {
    switch (expr.kind) {
        case Expr::Kind::String:
            return {expr.name};
        case Expr::Kind::Number: {
            Value v{expr.number};
            v.options.min_fraction_digits = expr.digits;
            v.options.max_fraction_digits = std::max(expr.digits, 3);
            return v;
        }
        case Expr::Kind::Variable:
            if (scope.variables != nullptr) {
                for (const auto& [name, value] : *scope.variables) {
                    if (name == expr.name) {
                        return value;
                    }
                }
            }
            return {"{$" + expr.name + "}"};
        case Expr::Kind::Message: {
            const Entry* entry = find_entry(scope, false, expr.name);
            if (entry == nullptr || scope.depth > 32) {
                return {"{" + expr.name + "}"};
            }
            const Pattern* p = &entry->value;
            if (!expr.attribute.empty()) {
                const auto it = entry->attributes.find(expr.attribute);
                if (it == entry->attributes.end()) {
                    return {"{" + expr.name + "." + expr.attribute + "}"};
                }
                p = &it->second;
            }
            ++scope.depth;
            std::string text = resolve_pattern(*p, scope);
            --scope.depth;
            return {std::move(text)};
        }
        case Expr::Kind::Term: {
            const Entry* entry = find_entry(scope, true, expr.name);
            if (entry == nullptr || scope.depth > 32) {
                return {"{-" + expr.name + "}"};
            }
            const Pattern* p = &entry->value;
            if (!expr.attribute.empty()) {
                const auto it = entry->attributes.find(expr.attribute);
                if (it == entry->attributes.end()) {
                    return {"{-" + expr.name + "." + expr.attribute + "}"};
                }
                p = &it->second;
            }
            // A term sees only its own arguments.
            Variables arguments;
            for (const auto& [name, value] : expr.named) {
                arguments.emplace_back(name, resolve(*value, scope));
            }
            Scope inner = scope;
            inner.variables = &arguments;
            inner.isolate = false;
            ++inner.depth;
            return {resolve_pattern(*p, inner)};
        }
        case Expr::Kind::Function: {
            std::vector<Value> positional;
            positional.reserve(expr.positional.size());
            for (const ExprPtr& p : expr.positional) {
                positional.push_back(resolve(*p, scope));
            }
            if (expr.name == "NUMBER") {
                if (positional.empty() || !positional[0].is_number()) {
                    return {"{NUMBER()}"};
                }
                Value v = positional[0];
                for (const auto& [name, arg] : expr.named) {
                    const Value option = resolve(*arg, scope);
                    const std::string text = to_text(option, "en");
                    if (name == "minimumFractionDigits") {
                        v.options.min_fraction_digits = static_cast<int>(option.number());
                        v.options.max_fraction_digits = std::max(v.options.max_fraction_digits, v.options.min_fraction_digits);
                    } else if (name == "maximumFractionDigits") {
                        v.options.max_fraction_digits = static_cast<int>(option.number());
                        v.options.min_fraction_digits = std::min(v.options.min_fraction_digits, v.options.max_fraction_digits);
                    } else if (name == "useGrouping") {
                        v.options.grouping = text != "false";
                    } else if (name == "style") {
                        v.options.percent = text == "percent";
                    } else if (name == "type") {
                        v.ordinal = text == "ordinal";
                    }
                }
                return v;
            }
            if (expr.name == "DURATION") {
                if (positional.empty() || !positional[0].is_number()) {
                    return {"{DURATION()}"};
                }
                return {scope.localization.format_duration(positional[0].number())};
            }
            const auto it = scope.functions->find(expr.name);
            if (it == scope.functions->end()) {
                return {"{" + expr.name + "()}"};
            }
            std::vector<LocalizedValue> args;
            args.reserve(positional.size());
            for (const Value& v : positional) {
                args.push_back(v.value);
            }
            std::map<std::string, LocalizedValue, std::less<>> named;
            for (const auto& [name, arg] : expr.named) {
                named[name] = resolve(*arg, scope).value;
            }
            return {it->second(scope.localization, args, named)};
        }
        case Expr::Kind::Select: {
            const Value selector = resolve(*expr.selector, scope);
            const Variant* chosen = &expr.variants[expr.default_variant];
            if (selector.is_number()) {
                const double n = selector.number();
                bool exact = false;
                for (const Variant& v : expr.variants) {
                    if (v.numeric && v.number == n) {
                        chosen = &v;
                        exact = true;
                        break;
                    }
                }
                if (!exact) {
                    const PluralOperands operands = PluralOperands::of(
                        selector.options.percent ? n * 100.0 : n, visible_digits(n, selector.options));
                    const char* category =
                        plural_category_name(plural_category(scope.language, operands, selector.ordinal));
                    for (const Variant& v : expr.variants) {
                        if (!v.numeric && v.key == category) {
                            chosen = &v;
                            break;
                        }
                    }
                }
            } else {
                const std::string& text = std::get<std::string>(selector.value);
                for (const Variant& v : expr.variants) {
                    if (!v.numeric && v.key == text) {
                        chosen = &v;
                        break;
                    }
                }
            }
            return {resolve_pattern(chosen->value, scope)};
        }
    }
    return {std::string()};
}

std::string resolve_pattern(const Pattern& pattern, Scope& scope) {
    std::string out;
    for (const Element& e : pattern.elements) {
        if (!e.expr) {
            out += e.text;
            continue;
        }
        const bool from_variable = e.expr->kind == Expr::Kind::Variable || e.expr->kind == Expr::Kind::Function;
        const std::string text = to_text(resolve(*e.expr, scope), scope.language);
        if (scope.isolate && from_variable) {
            out += "\xE2\x81\xA8";  // FSI: the value keeps its own direction in a right-to-left text
            out += text;
            out += "\xE2\x81\xA9";  // PDI
        } else {
            out += text;
        }
    }
    return out;
}

}  // namespace

// --- Localization -----------------------------------------------------------------------------------

Localization::Localization() = default;
Localization::~Localization() = default;

const Localization::Bundle* Localization::bundle(std::string_view language) const {
    const auto it = bundles_.find(language);
    return it == bundles_.end() ? nullptr : it->second.get();
}

Localization::Bundle& Localization::bundle_for_writing(std::string_view language) {
    auto it = bundles_.find(language);
    if (it == bundles_.end()) {
        it = bundles_.emplace(std::string(language), std::make_unique<Bundle>()).first;
    }
    return *it->second;
}

void Localization::clear() {
    bundles_.clear();
    issues_.clear();
    files_.clear();
    languages_.clear();
    ++generation_;
}

bool Localization::add_resource(std::string_view language, std::string_view text, std::string_view file) {
    Bundle& b = bundle_for_writing(language);
    Parser parser(text, file, language, issues_);
    const bool ok = parser.parse(b.messages, b.terms);
    if (language == base_) {
        make_pseudo();
    }
    ++generation_;
    return ok;
}

void Localization::make_pseudo() {
    const Bundle* base = bundle(base_);
    if (base == nullptr) {
        return;
    }
    auto pseudo = std::make_unique<Bundle>();
    for (const auto& [id, entry] : base->messages) {
        Entry copy = entry;
        if (copy.has_value) {
            copy.value = pseudo_pattern(entry.value, true);
        }
        for (auto& [name, pattern] : copy.attributes) {
            pattern = pseudo_pattern(entry.attributes.at(name), true);
        }
        pseudo->messages.emplace(id, std::move(copy));
    }
    for (const auto& [id, entry] : base->terms) {
        Entry copy = entry;
        copy.value = pseudo_pattern(entry.value, false);
        pseudo->terms.emplace(id, std::move(copy));  // attributes (genders) stay: selectors read them
    }
    bundles_[std::string(kPseudoLanguage)] = std::move(pseudo);
}

bool Localization::load(const std::string& folder, const std::vector<std::string>& engine_resources) {
    return load(std::vector<std::string>{folder}, engine_resources);
}

bool Localization::load(const std::vector<std::string>& folders, const std::vector<std::string>& engine_resources) {
    nlohmann::json document;
    bool found = false;
    for (const std::string& folder : folders) {
        const std::string index = folder + "/languages.json";
        if (!file_exists(index)) {
            continue;
        }
        try {
            const FileData file = read_file(index);
            document = parse_document(std::string_view(static_cast<const char*>(file.data()), file.size()));
            found = true;
        } catch (const std::exception& e) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Localization: %s", e.what());
        }
    }
    if (!found) {
        return false;
    }
    clear();
    base_ = document.value("base", std::string("fr"));
    std::vector<std::string> resources = engine_resources;
    for (const nlohmann::json& r : document.value("resources", nlohmann::json::array())) {
        if (r.is_string()) {
            resources.push_back(r.get<std::string>());
        }
    }
    for (const nlohmann::json& l : document.value("languages", nlohmann::json::array())) {
        LanguageInfo info;
        info.code = l.value("code", std::string());
        info.name = l.value("name", info.code);
        if (!info.code.empty()) {
            languages_.push_back(info);
        }
    }
    // The base language first: the pseudo-localization is made from it.
    std::vector<std::string> order{base_};
    for (const LanguageInfo& info : languages_) {
        if (info.code != base_) {
            order.push_back(info.code);
        }
    }
    for (const std::string& code : order) {
        for (const std::string& resource : resources) {
            for (const std::string& folder : folders) {
                std::string path = folder;
                path.append("/").append(code).append("/").append(resource).append(".ftl");
                if (!file_exists(path)) {
                    continue;  // a game without the engine's strings in a language falls back to the base
                }
                try {
                    const FileData file = read_file(path);
                    std::string name = code;
                    name.append("/").append(resource).append(".ftl");
                    add_resource(code, std::string_view(static_cast<const char*>(file.data()), file.size()), name);
                    files_.push_back(path);
                } catch (const std::exception& e) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Localization: %s", e.what());
                }
            }
        }
    }
    for (const LocalizationIssue& issue : issues_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Localization: %s:%d: %s", issue.file.c_str(), issue.line,
                    issue.message.c_str());
    }
    if (language_.empty() || bundle(language_) == nullptr) {
        language_ = base_;
    }
    ++generation_;
    return true;
}

std::vector<std::string> Localization::describe(const std::vector<LocalizationIssue>& issues, bool errors) {
    std::vector<std::string> lines;
    std::map<std::string, std::string> untranslated;
    for (const LocalizationIssue& issue : issues) {
        if (issue.is_error() != errors) {
            continue;
        }
        if (issue.kind == LocalizationIssue::Kind::Untranslated) {
            untranslated[issue.language] += " " + issue.id;
            continue;
        }
        std::string where = issue.file.empty() ? issue.language : issue.file;
        if (issue.line > 0) {
            where += ":" + std::to_string(issue.line);
        }
        lines.push_back("strings " + where + (issue.id.empty() ? "" : " (" + issue.id + ")") + ": " + issue.message);
    }
    for (const auto& [language, ids] : untranslated) {
        std::string line = "strings ";
        line.append(language).append(": same text as the base language (on purpose?):").append(ids);
        lines.push_back(std::move(line));
    }
    return lines;
}

bool Localization::set_language(std::string_view language) {
    if (bundle(language) == nullptr) {
        return false;
    }
    if (language_ != language) {
        language_ = std::string(language);
        ++generation_;
    }
    return true;
}

std::string Localization::preferred_language() const {
    int count = 0;
    SDL_Locale** locales = SDL_GetPreferredLocales(&count);
    std::string chosen;
    for (int i = 0; locales != nullptr && i < count && chosen.empty(); ++i) {
        const std::string lang = locales[i]->language != nullptr ? locales[i]->language : "";
        const std::string full = locales[i]->country != nullptr ? lang + "-" + locales[i]->country : lang;
        for (const std::string& candidate : {full, lang}) {
            for (const LanguageInfo& info : languages_) {
                if (info.code == candidate) {
                    chosen = info.code;
                    break;
                }
            }
            if (!chosen.empty()) {
                break;
            }
        }
    }
    SDL_free(static_cast<void*>(locales));
    return chosen.empty() ? base_ : chosen;
}

std::string Localization::format(std::string_view id, const std::vector<LocalizedArg>& args) const {
    Variables variables;
    variables.reserve(args.size());
    for (const LocalizedArg& a : args) {
        variables.emplace_back(a.name, Value{a.value});
    }
    const std::string_view language = language_.empty() ? std::string_view(base_) : std::string_view(language_);
    // The pseudo-localization writes numbers and chooses plurals as its base language.
    const std::string_view rules = language == kPseudoLanguage ? std::string_view(base_) : language;
    Scope scope{*this, bundle(language), bundle(base_), rules, &variables, &functions_};
    scope.isolate = is_rtl_language(rules);

    const std::size_t dot = id.find('.');
    const std::string_view message_id = id.substr(0, dot);
    const std::string_view attribute = dot == std::string_view::npos ? std::string_view() : id.substr(dot + 1);
    const Entry* entry = nullptr;
    const Bundle* current = scope.bundle;
    if (current != nullptr) {
        const auto it = current->messages.find(message_id);
        if (it != current->messages.end()) {
            entry = &it->second;
        }
    }
    if (entry == nullptr && scope.fallback != nullptr) {
        const auto it = scope.fallback->messages.find(message_id);
        if (it != scope.fallback->messages.end()) {
            entry = &it->second;
            scope.language = base_;  // its numbers and plurals are the base language's
            scope.isolate = is_rtl_language(base_);
            if (language != kPseudoLanguage) {
                warn_missing(message_id, false);
            }
        }
    }
    if (entry == nullptr) {
        warn_missing(message_id, true);
        return "{" + std::string(id) + "}";
    }
    const Pattern* pattern = &entry->value;
    if (!attribute.empty()) {
        const auto it = entry->attributes.find(attribute);
        if (it == entry->attributes.end()) {
            return "{" + std::string(id) + "}";
        }
        pattern = &it->second;
    } else if (!entry->has_value) {
        return "{" + std::string(id) + "}";
    }
    return resolve_pattern(*pattern, scope);
}

std::string Localization::format(std::string_view id, LocalizedArgs args) const {
    return format(id, std::vector<LocalizedArg>(args));
}

void Localization::warn_missing([[maybe_unused]] std::string_view id, [[maybe_unused]] bool everywhere) const {
#if !defined(MOTEUR_SHIPPING)
    const std::lock_guard lock(warned_mutex_);
    if (bundles_.empty() || !warned_.emplace(std::string(language_) + ":" + std::string(id)).second) {
        return;
    }
    if (everywhere) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Localization: no message '%.*s' in any language", static_cast<int>(id.size()),
                    id.data());
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Localization: '%.*s' missing in %s: the %s text is shown",
                    static_cast<int>(id.size()), id.data(), language_.c_str(), base_.c_str());
    }
#endif
}

bool Localization::has(std::string_view id) const {
    const std::string_view message_id = id.substr(0, id.find('.'));
    for (const Bundle* b : {bundle(language_), bundle(base_)}) {
        if (b != nullptr && b->messages.count(message_id) != 0) {
            return true;
        }
    }
    return false;
}

std::string Localization::format_number(double value, const NumberFormatOptions& options) const {
    const std::string& language = language_ == kPseudoLanguage ? base_ : language_;
    return moteur::format_number(language.empty() ? base_ : language, value, options);
}

std::string Localization::format_duration(double seconds) const {
    const auto total = static_cast<std::int64_t>(std::max(0.0, std::floor(seconds + 0.5)));
    const std::int64_t hours = total / 3600;
    const std::int64_t minutes = (total / 60) % 60;
    const std::int64_t secs = total % 60;
    if (hours > 0) {
        return format("moteur-duration-hours", {{"hours", hours}, {"minutes", minutes}});
    }
    if (minutes > 0) {
        return format("moteur-duration-minutes", {{"minutes", minutes}, {"seconds", secs}});
    }
    return format("moteur-duration-seconds", {{"seconds", secs}});
}

void Localization::add_function(std::string name, Function function) {
    functions_[std::move(name)] = std::move(function);
}

std::vector<std::string> Localization::message_ids(std::string_view language) const {
    std::vector<std::string> ids;
    if (const Bundle* b = bundle(language)) {
        for (const auto& [id, entry] : b->messages) {
            ids.push_back(id);
        }
    }
    return ids;
}

std::vector<LocalizationIssue> Localization::check() const {
    std::vector<LocalizationIssue> out = issues_;
    const Bundle* base = bundle(base_);
    const auto add = [&](LocalizationIssue::Kind kind, const std::string& language, const std::string& id,
                         const Entry* entry, std::string message) {
        LocalizationIssue issue;
        issue.kind = kind;
        issue.language = language;
        issue.id = id;
        issue.message = std::move(message);
        if (entry != nullptr) {
            issue.file = entry->file;
            issue.line = entry->line;
        }
        out.push_back(std::move(issue));
    };
    for (const auto& [language, b] : bundles_) {
        if (language == kPseudoLanguage) {
            continue;
        }
        // References to messages and terms that exist nowhere.
        const auto check_references = [&](const std::string& id, const Entry& entry) {
            std::vector<const Expr*> refs;
            collect_references(entry.value, refs);
            for (const auto& [name, pattern] : entry.attributes) {
                collect_references(pattern, refs);
            }
            for (const Expr* ref : refs) {
                const bool term = ref->kind == Expr::Kind::Term;
                const auto& own = term ? b->terms : b->messages;
                const bool found = own.count(ref->name) != 0 ||
                                   (!term && base != nullptr && base->messages.count(ref->name) != 0);
                if (!found) {
                    add(LocalizationIssue::Kind::BadReference, language, id, &entry,
                        std::string("refers to the unknown ") + (term ? "term -" : "message ") + ref->name);
                }
            }
        };
        for (const auto& [id, entry] : b->messages) {
            check_references(id, entry);
        }
        for (const auto& [id, entry] : b->terms) {
            check_references("-" + id, entry);
        }
        if (base == nullptr || b.get() == base) {
            continue;
        }
        for (const auto& [id, entry] : base->messages) {
            const auto it = b->messages.find(id);
            if (it == b->messages.end()) {
                add(LocalizationIssue::Kind::Missing, language, id, &entry, "missing (the " + base_ + " text is shown)");
                continue;
            }
            std::set<std::string> expected, found;
            collect_variables(entry.value, expected);
            collect_variables(it->second.value, found);
            if (expected != found) {
                std::string list;
                for (const std::string& v : expected) list += " $" + v;
                list += " /";
                for (const std::string& v : found) list += " $" + v;
                add(LocalizationIssue::Kind::Variables, language, id, &it->second, "variables differ:" + list);
            }
            const std::string base_text = flat_text(entry.value);
            if (entry.has_value && base_text == flat_text(it->second.value) &&
                std::any_of(base_text.begin(), base_text.end(), [](char c) { return is_identifier_start(c); })) {
                add(LocalizationIssue::Kind::Untranslated, language, id, &it->second, "same text as in " + base_);
            }
        }
        for (const auto& [id, entry] : b->messages) {
            if (base->messages.count(id) == 0) {
                add(LocalizationIssue::Kind::Extra, language, id, &entry, "not in the base language " + base_);
            }
        }
    }
    return out;
}

}  // namespace moteur
