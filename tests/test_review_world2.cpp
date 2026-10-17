// Regression tests of the defects found while documenting the API (milestone 8, part 9): world2.

#include <doctest/doctest.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "moteur/asset_cache.hpp"
#include "moteur/mesh.hpp"
#include "moteur/model.hpp"
#include "moteur/renderer.hpp"
#include "moteur/shadow.hpp"
#include "moteur/skeleton.hpp"
#include "moteur/world.hpp"

namespace {

moteur::Skeleton one_joint_skeleton() {
    moteur::SkeletonData data;
    data.joints.push_back({"root", -1, {}});
    return moteur::Skeleton::create(data, "one joint");
}

// The boxes the world hands over, in order.
struct Boxes final : moteur::WorldSink {
    std::vector<moteur::Aabb> boxes;
    void mesh(const moteur::Mesh&, const glm::mat4&, const moteur::Material&, const moteur::Aabb& bounds) override {
        boxes.push_back(bounds);
    }
    void light(const moteur::PointLight&) override {}
    void billboard(const moteur::Texture&, glm::vec3, glm::vec2, const moteur::BillboardOptions&) override {}
};

// A mesh without GPU buffers (the world only reads its box): a cube of side `size` at the origin.
moteur::Mesh cube(float size) {
    moteur::Mesh mesh;
    mesh.bounds.add(glm::vec3(-0.5f * size));
    mesh.bounds.add(glm::vec3(0.5f * size));
    return mesh;
}

}  // namespace

TEST_CASE("review world2: Skeleton::replace_in_place() with an empty skeleton on either side") {
    // An empty skeleton (default-constructed or moved-from) takes the fresh one.
    moteur::Skeleton empty;
    empty.replace_in_place(one_joint_skeleton());
    CHECK(empty.joint_count() == 1);
    CHECK(empty.name() == "one joint");

    moteur::Skeleton source = one_joint_skeleton();
    moteur::Skeleton moved = std::move(source);
    source.replace_in_place(one_joint_skeleton());  // NOLINT(bugprone-use-after-move): moved-from is empty
    CHECK(source.joint_count() == 1);

    // An empty fresh skeleton is refused, and the current one stays as it was.
    CHECK_THROWS_AS(moved.replace_in_place(moteur::Skeleton{}), std::invalid_argument);
    CHECK(moved.joint_count() == 1);
    moteur::Skeleton taken = std::move(moved);
    CHECK_THROWS_AS(taken.replace_in_place(std::move(moved)), std::invalid_argument);  // NOLINT(bugprone-use-after-move)
    CHECK(taken.joint_count() == 1);
    moteur::Skeleton nothing;
    CHECK_THROWS_AS(nothing.replace_in_place(moteur::Skeleton{}), std::invalid_argument);
    CHECK(nothing.joint_count() == 0);
}

TEST_CASE("review world2: a model reloaded in place drops the boxes the world kept for it") {
    float size = 1.0f;  // what the "file" holds
    moteur::AssetCache<moteur::Model> models("model");
    const auto load = [&size](std::vector<std::string>& files) {
        files.push_back("models/box.glb");
        moteur::Model model;
        model.parts.emplace_back();
        model.parts.back().mesh = cube(size);
        return model;
    };
    moteur::World world;
    entt::registry& registry = world.registry();
    const entt::entity decor = registry.create();
    registry.emplace<moteur::Transform>(decor);
    registry.emplace<moteur::ModelComponent>(decor, models.get("models/box.glb", load));

    Boxes first;
    world.collect(first, 1.0f);
    REQUIRE(first.boxes.size() == 1);
    CHECK(first.boxes[0].max.x == doctest::Approx(0.5f));
    CHECK(world.cached() == 1);

    // The same number of parts, a bigger box: no EnTT signal, the same handle.
    size = 4.0f;
    REQUIRE(models.reload("models/box.glb"));
    Boxes second;
    world.collect(second, 1.0f);
    REQUIRE(second.boxes.size() == 1);
    CHECK(second.boxes[0].max.x == doctest::Approx(2.0f));
    CHECK(world.cached() == 1);  // kept again, with the new box
}

TEST_CASE("review world2: a mesh reloaded in place drops the box the world kept for it") {
    float size = 1.0f;
    moteur::AssetCache<moteur::Mesh> meshes("mesh");
    const auto load = [&size](std::vector<std::string>& files) {
        files.push_back("meshes/box.bin");
        return cube(size);
    };
    moteur::World world;
    entt::registry& registry = world.registry();
    const entt::entity decor = registry.create();
    registry.emplace<moteur::Transform>(decor);
    registry.emplace<moteur::MeshComponent>(decor, meshes.get("meshes/box.bin", load));

    Boxes first;
    world.collect(first, 1.0f);
    REQUIRE(first.boxes.size() == 1);
    CHECK(first.boxes[0].max.y == doctest::Approx(0.5f));
    size = 3.0f;
    REQUIRE(meshes.reload("meshes/box.bin"));
    Boxes second;
    world.collect(second, 1.0f);
    REQUIRE(second.boxes.size() == 1);
    CHECK(second.boxes[0].max.y == doctest::Approx(1.5f));
    CHECK(world.cached() == 1);
}

TEST_CASE("review world2: fit_sun_shadow() with a resolution of 0 or less counts it as 1") {
    const glm::mat4 camera = glm::perspectiveRH_ZO(0.8f, 16.0f / 9.0f, 0.1f, 100.0f) *
                             glm::lookAtRH(glm::vec3(0.0f, 12.0f, 10.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 to_sun(0.3f, 1.0f, 0.2f);
    moteur::ShadowSettings one;
    one.resolution = 1;
    const moteur::ShadowFrame reference = moteur::fit_sun_shadow(camera, to_sun, one);
    for (const int resolution : {0, -1, -4096}) {
        moteur::ShadowSettings settings;
        settings.resolution = resolution;
        const moteur::ShadowFrame frame = moteur::fit_sun_shadow(camera, to_sun, settings);
        CHECK(std::isfinite(frame.texel_size));
        CHECK(frame.texel_size == reference.texel_size);
        CHECK(frame.texel_size == doctest::Approx(2.0f * frame.radius));
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                CHECK(std::isfinite(frame.view_projection[c][r]));
                CHECK(frame.view_projection[c][r] == reference.view_projection[c][r]);
            }
        }
    }
    // The normal path is unchanged.
    moteur::ShadowSettings normal;
    const moteur::ShadowFrame frame = moteur::fit_sun_shadow(camera, to_sun, normal);
    CHECK(frame.texel_size == 2.0f * frame.radius / 2048.0f);
}

// Mesh::create()'s messages (all "Mesh 'name' ..." or "Mesh (unnamed) ...") need a Renderer, which
// needs a GPU: they are not tested here.
