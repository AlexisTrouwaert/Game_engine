#include <doctest/doctest.h>

#include <glm/glm.hpp>

#include <cmath>

#include "moteur/environment.hpp"

namespace {

moteur::EnvironmentImage uniform(glm::vec3 color, int width = 64, int height = 32) {
    moteur::EnvironmentImage image;
    image.width = width;
    image.height = height;
    image.texels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), color);
    return image;
}

// White above the horizon, black below: a sky with a dark ground.
moteur::EnvironmentImage upper_hemisphere(int width = 128, int height = 64) {
    moteur::EnvironmentImage image = uniform(glm::vec3(0.0f), width, height);
    for (int y = 0; y < height / 2; ++y) {
        for (int x = 0; x < width; ++x) {
            image.at(x, y) = glm::vec3(1.0f);
        }
    }
    return image;
}

}  // namespace

TEST_CASE("equirect mapping: up is the top row, and the mapping goes both ways") {
    CHECK(moteur::direction_to_equirect({0, 1, 0}).y == doctest::Approx(0.0f));
    CHECK(moteur::direction_to_equirect({0, -1, 0}).y == doctest::Approx(1.0f));
    CHECK(moteur::direction_to_equirect({1, 0, 0}).y == doctest::Approx(0.5f));
    for (float u = 0.05f; u < 1.0f; u += 0.1f) {
        for (float v = 0.05f; v < 1.0f; v += 0.1f) {
            const glm::vec3 d = moteur::equirect_to_direction({u, v});
            CHECK(glm::length(d) == doctest::Approx(1.0f));
            const glm::vec2 back = moteur::direction_to_equirect(d);
            CHECK(back.x == doctest::Approx(u).epsilon(1e-4));
            CHECK(back.y == doctest::Approx(v).epsilon(1e-4));
        }
    }
}

TEST_CASE("irradiance of a uniform environment: a white surface reflects exactly that environment") {
    // E = pi * L for a constant radiance L, and evaluate() gives E / pi.
    const moteur::IrradianceSH sh = moteur::project_irradiance(uniform({0.5f, 1.0f, 2.0f}));
    for (const glm::vec3 n : {glm::vec3(0, 1, 0), glm::vec3(0, -1, 0), glm::vec3(1, 0, 0), glm::normalize(glm::vec3(1, 1, -1))}) {
        const glm::vec3 e = sh.evaluate(n);
        CHECK(e.r == doctest::Approx(0.5f).epsilon(0.01));
        CHECK(e.g == doctest::Approx(1.0f).epsilon(0.01));
        CHECK(e.b == doctest::Approx(2.0f).epsilon(0.01));
    }
}

TEST_CASE("irradiance of a lit upper hemisphere: full facing up, half sideways, almost none facing down") {
    const moteur::IrradianceSH sh = moteur::project_irradiance(upper_hemisphere());
    // Exact values are 1, 0.5 and 0; order-2 harmonics only approximate the hemisphere's sharp edge.
    CHECK(sh.evaluate({0, 1, 0}).r == doctest::Approx(1.0f).epsilon(0.06));
    CHECK(sh.evaluate({1, 0, 0}).r == doctest::Approx(0.5f).epsilon(0.02));
    CHECK(sh.evaluate({0, 0, 1}).r == doctest::Approx(0.5f).epsilon(0.02));
    CHECK(sh.evaluate({0, -1, 0}).r < 0.06f);
}

TEST_CASE("prefilter_specular: level sizes halve, a uniform environment stays uniform") {
    const auto levels = moteur::prefilter_specular(uniform({0.3f, 0.6f, 0.9f}), 32, 4, 16);
    REQUIRE(levels.size() == 4);
    for (std::size_t k = 0; k < levels.size(); ++k) {
        CHECK(levels[k].width == 32 >> k);
        CHECK(levels[k].height == (32 >> k) / 2);
        for (const glm::vec3& texel : levels[k].texels) {
            CHECK(glm::length(texel - glm::vec3(0.3f, 0.6f, 0.9f)) < 1e-4f);
        }
    }
}

TEST_CASE("prefilter_specular: rougher levels blur the horizon of a sharp sky") {
    // The real sizes: 256 x 128 down to 8 x 4.
    const auto levels = moteur::prefilter_specular(upper_hemisphere(512, 256), 256, 6, 64);
    // Near straight up it stays white, near straight down black, as long as the texels are small
    // enough (in the 16 x 8 and 8 x 4 levels, a texel next to the pole is already 11 to 22 degrees
    // away from it, and a rough lobe that wide rightly reaches past the horizon).
    for (std::size_t k = 0; k < 4; ++k) {
        CHECK(levels[k].sample({0, 1, 0}).r > 0.95f);
        CHECK(levels[k].sample({0, -1, 0}).r < 0.05f);
    }
    // ...but just above the horizon, a mirror sees the sky and a rough surface sees half ground.
    const glm::vec3 above = glm::normalize(glm::vec3(1.0f, 0.15f, 0.0f));
    CHECK(levels[0].sample(above).r > 0.95f);
    CHECK(levels[5].sample(above).r < 0.85f);
    CHECK(levels[5].sample(above).r > 0.4f);
}

TEST_CASE("make_sky: the zenith, horizon and ground colors land where they should") {
    moteur::SkySettings settings;
    // Tall enough for rows close to the horizon (the blend rises quickly above it).
    const moteur::EnvironmentImage sky = moteur::make_sky(64, 1024, settings);
    CHECK(glm::length(sky.at(10, 0) - settings.zenith) < 0.02f);
    CHECK(glm::length(sky.at(10, 1023) - settings.ground) < 1e-5f);
    const glm::vec3 at_horizon = sky.sample({1, 0.001f, 0});
    CHECK(glm::length(at_horizon - settings.horizon) < 0.1f);
}
