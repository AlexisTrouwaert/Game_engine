// Regression tests of the defects found while documenting the API (milestone 8, part 9): apps.

#include <doctest/doctest.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>

#include "moteur/movement.hpp"

// The demo 3D turned the characters without a Mover with std::atan2 in a tick (trigonometry in game
// logic: its last bits may differ between platforms and compilers). It now goes through
// facing_rotation() (square roots only, exact in IEEE 754), as the Movers are turned: the same
// rotation, to its rounding (near straight ahead the half-angle formula loses a few bits: at most
// a few 1e-4 rad, nothing that shows).
TEST_CASE("Review apps: facing_rotation of a unit move turns like the former atan2 about +Y") {
    for (int i = 0; i < 360; ++i) {
        const float a = static_cast<float>(i) * 0.0174532925f + 0.001f;  // every degree, off the axes too
        for (const float length : {1e-4f, 0.03f, 1.0f, 7.5f}) {
            const glm::vec2 moved = length * glm::vec2(std::sin(a), std::cos(a));  // x, z: KayKit faces +Z
            const glm::quat before = glm::angleAxis(std::atan2(moved.x, moved.y), glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::quat after = moteur::facing_rotation(moved / std::sqrt(glm::dot(moved, moved)));
            CHECK(std::abs(glm::dot(before, after)) == doctest::Approx(1.0f).epsilon(1e-6));  // same rotation
            const glm::vec3 forward = after * glm::vec3(0.0f, 0.0f, 1.0f);
            CHECK(std::abs(forward.x - std::sin(a)) < 2e-4f);
            CHECK(std::abs(forward.z - std::cos(a)) < 2e-4f);
            CHECK(std::abs(forward.y) < 1e-6f);
        }
    }
    // Straight ahead and straight back (the half-angle formulas' ends).
    const glm::quat ahead = moteur::facing_rotation({0.0f, 1.0f});
    CHECK(ahead.w == 1.0f);
    CHECK(ahead.y == 0.0f);
    const glm::vec3 back = moteur::facing_rotation({0.0f, -1.0f}) * glm::vec3(0.0f, 0.0f, 1.0f);
    CHECK(back.z == doctest::Approx(-1.0f));
}
