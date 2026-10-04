#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "moteur/map_document.hpp"

// Milestone 7, parts 7 and 8: the map being edited, its tools, undo, checks and writing.

namespace {

std::string read_text(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

moteur::MapDocument room() {
    moteur::MapDocument doc(8, 6, 2, "sol");
    const moteur::TileId wall = doc.add_tile_type({"mur", false, true, {}});
    doc.set_symbol({'#', {1, wall}, {}});
    doc.begin_action("salle");
    doc.rect({0, 0}, {7, 5}, {1, wall}, true);
    doc.end_action();
    doc.set_point({2, 2}, "depart");
    doc.description = "une salle";
    return doc;
}

}  // namespace

TEST_CASE("Rectangles, lines and fills; one undo step per action") {
    moteur::MapDocument doc = room();
    const moteur::TileId wall = doc.tile_id("mur");
    CHECK(doc.tile(1, {0, 0}) == wall);
    CHECK(doc.tile(1, {3, 0}) == wall);
    CHECK(doc.tile(1, {3, 3}) == moteur::kNoTile);  // hollow

    CHECK(moteur::MapDocument::line({0, 0}, {3, 1}).size() == 4);
    CHECK(moteur::MapDocument::line({2, 2}, {2, 2}).size() == 1);

    // A wall across the room, then a fill of one half with another stack.
    doc.begin_action("cloison");
    for (const glm::ivec2 cell : moteur::MapDocument::line({4, 1}, {4, 4})) {
        doc.set_stack(cell, {1, wall});
    }
    doc.end_action();
    const moteur::TileId grass = doc.add_tile_type({"herbe", true, true, {}});
    doc.begin_action("remplissage");
    CHECK(doc.fill({5, 2}, {grass}) == 8);  // the right half inside: 2 x 4 cells
    doc.end_action();
    CHECK(doc.tile(0, {6, 3}) == grass);
    CHECK(doc.tile(0, {2, 3}) == 1);  // the other half untouched

    CHECK(doc.undo_name() == "remplissage");
    CHECK(doc.undo());
    CHECK(doc.tile(0, {6, 3}) == 1);
    CHECK(doc.undo());  // the type "herbe" (its own action)
    CHECK(doc.undo());  // the whole partition at once
    CHECK(doc.tile(1, {4, 2}) == moteur::kNoTile);
    CHECK(doc.redo());
    CHECK(doc.tile(1, {4, 2}) == wall);
    CHECK(doc.can_redo());
    doc.set_tile(0, {1, 1}, 1);  // unchanged: nothing recorded
    CHECK(doc.can_redo());
    doc.set_tile(1, {1, 1}, wall);  // a change: the redo history goes
    CHECK_FALSE(doc.can_redo());
}

TEST_CASE("Points, objects and connectors; resize moves them") {
    moteur::MapDocument doc = room();
    const std::string tree = doc.add_object({"", "arbre", {3.5f, 0.0f, 2.25f}, 30.0f, 1.0f, {{"hauteur", 1.5}}});
    CHECK(tree == "o1");
    const std::string crate = doc.add_object({"o1", "caisse", {5.0f, 0.0f, 3.0f}, 0.0f, 1.0f, {}});
    CHECK(crate == "o2");  // "o1" was taken
    doc.set_connectors({{"est", "porte", {7, 2}, moteur::MapConnector::Direction::East}});
    CHECK(doc.point_at({2, 2}) == std::optional<std::string>("depart"));

    doc.resize(2, 1, 0, -1, {1});
    CHECK(doc.width() == 10);
    CHECK(doc.height() == 6);
    CHECK(doc.point_at({4, 3}) == std::optional<std::string>("depart"));
    CHECK(doc.object("o1")->position.x == 5.5f);
    CHECK(doc.object("o1")->position.z == 3.25f);
    CHECK(doc.connectors()[0].cell == glm::ivec2(9, 3));
    CHECK(doc.tile(0, {0, 0}) == 1);  // new cells: the given stack
    CHECK(doc.undo());
    CHECK(doc.width() == 8);
    CHECK(doc.point_at({2, 2}) == std::optional<std::string>("depart"));
    CHECK(doc.object("o1")->position.x == 3.5f);
    CHECK(doc.redo());
    CHECK(doc.width() == 10);
    doc.remove_object("o2");
    CHECK(doc.object("o2") == nullptr);
    CHECK(doc.undo());
    CHECK(doc.object("o2") != nullptr);
}

TEST_CASE("Writing gives back the same map, and the same text") {
    moteur::MapDocument doc = room();
    doc.add_object({"", "arbre", {3.5f, 0.0f, 2.1f}, 30.0f, 1.25f, {{"hauteur", 1.5}}});
    doc.set_connectors({{"est", "porte", {7, 2}, moteur::MapConnector::Direction::East}});
    doc.set_tile(1, {3, 3}, doc.add_tile_type({"pilier", false, true, "colonne"}));  // a new stack: a new character
    const std::string text = doc.to_json();
    const moteur::MapData map = moteur::MapData::parse(text, "doc.json");
    CHECK(map.version() == 2);
    CHECK(map.description() == "une salle");
    CHECK(map.point("depart") == glm::ivec2(2, 2));
    REQUIRE(map.objects().size() == 1);
    CHECK(map.objects()[0].position.z == 2.1f);  // exactly
    CHECK(map.objects()[0].rotation == 30.0f);
    CHECK(map.objects()[0].props["hauteur"] == 1.5);
    REQUIRE(map.connectors().size() == 1);
    CHECK(map.connectors()[0].direction == moteur::MapConnector::Direction::East);
    CHECK(map.tileset().type(map.tile_id("pilier")).region == "colonne");
    CHECK(map.tiles().at(1, {3, 3}) == map.tile_id("pilier"));
    // Read back and written again: the same text, byte for byte.
    CHECK(moteur::MapDocument::from(map).to_json() == text);
}

TEST_CASE("Checks: missing start, unreachable cells, connectors off the edge") {
    moteur::MapDocument doc = room();
    CHECK(doc.validate().empty());
    // A closed box inside the room: its inside cannot be reached.
    const moteur::TileId wall = doc.tile_id("mur");
    doc.begin_action("boîte");
    doc.rect({4, 1}, {6, 4}, {1, wall}, true);
    doc.end_action();
    auto issues = doc.validate();
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].cells.size() == 2);  // (5, 2) and (5, 3)
    doc.set_point({2, 2}, "");
    doc.set_connectors({{"milieu", "", {3, 3}, moteur::MapConnector::Direction::North}});
    issues = doc.validate();
    CHECK(issues[0].message.find("depart") != std::string::npos);
    CHECK(issues.back().message.find("connecteur milieu") != std::string::npos);
    doc.chunk = true;  // a chunk needs no start
    issues = doc.validate();
    CHECK(issues.size() == 1);
}

TEST_CASE("Every map of assets/maps reads into a document and writes back the same map") {
    for (const auto& entry : std::filesystem::directory_iterator(MOTEUR_SOURCE_ASSETS "maps")) {
        if (entry.path().extension() != ".json") {
            continue;
        }
        CAPTURE(entry.path().filename().string());
        const moteur::MapData map = moteur::MapData::parse(read_text(entry.path()), "map");
        const moteur::MapData back = moteur::MapDocument::from(map).to_map_data();
        CHECK(back.to_ascii() == map.to_ascii());
        CHECK(back.points() == map.points());
        CHECK(back.objects().size() == map.objects().size());
    }
}

TEST_CASE("Description and chunk are undone like the rest; a fresh history has nothing to undo") {
    moteur::MapDocument doc = room();
    doc.clear_history();
    CHECK_FALSE(doc.can_undo());
    const std::uint64_t revision = doc.revision();
    doc.set_description("une autre salle");
    doc.set_chunk(true);
    CHECK(doc.revision() > revision);
    CHECK(doc.description == "une autre salle");
    CHECK(doc.chunk);
    doc.set_chunk(true);  // unchanged: no step
    REQUIRE(doc.undo());
    CHECK_FALSE(doc.chunk);
    REQUIRE(doc.undo());
    CHECK(doc.description == "une salle");
    CHECK_FALSE(doc.can_undo());
    REQUIRE(doc.redo());
    CHECK(doc.description == "une autre salle");
    // An action held open by a widget: its changes are one step.
    doc.begin_action("propriétés");
    CHECK(doc.in_action());
    doc.set_description("a");
    doc.set_description("ab");
    doc.end_action();
    CHECK_FALSE(doc.in_action());
    REQUIRE(doc.undo());
    CHECK(doc.description == "une autre salle");
}
