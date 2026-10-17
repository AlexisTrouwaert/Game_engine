// Regression tests of the defects found while documenting the API (milestone 8, part 9): render.

#include <doctest/doctest.h>

#include <glm/glm.hpp>

#include <cmath>
#include <stdexcept>

#include "moteur/asset_cache.hpp"
#include "moteur/camera3d.hpp"
#include "moteur/environment.hpp"
#include "moteur/particles.hpp"
#include "moteur/shadow.hpp"
#include "moteur/sprite_batcher.hpp"
#include "moteur/visibility.hpp"

namespace {

bool finite(glm::vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool finite(const glm::mat4& m) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (!std::isfinite(m[c][r])) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

TEST_CASE("review render: Camera3D never looks straight down, so the billboards' right axis exists") {
    // BillboardRenderer::set_camera() derives its right axis from cross(forward, +y).
    for (const float pitch : {-90.0f, 0.0f, 89.0f, 90.0f, 1000.0f}) {
        moteur::Camera3D camera;
        camera.set_angles(30.0f, pitch);
        CHECK(glm::length(glm::cross(camera.forward(), glm::vec3(0.0f, 1.0f, 0.0f))) > 0.01f);
    }
}

TEST_CASE("review render: playing an empty effect handle returns kNoEffect") {
    moteur::ParticleSystem particles;
    const moteur::Asset<moteur::ParticleEffect> empty;
    CHECK(particles.play(empty, glm::vec3(0.0f)) == moteur::kNoEffect);
    particles.update(0.05f);
    CHECK(particles.stats().effects == 0);
    // A real effect still plays afterwards.
    const auto effect = moteur::make_asset(moteur::ParticleEffect::parse(
        R"({"version": 1, "emitters": [{"texture": "a.png", "burst": 3}]})", "test.json"));
    const moteur::EffectId id = particles.play(effect, glm::vec3(0.0f));
    CHECK(id != moteur::kNoEffect);
    CHECK(particles.alive(id));
}

TEST_CASE("review render: SpriteBatcher caps its runs at kMaxQuadsPerRun") {
    moteur::SpriteBatcher batcher(moteur::SpriteBatcher::kMaxQuadsPerRun * 4);
    batcher.begin();
    moteur::SpriteDesc sprite;
    int texture = 0;
    sprite.texture = &texture;
    for (std::uint32_t i = 0; i < moteur::SpriteBatcher::kMaxQuadsPerRun + 10; ++i) {
        batcher.add(sprite);
    }
    batcher.finish();
    REQUIRE(batcher.runs().size() == 2);
    CHECK(batcher.runs()[0].count == moteur::SpriteBatcher::kMaxQuadsPerRun);
    CHECK(batcher.runs()[1].count == 10);
}

TEST_CASE("review render: Camera3D without a viewport gives documented results, not NaN") {
    moteur::Camera3D reference;
    for (const glm::vec2 viewport : {glm::vec2(0.0f), glm::vec2(0.0f, 720.0f), glm::vec2(1280.0f, 0.0f)}) {
        moteur::Camera3D camera;
        camera.set_viewport(viewport);
        CHECK(finite(camera.projection_matrix()));
        const moteur::Ray ray = camera.screen_ray({100.0f, 100.0f});
        CHECK(finite(ray.origin));
        CHECK(finite(ray.direction));
        // The ray through the middle of the view: it reaches the target.
        CHECK(glm::length(glm::cross(ray.direction, camera.forward())) < 1e-3f);
        CHECK_FALSE(camera.world_to_screen(glm::vec3(0.0f)).has_value());
    }
    // A normal viewport is unchanged.
    const std::optional<glm::vec2> middle = reference.world_to_screen(reference.target());
    REQUIRE(middle.has_value());
    CHECK(middle->x == doctest::Approx(640.0f));
    CHECK(middle->y == doctest::Approx(360.0f));
}

TEST_CASE("review render: a degenerate matrix gives zero frustum planes, not NaN") {
    const moteur::Frustum frustum = moteur::Frustum::from_view_projection(glm::mat4(0.0f));
    for (const glm::vec4& plane : frustum.planes) {
        CHECK(plane == glm::vec4(0.0f));
    }
    CHECK(frustum.contains(glm::vec3(1.0f, 2.0f, 3.0f)));
    // A normal camera still culls.
    const moteur::Frustum view = moteur::Camera3D().frustum();
    CHECK(view.contains(glm::vec3(0.0f)));
    CHECK_FALSE(view.contains(glm::vec3(1000.0f, 0.0f, 0.0f)));
}

TEST_CASE("review render: fit_sun_shadow() with a zero sun direction counts it as straight up") {
    const glm::mat4 camera = moteur::Camera3D().view_projection();
    const moteur::ShadowFrame zero = moteur::fit_sun_shadow(camera, glm::vec3(0.0f));
    const moteur::ShadowFrame up = moteur::fit_sun_shadow(camera, glm::vec3(0.0f, 1.0f, 0.0f));
    CHECK(finite(zero.view_projection));
    CHECK(finite(zero.center));
    CHECK(zero.view_projection == up.view_projection);
    CHECK(zero.center == up.center);
}

TEST_CASE("review render: sampling an empty environment image gives black") {
    const moteur::EnvironmentImage empty;
    CHECK(empty.sample(glm::vec3(0.0f, 1.0f, 0.0f)) == glm::vec3(0.0f));
    moteur::EnvironmentImage one;
    one.width = 1;
    one.height = 1;
    one.texels = {glm::vec3(0.5f)};
    CHECK(one.sample(glm::vec3(1.0f, 0.0f, 0.0f)) == glm::vec3(0.5f));
}

TEST_CASE("review render: ExploredMap refuses a negative size") {
    CHECK_THROWS_AS(moteur::ExploredMap(-1, 10), std::invalid_argument);
    CHECK_THROWS_AS(moteur::ExploredMap(10, -1), std::invalid_argument);
    const moteur::ExploredMap empty(0, 5);
    CHECK(empty.cells().empty());
    const moteur::ExploredMap map(3, 2);
    CHECK(map.cells().size() == 6);
}

TEST_CASE("set_ground() on no effect does nothing") {
    moteur::ParticleSystem system(100, 1);
    // The ground itself is used when drawing (a GPU is needed): see the slice reference capture.
    system.set_ground(moteur::kNoEffect, 2.0f);
    CHECK_FALSE(system.alive(moteur::kNoEffect));
}
