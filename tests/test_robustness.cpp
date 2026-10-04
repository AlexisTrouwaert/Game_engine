// Damaged files (milestone 7, part 10): every reader of the game's files answers a broken one with
// a message naming it, never with a crash; what is the game's to judge (an unknown object type)
// is kept for the game to leave out.

#include <doctest/doctest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "moteur/animation_set.hpp"
#include "moteur/map_data.hpp"
#include "moteur/map_document.hpp"
#include "moteur/particles.hpp"
#include "moteur/save_file.hpp"

#include <nlohmann/json.hpp>

namespace {

const char* const kDamaged[] = {"", "{", "{}", "[]", "null", "{\"version\": 1", "\xEF\xBB\xBF{", "{\"version\": \"un\"}"};

}  // namespace

TEST_CASE("Damaged maps, animation sets and effects give a message naming the file") {
    for (const char* text : kDamaged) {
        CAPTURE(std::string(text));
        CHECK_THROWS_WITH_AS(moteur::MapData::parse(text, "maps/abimee.json"), doctest::Contains("abimee.json"), std::runtime_error);
        CHECK_THROWS_WITH_AS(moteur::AnimationSet::parse(text, "animations/abime.json"), doctest::Contains("abime.json"),
                             std::runtime_error);
        CHECK_THROWS_WITH_AS(moteur::ParticleEffect::parse(text, "effects/abime.json"), doctest::Contains("abime.json"),
                             std::runtime_error);
    }
}

TEST_CASE("A map with an object type the game does not know is read; the type is the game's to judge") {
    const char* text = R"({
      "version": 2, "tiles": { "sol": {} }, "legend": { ".": ["sol"] }, "rows": [ "...", "..." ],
      "objects": [ { "id": "o1", "type": "dragon_inconnu", "x": 1.5, "z": 0.5, "props": { "hauteur": "pas un nombre" } } ]
    })";
    const moteur::MapData map = moteur::MapData::parse(text, "maps/inconnu.json");
    REQUIRE(map.objects().size() == 1);
    CHECK(map.objects()[0].type == "dragon_inconnu");
    // The editor keeps it as it is, and writes it back.
    const moteur::MapDocument doc = moteur::MapDocument::from(map);
    CHECK(doc.to_json().find("dragon_inconnu") != std::string::npos);
    CHECK(moteur::MapData::parse(doc.to_json(), "back.json").objects().size() == 1);
}

TEST_CASE("Damaged saves are refused with a reason, whatever is cut or changed") {
    moteur::SaveGame game;
    game.header.game_version = "essai";
    game.set_section("monde", 1, {{"tick", 42}, {"entites", nlohmann::json::array({1, 2, 3})}});
    for (const moteur::SaveEncoding encoding : {moteur::SaveEncoding::Json, moteur::SaveEncoding::Cbor}) {
        const std::vector<std::uint8_t> bytes = game.encode(encoding);
        std::string error;
        REQUIRE(moteur::SaveGame::decode(bytes, error));
        // Cut anywhere: refused, never read half.
        for (std::size_t size : {std::size_t{0}, std::size_t{3}, bytes.size() / 2, bytes.size() - 1}) {
            const std::vector<std::uint8_t> cut(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size));
            error.clear();
            CHECK_FALSE(moteur::SaveGame::decode(cut, error));
            CHECK_FALSE(error.empty());
        }
        // One byte changed in the body: the checksum notices.
        std::vector<std::uint8_t> changed = bytes;
        changed[changed.size() - 2] ^= 0x5A;
        error.clear();
        CHECK_FALSE(moteur::SaveGame::decode(changed, error));
        CHECK_FALSE(error.empty());
    }
}
