#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <vector>

#include "moteur/collision_scene.hpp"
#include "moteur/reflection.hpp"

namespace moteur {

class World;

// What a level's entities give to its CollisionScene (milestone 9, part 5): the pieces of a level
// (kit bricks, props) say so with a StaticCollision component, and gather_static_collision() builds
// the scene from them, in the order of their identifiers.

/// A box of collision, in the entity's space: a crate, a low wall, a piece of a prop whose model has
/// no collision of its own.
struct CollisionBox {
    glm::vec3 centre{0.0f};               ///< metres, in the entity's space
    glm::vec3 half_size{0.5f};            ///< metres
    SurfaceKind top = SurfaceKind::Obstacle;    ///< its top face
    SurfaceKind sides = SurfaceKind::Obstacle;  ///< its other faces
};
MOTEUR_REFLECT(CollisionBox,
    MOTEUR_FIELD(centre, {.unit = "m", .flags = kFieldDefault | kFieldLogic}),
    MOTEUR_FIELD(half_size, {.min = 0.0, .unit = "m", .flags = kFieldDefault | kFieldLogic}),
    MOTEUR_FIELD(top, {.flags = kFieldDefault | kFieldLogic}),
    MOTEUR_FIELD(sides, {.flags = kFieldDefault | kFieldLogic}));

/// Marks an entity whose collision is part of the level's static geometry: its model's collision
/// (Model::collision, when `from_model` and the entity has a ModelComponent) and its boxes, at its
/// world matrix. A component (name `static_collision`); the entity must not move once the scene is
/// built (build it again when the level changes).
struct StaticCollision {
    bool from_model = true;          ///< the model's collision geometry (made by its import)
    std::vector<CollisionBox> boxes; ///< more, in the entity's space
};
MOTEUR_REFLECT(StaticCollision,
    MOTEUR_FIELD(from_model, {.doc = "use the collision geometry of the entity's model", .flags = kFieldDefault | kFieldLogic}),
    MOTEUR_FIELD(boxes, {.flags = kFieldDefault | kFieldLogic}));

/// Clears `scene`, adds the collision of every entity with a StaticCollision (in the order of their
/// identifiers; each triangle's body is the entity's identifier), then builds it. Returns the number
/// of triangles. Main thread (reads the world's registry).
std::size_t gather_static_collision(const World& world, CollisionScene& scene);

}  // namespace moteur
