#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

#include "moteur/sprite_batcher.hpp"

using moteur::SpriteBatcher;
using moteur::SpriteDesc;

namespace {

// Two distinct addresses stand in for two textures.
int texture_a = 0;
int texture_b = 0;

SpriteDesc sprite(const void* texture, float x, float depth = 0.0f) {
    SpriteDesc s;
    s.texture = texture;
    s.position = {x, 0.0f};
    s.size = {10.0f, 10.0f};
    s.depth = depth;
    return s;
}

}  // namespace

TEST_CASE("pack_color converts to bytes with red in the lowest byte") {
    CHECK(moteur::pack_color({1.0f, 1.0f, 1.0f, 1.0f}) == 0xFFFFFFFFu);
    CHECK(moteur::pack_color({0.0f, 0.0f, 0.0f, 0.0f}) == 0x00000000u);
    CHECK(moteur::pack_color({1.0f, 0.5f, 0.0f, 1.0f}) == 0xFF0080FFu);
}

TEST_CASE("pack_color clamps out-of-range components") {
    CHECK(moteur::pack_color({2.0f, -1.0f, 0.0f, 1.0f}) == 0xFF0000FFu);
}

TEST_CASE("a sprite becomes four corners with matching texture coordinates and tint") {
    SpriteBatcher batcher;
    batcher.begin();
    SpriteDesc s;
    s.texture = &texture_a;
    s.position = {10.0f, 20.0f};
    s.size = {30.0f, 40.0f};
    s.uv_rect = {0.25f, 0.5f, 0.75f, 1.0f};
    s.tint = moteur::pack_color({1.0f, 0.0f, 0.0f, 0.5f});
    batcher.add(s);
    batcher.finish();

    REQUIRE(batcher.vertices().size() == 4);
    const auto& v = batcher.vertices();
    // top left, top right, bottom right, bottom left
    CHECK((v[0].x == 10.0f && v[0].y == 20.0f && v[0].u == 0.25f && v[0].v == 0.5f));
    CHECK((v[1].x == 40.0f && v[1].y == 20.0f && v[1].u == 0.75f && v[1].v == 0.5f));
    CHECK((v[2].x == 40.0f && v[2].y == 60.0f && v[2].u == 0.75f && v[2].v == 1.0f));
    CHECK((v[3].x == 10.0f && v[3].y == 60.0f && v[3].u == 0.25f && v[3].v == 1.0f));
    for (const auto& corner : v) {
        CHECK((corner.r == 255 && corner.g == 0 && corner.b == 0 && corner.a == 128));
    }
}

TEST_CASE("flipping swaps the texture coordinates, not the positions") {
    SpriteBatcher batcher;
    batcher.begin();
    SpriteDesc flipped = sprite(&texture_a, 0.0f);
    flipped.flip_x = true;
    batcher.add(flipped);
    SpriteDesc flipped_y = sprite(&texture_a, 0.0f);
    flipped_y.flip_y = true;
    batcher.add(flipped_y);
    batcher.finish();

    const auto& v = batcher.vertices();
    // flip_x: the left corners now show u = 1 and the right corners u = 0
    CHECK(v[0].u == 1.0f);
    CHECK(v[1].u == 0.0f);
    CHECK(v[0].x == 0.0f);
    CHECK(v[1].x == 10.0f);
    // flip_y: the top corners now show v = 1 and the bottom corners v = 0
    CHECK(v[4].v == 1.0f);
    CHECK(v[6].v == 0.0f);
}

TEST_CASE("consecutive sprites with the same texture share one run") {
    SpriteBatcher batcher;
    batcher.begin();
    for (int i = 0; i < 5; ++i) {
        batcher.add(sprite(&texture_a, static_cast<float>(i)));
    }
    batcher.finish();

    REQUIRE(batcher.runs().size() == 1);
    CHECK(batcher.runs()[0].texture == &texture_a);
    CHECK(batcher.runs()[0].first_sprite == 0);
    CHECK(batcher.runs()[0].count == 5);
}

TEST_CASE("a texture change starts a new run and keeps the recording order") {
    SpriteBatcher batcher;
    batcher.begin();
    batcher.add(sprite(&texture_a, 0.0f));
    batcher.add(sprite(&texture_a, 1.0f));
    batcher.add(sprite(&texture_b, 2.0f));
    batcher.add(sprite(&texture_b, 3.0f));
    batcher.add(sprite(&texture_a, 4.0f));
    batcher.finish();

    const auto& runs = batcher.runs();
    REQUIRE(runs.size() == 3);
    CHECK((runs[0].texture == &texture_a && runs[0].first_sprite == 0 && runs[0].count == 2));
    CHECK((runs[1].texture == &texture_b && runs[1].first_sprite == 2 && runs[1].count == 2));
    CHECK((runs[2].texture == &texture_a && runs[2].first_sprite == 4 && runs[2].count == 1));
}

TEST_CASE("sprites are drawn in depth order") {
    SpriteBatcher batcher;
    batcher.begin();
    batcher.add(sprite(&texture_a, 0.0f, 2.0f));  // recorded first, drawn last
    batcher.add(sprite(&texture_a, 1.0f, 0.0f));
    batcher.add(sprite(&texture_a, 2.0f, 1.0f));
    batcher.finish();

    const auto& v = batcher.vertices();
    CHECK(v[0].x == 1.0f);
    CHECK(v[4].x == 2.0f);
    CHECK(v[8].x == 0.0f);
}

TEST_CASE("sprites with the same depth keep the order in which they were recorded") {
    SpriteBatcher batcher;
    batcher.begin();
    batcher.add(sprite(&texture_a, 5.0f, 1.0f));
    batcher.add(sprite(&texture_a, 6.0f, 1.0f));
    batcher.add(sprite(&texture_a, 7.0f, 0.0f));  // forces a sort
    batcher.finish();

    const auto& v = batcher.vertices();
    CHECK(v[0].x == 7.0f);
    CHECK(v[4].x == 5.0f);
    CHECK(v[8].x == 6.0f);
}

TEST_CASE("sprites already recorded in depth order are left alone") {
    SpriteBatcher batcher;
    batcher.begin();
    batcher.add(sprite(&texture_a, 0.0f, 0.0f));
    batcher.add(sprite(&texture_a, 1.0f, 0.5f));
    batcher.add(sprite(&texture_a, 2.0f, 0.5f));
    batcher.finish();

    const auto& v = batcher.vertices();
    CHECK(v[0].x == 0.0f);
    CHECK(v[4].x == 1.0f);
    CHECK(v[8].x == 2.0f);
}

TEST_CASE("depth order wins over texture grouping") {
    SpriteBatcher batcher;
    batcher.begin();
    batcher.add(sprite(&texture_a, 0.0f, 1.0f));
    batcher.add(sprite(&texture_b, 1.0f, 0.0f));
    batcher.add(sprite(&texture_a, 2.0f, 0.0f));
    batcher.finish();

    // Sorted: b(0), a(0), a(1) -> the two a sprites end up adjacent.
    const auto& runs = batcher.runs();
    REQUIRE(runs.size() == 2);
    CHECK((runs[0].texture == &texture_b && runs[0].count == 1));
    CHECK((runs[1].texture == &texture_a && runs[1].count == 2));
}

TEST_CASE("sorting matches a reference stable sort on many random depths") {
    // Depths include negative values, zero, ties and very different magnitudes.
    std::uint32_t state = 2024;
    const auto next = [&state] {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    std::vector<float> depths;
    for (int i = 0; i < 3000; ++i) {
        const auto choice = next() % 4;
        float depth = 0.0f;
        if (choice == 0) depth = static_cast<float>(next() % 10) - 5.0f;               // many ties, both signs
        if (choice == 1) depth = static_cast<float>(next() % 100000) / 100.0f;          // large positive
        if (choice == 2) depth = -static_cast<float>(next() % 100000) / 1000.0f;        // negative
        if (choice == 3) depth = static_cast<float>(next() % 1000) * 1e-6f;             // tiny
        depths.push_back(depth);
    }

    SpriteBatcher batcher;
    batcher.begin();
    for (std::size_t i = 0; i < depths.size(); ++i) {
        batcher.add(sprite(&texture_a, static_cast<float>(i), depths[i]));  // x carries the recording index
    }
    batcher.finish();

    std::vector<std::size_t> expected(depths.size());
    std::iota(expected.begin(), expected.end(), std::size_t{0});
    std::stable_sort(expected.begin(), expected.end(),
                     [&](std::size_t a, std::size_t b) { return depths[a] < depths[b]; });

    REQUIRE(batcher.vertices().size() == depths.size() * 4);
    std::size_t wrong = 0;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (batcher.vertices()[i * 4].x != static_cast<float>(expected[i])) {
            ++wrong;
        }
    }
    CHECK(wrong == 0);
}

TEST_CASE("sorting handles negative depths and zero") {
    SpriteBatcher batcher;
    batcher.begin();
    batcher.add(sprite(&texture_a, 0.0f, 1.0f));
    batcher.add(sprite(&texture_a, 1.0f, -1.0f));
    batcher.add(sprite(&texture_a, 2.0f, 0.0f));
    batcher.add(sprite(&texture_a, 3.0f, -2.5f));
    batcher.finish();

    const auto& v = batcher.vertices();
    CHECK(v[0].x == 3.0f);  // -2.5
    CHECK(v[4].x == 1.0f);  // -1
    CHECK(v[8].x == 2.0f);  //  0
    CHECK(v[12].x == 0.0f);  //  1
}

TEST_CASE("a run never exceeds the maximum number of quads") {
    SpriteBatcher batcher(4);
    batcher.begin();
    for (int i = 0; i < 10; ++i) {
        batcher.add(sprite(&texture_a, static_cast<float>(i)));
    }
    batcher.finish();

    const auto& runs = batcher.runs();
    REQUIRE(runs.size() == 3);
    CHECK((runs[0].first_sprite == 0 && runs[0].count == 4));
    CHECK((runs[1].first_sprite == 4 && runs[1].count == 4));
    CHECK((runs[2].first_sprite == 8 && runs[2].count == 2));
}

TEST_CASE("the default maximum fits 16-bit indices") {
    // 4 vertices per quad, indices 0..65535: 65536 vertices at most.
    CHECK(SpriteBatcher::kMaxQuadsPerRun * 4 == 65536);
}

TEST_CASE("with batching disabled every sprite is its own run") {
    SpriteBatcher batcher;
    batcher.set_batching(false);
    batcher.begin();
    for (int i = 0; i < 3; ++i) {
        batcher.add(sprite(&texture_a, static_cast<float>(i)));
    }
    batcher.finish();

    REQUIRE(batcher.runs().size() == 3);
    CHECK(batcher.runs()[2].first_sprite == 2);
    CHECK(batcher.runs()[2].count == 1);
}

TEST_CASE("begin() starts a fresh frame") {
    SpriteBatcher batcher;
    batcher.begin();
    batcher.add(sprite(&texture_a, 0.0f, 5.0f));
    batcher.add(sprite(&texture_a, 1.0f, 0.0f));
    batcher.finish();
    REQUIRE(batcher.sprite_count() == 2);

    batcher.begin();
    batcher.finish();
    CHECK(batcher.sprite_count() == 0);
    CHECK(batcher.vertices().empty());
    CHECK(batcher.runs().empty());

    // The sorted flag was reset too: depth 0 after the earlier depth 5 is not "out of order".
    batcher.add(sprite(&texture_a, 3.0f, 0.0f));
    batcher.add(sprite(&texture_a, 4.0f, 0.0f));
    batcher.finish();
    CHECK(batcher.vertices()[0].x == 3.0f);
}
