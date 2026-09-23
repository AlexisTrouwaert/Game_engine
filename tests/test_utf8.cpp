#include <doctest/doctest.h>

#include <cstring>

#include "moteur/utf8.hpp"

using moteur::decode_utf8;

namespace {

// u8"..." literals are guaranteed UTF-8 regardless of the source or execution encoding (unlike a
// plain "..." literal), but they are typed char8_t* since C++20; decode_utf8() takes a plain
// string_view, so this just reinterprets the bytes, which is what string_view would do anyway.
std::string_view u8sv(const char8_t* text) {
    return {reinterpret_cast<const char*>(text), std::char_traits<char8_t>::length(text)};
}

}  // namespace

// U"..." and U'...' (char32_t) literals below are written with \u escapes rather than the literal
// accented characters: unlike u8"...", the standard does not guarantee how a compiler maps the
// source file's bytes to a char32_t literal, so a compiler that assumes the wrong source encoding
// could misread a literal accented character. u8"..." literals do not have that problem.

TEST_CASE("plain ASCII decodes unchanged") {
    CHECK(decode_utf8("Hello!") == U"Hello!");
    CHECK(decode_utf8("") == U"");
}

TEST_CASE("French accented letters decode to their code point") {
    CHECK(decode_utf8(u8sv(u8"École")) == U"École");
    CHECK(decode_utf8(u8sv(u8"loupé")) == U"loupé");
}

TEST_CASE("a full French sentence with guillemets and the oe ligature decodes correctly") {
    CHECK(decode_utf8(u8sv(u8"Où étaient les œufs d'été ? « Ici. »")) ==
          U"Où étaient les œufs d'été ? « Ici. »");
}

TEST_CASE("a 3-byte sequence (e.g. the euro sign) decodes to one code point") {
    CHECK(decode_utf8(u8sv(u8"5€")) == U"5€");
}

TEST_CASE("a 4-byte sequence (beyond the Basic Multilingual Plane) decodes correctly") {
    CHECK(decode_utf8(u8sv(u8"\U0001F600")) == U"\U0001F600");  // an emoji, not covered by any bitmap font here
}

TEST_CASE("byte-by-byte decoding is what this function exists to avoid") {
    // The two bytes of an accented letter must NOT be read as two separate code points.
    const std::u32string decoded = decode_utf8(u8sv(u8"é"));
    CHECK(decoded.size() == 1);
    CHECK(decoded[0] == static_cast<char32_t>(0x00E9));
}

TEST_CASE("a lone continuation byte is replaced, and decoding continues") {
    const std::string text = "a\x80posb";  // 0x80 is a continuation byte with no lead byte
    const std::u32string decoded = decode_utf8(text);
    CHECK(decoded == U"a�posb");
}

TEST_CASE("a truncated multi-byte sequence at the end of the string is replaced") {
    const std::string text = "ok\xE2\x82";  // the start of a 3-byte sequence, missing its last byte
    CHECK(decode_utf8(text) == U"ok��");
}

TEST_CASE("an overlong encoding is rejected") {
    // 0xC0 0x80 is a 2-byte encoding of NUL, which fits in 1 byte: not valid UTF-8.
    const std::string text = "\xC0\x80";
    CHECK(decode_utf8(text) == U"��");
}

TEST_CASE("a surrogate code point is rejected") {
    // 0xED 0xA0 0x80 would decode to U+D800, a UTF-16 surrogate, never valid UTF-8.
    const std::string text = "\xED\xA0\x80";
    CHECK(decode_utf8(text) == U"���");
}

TEST_CASE("an invalid lead byte is replaced on its own") {
    const std::string text = "\xFF\xFEx";
    CHECK(decode_utf8(text) == U"��x");
}
