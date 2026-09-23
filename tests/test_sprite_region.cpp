#include <doctest/doctest.h>

#include "moteur/sprite_region.hpp"

using moteur::SpriteRegion;

namespace {

// A 48x64 original image whose visible part is a 20x30 block at (10, 4); the pivot is at the feet.
SpriteRegion character() {
    SpriteRegion region;
    region.size = {20.0f, 30.0f};
    region.source_size = {48.0f, 64.0f};
    region.offset = {10.0f, 4.0f};
    region.pivot = {24.0f, 64.0f};
    return region;
}

}  // namespace

TEST_CASE("the pivot lands on the anchor, and trimmed margins are put back") {
    const moteur::PlacedSprite placed = moteur::place_region(character(), {100.0f, 200.0f});
    // (offset - pivot) = (10 - 24, 4 - 64) = (-14, -60)
    CHECK(placed.position == glm::vec2(86.0f, 140.0f));
    CHECK(placed.size == glm::vec2(20.0f, 30.0f));
}

TEST_CASE("an untrimmed sprite with the pivot in its corner is drawn from the anchor") {
    SpriteRegion region;
    region.size = region.source_size = {16.0f, 16.0f};
    region.pivot = {0.0f, 0.0f};
    const moteur::PlacedSprite placed = moteur::place_region(region, {5.0f, 7.0f});
    CHECK(placed.position == glm::vec2(5.0f, 7.0f));
}

TEST_CASE("the scale grows the sprite around its pivot") {
    const moteur::PlacedSprite placed = moteur::place_region(character(), {100.0f, 200.0f}, 2.0f);
    CHECK(placed.position == glm::vec2(72.0f, 80.0f));  // 100 + (-14 * 2), 200 + (-60 * 2)
    CHECK(placed.size == glm::vec2(40.0f, 60.0f));
}

TEST_CASE("a flip mirrors the sprite around its pivot") {
    const SpriteRegion region = character();
    const glm::vec2 anchor(100.0f, 200.0f);
    const moteur::PlacedSprite normal = moteur::place_region(region, anchor);
    const moteur::PlacedSprite flipped = moteur::place_region(region, anchor, 1.0f, true);

    // Distance from the pivot to the left and right edges swap places (and their signs).
    const float normal_left = normal.position.x - anchor.x;
    const float normal_right = normal.position.x + normal.size.x - anchor.x;
    const float flipped_left = flipped.position.x - anchor.x;
    const float flipped_right = flipped.position.x + flipped.size.x - anchor.x;
    CHECK(flipped_left == -normal_right);
    CHECK(flipped_right == -normal_left);
    // The vertical placement is untouched, and the size is the same.
    CHECK(flipped.position.y == normal.position.y);
    CHECK(flipped.size == normal.size);
    CHECK(flipped.position == glm::vec2(94.0f, 140.0f));  // known values: [-6, +14] around the pivot
}

TEST_CASE("flipping vertically mirrors around the pivot too") {
    const SpriteRegion region = character();
    const moteur::PlacedSprite normal = moteur::place_region(region, {0.0f, 0.0f});
    const moteur::PlacedSprite flipped = moteur::place_region(region, {0.0f, 0.0f}, 1.0f, false, true);
    CHECK(flipped.position.y == -(normal.position.y + normal.size.y));
    CHECK(flipped.position.x == normal.position.x);
}

TEST_CASE("a centered sprite does not move when it is flipped") {
    SpriteRegion region;
    region.size = {20.0f, 10.0f};
    region.source_size = {20.0f, 10.0f};
    region.pivot = {10.0f, 5.0f};
    const moteur::PlacedSprite normal = moteur::place_region(region, {50.0f, 50.0f});
    const moteur::PlacedSprite flipped = moteur::place_region(region, {50.0f, 50.0f}, 1.0f, true, true);
    CHECK(flipped.position == normal.position);
}

TEST_CASE("flipping twice restores the position") {
    // A pivot expressed in the mirrored image and mirrored again is the original one.
    const SpriteRegion region = character();
    const moteur::PlacedSprite once = moteur::place_region(region, {0.0f, 0.0f}, 1.0f, true);
    SpriteRegion mirrored = region;
    mirrored.offset.x = region.source_size.x - region.offset.x - region.size.x;
    mirrored.pivot.x = region.source_size.x - region.pivot.x;
    const moteur::PlacedSprite twice = moteur::place_region(mirrored, {0.0f, 0.0f}, 1.0f, true);
    const moteur::PlacedSprite plain = moteur::place_region(region, {0.0f, 0.0f});
    CHECK(twice.position == plain.position);
    CHECK(once.position != plain.position);
}
