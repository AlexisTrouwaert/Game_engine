#include <doctest/doctest.h>

#include <cmath>
#include <stdexcept>

#include "moteur/iso.hpp"

using moteur::IsoProjection;

namespace {

bool near_vec(glm::vec2 a, glm::vec2 b, float tolerance = 1e-3f) {
    return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance;
}

}  // namespace

TEST_CASE("tile corners land on the classic 2:1 lattice") {
    const IsoProjection iso(64.0f, 32.0f);
    CHECK(iso.to_world({0.0f, 0.0f}) == glm::vec2(0.0f, 0.0f));
    CHECK(iso.to_world({1.0f, 0.0f}) == glm::vec2(32.0f, 16.0f));   // one tile right: down-right
    CHECK(iso.to_world({0.0f, 1.0f}) == glm::vec2(-32.0f, 16.0f));  // one tile down: down-left
    CHECK(iso.to_world({1.0f, 1.0f}) == glm::vec2(0.0f, 32.0f));
}

TEST_CASE("height lifts a point up the screen") {
    const IsoProjection iso(64.0f, 32.0f);
    CHECK(iso.to_world({1.0f, 0.0f}, 10.0f) == glm::vec2(32.0f, 6.0f));
}

TEST_CASE("from_world undoes to_world") {
    const IsoProjection iso(64.0f, 32.0f);
    for (float x = -20.0f; x <= 20.0f; x += 1.7f) {
        for (float y = -20.0f; y <= 20.0f; y += 2.3f) {
            const glm::vec2 tile(x, y);
            CHECK(near_vec(iso.from_world(iso.to_world(tile)), tile));
        }
    }
}

TEST_CASE("it works for other tile sizes") {
    const IsoProjection iso(128.0f, 64.0f);
    CHECK(iso.to_world({1.0f, 0.0f}) == glm::vec2(64.0f, 32.0f));
    CHECK(near_vec(iso.from_world({64.0f, 32.0f}), {1.0f, 0.0f}));
}

TEST_CASE("the middle of a tile belongs to that tile") {
    const IsoProjection iso(64.0f, 32.0f);
    for (int i = -6; i <= 6; ++i) {
        for (int j = -6; j <= 6; ++j) {
            CHECK(iso.tile_at(iso.tile_center({i, j})) == glm::ivec2(i, j));
        }
    }
}

TEST_CASE("points just inside the corners of a tile belong to it") {
    const IsoProjection iso(64.0f, 32.0f);
    const glm::vec2 top = iso.to_world({3.0f, 2.0f});  // the top corner of tile (3, 2)
    CHECK(iso.tile_at(top + glm::vec2(0.0f, 0.5f)) == glm::ivec2(3, 2));
    CHECK(iso.tile_at(top + glm::vec2(0.0f, -0.5f)) == glm::ivec2(2, 1));  // just above: the tile behind
    CHECK(iso.tile_at(top + glm::vec2(31.0f, 15.5f)) == glm::ivec2(3, 2));  // near the right corner
    CHECK(iso.tile_at(top + glm::vec2(-31.0f, 15.5f)) == glm::ivec2(3, 2)); // near the left corner
}

TEST_CASE("negative coordinates round down, not towards zero") {
    const IsoProjection iso(64.0f, 32.0f);
    CHECK(iso.tile_at(iso.to_world({-0.5f, -0.5f})) == glm::ivec2(-1, -1));
    CHECK(iso.tile_at(iso.to_world({-0.1f, 0.5f})) == glm::ivec2(-1, 0));
    CHECK(iso.tile_at(iso.to_world({0.1f, 0.5f})) == glm::ivec2(0, 0));
}

TEST_CASE("the sprite box of a tile puts its top corner on the lattice") {
    const IsoProjection iso(64.0f, 32.0f);
    // The image is 64 wide: its top-middle is the top corner of the diamond.
    CHECK(iso.tile_sprite_position({0, 0}) == glm::vec2(-32.0f, 0.0f));
    CHECK(iso.tile_sprite_position({2, 1}) == glm::vec2(0.0f, 48.0f));
    // Neighbouring tiles are exactly half a tile apart: the diamonds tile the plane without gaps.
    CHECK(iso.tile_sprite_position({1, 0}) - iso.tile_sprite_position({0, 0}) == glm::vec2(32.0f, 16.0f));
    CHECK(iso.tile_sprite_position({0, 1}) - iso.tile_sprite_position({0, 0}) == glm::vec2(-32.0f, 16.0f));
}

TEST_CASE("tiles_in contains every tile whose center is in the area") {
    const IsoProjection iso(64.0f, 32.0f);
    const moteur::Rect areas[] = {
        {{-200.0f, -100.0f}, {300.0f, 250.0f}},
        {{50.0f, 50.0f}, {60.0f, 60.0f}},
        {{-1000.0f, 400.0f}, {-100.0f, 900.0f}},
    };
    for (const moteur::Rect& area : areas) {
        const moteur::TileRange range = iso.tiles_in(area, 0);
        int missing = 0;
        for (int i = -60; i <= 60; ++i) {
            for (int j = -60; j <= 60; ++j) {
                const bool inside = area.contains(iso.tile_center({i, j}));
                const bool in_range = i >= range.min.x && i <= range.max.x && j >= range.min.y && j <= range.max.y;
                if (inside && !in_range) {
                    ++missing;
                }
            }
        }
        CHECK(missing == 0);
    }
}

TEST_CASE("the margin widens the range on every side") {
    const IsoProjection iso(64.0f, 32.0f);
    const moteur::Rect area{{-100.0f, 0.0f}, {100.0f, 200.0f}};
    const moteur::TileRange tight = iso.tiles_in(area, 0);
    const moteur::TileRange wide = iso.tiles_in(area, 2);
    CHECK(wide.min == tight.min - glm::ivec2(2));
    CHECK(wide.max == tight.max + glm::ivec2(2));
}

TEST_CASE("a range that shows a lot of screen stays a sensible size") {
    const IsoProjection iso(64.0f, 32.0f);
    // A 1280x720 window at zoom 1 shows a diamond of about 43 x 43 tiles once turned into a box
    // (46 x 46 with the margin): the range must stay of that order and not explode.
    const moteur::TileRange range = iso.tiles_in({{-640.0f, -360.0f}, {640.0f, 360.0f}}, 1);
    CHECK_FALSE(range.empty());
    CHECK((range.max.x - range.min.x + 1) == 46);
    CHECK((range.max.y - range.min.y + 1) == 46);
}

TEST_CASE("an empty range is empty") {
    const moteur::TileRange none;
    CHECK(none.empty());
}

TEST_CASE("the tile size must be positive") {
    CHECK_THROWS_AS(IsoProjection(0.0f, 32.0f), std::invalid_argument);
    CHECK_THROWS_AS(IsoProjection(64.0f, -1.0f), std::invalid_argument);
}
