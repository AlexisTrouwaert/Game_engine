#include "moteur/level_collision.hpp"

#include <algorithm>

#include "moteur/model.hpp"
#include "moteur/profiler.hpp"
#include "moteur/world.hpp"

namespace moteur {

std::size_t gather_static_collision(const World& world, CollisionScene& scene) {
    MOTEUR_PROFILE("collision scene: gather");
    const entt::registry& registry = world.registry();
    std::vector<entt::entity> entities;
    for (const entt::entity entity : registry.view<StaticCollision>()) {
        entities.push_back(entity);
    }
    std::sort(entities.begin(), entities.end(),
              [](entt::entity a, entt::entity b) { return entt::to_integral(a) < entt::to_integral(b); });
    scene.clear();
    for (const entt::entity entity : entities) {
        const StaticCollision& collision = registry.get<StaticCollision>(entity);
        const glm::mat4 matrix = world.world_matrix(entity, 1.0f);
        const auto body = static_cast<std::uint32_t>(entt::to_integral(entity));
        if (collision.from_model) {
            if (const ModelComponent* model = registry.try_get<ModelComponent>(entity); model != nullptr && model->model) {
                scene.add(model->model->collision, matrix, body);
            }
        }
        if (!collision.boxes.empty()) {
            CollisionMeshData boxes;
            for (const CollisionBox& box : collision.boxes) {
                Aabb aabb;
                aabb.add(box.centre - box.half_size);
                aabb.add(box.centre + box.half_size);
                boxes.add_box(aabb, box.top, box.sides);
            }
            scene.add(boxes, matrix, body);
        }
    }
    scene.build();
    return scene.triangles().size();
}

}  // namespace moteur
