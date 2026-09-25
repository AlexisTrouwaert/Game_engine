#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "moteur/debug_tools.hpp"
#include "moteur/world.hpp"

namespace {

struct Health {
    float value = 1.0f;
};
struct Secret {
    int value = 0;
};

// Only the world matrices of what is drawn.
struct Positions final : moteur::WorldSink {
    std::vector<glm::vec3> meshes;
    void mesh(const moteur::Mesh&, const glm::mat4& world, const moteur::Material&, const moteur::Aabb&) override {
        meshes.emplace_back(world[3]);
    }
    void light(const moteur::PointLight&) override {}
    void billboard(const moteur::Texture&, glm::vec3, glm::vec2, const moteur::BillboardOptions&) override {}
};

void count_update(int& count, entt::registry&, entt::entity) {
    ++count;
}

moteur::Asset<moteur::Mesh> box_mesh() {
    moteur::Mesh mesh;
    mesh.bounds.add(glm::vec3(-0.5f));
    mesh.bounds.add(glm::vec3(0.5f));
    return moteur::make_asset(std::move(mesh));
}

}  // namespace

TEST_CASE("inspector filter: by name without case, or by number") {
    CHECK(moteur::inspector_filter_matches("", 7, "torche 3"));
    CHECK(moteur::inspector_filter_matches("  ", 7, ""));
    CHECK(moteur::inspector_filter_matches("TORCHE", 7, "torche 3"));
    CHECK(moteur::inspector_filter_matches("che 3", 7, "torche 3"));
    CHECK_FALSE(moteur::inspector_filter_matches("créature", 7, "torche 3"));
    CHECK(moteur::inspector_filter_matches("créature", 7, "créature 12"));  // accents compared as they are
    CHECK(moteur::inspector_filter_matches("#7", 7, ""));
    CHECK(moteur::inspector_filter_matches("7", 7, ""));
    CHECK(moteur::inspector_filter_matches(" 7 ", 7, ""));
    CHECK_FALSE(moteur::inspector_filter_matches("#70", 7, ""));
    CHECK(moteur::inspector_filter_matches("12", 5, "créature 12"));  // a number in the name counts too
    CHECK_FALSE(moteur::inspector_filter_matches("#", 5, "créature"));
}

TEST_CASE("component inspectors: a game's component, edited through a copy") {
    moteur::ComponentInspectors inspectors;
    entt::registry registry;
    const entt::entity entity = registry.create();
    registry.emplace<Health>(entity, 0.5f);
    registry.emplace<Secret>(entity, 42);

    int updates = 0;
    registry.on_update<Health>().connect<&count_update>(updates);

    bool change = false;
    inspectors.add<Health>("Santé", [&change](Health& health) {
        if (change) {
            health.value = 0.25f;
        }
        return change;
    });
    CHECK(inspectors.registered(entt::type_id<Health>()));
    CHECK(inspectors.name(entt::type_id<Health>()) == "Santé");
    CHECK_FALSE(inspectors.registered(entt::type_id<Secret>()));
    CHECK(inspectors.name(entt::type_id<Secret>()).find("Secret") != std::string::npos);  // its C++ name

    // Looked at, not changed: nothing replaced, no signal.
    CHECK_FALSE(inspectors.edit(registry, entity, entt::type_id<Health>()));
    CHECK(updates == 0);
    CHECK(registry.get<Health>(entity).value == 0.5f);

    change = true;
    CHECK(inspectors.edit(registry, entity, entt::type_id<Health>()));
    CHECK(updates == 1);
    CHECK(registry.get<Health>(entity).value == 0.25f);

    // Not registered, or a tag: nothing to run.
    CHECK_FALSE(inspectors.edit(registry, entity, entt::type_id<Secret>()));
    inspectors.add_tag<moteur::Hidden>("Caché");
    CHECK(inspectors.name(entt::type_id<moteur::Hidden>()) == "Caché");
    CHECK_FALSE(inspectors.edit(registry, entity, entt::type_id<moteur::Hidden>()));
}

TEST_CASE("component inspectors: moving a still entity drops its kept world matrix") {
    moteur::World world;
    entt::registry& registry = world.registry();
    const entt::entity crate = registry.create();
    registry.emplace<moteur::Transform>(crate, moteur::Transform{glm::vec3(1.0f, 0.0f, 2.0f)});
    registry.emplace<moteur::MeshComponent>(crate, box_mesh(), moteur::Material{});

    Positions first;
    world.collect(first, 1.0f);
    CHECK(world.cached() == 1);

    moteur::ComponentInspectors inspectors;
    inspectors.add<moteur::Transform>("Transform", [](moteur::Transform& transform) {
        transform.position.x = 5.0f;
        return true;
    });
    CHECK(inspectors.edit(registry, crate, entt::type_id<moteur::Transform>()));
    CHECK(world.cached() == 0);

    Positions second;
    world.collect(second, 1.0f);
    REQUIRE(second.meshes.size() == 1);
    CHECK(second.meshes[0].x == doctest::Approx(5.0f));
    CHECK(second.meshes[0].z == doctest::Approx(2.0f));
}
