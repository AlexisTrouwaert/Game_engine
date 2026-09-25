#include <doctest/doctest.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <string>
#include <vector>

#include "moteur/world.hpp"

namespace {

// Meshes without GPU buffers: the world only reads their box.
moteur::Asset<moteur::Mesh> make_mesh() {
    moteur::Mesh mesh;
    mesh.bounds.add(glm::vec3(-0.5f));
    mesh.bounds.add(glm::vec3(0.5f));
    return moteur::make_asset(std::move(mesh));
}

// Records what the world collects.
struct Recorder final : moteur::WorldSink {
    struct Draw {
        const moteur::Mesh* mesh;
        glm::mat4 world;
        glm::vec4 color;
        moteur::Aabb bounds;
    };
    std::vector<Draw> draws;
    std::vector<moteur::PointLight> lights;
    std::vector<glm::vec3> billboards;

    void mesh(const moteur::Mesh& mesh, const glm::mat4& world, const moteur::Material& material, const moteur::Aabb& bounds) override {
        draws.push_back({&mesh, world, material.base_color, bounds});
    }
    void light(const moteur::PointLight& light) override { lights.push_back(light); }
    void billboard(const moteur::Texture&, glm::vec3 center, glm::vec2, const moteur::BillboardOptions&) override {
        billboards.push_back(center);
    }
};

moteur::Transform at(glm::vec3 position) {
    moteur::Transform transform;
    transform.position = position;
    return transform;
}

moteur::MeshComponent colored(const moteur::Asset<moteur::Mesh>& mesh, float red) {
    moteur::MeshComponent instance{mesh, {}};
    instance.material.base_color = {red, 0.0f, 0.0f, 1.0f};
    return instance;
}

}  // namespace

TEST_CASE("Transform: matrix is translate * rotate * scale") {
    moteur::Transform transform;
    transform.position = {1.0f, 2.0f, 3.0f};
    transform.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
    transform.scale = {2.0f, 1.0f, 1.0f};
    const glm::vec3 p = transform.matrix() * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    // x scaled to 2, turned a quarter around Y (x -> -z), then moved.
    CHECK(p.x == doctest::Approx(1.0f));
    CHECK(p.y == doctest::Approx(2.0f));
    CHECK(p.z == doctest::Approx(1.0f));
    // Without rotation, the matrix is exactly the translation and the scale.
    moteur::Transform plain;
    plain.position = {0.1f, 0.2f, 0.3f};
    plain.scale = {0.45f, 1.0f, 0.45f};
    CHECK(plain.matrix() == glm::scale(glm::translate(glm::mat4(1.0f), plain.position), plain.scale));
}

TEST_CASE("Transform: interpolation is exact when nothing moved") {
    moteur::Transform a = at({0.1f, 0.2f, 0.3f});
    a.rotation = glm::angleAxis(0.7f, glm::vec3(0.0f, 1.0f, 0.0f));
    for (const float t : {0.0f, 0.13f, 0.5f, 0.999f}) {
        CHECK(moteur::interpolate(a, a, t) == a);
    }
    moteur::Transform b = at({1.1f, 0.2f, 0.3f});
    b.rotation = glm::angleAxis(1.7f, glm::vec3(0.0f, 1.0f, 0.0f));
    const moteur::Transform half = moteur::interpolate(a, b, 0.5f);
    CHECK(half.position.x == doctest::Approx(0.6f));
    CHECK(glm::angle(half.rotation) == doctest::Approx(1.2f));
}

TEST_CASE("World: begin_tick remembers the moving entities, drawing interpolates them") {
    moteur::World world;
    entt::registry& registry = world.registry();
    const auto mesh = make_mesh();
    const entt::entity mover = registry.create();
    registry.emplace<moteur::Transform>(mover, at({0.0f, 0.0f, 0.0f}));
    registry.emplace<moteur::PreviousTransform>(mover, at({0.0f, 0.0f, 0.0f}));
    registry.emplace<moteur::MeshComponent>(mover, mesh, moteur::Material{});

    world.begin_tick();
    registry.get<moteur::Transform>(mover).position.x = 2.0f;  // the tick's move
    Recorder recorder;
    world.collect(recorder, 0.25f);
    REQUIRE(recorder.draws.size() == 1);
    CHECK(recorder.draws[0].world[3].x == doctest::Approx(0.5f));
    CHECK(recorder.draws[0].bounds.min.x == doctest::Approx(0.0f));
    CHECK(recorder.draws[0].bounds.max.x == doctest::Approx(1.0f));
    CHECK(world.world_position(mover, 1.0f).x == 2.0f);

    // Next tick, standing still: the same place whatever alpha.
    world.begin_tick();
    CHECK(world.world_position(mover, 0.0f).x == 2.0f);
    CHECK(world.world_position(mover, 0.6f).x == 2.0f);
    CHECK(world.cached() == 0);  // moving entities are never cached
}

TEST_CASE("World: still entities are cached until patched") {
    moteur::World world;
    entt::registry& registry = world.registry();
    const auto mesh = make_mesh();
    const entt::entity rock = registry.create();
    registry.emplace<moteur::Transform>(rock, at({5.0f, 0.0f, 0.0f}));
    registry.emplace<moteur::MeshComponent>(rock, mesh, moteur::Material{});

    Recorder first;
    world.collect(first, 0.5f);
    CHECK(world.cached() == 1);
    REQUIRE(first.draws.size() == 1);
    CHECK(first.draws[0].world[3].x == 5.0f);

    // Written directly: not seen (documented), since the cache is kept.
    registry.get<moteur::Transform>(rock).position.x = 6.0f;
    Recorder second;
    world.collect(second, 0.5f);
    CHECK(second.draws[0].world[3].x == 5.0f);

    // Through patch: the cache is dropped, and made again at the next collection.
    registry.patch<moteur::Transform>(rock, [](moteur::Transform& transform) { transform.position.x = 7.0f; });
    CHECK(world.cached() == 0);
    Recorder third;
    world.collect(third, 0.5f);
    CHECK(third.draws[0].world[3].x == 7.0f);
    CHECK(third.draws[0].bounds.min.x == doctest::Approx(6.5f));
    CHECK(world.cached() == 1);

    // Becoming a mover drops it too.
    registry.emplace<moteur::PreviousTransform>(rock, registry.get<moteur::Transform>(rock));
    CHECK(world.cached() == 0);
}

TEST_CASE("World: children follow their parent, and go with it") {
    moteur::World world;
    entt::registry& registry = world.registry();
    const auto mesh = make_mesh();
    const entt::entity body = registry.create();
    registry.emplace<moteur::Transform>(body, at({0.0f, 0.0f, 0.0f}));
    registry.emplace<moteur::PreviousTransform>(body, at({0.0f, 0.0f, 0.0f}));
    const entt::entity head = registry.create();
    registry.emplace<moteur::Transform>(head, at({0.0f, 1.2f, 0.0f}));
    registry.emplace<moteur::Parent>(head, body);
    registry.emplace<moteur::MeshComponent>(head, mesh, moteur::Material{});
    const entt::entity hat = registry.create();
    registry.emplace<moteur::Transform>(hat, at({0.0f, 0.5f, 0.0f}));
    registry.emplace<moteur::Parent>(hat, head);
    registry.emplace<moteur::MeshComponent>(hat, mesh, moteur::Material{});

    world.begin_tick();
    registry.get<moteur::Transform>(body).position = {4.0f, 0.0f, 0.0f};
    const glm::vec3 hat_position = world.world_position(hat, 0.5f);
    CHECK(hat_position.x == doctest::Approx(2.0f));
    CHECK(hat_position.y == doctest::Approx(1.7f));

    // A hidden parent hides its children, which keep their place.
    registry.emplace<moteur::Hidden>(head);
    Recorder hidden;
    world.collect(hidden, 1.0f);
    CHECK(hidden.draws.empty());
    CHECK(world.world_position(hat, 1.0f).y == doctest::Approx(1.7f));
    registry.remove<moteur::Hidden>(head);

    // Destroying the body destroys the head and the hat.
    world.destroy(body);
    CHECK_FALSE(registry.valid(head));
    CHECK_FALSE(registry.valid(hat));

    // An orphan (its parent destroyed on its own) is not drawn.
    const entt::entity parent = registry.create();
    registry.emplace<moteur::Transform>(parent);
    const entt::entity orphan = registry.create();
    registry.emplace<moteur::Transform>(orphan);
    registry.emplace<moteur::Parent>(orphan, parent);
    registry.emplace<moteur::MeshComponent>(orphan, mesh, moteur::Material{});
    registry.destroy(parent);
    Recorder orphaned;
    world.collect(orphaned, 1.0f);
    CHECK(orphaned.draws.empty());

    // A cycle is not followed forever.
    const entt::entity a = registry.create();
    const entt::entity b = registry.create();
    registry.emplace<moteur::Transform>(a);
    registry.emplace<moteur::Transform>(b);
    registry.emplace<moteur::Parent>(a, b);
    registry.emplace<moteur::Parent>(b, a);
    registry.emplace<moteur::MeshComponent>(a, mesh, moteur::Material{});
    Recorder cycle;
    world.collect(cycle, 1.0f);
    CHECK(cycle.draws.empty());
}

TEST_CASE("World: collection follows the creation order") {
    moteur::World world;
    entt::registry& registry = world.registry();
    const auto mesh = make_mesh();
    for (int k = 0; k < 5; ++k) {
        const entt::entity entity = registry.create();
        registry.emplace<moteur::Transform>(entity, at({static_cast<float>(k), 0.0f, 0.0f}));
        registry.emplace<moteur::MeshComponent>(entity, colored(mesh, static_cast<float>(k)));
        if (k % 2 == 1) {
            registry.emplace<moteur::PreviousTransform>(entity, at({static_cast<float>(k), 0.0f, 0.0f}));
        }
    }
    for (int run = 0; run < 2; ++run) {  // the second one from the cache, for the still ones
        Recorder recorder;
        world.collect(recorder, 0.5f);
        REQUIRE(recorder.draws.size() == 5);
        for (std::size_t k = 0; k < 5; ++k) {
            CHECK(recorder.draws[k].color.r == static_cast<float>(k));
        }
    }
}

TEST_CASE("World: the lights nearest to the focus, nearest first, ties by creation") {
    moteur::World world;
    entt::registry& registry = world.registry();
    const glm::vec3 positions[] = {{10.0f, 0.0f, 0.0f}, {-3.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 20.0f}};
    for (const glm::vec3& position : positions) {
        const entt::entity entity = registry.create();
        registry.emplace<moteur::Transform>(entity, at(position));
        registry.emplace<moteur::LightSource>(entity, glm::vec3(position.x), 2.0f, 5.0f, true);
    }
    moteur::CollectOptions options;
    options.max_lights = 3;
    Recorder recorder;
    world.collect(recorder, 1.0f, options);
    REQUIRE(recorder.lights.size() == 3);
    CHECK(recorder.lights[0].position.x == 1.0f);
    CHECK(recorder.lights[1].position.x == -3.0f);  // as near as the next one, but created first
    CHECK(recorder.lights[2].position.x == 3.0f);
    CHECK(recorder.lights[0].intensity == 2.0f);
    CHECK(recorder.lights[0].range == 5.0f);
    CHECK(recorder.lights[0].casts_shadows);

    options.max_lights = 0;
    Recorder none;
    world.collect(none, 1.0f, options);
    CHECK(none.lights.empty());
}

TEST_CASE("World: billboards and models without their asset are skipped") {
    moteur::World world;
    entt::registry& registry = world.registry();
    const entt::entity glow = registry.create();
    registry.emplace<moteur::Transform>(glow, at({1.0f, 2.0f, 3.0f}));
    registry.emplace<moteur::Billboard>(glow, moteur::make_asset(moteur::Texture{}), glm::vec2(1.0f), moteur::BillboardOptions{});
    const entt::entity empty = registry.create();
    registry.emplace<moteur::Transform>(empty);
    registry.emplace<moteur::Billboard>(empty);
    registry.emplace<moteur::ModelComponent>(empty);
    registry.emplace<moteur::MeshComponent>(empty);
    Recorder recorder;
    world.collect(recorder, 1.0f);
    REQUIRE(recorder.billboards.size() == 1);
    CHECK(recorder.billboards[0] == glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(recorder.draws.empty());
}

TEST_CASE("World: a still model keeps the boxes of its parts") {
    moteur::World world;
    entt::registry& registry = world.registry();
    moteur::Model model;
    for (int k = 0; k < 2; ++k) {
        moteur::Model::Part part;
        part.mesh.bounds.add(glm::vec3(-0.5f));
        part.mesh.bounds.add(glm::vec3(0.5f));
        part.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, static_cast<float>(k), 0.0f));
        part.material = k == 0 ? 0 : -1;  // the second one takes the default material
        model.parts.push_back(std::move(part));
    }
    moteur::Material red;
    red.base_color = {1.0f, 0.0f, 0.0f, 1.0f};
    model.materials.push_back(red);
    const auto barrel = moteur::make_asset(std::move(model));
    const entt::entity entity = registry.create();
    registry.emplace<moteur::Transform>(entity, at({10.0f, 0.0f, 0.0f}));
    registry.emplace<moteur::ModelComponent>(entity, barrel);

    for (int run = 0; run < 2; ++run) {  // the second one from the cache
        Recorder recorder;
        world.collect(recorder, 1.0f);
        REQUIRE(recorder.draws.size() == 2);
        CHECK(recorder.draws[0].color == glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
        CHECK(recorder.draws[1].color == glm::vec4(1.0f));
        CHECK(recorder.draws[1].world[3] == glm::vec4(10.0f, 1.0f, 0.0f, 1.0f));
        CHECK(recorder.draws[1].bounds.min == glm::vec3(9.5f, 0.5f, -0.5f));
        CHECK(world.cached() == 1);
    }

    // Hidden, then shown again.
    registry.emplace<moteur::Hidden>(entity);
    CHECK(world.cached() == 0);
    Recorder hidden;
    world.collect(hidden, 1.0f);
    CHECK(hidden.draws.empty());
    registry.remove<moteur::Hidden>(entity);
    Recorder shown;
    world.collect(shown, 1.0f);
    CHECK(shown.draws.size() == 2);
}

TEST_CASE("World: entity count") {
    moteur::World world;
    CHECK(world.entity_count() == 0);
    const entt::entity a = world.registry().create();
    static_cast<void>(world.registry().create());
    CHECK(world.entity_count() == 2);
    world.destroy(a);
    CHECK(world.entity_count() == 1);
}
