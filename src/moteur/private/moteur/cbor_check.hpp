#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace moteur {

// The deepest nesting accepted in a CBOR document the engine reads (compiled tables, saves).
inline constexpr int kMaxCborDepth = 128;

// Checks the structure of a CBOR document (RFC 8949) without building it: every item complete within
// the bytes, and arrays, maps and tags nested at most `max_depth` levels. nlohmann::json::from_cbor
// reads nested items recursively: a damaged or crafted file of a few hundred kilobytes of nested
// arrays overflows the stack and kills the program before any error can be reported (found by long
// fuzzing, milestone 8 part 8). This check is iterative, in one pass, and runs before it.
// False with the reason in `error`.
bool check_cbor_structure(const std::uint8_t* data, std::size_t size, std::string& error, int max_depth = kMaxCborDepth);

}  // namespace moteur
