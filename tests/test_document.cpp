#include <doctest/doctest.h>

#include <stdexcept>
#include <string>

#include "moteur/document.hpp"
#include "moteur/animation_set.hpp"

// Milestone 9, part 4: the shipped game reads its structured assets in CBOR, under their names.

TEST_CASE("documents: JSON or CBOR, the same document; damaged CBOR refused") {
    const nlohmann::json json = nlohmann::json::parse(R"({"version": 1, "rows": [1, 2.5, "trois"], "nested": {"a": true}})");
    const std::string text = json.dump();
    const std::vector<std::uint8_t> cbor = moteur::shipped_document(json);
    const std::string_view cbor_view(reinterpret_cast<const char*>(cbor.data()), cbor.size());
    CHECK_FALSE(moteur::is_cbor_document(text));
    CHECK(moteur::is_cbor_document(cbor_view));
    CHECK(moteur::parse_document(text) == json);
    CHECK(moteur::parse_document(cbor_view) == json);
    CHECK_THROWS_WITH_AS(moteur::parse_document(cbor_view.substr(0, cbor.size() - 2)), doctest::Contains("invalid CBOR"),
                         std::runtime_error);
    CHECK_THROWS_WITH_AS(moteur::parse_document("{\"a\": "), doctest::Contains("invalid JSON"), std::runtime_error);
    // Nested too deep (a stack overflow in the reader otherwise): refused before reading.
    std::string deep(500, static_cast<char>(0x81));
    deep += static_cast<char>(0x01);
    CHECK_THROWS_WITH_AS(moteur::parse_document(deep), doctest::Contains("invalid CBOR"), std::runtime_error);
}

TEST_CASE("documents: the engine's readers take the shipped form") {
    const nlohmann::json set = nlohmann::json::parse(R"({"version": 1, "clips": {"Attack": {"speed": 1.5}}})");
    const std::vector<std::uint8_t> cbor = moteur::shipped_document(set);
    const moteur::AnimationSet read =
        moteur::AnimationSet::parse(std::string_view(reinterpret_cast<const char*>(cbor.data()), cbor.size()), "set.json");
    CHECK(read.source() == "set.json");
}
