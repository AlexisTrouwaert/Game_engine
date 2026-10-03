#include "moteur/world.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <functional>

#include "moteur/animator.hpp"
#include "moteur/fixed_timestep.hpp"

namespace moteur {

namespace {

// The world matrix and box of an entity that does not move (see World), computed the first time
// it is collected.
struct WorldCache {
    glm::mat4 world{1.0f};
    Aabb bounds;              // of its MeshComponent, when it has one
    std::vector<Aabb> parts;  // of the parts of its ModelComponent, when it has one
};

// Parent chains longer than this are taken for a cycle.
constexpr int kMaxDepth = 64;

void drop_cache(entt::registry& registry, entt::entity entity) {
    registry.remove<WorldCache>(entity);
}

}  // namespace

glm::mat4 Transform::matrix() const {
    return glm::scale(glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation), scale);
}

Transform interpolate(const Transform& previous, const Transform& current, float t) {
    Transform result;
    result.position = interpolate(previous.position, current.position, t);
    result.scale = interpolate(previous.scale, current.scale, t);
    result.rotation = previous.rotation == current.rotation ? current.rotation : glm::slerp(previous.rotation, current.rotation, t);
    return result;
}

std::size_t ModelComponent::hide(std::string_view node) {
    if (!model) {
        return 0;
    }
    const std::vector<std::uint32_t> parts = model->parts_of(node);
    for (const std::uint32_t part : parts) {
        const auto at = std::lower_bound(hidden_parts.begin(), hidden_parts.end(), part);
        if (at == hidden_parts.end() || *at != part) {
            hidden_parts.insert(at, part);
        }
    }
    return parts.size();
}

bool ModelComponent::hidden(std::size_t part) const {
    return std::binary_search(hidden_parts.begin(), hidden_parts.end(), static_cast<std::uint32_t>(part));
}

World::World() {
    // What the cached matrix and box of a still entity depend on. EnTT's signals are kept to this.
    registry_.on_update<Transform>().connect<&drop_cache>();
    registry_.on_destroy<Transform>().connect<&drop_cache>();
    registry_.on_construct<PreviousTransform>().connect<&drop_cache>();
    registry_.on_destroy<PreviousTransform>().connect<&drop_cache>();
    registry_.on_construct<Parent>().connect<&drop_cache>();
    registry_.on_update<Parent>().connect<&drop_cache>();
    registry_.on_destroy<Parent>().connect<&drop_cache>();
    registry_.on_update<MeshComponent>().connect<&drop_cache>();
    // A cached entity is drawn: hiding it drops its cache (one lookup per still entity and frame).
    registry_.on_construct<Hidden>().connect<&drop_cache>();
    // An entity on a joint moves with the pose: never cached (it has a Parent too, but be sure).
    registry_.on_construct<BoneAttachment>().connect<&drop_cache>();
}

void World::begin_tick() {
    for (auto [entity, transform, previous] : registry_.view<const Transform, PreviousTransform>().each()) {
        previous.value = transform;
    }
}

namespace {

// The last parent resolved during a collection: siblings (a body and a head) share it.
struct ParentMemo {
    entt::entity entity = entt::null;
    bool placed = false;
    glm::mat4 world{1.0f};
};

// The model-space matrices of an animated entity's pose, for entities on its joints.
using PoseLookup = std::function<const std::vector<glm::mat4>*(entt::entity owner)>;

// A joint's matrix without its scale: its place and rotation only.
glm::mat4 without_scale(const glm::mat4& m) {
    glm::mat4 result = m;
    for (int c = 0; c < 3; ++c) {
        const float length = glm::length(glm::vec3(m[c]));
        if (length > 0.0f) {
            result[c] = m[c] / length;
        }
    }
    return result;
}

// The world matrix of `entity` at `alpha`; false when it has no Transform, hangs from a parent that
// is gone (or from a cycle), or, for `drawn`, is hidden (itself or a parent). `poses` gives the
// poses of the parents that entities hang on by a joint (null: the last drawn, or the rest pose).
bool resolve(const entt::registry& registry, entt::entity entity, float alpha, glm::mat4& world, int depth, bool drawn,
             ParentMemo* memo = nullptr, const PoseLookup* poses = nullptr) {
    const Transform* transform = registry.try_get<Transform>(entity);
    if (transform == nullptr || depth > kMaxDepth || (drawn && registry.all_of<Hidden>(entity))) {
        return false;
    }
    const PreviousTransform* previous = registry.try_get<PreviousTransform>(entity);
    const glm::mat4 local = previous != nullptr ? interpolate(previous->value, *transform, alpha).matrix() : transform->matrix();
    if (const Parent* parent = registry.try_get<Parent>(entity)) {
        // On a joint: between the parent and the entity's own offset.
        glm::mat4 on_joint(1.0f);
        if (const BoneAttachment* bone = registry.try_get<BoneAttachment>(entity)) {
            const std::vector<glm::mat4>* pose = nullptr;
            if (poses != nullptr) {
                pose = (*poses)(parent->entity);
            } else if (const AnimationPose* last = registry.try_get<AnimationPose>(parent->entity)) {
                pose = &last->sampler.model();
            }
            if (!attach_point_matrix(registry, parent->entity, bone->point, pose, on_joint)) {
                on_joint = glm::mat4(1.0f);
            }
        }
        const glm::mat4 attached = on_joint * local;
        if (memo != nullptr && memo->entity == parent->entity) {
            world = memo->world * attached;
            return memo->placed;
        }
        glm::mat4 parent_world(1.0f);
        const bool placed =
            registry.valid(parent->entity) && resolve(registry, parent->entity, alpha, parent_world, depth + 1, drawn, nullptr, poses);
        if (memo != nullptr && depth == 0) {
            *memo = {parent->entity, placed, parent_world};
        }
        if (!placed) {
            return false;
        }
        world = parent_world * attached;
    } else {
        world = local;
    }
    return true;
}

}  // namespace

bool attach_point_matrix(const entt::registry& registry, entt::entity owner, const std::string& point,
                         const std::vector<glm::mat4>* pose, glm::mat4& matrix) {
    const Animator* animator = registry.try_get<Animator>(owner);
    if (animator == nullptr || !animator->skeleton) {
        return false;
    }
    const AttachPoint* named = animator->set ? animator->set->attach_point(point) : nullptr;
    const SkeletonData& skeleton = animator->skeleton->data();
    const int joint = skeleton.find(named != nullptr ? named->joint : point);
    if (joint < 0) {
        return false;
    }
    const auto j = static_cast<std::size_t>(joint);
    glm::mat4 bone;
    if (pose != nullptr && j < pose->size()) {
        bone = (*pose)[j];
    } else {
        bone = skeleton.rest_model_matrices()[j];  // allocates: tools only
    }
    matrix = without_scale(bone) * (named != nullptr ? named->offset() : glm::mat4(1.0f));
    return true;
}

const AnimationPose& World::sample_pose(entt::entity entity, const Animator& animator, float alpha) {
    AnimationPose& pose = registry_.get_or_emplace<AnimationPose>(entity);
    if (pose.collection != collection_) {
        const auto start = std::chrono::steady_clock::now();
        const ModelComponent* model = registry_.try_get<ModelComponent>(entity);
        if (bind_pose_ && model != nullptr && model->model) {
            pose.sampler.bind(*animator.skeleton, model->model->skins);
        } else {
            animator.pose(alpha, pose_request_);
            pose.sampler.blend(*animator.skeleton, pose_request_.inputs);
        }
        pose.collection = collection_;
        ++animation_stats_.poses;
        animation_stats_.sample_ms +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
    return pose;
}

glm::mat4 World::world_matrix(entt::entity entity, float alpha) const {
    glm::mat4 world(1.0f);
    if (registry_.valid(entity)) {
        resolve(registry_, entity, alpha, world, 0, false);
    }
    return world;
}

glm::vec3 World::world_position(entt::entity entity, float alpha) const {
    return glm::vec3(world_matrix(entity, alpha)[3]);
}

void World::destroy(entt::entity entity) {
    if (!registry_.valid(entity)) {
        return;
    }
    std::vector<entt::entity> children;
    for (auto [child, parent] : registry_.view<const Parent>().each()) {
        if (parent.entity == entity) {
            children.push_back(child);
        }
    }
    for (const entt::entity child : children) {
        destroy(child);
    }
    registry_.destroy(entity);
}

void World::collect(WorldSink& sink, float alpha, const CollectOptions& options) {
    ++collection_;
    bind_pose_ = options.bind_pose;
    animation_stats_ = {};
    // Entities on joints ask for their parent's pose of this frame, whether the parent is in view
    // or not (a long spear can be seen when its bearer is not).
    const PoseLookup poses = [this, alpha](entt::entity owner) -> const std::vector<glm::mat4>* {
        const Animator* animator = registry_.try_get<Animator>(owner);
        if (animator == nullptr || !animator->skeleton) {
            return nullptr;
        }
        return &sample_pose(owner, *animator, alpha).sampler.model();
    };
    // Where a drawn entity is (false: hidden, or orphaned). Nothing moves during a collection, so
    // the parent last resolved is remembered for the next sibling.
    ParentMemo memo;
    const auto placed = [&](entt::entity entity, glm::mat4& world) {
        return resolve(registry_, entity, alpha, world, 0, true, &memo, &poses);
    };
    // A still root entity (visible): its matrix (and mesh box) once, then from the cache. Having a
    // cache means being still and visible, since becoming anything else drops it.
    const auto still = [this](entt::entity entity) {
        return !registry_.any_of<PreviousTransform, Parent, Hidden>(entity) && registry_.all_of<Transform>(entity);
    };

    // Meshes, in the order the entities were created (reach(): EnTT's own order is the reverse).
    for (auto [entity, instance] : registry_.storage<MeshComponent>().reach()) {
        if (!instance.mesh) {
            continue;
        }
        const Mesh& mesh = *instance.mesh;
        if (const WorldCache* cache = registry_.try_get<WorldCache>(entity)) {
            sink.mesh(mesh, cache->world, instance.material, cache->bounds);
            continue;
        }
        if (still(entity)) {
            const glm::mat4 world = registry_.get<Transform>(entity).matrix();
            const WorldCache& cache = registry_.emplace<WorldCache>(entity, world, transform_box(mesh.bounds, world));
            sink.mesh(mesh, cache.world, instance.material, cache.bounds);
            continue;
        }
        glm::mat4 world;
        if (placed(entity, world)) {
            sink.mesh(mesh, world, instance.material, transform_box(mesh.bounds, world));
        }
    }

    // Models: each part with its own place and material.
    const Material fallback;
    for (auto [entity, instance] : registry_.storage<ModelComponent>().reach()) {
        if (!instance.model) {
            continue;
        }
        const Model& model = *instance.model;
        const auto material_of = [&](const Model::Part& part) -> const Material& {
            return part.material >= 0 ? model.materials[static_cast<std::size_t>(part.material)] : fallback;
        };

        // An animated model: its pose between the last two ticks, sampled only when in view.
        if (const Animator* animator = registry_.try_get<Animator>(entity); animator != nullptr && animator->skeleton) {
            ++animation_stats_.animated;
            glm::mat4 world;
            if (!placed(entity, world)) {
                continue;
            }
            // An animation reaches out of the rest pose: its box, twice as big around its centre
            // (boxes per clip come with skinning).
            const Aabb rest = transform_box(model.bounds, world);
            Aabb reach;
            if (!rest.empty()) {
                reach.add(rest.center() - rest.size());
                reach.add(rest.center() + rest.size());
            }
            if (!rest.empty() && !options.view.intersects(reach)) {
                continue;
            }
            const AnimationPose& pose = sample_pose(entity, *animator, alpha);
            const std::vector<glm::mat4>& joints = pose.sampler.model();
            // Each skin's palette is given once, whatever the number of parts that use it.
            skin_palettes_.assign(model.skins.size(), MeshRenderer::Palette{-2, 0});  // -2: not yet
            skin_boxes_.assign(model.skins.size(), Aabb{});
            for (std::size_t k = 0; k < model.parts.size(); ++k) {
                if (instance.hidden(k)) {
                    continue;
                }
                const Model::Part& part = model.parts[k];
                const bool skinned = part.skin >= 0 && static_cast<std::size_t>(part.skin) < model.skins.size() &&
                                     part.mesh.skinned() && model.joint_count == joints.size();
                if (skinned) {
                    const auto s = static_cast<std::size_t>(part.skin);
                    const SkinData& skin = model.skins[s];
                    if (skin_palettes_[s].first == -2) {
                        const auto start = std::chrono::steady_clock::now();
                        pose.sampler.palette(skin, palette_scratch_);
                        skin_palettes_[s] = sink.palette(palette_scratch_);
                        ++animation_stats_.palettes;
                        animation_stats_.matrices += palette_scratch_.size();
                        animation_stats_.palette_ms +=
                            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                        for (const int joint : skin.joints) {
                            skin_boxes_[s].add(glm::vec3(joints[static_cast<std::size_t>(joint)][3]));
                        }
                    }
                    // In this pose: the box of the skin's joints, grown by how far the vertices reach from them.
                    Aabb reach_box = skin_boxes_[s];
                    reach_box.min -= glm::vec3(part.skin_radius);
                    reach_box.max += glm::vec3(part.skin_radius);
                    sink.skinned_mesh(part.mesh, world, material_of(part), transform_box(reach_box, world), skin_palettes_[s]);
                    continue;
                }
                // Rigid parts follow their joint.
                const bool on_joint = part.joint >= 0 && static_cast<std::size_t>(part.joint) < joints.size();
                const glm::mat4 part_world = world * (on_joint ? joints[static_cast<std::size_t>(part.joint)] * part.joint_offset
                                                               : part.transform);
                sink.mesh(part.mesh, part_world, material_of(part), transform_box(part.mesh.bounds, part_world));
            }
            continue;
        }

        const WorldCache* cache = registry_.try_get<WorldCache>(entity);
        if (cache == nullptr && still(entity)) {
            WorldCache fresh{registry_.get<Transform>(entity).matrix(), Aabb{}, {}};
            for (const Model::Part& part : model.parts) {
                fresh.parts.push_back(transform_box(part.mesh.bounds, fresh.world * part.transform));
            }
            cache = &registry_.emplace<WorldCache>(entity, std::move(fresh));
        }
        // A model reloaded with another number of parts: its boxes are computed again below.
        if (cache != nullptr && cache->parts.size() == model.parts.size()) {
            for (std::size_t k = 0; k < model.parts.size(); ++k) {
                if (instance.hidden(k)) {
                    continue;
                }
                const Model::Part& part = model.parts[k];
                sink.mesh(part.mesh, cache->world * part.transform, material_of(part), cache->parts[k]);
            }
            continue;
        }
        glm::mat4 world;
        if (cache != nullptr) {
            world = cache->world;
        } else if (!placed(entity, world)) {
            continue;
        }
        for (std::size_t k = 0; k < model.parts.size(); ++k) {
            if (instance.hidden(k)) {
                continue;
            }
            const Model::Part& part = model.parts[k];
            const glm::mat4 part_world = world * part.transform;
            sink.mesh(part.mesh, part_world, material_of(part), transform_box(part.mesh.bounds, part_world));
        }
    }

    // Lights: the nearest to the focus.
    lights_.clear();
    for (auto [entity, light] : registry_.storage<LightSource>().reach()) {
        glm::mat4 world;
        if (placed(entity, world)) {
            const glm::vec3 position(world[3]);
            const glm::vec3 d = position - options.light_focus;
            lights_.push_back({glm::dot(d, d), entity, position});
        }
    }
    const auto count = std::min(lights_.size(), static_cast<std::size_t>(std::max(options.max_lights, 0)));
    // Ties go to the smaller entity index: the entity created first, as long as none was destroyed.
    std::partial_sort(lights_.begin(), lights_.begin() + static_cast<std::ptrdiff_t>(count), lights_.end(),
                      [](const NearLight& a, const NearLight& b) {
                          return a.distance2 < b.distance2 ||
                                 (a.distance2 == b.distance2 && entt::to_entity(a.entity) < entt::to_entity(b.entity));
                      });
    for (std::size_t k = 0; k < count; ++k) {
        const LightSource& light = registry_.get<LightSource>(lights_[k].entity);
        sink.light({lights_[k].position, light.color, light.intensity, light.range, light.casts_shadows});
    }

    // Billboards.
    for (auto [entity, billboard] : registry_.storage<Billboard>().reach()) {
        glm::mat4 world;
        if (billboard.texture && placed(entity, world)) {
            sink.billboard(*billboard.texture, glm::vec3(world[3]), billboard.size, billboard.options);
        }
    }
}

namespace {

class RendererSink final : public WorldSink {
public:
    explicit RendererSink(Renderer& renderer) : meshes_(renderer.meshes()), billboards_(renderer.billboards()) {}

    void mesh(const Mesh& mesh, const glm::mat4& world, const Material& material, const Aabb& bounds) override {
        meshes_.draw(mesh, world, material, bounds);
    }
    MeshRenderer::Palette palette(const std::vector<glm::mat4>& matrices) override { return meshes_.add_palette(matrices); }
    void skinned_mesh(const Mesh& mesh, const glm::mat4& world, const Material& material, const Aabb& bounds,
                      MeshRenderer::Palette palette) override {
        meshes_.draw(mesh, world, material, bounds, palette);
    }
    void light(const PointLight& light) override { meshes_.add_light(light); }
    void billboard(const Texture& texture, glm::vec3 center, glm::vec2 size, const BillboardOptions& options) override {
        billboards_.draw(texture, center, size, options);
    }

private:
    MeshRenderer& meshes_;
    BillboardRenderer& billboards_;
};

}  // namespace

void World::submit(Renderer& renderer, float alpha, const CollectOptions& options) {
    RendererSink sink(renderer);
    collect(sink, alpha, options);
}

std::size_t World::entity_count() const {
    const auto* entities = registry_.storage<entt::entity>();
    return entities != nullptr ? entities->free_list() : 0;  // the living ones come first
}

std::size_t World::cached() const {
    const auto* storage = registry_.storage<WorldCache>();
    return storage != nullptr ? storage->size() : 0;
}

}  // namespace moteur
