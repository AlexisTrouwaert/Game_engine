#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace moteur {

class ClearanceMap;
class DebugLineBuffer;
class FieldOfView;
class FlowField;
class NavGrid;
class Variable;
class Variables;
class World;

// The overlays of the world (milestone 6, made common to every scene in milestone 7, part 4): the
// grid, the clearance, the colliders, the paths, a flow field and a field of view, in debug lines,
// each switched by a console variable ("debug.grid", "debug.colliders"...).
struct WorldDebugFlags {
    Variable* grid = nullptr;       // blocked cells (red: opaque, orange: see-through)
    Variable* clearance = nullptr;  // a square per walkable cell, larger and greener with more room
    Variable* colliders = nullptr;  // circles (yellow: immovable this tick)
    Variable* paths = nullptr;      // the points left on each path
    Variable* field = nullptr;      // arrows of the flow field
    Variable* view = nullptr;       // cells in the field of view

    // Declares the variables (once; again gives the same ones).
    static WorldDebugFlags declare(Variables& variables);
    bool any() const;
};

// What the overlays look at; null members are skipped.
struct WorldDebugSources {
    const World* world = nullptr;
    const NavGrid* grid = nullptr;
    const ClearanceMap* clearance = nullptr;
    const FlowField* field = nullptr;
    const FieldOfView* view = nullptr;
    entt::entity highlight = entt::null;  // drawn white (the hero)
};

// Draws the overlays the flags ask for, for the cells within `radius` metres of `centre` (the
// camera's target) only. `alpha`: the frame's interpolation, for the entities' places.
void draw_world_debug(DebugLineBuffer& lines, const WorldDebugFlags& flags, const WorldDebugSources& sources,
                      glm::vec3 centre, float radius, float alpha);

}  // namespace moteur
