#pragma once

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <string>
#include <vector>

#include "moteur/aabb.hpp"
#include "moteur/asset_cache.hpp"
#include "moteur/billboard_renderer.hpp"
#include "moteur/material.hpp"
#include "moteur/mesh.hpp"
#include "moteur/mesh_renderer.hpp"
#include "moteur/model.hpp"
#include "moteur/renderer.hpp"

namespace moteur {

// The world of a scene as entities (milestone 4, part 5): what has a place in the map (characters,
// objects, lights, effects) is an entity of an EnTT registry, carrying components (plain data),
// updated by systems (functions). The engine's own systems (renderer, audio, assets, inputs) stay
// classes, and so do the game's menus.
//
// The components below are the engine's: they know nothing of the game. The game adds its own to
// the same registry (world.registry().emplace<Health>(entity, ...)) and writes its systems as
// functions over views. Components hold values and handles (assets, other entities), never
// pointers: that keeps them easy to inspect and, later, to save. (Material is the exception for
// now: its textures are pointers, into an asset the entity must also hold, such as a model.)

// Where an entity is: in the world, or relative to its Parent when it has one. Scale is applied
// first, then rotation, then translation.
struct Transform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // identity (glm's order: w, x, y, z)
    glm::vec3 scale{1.0f};

    glm::mat4 matrix() const;
    bool operator==(const Transform&) const = default;
};

// Between two ticks: position and scale linearly, rotation by slerp. Exactly `current` when
// nothing changed, whatever t (see moteur::interpolate), so a still entity never shifts.
Transform interpolate(const Transform& previous, const Transform& current, float t);

// Marks an entity that moves: its Transform at the previous tick, copied by World::begin_tick(),
// so that drawing can interpolate. An entity without it is drawn where it is, and its world
// matrix and box are computed once (see World).
struct PreviousTransform {
    Transform value;
};

// Attaches an entity to another: its Transform is then relative to the parent's (a head on a body,
// a ring under a character; a weapon in a hand with milestone 5). Chains are allowed, cycles are
// not. A child whose parent no longer exists is not drawn. Children follow the parent's
// interpolation and need no PreviousTransform of their own.
struct Parent {
    entt::entity entity = entt::null;
};

// A name for debugging (the inspector, messages). Optional.
struct Name {
    std::string value;
};

// Not drawn (meshes, models, billboards) and gives no light, but otherwise untouched.
struct Hidden {};

// A mesh drawn at the entity's place.
struct MeshComponent {
    Asset<Mesh> mesh;
    Material material;
};

// Every part of a model, at the entity's place.
struct ModelComponent {
    Asset<Model> model;
};

// A point light at the entity's position (see PointLight).
struct LightSource {
    glm::vec3 color{1.0f};   // linear
    float intensity = 1.0f;  // radiance at 1 m
    float range = 10.0f;     // metres
    bool casts_shadows = false;
};

// A camera-facing sprite at the entity's position (see BillboardRenderer).
struct Billboard {
    Asset<Texture> texture;
    glm::vec2 size{1.0f};  // metres
    BillboardOptions options;
};

// What World::collect() produces for a frame. The real one feeds the renderer; tests record.
class WorldSink {
public:
    virtual ~WorldSink() = default;
    // `bounds`: the mesh's box moved by `world`.
    virtual void mesh(const Mesh& mesh, const glm::mat4& world, const Material& material, const Aabb& bounds) = 0;
    virtual void light(const PointLight& light) = 0;
    virtual void billboard(const Texture& texture, glm::vec3 center, glm::vec2 size, const BillboardOptions& options) = 0;
};

struct CollectOptions {
    // Only the lights nearest to this point are given (the renderer takes kMaxPointLights at most).
    glm::vec3 light_focus{0.0f};
    int max_lights = MeshRenderer::kMaxPointLights;
};

// The registry of one scene, and the engine's systems over it. One World per scene: closing the
// scene destroys it, and everything in it goes at once.
//
// A fixed step (see Application) runs, in this order:
//   1. world.begin_tick();          // PreviousTransform <- Transform, for everything that moves
//   2. the game's systems            // inputs, moves, AI..., in an order the game writes once
// and every frame, the renderer is fed with the state between the last two ticks:
//   world.submit(renderer, alpha, options);
//
// Entities without PreviousTransform (decor) keep their world matrix and box from frame to frame.
// Changing their Transform (or their Parent, or their mesh) must go through registry.patch<>() or
// replace<>() so that this cache is dropped; a Transform written directly through get<>() is only
// seen for entities that move or are attached (which are never cached).
class World {
public:
    World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    entt::registry& registry() { return registry_; }
    const entt::registry& registry() const { return registry_; }

    // First system of a fixed step: remembers where moving entities are.
    void begin_tick();

    // The world matrix of an entity between the previous tick (alpha 0) and the current one
    // (alpha 1), through its parents. Identity for an entity without Transform.
    glm::mat4 world_matrix(entt::entity entity, float alpha) const;
    // Its world position (the translation of world_matrix()).
    glm::vec3 world_position(entt::entity entity, float alpha) const;

    // Destroys an entity and, first, every entity attached to it (Parent), recursively.
    void destroy(entt::entity entity);

    // Everything drawable (meshes, models, billboards) and the lights, at `alpha`, in the order their
    // component was added (that of creation, usually). Lights: the options.max_lights nearest to
    // options.light_focus, nearest first (ties: the smaller entity index).
    void collect(WorldSink& sink, float alpha, const CollectOptions& options = {});
    // The same, into the renderer's meshes and billboards.
    void submit(Renderer& renderer, float alpha, const CollectOptions& options = {});

    // Living entities.
    std::size_t entity_count() const;
    // Entities whose world matrix and box are currently kept (see above), for statistics and tests.
    std::size_t cached() const;

private:
    entt::registry registry_;
    struct NearLight {
        float distance2;
        entt::entity entity;
        glm::vec3 position;
    };
    std::vector<NearLight> lights_;  // reused by collect()
};

}  // namespace moteur
