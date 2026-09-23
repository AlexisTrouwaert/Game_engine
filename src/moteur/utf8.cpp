#include "moteur/utf8.hpp"

namespace moteur {

namespace {

constexpr char32_t kReplacementCharacter = 0xFFFD;

bool is_continuation(unsigned char byte) {
    return (byte & 0xC0u) == 0x80u;
}

}  // namespace

std::u32string decode_utf8(std::string_view text) {
    std::u32string result;
    result.reserve(text.size());

    std::size_t i = 0;
    while (i < text.size()) {
        const auto first = static_cast<unsigned char>(text[i]);

        std::size_t extra_bytes;
        char32_t codepoint;
        char32_t minimum;  // smallest codepoint this length is allowed to encode (rejects overlong forms)
        if (first < 0x80u) {
            result.push_back(first);
            ++i;
            continue;
        } else if ((first & 0xE0u) == 0xC0u) {
            extra_bytes = 1;
            codepoint = first & 0x1Fu;
            minimum = 0x80;
        } else if ((first & 0xF0u) == 0xE0u) {
            extra_bytes = 2;
            codepoint = first & 0x0Fu;
            minimum = 0x800;
        } else if ((first & 0xF8u) == 0xF0u) {
            extra_bytes = 3;
            codepoint = first & 0x07u;
            minimum = 0x10000;
        } else {
            // A stray continuation byte or an invalid lead byte.
            result.push_back(kReplacementCharacter);
            ++i;
            continue;
        }

        bool ok = true;
        if (i + extra_bytes >= text.size()) {
            ok = false;
        } else {
            for (std::size_t k = 1; k <= extra_bytes; ++k) {
                const auto byte = static_cast<unsigned char>(text[i + k]);
                if (!is_continuation(byte)) {
                    ok = false;
                    break;
                }
                codepoint = (codepoint << 6) | (byte & 0x3Fu);
            }
        }

        if (!ok || codepoint < minimum || codepoint > 0x10FFFFu || (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) {
            result.push_back(kReplacementCharacter);
            ++i;  // resynchronize one byte at a time
            continue;
        }

        result.push_back(codepoint);
        i += extra_bytes + 1;
    }
    return result;
}

}  // namespace moteur
