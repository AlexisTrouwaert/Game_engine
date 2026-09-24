#include <doctest/doctest.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

#include "moteur/debug_lines.hpp"

TEST_CASE("DebugLineBuffer: a box has its 12 edges, each along one axis") {
    moteur::DebugLineBuffer lines;
    moteur::Aabb box;
    box.add({-1.0f, 0.0f, 2.0f});
    box.add({3.0f, 2.0f, 5.0f});
    lines.box(box, {1, 0, 0, 1});
    REQUIRE(lines.depth_tested().size() == 24);
    CHECK(lines.on_top().empty());
    float total = 0.0f;
    for (std::size_t i = 0; i < 24; i += 2) {
        const glm::vec3 d = lines.depth_tested()[i + 1].position - lines.depth_tested()[i].position;
        const int axes = (d.x != 0.0f) + (d.y != 0.0f) + (d.z != 0.0f);
        CHECK(axes == 1);
        total += glm::length(d);
    }
    CHECK(total == doctest::Approx(4.0f * (4.0f + 2.0f + 3.0f)));  // four edges along each axis
    lines.box(moteur::Aabb{}, {1, 1, 1, 1});                      // an empty box draws nothing
    CHECK(lines.line_count() == 12);
}

TEST_CASE("DebugLineBuffer: a frustum's corners are those of the view volume") {
    const glm::mat4 projection = glm::orthoRH_ZO(-2.0f, 2.0f, -1.0f, 1.0f, 0.5f, 10.0f);
    const glm::mat4 view = glm::lookAtRH(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    moteur::DebugLineBuffer lines;
    lines.frustum(projection * view, {1, 1, 0, 1}, true);
    REQUIRE(lines.on_top().size() == 24);
    for (const moteur::DebugLineVertex& vertex : lines.on_top()) {
        CHECK(std::abs(vertex.position.x) == doctest::Approx(2.0f));
        CHECK(std::abs(vertex.position.y) == doctest::Approx(1.0f));
        const float z = vertex.position.z;
        CHECK((z == doctest::Approx(-0.5f) || z == doctest::Approx(-10.0f)));
    }
}

TEST_CASE("DebugLineBuffer: circles, spheres and axes") {
    moteur::DebugLineBuffer lines;
    lines.circle({1.0f, 2.0f, 3.0f}, {0.0f, 1.0f, 0.0f}, 2.0f, {1, 1, 1, 1}, 16);
    REQUIRE(lines.line_count() == 16);
    for (const moteur::DebugLineVertex& vertex : lines.depth_tested()) {
        const glm::vec3 d = vertex.position - glm::vec3(1.0f, 2.0f, 3.0f);
        CHECK(glm::length(d) == doctest::Approx(2.0f));
        CHECK(d.y == doctest::Approx(0.0f).epsilon(1e-5));  // in the plane perpendicular to the normal
    }
    // Closed: the last point is the first.
    CHECK(glm::length(lines.depth_tested().back().position - lines.depth_tested().front().position) < 1e-5f);

    lines.clear();
    lines.sphere({0.0f, 0.0f, 0.0f}, 1.0f, {1, 1, 1, 1});
    CHECK(lines.line_count() == 3 * 32);
    lines.clear();
    lines.axes({0.0f, 0.0f, 0.0f}, 2.0f);  // drawn on top by default
    REQUIRE(lines.on_top().size() == 6);
    CHECK(lines.on_top()[1].position == glm::vec3(2.0f, 0.0f, 0.0f));
    CHECK(lines.on_top()[3].position == glm::vec3(0.0f, 2.0f, 0.0f));
    CHECK(lines.on_top()[5].position == glm::vec3(0.0f, 0.0f, 2.0f));
}
