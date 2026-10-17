#pragma once

#include <string>
#include <string_view>

namespace moteur {

/// Decodes a UTF-8 string into Unicode code points. Never throws (except std::bad_alloc): a
/// truncated or invalid byte sequence (overlong forms, UTF-16 surrogates and values above U+10FFFF
/// included) is replaced by one U+FFFD (replacement character) per offending byte, so the rest of
/// the string still decodes. O(n), one allocation; any thread. Reading text byte by byte instead of through this function breaks
/// every accented letter and the French guillemets, since they are encoded on 2 or 3 bytes.
std::u32string decode_utf8(std::string_view text);

}  // namespace moteur
