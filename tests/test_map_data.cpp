#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "moteur/map_data.hpp"
#include "moteur/nav_grid.hpp"

namespace {

// A map in the format of assets/maps, asymmetric so that rows and columns cannot be swapped.
const char* const kMap = R"({
  "version": 1,
  "description": "test",
  "tiles": {
    "floor": {},
    "wall": { "walkable": false, "opaque": true },
    "fence": { "walkable": false, "region": "fence" }
  },
  "legend": {
    ".": ["floor"],
    "#": ["floor", "wall"],
    "=": ["floor", "fence"],
    " ": [],
    "@": { "tiles": ["floor"], "point": "start" },
    "s": { "tiles": ["floor"], "point": "spawn" }
  },
  "rows": [
    "#####",
    "#@.s#",
    "#= s ",
    "#####"
  ]
})";

std::string replaced(std::string text, const std::string& from, const std::string& to) {
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    return text.replace(at, from.size(), to);
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

}  // namespace

TEST_CASE("MapData reads tiles, layers, cells and points") {
    const moteur::MapData map = moteur::MapData::parse(kMap, "test.json");
    CHECK(map.description() == "test");
    CHECK(map.width() == 5);
    CHECK(map.height() == 4);
    CHECK(map.tiles().layer_count() == 2);

    // Ids in the alphabetical order of the names.
    CHECK(map.tile_id("fence") == 1);
    CHECK(map.tile_id("floor") == 2);
    CHECK(map.tile_id("wall") == 3);
    CHECK(map.tile_id("lava") == moteur::kNoTile);
    CHECK(map.tile_name(3) == "wall");
    CHECK(map.tile_name(moteur::kNoTile).empty());
    CHECK(map.tileset().type(map.tile_id("fence")).region == "fence");
    CHECK_FALSE(map.tileset().type(map.tile_id("fence")).walkable);
    CHECK_FALSE(map.tileset().type(map.tile_id("fence")).opaque);

    // Column i, row j is tile (i, j).
    CHECK(map.tiles().at(0, {1, 1}) == map.tile_id("floor"));
    CHECK(map.tiles().at(1, {1, 1}) == moteur::kNoTile);
    CHECK(map.tiles().at(1, {0, 2}) == map.tile_id("wall"));
    CHECK(map.tiles().at(1, {1, 2}) == map.tile_id("fence"));
    CHECK(map.tiles().at(0, {2, 2}) == moteur::kNoTile);  // the hole
    CHECK(map.tiles().at(0, {4, 2}) == moteur::kNoTile);

    CHECK(map.point("start") == glm::ivec2(1, 1));
    REQUIRE(map.points("spawn").size() == 2);
    CHECK(map.points("spawn")[0] == glm::ivec2(3, 1));  // reading order
    CHECK(map.points("spawn")[1] == glm::ivec2(3, 2));
    CHECK(map.points("exit").empty());
    CHECK_THROWS_AS(map.point("exit"), std::out_of_range);
}

TEST_CASE("MapData writes back the rows it read") {
    const moteur::MapData map = moteur::MapData::parse(kMap, "test.json");
    CHECK(map.to_ascii() == "#####\n#@.s#\n#= s \n#####\n");
}

TEST_CASE("MapData drops the carriage return of Windows line ends") {
    const moteur::MapData map = moteur::MapData::parse(replaced(kMap, "\"#####\"\n  ]", "\"#####\\r\"\n  ]"), "test.json");
    CHECK(map.width() == 5);
}

TEST_CASE("MapData refuses broken files with a message naming the place") {
    auto message = [](const std::string& json) {
        try {
            moteur::MapData::parse(json, "broken.json");
        } catch (const std::runtime_error& e) {
            return std::string(e.what());
        }
        return std::string("no error");
    };
    CHECK(message("{").find("broken.json") != std::string::npos);
    CHECK(message(replaced(kMap, "\"version\": 1", "\"version\": 3")).find("version") != std::string::npos);
    CHECK(message(replaced(kMap, "[\"floor\", \"fence\"]", "[\"floor\", \"lava\"]")).find("lava") != std::string::npos);
    CHECK(message(replaced(kMap, "\"=\": [", "\"==\": [")).find("one character") != std::string::npos);
    CHECK(message(replaced(kMap, "\"=\": [\"floor\", \"fence\"]", "\"=\": [\"floor\", \"wall\"]")).find("the same") !=
          std::string::npos);
    CHECK(message(replaced(kMap, "\"#@.s#\"", "\"#@.s\"")).find("row 1") != std::string::npos);
    CHECK(message(replaced(kMap, "\"#@.s#\"", "\"#@?s#\"")).find("column 2, row 1") != std::string::npos);
    CHECK(message(replaced(kMap, "\"rows\": [", "\"rows\": [], \"x\": [")).find("empty") != std::string::npos);
}

TEST_CASE("NavGrid combines the layers; holes and the outside block the way") {
    const moteur::MapData map = moteur::MapData::parse(kMap, "test.json");
    const moteur::NavGrid grid(map.tiles(), map.tileset());
    CHECK(grid.width() == 5);
    CHECK(grid.height() == 4);
    CHECK(grid.walkable({1, 1}));
    CHECK_FALSE(grid.opaque({1, 1}));
    CHECK_FALSE(grid.walkable({0, 0}));  // wall
    CHECK(grid.opaque({0, 0}));
    CHECK_FALSE(grid.walkable({1, 2}));  // fence: blocks the way, not the sight
    CHECK_FALSE(grid.opaque({1, 2}));
    CHECK_FALSE(grid.walkable({2, 2}));  // hole
    CHECK_FALSE(grid.opaque({2, 2}));
    CHECK_FALSE(grid.walkable({-1, 1}));  // outside
    CHECK(grid.opaque({5, 1}));
    CHECK(grid.to_ascii() == "#####\n#...#\n#xx.x\n#####\n");
    CHECK(grid.version() == 0);
}

TEST_CASE("NavGrid changes its version only when a cell changes") {
    moteur::MapData map = moteur::MapData::parse(kMap, "test.json");
    moteur::TileMap tiles = map.tiles();
    moteur::NavGrid grid(tiles, map.tileset());

    CHECK_FALSE(grid.refresh(tiles, map.tileset(), {2, 1}));  // nothing changed in the map
    CHECK(grid.version() == 0);
    tiles.set(1, {2, 1}, map.tile_id("wall"));
    CHECK(grid.refresh(tiles, map.tileset(), {2, 1}));
    CHECK(grid.version() == 1);
    CHECK_FALSE(grid.walkable({2, 1}));

    CHECK(grid.set({2, 1}, true, true));  // walkable but opaque
    CHECK(grid.version() == 2);
    CHECK_FALSE(grid.set({2, 1}, true, true));
    CHECK(grid.version() == 2);
    CHECK(grid.to_ascii().substr(6, 5) == "#.%.#");
    CHECK_THROWS_AS(grid.set({5, 0}, true, false), std::out_of_range);
    CHECK_THROWS_AS(grid.refresh(tiles, map.tileset(), {0, -1}), std::out_of_range);
}

TEST_CASE("cell_at and cell_centre") {
    CHECK(moteur::cell_at({0.0f, 0.99f}) == glm::ivec2(0, 0));
    CHECK(moteur::cell_at({1.0f, 2.5f}) == glm::ivec2(1, 2));
    CHECK(moteur::cell_at({-0.01f, 3.0f}) == glm::ivec2(-1, 3));
    CHECK(moteur::cell_centre({2, -1}) == glm::vec2(2.5f, -0.5f));
}

TEST_CASE("Every test map of assets/maps reads and writes back the same") {
    int maps = 0;
    for (const auto& entry : std::filesystem::directory_iterator(MOTEUR_SOURCE_ASSETS "maps")) {
        if (entry.path().extension() != ".json") {
            continue;
        }
        CAPTURE(entry.path().filename().string());
        const std::string text = read_text(entry.path());
        const moteur::MapData map = moteur::MapData::parse(text, entry.path().filename().string());
        CHECK_FALSE(map.description().empty());
        // Every row of the file, in order, is a line of to_ascii().
        std::string rows;
        const std::size_t start = text.find("\"rows\"");
        std::istringstream lines(text.substr(start));
        std::string line;
        std::getline(lines, line);
        while (std::getline(lines, line) && line.find(']') == std::string::npos) {
            const std::size_t first = line.find('"');
            const std::size_t last = line.rfind('"');
            rows += line.substr(first + 1, last - first - 1) + "\n";
        }
        CHECK(map.to_ascii() == rows);
        // Every map has a start on a walkable cell, inside walls.
        const moteur::NavGrid grid(map.tiles(), map.tileset());
        CHECK(grid.walkable(map.point("depart")));
        ++maps;
    }
    CHECK(maps >= 8);
}
