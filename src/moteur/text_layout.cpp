#include "moteur/text_layout.hpp"

#include <algorithm>

namespace moteur {

namespace {

enum class TokenKind { Word, HardBreak };

struct Token {
    TokenKind kind;
    std::u32string text;  // empty for HardBreak
};

bool is_non_breaking_mark(char32_t c) {
    return c == U':' || c == U';' || c == U'!' || c == U'?';
}

std::vector<Token> tokenize(const std::u32string& text) {
    std::vector<Token> tokens;
    std::u32string word;
    const auto flush_word = [&] {
        if (!word.empty()) {
            tokens.push_back({TokenKind::Word, word});
            word.clear();
        }
    };
    for (const char32_t c : text) {
        if (c == U'\n') {
            flush_word();
            tokens.push_back({TokenKind::HardBreak, {}});
        } else if (c == U' ') {
            flush_word();  // runs of spaces collapse to a single separator
        } else {
            word.push_back(c);
        }
    }
    flush_word();
    return tokens;
}

// Attaches a lone :;!? to the word before it, with the space between them kept (so there is
// still a visible gap), so that a line break can no longer fall between the word and the mark.
void apply_non_breaking_marks(std::vector<Token>& tokens) {
    std::vector<Token> merged;
    merged.reserve(tokens.size());
    for (Token& token : tokens) {
        const bool is_lone_mark = token.kind == TokenKind::Word && token.text.size() == 1 &&
                                  is_non_breaking_mark(token.text.front());
        if (is_lone_mark && !merged.empty() && merged.back().kind == TokenKind::Word) {
            merged.back().text.push_back(U' ');
            merged.back().text.push_back(token.text.front());
        } else {
            merged.push_back(std::move(token));
        }
    }
    tokens = std::move(merged);
}

float measure_run(const std::u32string& run, const AdvanceFn& advance_of, const KerningFn& kerning_of) {
    float width = 0.0f;
    for (std::size_t i = 0; i < run.size(); ++i) {
        if (i > 0) {
            width += kerning_of(run[i - 1], run[i]);
        }
        width += advance_of(run[i]);
    }
    return width;
}

void emit_run(const std::u32string& run, float start_x, float y, const AdvanceFn& advance_of,
             const KerningFn& kerning_of, std::vector<GlyphPlacement>& glyphs) {
    float pen = start_x;
    for (std::size_t i = 0; i < run.size(); ++i) {
        if (i > 0) {
            pen += kerning_of(run[i - 1], run[i]);
        }
        glyphs.push_back({run[i], pen, y});
        pen += advance_of(run[i]);
    }
}

}  // namespace

TextLayoutResult layout_text(const std::u32string& text, float line_height, float max_width, TextAlign align,
                             const AdvanceFn& advance_of, const KerningFn& kerning_of) {
    std::vector<Token> tokens = tokenize(text);
    apply_non_breaking_marks(tokens);

    const float space_width = advance_of(U' ');

    TextLayoutResult result;
    std::vector<Token*> current_line;
    float current_width = 0.0f;

    const auto finish_line = [&] {
        TextLine line;
        float pen = 0.0f;
        for (std::size_t i = 0; i < current_line.size(); ++i) {
            if (i > 0) {
                pen += space_width;
            }
            emit_run(current_line[i]->text, pen, static_cast<float>(result.lines.size()) * line_height, advance_of,
                    kerning_of, line.glyphs);
            pen += measure_run(current_line[i]->text, advance_of, kerning_of);
        }
        line.width = pen;
        result.lines.push_back(std::move(line));
        current_line.clear();
        current_width = 0.0f;
    };

    for (Token& token : tokens) {
        if (token.kind == TokenKind::HardBreak) {
            finish_line();
            continue;
        }

        const float token_width = measure_run(token.text, advance_of, kerning_of);
        const float extra = current_line.empty() ? 0.0f : space_width;
        if (max_width > 0.0f && !current_line.empty() && current_width + extra + token_width > max_width) {
            finish_line();
            current_line.push_back(&token);
            current_width = token_width;
        } else {
            current_line.push_back(&token);
            current_width += extra + token_width;
        }
    }
    finish_line();  // always at least one line, even for empty text

    // The alignment box is at least as wide as the widest line, so a single word longer than
    // max_width does not push the other lines to a negative offset.
    float box_width = std::max(max_width, 0.0f);
    for (const TextLine& line : result.lines) {
        box_width = std::max(box_width, line.width);
    }
    if (align != TextAlign::Left) {
        for (TextLine& line : result.lines) {
            const float offset = align == TextAlign::Center ? (box_width - line.width) / 2.0f : (box_width - line.width);
            for (GlyphPlacement& glyph : line.glyphs) {
                glyph.x += offset;
            }
        }
    }
    return result;
}

}  // namespace moteur
