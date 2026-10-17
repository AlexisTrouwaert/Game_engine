#pragma once

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "moteur/aabb.hpp"
#include "moteur/animator.hpp"
#include "moteur/asset_cache.hpp"
#include "moteur/billboard_renderer.hpp"
#include "moteur/camera3d.hpp"
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

/// Where an entity is: in the world, or relative to its Parent when it has one. Scale is applied
/// first, then rotation, then translation. Y is up; the ground is the XZ plane.
struct Transform {
    glm::vec3 position{0.0f};                    ///< metres
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  ///< identity (glm's order: w, x, y, z); keep it normalised
    glm::vec3 scale{1.0f};                       ///< per axis, 1 = the asset's size

    /// The local matrix: translate(position) * rotation * scale(scale).
    glm::mat4 matrix() const;
    /// Exact comparison of the three members (no tolerance).
    bool operator==(const Transform&) const = default;
};

/// Between two ticks: position and scale linearly, rotation by slerp. Exactly `current` when
/// nothing changed, whatever t (see moteur::interpolate), so a still entity never shifts.
Transform interpolate(const Transform& previous, const Transform& current, float t);

/// Marks an entity that moves: its Transform at the previous tick, copied by World::begin_tick(),
/// so that drawing can interpolate. An entity without it is drawn where it is, and its world
/// matrix and box are computed once (see World).
struct PreviousTransform {
    Transform value;  ///< the Transform at the previous tick (set it equal to the Transform when teleporting)
};

/// Attaches an entity to another: its Transform is then relative to the parent's (a head on a body,
/// a ring under a character; a weapon in a hand with milestone 5). Chains are allowed, cycles are
/// not. A child whose parent no longer exists is not drawn. Children follow the parent's
/// interpolation and need no PreviousTransform of their own.
struct Parent {
    entt::entity entity = entt::null;  ///< the parent entity (of the same registry)
};

/// With a Parent that is animated (an Animator), hangs the entity on a joint of the parent's pose
/// (milestone 5, part 8): a sword in a hand, a torch, an effect on the head. `point` is an attach
/// point of the parent's AnimationSet ("right_hand": a joint and an offset), or else a joint name.
/// The entity's Transform is then relative to that point. Computed when drawing, from the pose of
/// the frame (between two ticks): the object never lags behind the hand; the game does not read it.
/// The joint's own scale is not passed on (only its place and rotation); the parent's entity scale
/// is, as with any Parent. An unknown point, or a parent without Animator: the parent's origin.
struct BoneAttachment {
    std::string point;  ///< an attach point of the parent's AnimationSet, or a joint name
};

/// A name for debugging (the inspector, messages). Optional.
struct Name {
    std::string value;  ///< any text; not required to be unique
};

/// Not drawn (meshes, models, billboards) and gives no light, but otherwise untouched.
struct Hidden {};

/// A mesh drawn at the entity's place.
struct MeshComponent {
    Asset<Mesh> mesh;   ///< the mesh; an empty handle draws nothing
    Material material;  ///< how it is drawn (its textures must stay alive: see the note at the top of world.hpp)
};

/// Every part of a model, at the entity's place, but those in `hidden_parts` (indices in
/// Model::parts, sorted: see Model::parts_of(), to hide the shields a KayKit knight carries in the
/// file). Replace or patch the component to change them. With an Animator, the model is drawn in
/// its pose.
struct ModelComponent {
    Asset<Model> model;                       ///< the model; an empty handle draws nothing
    std::vector<std::uint32_t> hidden_parts;  ///< indices in Model::parts, sorted, without duplicates

    /// Hides every part made from the node `node`; returns how many parts that hid (0 without a
    /// model, or if no part comes from that node).
    std::size_t hide(std::string_view node);
    /// Whether the part at index `part` of Model::parts is in #hidden_parts (a binary search).
    bool hidden(std::size_t part) const;
};

/// A point light at the entity's position (see PointLight).
struct LightSource {
    glm::vec3 color{1.0f};   ///< linear
    float intensity = 1.0f;  ///< radiance at 1 m
    float range = 10.0f;     ///< metres
    bool casts_shadows = false;  ///< may cast shadows (see PointLight::casts_shadows)
    float shadow_near = 0.0f;    ///< metres; nearer casters cast no shadow (see PointLight::shadow_near)
};

/// A camera-facing sprite at the entity's position (see BillboardRenderer).
struct Billboard {
    Asset<Texture> texture;    ///< the sprite; an empty handle draws nothing
    glm::vec2 size{1.0f};  ///< metres
    BillboardOptions options;  ///< colour, additive blending, facing, texture rectangle, rotation, softness
};

/// What World::collect() produces for a frame. The real one feeds the renderer; tests record.
/// The references passed are valid only during the call.
class WorldSink {
public:
    virtual ~WorldSink() = default;
    /// `bounds`: the mesh's box moved by `world`.
    virtual void mesh(const Mesh& mesh, const glm::mat4& world, const Material& material, const Aabb& bounds) = 0;
    /// Skinning (milestone 5): the matrices of a pose, for the skinned meshes that follow; returns
    /// what to hand them. By default, nothing is kept.
    virtual MeshRenderer::Palette palette(const std::vector<glm::mat4>& matrices) {
        static_cast<void>(matrices);
        return {};
    }
    /// A skinned mesh in its pose: its vertices follow `palette`, then `world`; `bounds` is where it
    /// is in this pose. By default, given as a plain mesh.
    virtual void skinned_mesh(const Mesh& mesh, const glm::mat4& world, const Material& material, const Aabb& bounds,
                              MeshRenderer::Palette palette) {
        static_cast<void>(palette);
        this->mesh(mesh, world, material, bounds);
    }
    /// A point light of the frame, its position in the world; at most CollectOptions::max_lights per
    /// collect(), nearest to CollectOptions::light_focus first.
    virtual void light(const PointLight& light) = 0;
    /// A billboard: `center` in the world, `size` in metres (see BillboardRenderer::draw()).
    virtual void billboard(const Texture& texture, glm::vec3 center, glm::vec2 size, const BillboardOptions& options) = 0;
};

/// What the last collect() spent on animation (milestone 5, part 10): poses sampled and blended,
/// palettes computed and handed to the sink, and the time of each (CPU, milliseconds).
struct AnimationStats {
    int animated = 0;        ///< animated models met (in view or not)
    int poses = 0;           ///< poses sampled (the others were out of view)
    int palettes = 0;          ///< skinning palettes handed to the sink (one per skin of each posed model)
    std::size_t matrices = 0;  ///< in those palettes
    double sample_ms = 0.0;    ///< sampling, blending, local to model space
    double palette_ms = 0.0;   ///< palettes (model x inverse bind) and handing them over
};

/// How World::collect() and World::submit() gather a frame.
struct CollectOptions {
    /// Only the lights nearest to this point are given (the renderer takes kMaxPointLights at most).
    glm::vec3 light_focus{0.0f};
    int max_lights = MeshRenderer::kMaxPointLights;  ///< how many lights at most; 0 or less: none
    /// What the camera sees: animated models outside are not sampled nor given (the renderer culls
    /// the rest). The default contains everything.
    Frustum view;
    /// Tools: animated models in their bind pose (PoseSampler::bind) rather than their clips.
    bool bind_pose = false;
};

/// The registry of one scene, and the engine's systems over it. One World per scene: closing the
/// scene destroys it, and everything in it goes at once. Not copyable; the main thread only (EnTT
/// registries are not thread-safe).
///
/// A fixed step (see Application) runs, in this order:
/// @code{.cpp}
/// world.begin_tick();   // 1. PreviousTransform <- Transform, for everything that moves
/// // 2. the game's systems: inputs, moves, AI..., in an order the game writes once,
/// //    with advance_animators(registry) after what picks the clips
/// @endcode
/// and every frame, the renderer is fed with the state between the last two ticks:
/// @code{.cpp}
/// world.submit(renderer, alpha, options);
/// @endcode
///
/// Entities without PreviousTransform (decor) keep their world matrix and box from frame to frame.
/// Changing their Transform (or their Parent, or their MeshComponent or ModelComponent) must go
/// through `registry.patch<>()` or `replace<>()` so that this cache is dropped (adding or removing
/// these components drops it too); a component written directly through `get<>()` is only seen for
/// entities that move or are attached (which are never cached). A model or mesh reloaded in place
/// by an AssetCache (hot reload) is seen too: the next collect() drops every kept box (see
/// AssetCache::reload_generation()).
class World {
public:
    /// An empty world, its registry connected to the cache's signals.
    World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    /// The EnTT registry: the game creates its entities and adds its own components there. Valid as
    /// long as the World. Destroy entities with destroy() rather than `registry().destroy()`, which
    /// leaves their children orphaned (no longer drawn).
    ///@{
    entt::registry& registry() { return registry_; }
    const entt::registry& registry() const { return registry_; }
    ///@}

    /// First system of a fixed step: remembers where moving entities are (PreviousTransform =
    /// Transform for every entity having both). O(moving entities).
    void begin_tick();

    /// The world matrix of an entity between the previous tick (alpha 0) and the current one
    /// (alpha 1), through its parents. Identity for an entity without Transform, an invalid entity,
    /// or one whose parent chain is broken (a parent destroyed, a cycle); Hidden is ignored. For an
    /// entity on a joint (BoneAttachment), the pose last drawn (the rest pose if its parent never
    /// was): for tools, not for the game's logic.
    glm::mat4 world_matrix(entt::entity entity, float alpha) const;
    /// Its world position (the translation of world_matrix()), in metres.
    glm::vec3 world_position(entt::entity entity, float alpha) const;

    /// Destroys an entity and, first, every entity attached to it (Parent), recursively. Nothing
    /// happens for an invalid entity. O(entities with a Parent) per level.
    void destroy(entt::entity entity);

    /// Everything drawable (meshes, models, billboards) and the lights, at `alpha` (the fraction of
    /// the fixed step elapsed, in [0, 1]), handed to `sink`: meshes first, then models, lights and
    /// billboards, each kind in the order its component was added (that of creation, usually).
    /// Hidden entities, entities without Transform and children of a missing parent are skipped.
    /// Lights: the options.max_lights nearest to options.light_focus, nearest first (ties: the
    /// smaller entity index). Samples the poses of animated models in view (and of those carrying
    /// something on a joint), and fills the caches: not const. Updates animation_stats().
    void collect(WorldSink& sink, float alpha, const CollectOptions& options = {});
    /// The same, into the renderer's meshes and billboards (Renderer::meshes(), Renderer::billboards()),
    /// for the frame being recorded: once per frame, between Renderer::begin_frame() and
    /// Renderer::end_frame().
    void submit(Renderer& renderer, float alpha, const CollectOptions& options = {});

    /// Living entities (of every kind, the game's included).
    std::size_t entity_count() const;
    /// Entities whose world matrix and box are currently kept (see above), for statistics and tests.
    std::size_t cached() const;
    /// The animation work of the last collect().
    const AnimationStats& animation_stats() const { return animation_stats_; }

private:
    entt::registry registry_;
    struct NearLight {
        float distance2;
        entt::entity entity;
        glm::vec3 position;
    };
    std::vector<NearLight> lights_;  ///< reused by collect()
    /// Reused by collect() for skinned models: per skin of a model, its palette and joints' box.
    std::vector<glm::mat4> palette_scratch_;
    std::vector<MeshRenderer::Palette> skin_palettes_;
    std::vector<Aabb> skin_boxes_;
    PoseRequest pose_request_;  ///< reused: an animated model's clips and weights
    std::uint64_t collection_ = 0;  ///< counts collect(): a pose is sampled once per collection
    bool bind_pose_ = false;        ///< CollectOptions::bind_pose of the current collection
    std::uint64_t reloads_seen_ = 0;  ///< model and mesh reloads the kept boxes were computed after
    AnimationStats animation_stats_;

    /// The pose of an animated entity for this collection: sampled the first time it is asked for
    /// (by its model, or by an entity on one of its joints).
    const AnimationPose& sample_pose(entt::entity entity, const Animator& animator, float alpha);
};

/// Where `point` is on `owner` (see BoneAttachment), in its model space, without the joint's scale:
/// the joint's matrix in `pose` (model-space joint matrices, as PoseSampler::model(); or the rest
/// pose if null or too short, which allocates: tools only) times the point's offset, written to
/// `matrix`. False, `matrix` untouched, if the owner has no Animator with a skeleton, or no such
/// point or joint.
bool attach_point_matrix(const entt::registry& registry, entt::entity owner, const std::string& point,
                         const std::vector<glm::mat4>* pose, glm::mat4& matrix);

}  // namespace moteur
