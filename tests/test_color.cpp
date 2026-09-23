#include <doctest/doctest.h>

#include <glm/glm.hpp>

#include "moteur/color.hpp"

TEST_CASE("sRGB transfer functions: known values") {
    CHECK(moteur::srgb_to_linear(0.0f) == 0.0f);
    CHECK(moteur::srgb_to_linear(1.0f) == doctest::Approx(1.0f));
    CHECK(moteur::srgb_to_linear(0.5f) == doctest::Approx(0.21404f).epsilon(1e-4));  // mid grey is dark in linear
    CHECK(moteur::linear_to_srgb(0.18f) == doctest::Approx(0.46135f).epsilon(1e-4));  // "18 % grey"
    // The linear segment near black.
    CHECK(moteur::srgb_to_linear(0.02f) == doctest::Approx(0.02f / 12.92f));
    // Out-of-range inputs are clamped.
    CHECK(moteur::srgb_to_linear(2.0f) == doctest::Approx(1.0f));
    CHECK(moteur::linear_to_srgb(-1.0f) == 0.0f);
}

TEST_CASE("sRGB transfer functions are inverse of each other and increasing") {
    float previous = -1.0f;
    for (int i = 0; i <= 255; ++i) {
        const float srgb = static_cast<float>(i) / 255.0f;
        const float linear = moteur::srgb_to_linear(srgb);
        CHECK(linear > previous);
        previous = linear;
        CHECK(moteur::linear_to_srgb(linear) == doctest::Approx(srgb).epsilon(1e-5));
    }
    const glm::vec3 color(0.2f, 0.5f, 0.9f);
    const glm::vec3 back = moteur::linear_to_srgb(moteur::srgb_to_linear(color));
    CHECK(glm::length(back - color) < 1e-5f);
}

TEST_CASE("PBR Neutral tone mapping keeps ordinary colors and compresses highlights") {
    // Black stays black.
    CHECK(glm::length(moteur::tonemap_pbr_neutral(glm::vec3(0.0f))) < 1e-6f);
    // A mid grey only loses the small constant offset.
    CHECK(moteur::tonemap_pbr_neutral(glm::vec3(0.5f)).r == doctest::Approx(0.46f));
    // A colored surface below the compression threshold keeps its hue exactly (same offset on every channel).
    const glm::vec3 wood(0.4f, 0.25f, 0.1f);
    const glm::vec3 mapped = moteur::tonemap_pbr_neutral(wood);
    CHECK(glm::length((mapped - wood) - (mapped.r - wood.r)) < 1e-6f);

    // Very bright values stay below 1, and brighter in means brighter out.
    float previous = 0.0f;
    for (float value = 0.1f; value < 100.0f; value *= 1.5f) {
        const glm::vec3 out = moteur::tonemap_pbr_neutral(glm::vec3(value));
        CHECK(out.r <= 1.0f);
        CHECK(out.r > previous);
        previous = out.r;
    }
    // A very bright saturated light is pulled towards white (desaturation).
    const glm::vec3 fire = moteur::tonemap_pbr_neutral({20.0f, 4.0f, 0.5f});
    CHECK(fire.r <= 1.0f);
    CHECK(fire.b / fire.r > 0.5f / 20.0f);
}
