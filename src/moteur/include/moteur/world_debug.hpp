#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace moteur {

class ClearanceMap;
class CollisionScene;
class DebugLineBuffer;
class FieldOfView;
class FlowField;
class NavGrid;
class NavMesh;
class Variable;
class Variables;
class World;

/// The overlays of the world (milestone 6, made common to every scene in milestone 7, part 4): the
/// grid, the clearance, the colliders, the paths, a flow field and a field of view, in debug lines,
/// each switched by a boolean console variable (`debug.grid`, `debug.clearance`, `debug.colliders`,
/// `debug.paths`, `debug.flowfield`, `debug.view`; all off by default). The pointers are owned by
/// the Variables they were declared in; a null one counts as off.
struct WorldDebugFlags {
    Variable* grid = nullptr;       ///< `debug.grid`: blocked cells (red: opaque, orange: see-through)
    Variable* clearance = nullptr;  ///< `debug.clearance`: a square per walkable cell, larger and greener with more room
    Variable* colliders = nullptr;  ///< `debug.colliders`: circles (yellow: Collider::push_weight 0, immovable this tick)
    Variable* paths = nullptr;      ///< `debug.paths`: the points left on each path (movers in MoveMode::ToPoint)
    Variable* field = nullptr;      ///< `debug.flowfield`: arrows of the flow field
    Variable* view = nullptr;       ///< `debug.view`: cells in the field of view
    /// `debug.collision`: the level's collision triangles near the centre (green: walkable, red: wall,
    /// orange: obstacle, purple: void), and the colliders as capsules
    Variable* collision = nullptr;
    Variable* navmesh = nullptr;  ///< `debug.navmesh`: the navigation mesh's triangles near the centre

    /// Declares the variables in `variables` (once; again, as each scene does when it starts, gives
    /// the same ones back) and returns pointers to them.
    static WorldDebugFlags declare(Variables& variables);
    /// True if at least one overlay is switched on (to skip draw_world_debug() altogether).
    bool any() const;
};

/// What the overlays look at; null members are skipped (with their overlays). Nothing is owned: the
/// pointed objects must outlive the call to draw_world_debug().
struct WorldDebugSources {
    const World* world = nullptr;              ///< for the colliders and paths overlays (entities with Collider, Mover)
    const NavGrid* grid = nullptr;             ///< for the grid and clearance overlays; also bounds the cells drawn
    const ClearanceMap* clearance = nullptr;   ///< for the clearance overlay (needs #grid too)
    const FlowField* field = nullptr;          ///< for the flow field overlay (skipped while not valid)
    const FieldOfView* view = nullptr;         ///< for the field of view overlay
    const CollisionScene* scene = nullptr;     ///< for the collision overlay (a 3D level)
    const NavMesh* navmesh = nullptr;          ///< for the navigation mesh overlay
    entt::entity highlight = entt::null;  ///< drawn white (the hero)
};

/// Draws the overlays the flags ask for into `lines`, for the cells within about `radius` metres of
/// `centre` (the camera's target) only: the cost grows with the square of `radius`. `alpha`: the
/// frame's interpolation between the last two ticks, for the entities' places (see
/// World::world_position()). Hidden entities get no collider circle.
void draw_world_debug(DebugLineBuffer& lines, const WorldDebugFlags& flags, const WorldDebugSources& sources,
                      glm::vec3 centre, float radius, float alpha);

}  // namespace moteur
