#include <doctest/doctest.h>

#include <stdexcept>

#include "moteur/tilemap.hpp"

TEST_CASE("Tileset numbers its types from 1") {
    moteur::Tileset tileset;
    const moteur::TileId grass = tileset.add({"grass", true, false});
    const moteur::TileId wall = tileset.add({"wall", false, true});
    CHECK(grass == 1);
    CHECK(wall == 2);
    CHECK(tileset.size() == 2);
    CHECK(tileset.type(grass).region == "grass");
    CHECK_FALSE(tileset.type(wall).walkable);
    CHECK(tileset.type(wall).opaque);
    CHECK_THROWS_AS(tileset.type(moteur::kNoTile), std::out_of_range);
    CHECK_THROWS_AS(tileset.type(3), std::out_of_range);
}

TEST_CASE("TileMap starts empty and stores tiles per layer") {
    moteur::TileMap map(4, 3, 2);
    CHECK(map.width() == 4);
    CHECK(map.height() == 3);
    CHECK(map.layer_count() == 2);
    CHECK(map.at(0, {3, 2}) == moteur::kNoTile);

    map.set(1, {3, 2}, 7);
    CHECK(map.at(1, {3, 2}) == 7);
    CHECK(map.at(0, {3, 2}) == moteur::kNoTile);  // other layer untouched
    CHECK(map.at(1, {2, 2}) == moteur::kNoTile);

    map.fill(0, 5);
    CHECK(map.at(0, {0, 0}) == 5);
    CHECK(map.at(0, {3, 2}) == 5);
    CHECK(map.at(1, {0, 0}) == moteur::kNoTile);  // fill stays in its layer
    CHECK(map.at(1, {3, 2}) == 7);
}

TEST_CASE("TileMap refuses cells outside the map") {
    CHECK_THROWS_AS(moteur::TileMap(0, 3), std::invalid_argument);
    CHECK_THROWS_AS(moteur::TileMap(3, 3, 0), std::invalid_argument);

    moteur::TileMap map(4, 3);
    CHECK(map.contains({0, 0}));
    CHECK(map.contains({3, 2}));
    CHECK_FALSE(map.contains({4, 0}));
    CHECK_FALSE(map.contains({0, -1}));
    CHECK_THROWS_AS(map.at(0, {4, 0}), std::out_of_range);
    CHECK_THROWS_AS(map.at(1, {0, 0}), std::out_of_range);
    CHECK_THROWS_AS(map.set(0, {-1, 0}, 1), std::out_of_range);
}

TEST_CASE("TileMap clip keeps the part of a range inside the map") {
    const moteur::TileMap map(10, 5);

    const moteur::TileRange inside = map.clip({{2, 1}, {4, 3}});
    CHECK(inside.min == glm::ivec2(2, 1));
    CHECK(inside.max == glm::ivec2(4, 3));

    const moteur::TileRange overlapping = map.clip({{-3, -2}, {20, 2}});
    CHECK(overlapping.min == glm::ivec2(0, 0));
    CHECK(overlapping.max == glm::ivec2(9, 2));

    CHECK(map.clip({{11, 0}, {15, 4}}).empty());  // entirely to the right
    CHECK(map.clip({{-5, -5}, {-1, 3}}).empty());  // entirely to the left
    CHECK(map.clip(moteur::TileRange{}).empty());
}

TEST_CASE("TileMap clip of the visible range gives only tiles on screen") {
    // A camera looking at the corner of the map: part of the visible range is outside it.
    const moteur::IsoProjection iso(64.0f, 32.0f);
    const moteur::TileMap map(100, 100);
    const moteur::Rect view{{-640.0f, -360.0f}, {640.0f, 360.0f}};
    const moteur::TileRange range = map.clip(iso.tiles_in(view, 2));
    REQUIRE_FALSE(range.empty());
    CHECK(range.min == glm::ivec2(0, 0));
    CHECK(range.max.x < 100);
    CHECK(range.max.y < 100);
    // The origin of the world is on screen, so tile (0, 0) must be drawn.
    CHECK(range.min.x <= 0);
    CHECK(range.min.y <= 0);
}

TEST_CASE("TileMap walkable looks at every layer") {
    moteur::Tileset tileset;
    const moteur::TileId ground = tileset.add({"ground", true, false});
    const moteur::TileId wall = tileset.add({"wall", false, true});
    const moteur::TileId rug = tileset.add({"rug", true, false});

    moteur::TileMap map(3, 3, 2);
    map.fill(0, ground);
    map.set(1, {1, 1}, wall);
    map.set(1, {2, 1}, rug);

    CHECK(map.walkable(tileset, {0, 0}));
    CHECK_FALSE(map.walkable(tileset, {1, 1}));
    CHECK(map.walkable(tileset, {2, 1}));
    CHECK_FALSE(map.walkable(tileset, {3, 1}));   // outside the map
    CHECK_FALSE(map.walkable(tileset, {-1, 0}));
}
