#include <doctest/doctest.h>

#include <glm/glm.hpp>

#include <stdexcept>

#include "moteur/fixed_timestep.hpp"

// Powers of two are exact in binary floating point, so these tests do not depend on rounding.
namespace {
constexpr double kStep = 1.0 / 64.0;
constexpr double kMaxFrame = 0.25;  // 16 steps
}  // namespace

TEST_CASE("FixedTimestep runs one step per step of elapsed time") {
    moteur::FixedTimestep timestep(kStep, kMaxFrame);
    CHECK(timestep.advance(kStep) == 1);
    CHECK(timestep.advance(kStep) == 1);
    CHECK(timestep.advance(3 * kStep) == 3);
}

TEST_CASE("FixedTimestep runs nothing until a full step has accumulated") {
    moteur::FixedTimestep timestep(kStep, kMaxFrame);
    CHECK(timestep.advance(kStep / 2) == 0);
    CHECK(timestep.alpha() == doctest::Approx(0.5));
    CHECK(timestep.advance(kStep / 2) == 1);
    CHECK(timestep.alpha() == doctest::Approx(0.0));
}

TEST_CASE("FixedTimestep keeps the remainder for the next frame") {
    moteur::FixedTimestep timestep(kStep, kMaxFrame);
    CHECK(timestep.advance(1.5 * kStep) == 1);
    CHECK(timestep.alpha() == doctest::Approx(0.5));
    CHECK(timestep.advance(kStep) == 1);
    CHECK(timestep.alpha() == doctest::Approx(0.5));
}

TEST_CASE("FixedTimestep alpha stays in [0, 1)") {
    moteur::FixedTimestep timestep(kStep, kMaxFrame);
    for (int i = 0; i < 1000; ++i) {
        timestep.advance(0.0137);
        CHECK(timestep.alpha() >= 0.0);
        CHECK(timestep.alpha() < 1.0);
    }
}

TEST_CASE("FixedTimestep caps a long stall instead of catching up") {
    moteur::FixedTimestep timestep(kStep, kMaxFrame);
    CHECK(timestep.advance(10.0) == 16);  // max_frame_time / step
    CHECK(timestep.advance(kStep) == 1);  // back to normal right after
}

TEST_CASE("FixedTimestep ignores negative frame times") {
    moteur::FixedTimestep timestep(kStep, kMaxFrame);
    CHECK(timestep.advance(-1.0) == 0);
    CHECK(timestep.alpha() == doctest::Approx(0.0));
}

TEST_CASE("FixedTimestep simulates the same time whatever the frame rate") {
    // One second of real time, cut into frames of different lengths.
    for (const double frame : {1.0 / 32.0, 1.0 / 64.0, 1.0 / 128.0, 1.0 / 16.0}) {
        moteur::FixedTimestep timestep(kStep, kMaxFrame);
        int steps = 0;
        for (double elapsed = 0.0; elapsed < 1.0; elapsed += frame) {
            steps += timestep.advance(frame);
        }
        CHECK(steps == 64);
    }
}

TEST_CASE("FixedTimestep rejects invalid parameters") {
    CHECK_THROWS_AS(moteur::FixedTimestep(0.0, kMaxFrame), std::invalid_argument);
    CHECK_THROWS_AS(moteur::FixedTimestep(-kStep, kMaxFrame), std::invalid_argument);
    CHECK_THROWS_AS(moteur::FixedTimestep(kStep, 0.0), std::invalid_argument);
}

TEST_CASE("interpolate: exact at both ends, and exactly still when nothing moved") {
    const glm::vec3 a(0.1f, 12.3f, -7.77f);
    const glm::vec3 b(3.3f, -1.0f, 250.5f);
    CHECK(moteur::interpolate(a, b, 0.0f) == a);
    CHECK(moteur::interpolate(2.0f, 6.0f, 0.25f) == 3.0f);
    // A value that did not change stays bit for bit the same whatever t (glm::mix does not).
    for (float t = 0.0f; t < 1.0f; t += 0.013f) {
        CHECK(moteur::interpolate(a, a, t) == a);
        CHECK(moteur::interpolate(0.3f, 0.3f, t) == 0.3f);
    }
}
