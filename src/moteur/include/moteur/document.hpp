#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace moteur {

/// The structured assets of the engine (maps, effects, animation descriptions, prefabs, key
/// bindings, credits, atlases) are written in JSON and shipped in CBOR (milestone 9, part 4: the
/// shipped game reads no JSON), under the same name: their readers take either.

/// The document in `bytes`: CBOR when it starts like a CBOR map or array (a first byte from 0x80 to
/// 0xBF, which no JSON document starts with), checked first (structure and depth, as the data tables
/// and saves are), else JSON text. Throws std::runtime_error saying what is wrong ("invalid JSON:
/// ...", "invalid CBOR: ...").
nlohmann::json parse_document(std::string_view bytes);

/// Whether `bytes` hold a document in CBOR (see parse_document()).
bool is_cbor_document(std::string_view bytes);

/// The shipped form of a document: its CBOR.
std::vector<std::uint8_t> shipped_document(const nlohmann::json& document);

}  // namespace moteur
