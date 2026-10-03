#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "moteur/particles.hpp"

namespace {

std::string read_text(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

moteur::Asset<moteur::ParticleEffect> effect(const std::string& json) {
    return moteur::make_asset(moteur::ParticleEffect::parse(json, "test.json"));
}

}  // namespace

TEST_CASE("ParticleEffect reads emitters, curves and lights") {
    const moteur::ParticleEffect e = moteur::ParticleEffect::parse(R"({
      "version": 1,
      "emitters": [{
        "texture": "particles/spark.png", "frames": [4, 2], "burst": 10, "lifetime": [0.5, 1],
        "shape": "cone", "angle": 90, "direction": [0, 2, 0], "speed": 3,
        "size": [[0, 0.2], [1, 0.0]], "color": [[0, [1, 2, 3, 1]], [0.5, [0, 0, 0, 0]]],
        "facing": "flat", "light": { "intensity": 4, "flicker": 2 }
      }]
    })", "test.json");
    REQUIRE(e.emitters().size() == 1);
    const moteur::EmitterDesc& d = e.emitters()[0];
    CHECK(d.frames == glm::ivec2(4, 2));
    CHECK(d.burst == 10);
    CHECK(d.shape == moteur::EmitterShape::Cone);
    CHECK(d.direction == glm::vec3(0.0f, 1.0f, 0.0f));
    CHECK(d.cos_angle == doctest::Approx(0.0f).epsilon(1e-6));
    CHECK(d.speed == glm::vec2(3.0f));
    CHECK(d.size.at(0.5f) == doctest::Approx(0.1f));
    CHECK(d.color.at(0.25f).y == doctest::Approx(1.0f));
    CHECK(d.color.at(0.9f) == glm::vec4(0.0f));  // past the last key: its value
    CHECK(d.facing == moteur::BillboardFacing::Flat);
    CHECK(d.light.enabled);
    CHECK(d.light.flicker == 1.0f);  // clamped
    CHECK_FALSE(e.loops());
}

TEST_CASE("ParticleEffect refuses broken files") {
    CHECK_THROWS_AS(moteur::ParticleEffect::parse("{", "a.json"), std::runtime_error);
    CHECK_THROWS_AS(moteur::ParticleEffect::parse(R"({"version": 1, "emitters": []})", "a.json"), std::runtime_error);
    CHECK_THROWS_AS(moteur::ParticleEffect::parse(R"({"version": 1, "emitters": [{"burst": 1}]})", "a.json"),
                    std::runtime_error);  // no texture
    CHECK_THROWS_AS(moteur::ParticleEffect::parse(R"({"version": 1, "emitters": [{"texture": "a.png"}]})", "a.json"),
                    std::runtime_error);  // emits nothing
    CHECK_THROWS_AS(moteur::ParticleEffect::parse(
                        R"({"version": 1, "emitters": [{"texture": "a.png", "burst": 1, "shape": "torus"}]})", "a.json"),
                    std::runtime_error);
}

TEST_CASE("A burst lives its lifetime, then the effect is forgotten") {
    moteur::ParticleSystem particles(1000, 7);
    const auto burst = effect(R"({"version": 1, "emitters": [{"texture": "a.png", "burst": 20, "lifetime": [0.5, 0.5],
                                  "shape": "sphere", "speed": [1, 2], "gravity": 9.8}]})");
    const moteur::EffectId id = particles.play(burst, {1.0f, 2.0f, 3.0f});
    particles.update(0.016f);
    CHECK(particles.stats().particles == 20);
    CHECK(particles.alive(id));
    for (int i = 0; i < 40; ++i) {
        particles.update(0.016f);
    }
    CHECK(particles.stats().particles == 0);
    CHECK_FALSE(particles.alive(id));
    CHECK(particles.stats().effects == 0);
}

TEST_CASE("A looping emitter emits at its rate until stopped; the budget refuses the rest") {
    moteur::ParticleSystem particles(50, 7);
    const auto fire = effect(R"({"version": 1, "emitters": [{"texture": "a.png", "rate": 100, "loop": true,
                                 "duration": 1, "lifetime": [10, 10]}]})");
    const moteur::EffectId id = particles.play(fire, glm::vec3(0.0f));
    for (int i = 0; i < 20; ++i) {
        particles.update(0.01f);  // 0.2 s: 20 particles
    }
    CHECK(particles.stats().particles == 20);
    for (int i = 0; i < 50; ++i) {
        particles.update(0.01f);
    }
    CHECK(particles.stats().particles == 50);
    CHECK(particles.stats().refused == 20);
    particles.stop(id);
    particles.update(0.01f);
    CHECK(particles.stats().refused == 20);  // no more emission
    CHECK(particles.alive(id));               // its particles still live
    particles.kill(id);
    CHECK(particles.stats().particles == 0);
    CHECK_FALSE(particles.alive(id));
}

TEST_CASE("Every effect of assets/effects reads") {
    int effects = 0;
    for (const auto& entry : std::filesystem::directory_iterator(MOTEUR_SOURCE_ASSETS "effects")) {
        CAPTURE(entry.path().filename().string());
        const moteur::ParticleEffect e = moteur::ParticleEffect::parse(read_text(entry.path()), entry.path().filename().string());
        for (const moteur::EmitterDesc& d : e.emitters()) {
            CHECK(std::filesystem::exists(std::filesystem::path(MOTEUR_SOURCE_ASSETS) / d.texture));
        }
        ++effects;
    }
    CHECK(effects >= 6);
}
