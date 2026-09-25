#include "moteur/world.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

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

// The world matrix of `entity` at `alpha`; false when it has no Transform, hangs from a parent that
// is gone (or from a cycle), or, for `drawn`, is hidden (itself or a parent).
bool resolve(const entt::registry& registry, entt::entity entity, float alpha, glm::mat4& world, int depth, bool drawn,
             ParentMemo* memo = nullptr) {
    const Transform* transform = registry.try_get<Transform>(entity);
    if (transform == nullptr || depth > kMaxDepth || (drawn && registry.all_of<Hidden>(entity))) {
        return false;
    }
    const PreviousTransform* previous = registry.try_get<PreviousTransform>(entity);
    const glm::mat4 local = previous != nullptr ? interpolate(previous->value, *transform, alpha).matrix() : transform->matrix();
    if (const Parent* parent = registry.try_get<Parent>(entity)) {
        if (memo != nullptr && memo->entity == parent->entity) {
            world = memo->world * local;
            return memo->placed;
        }
        glm::mat4 parent_world(1.0f);
        const bool placed = registry.valid(parent->entity) && resolve(registry, parent->entity, alpha, parent_world, depth + 1, drawn);
        if (memo != nullptr && depth == 0) {
            *memo = {parent->entity, placed, parent_world};
        }
        if (!placed) {
            return false;
        }
        world = parent_world * local;
    } else {
        world = local;
    }
    return true;
}

}  // namespace

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
    // Where a drawn entity is (false: hidden, or orphaned). Nothing moves during a collection, so
    // the parent last resolved is remembered for the next sibling.
    ParentMemo memo;
    const auto placed = [&](entt::entity entity, glm::mat4& world) {
        return resolve(registry_, entity, alpha, world, 0, true, &memo);
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
        for (const Model::Part& part : model.parts) {
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
