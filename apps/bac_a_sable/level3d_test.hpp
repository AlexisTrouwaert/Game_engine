#pragma once

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <vector>

#include "moteur/application.hpp"
#include "moteur/camera3d.hpp"
#include "moteur/collision.hpp"
#include "moteur/collision_scene.hpp"
#include "moteur/environment.hpp"
#include "moteur/input.hpp"
#include "moteur/mesh.hpp"
#include "moteur/movement.hpp"
#include "moteur/navigation.hpp"
#include "moteur/picking.hpp"
#include "moteur/spatial_hash.hpp"
#include "moteur/world.hpp"
#include "moteur/world_debug.hpp"

#include "sandbox_scene.hpp"

// The "Level 3D" test (milestone 9, parts 5 and 6): the logic in true 3D on a blocked-out level (a
// ground, walls, a flight of stairs up to a platform, a bridge over a passage reached by a ramp, a
// ramp too steep, a block too high for a step, a low ceiling): the level's CollisionScene and its
// NavMesh; the hero moved by click (a path on the mesh to the point under the pointer: by the stairs
// or the ramp) or by keys; monsters (M under the pointer, or Options::crowd of them) chasing the hero
// along a flow field over the mesh, steering round each other; walkers on the bridge and under it;
// crates put or taken away with the right click (the mesh rebuilt there, timed); the overlays
// (`debug.collision`, `debug.navmesh`, `debug.paths`). Part 7: the monster under the pointer is
// outlined (the id buffer, a frame late), a click on a monster sends the hero to it (rays and
// priorities, at the tick), and the bridge's deck is cut away while the hero walks under it. The
// panel shows the costs.
class Level3DTest final : public SandboxScene {
public:
    struct Options {
        double run_seconds = 0.0;  // > 0 quits by itself (standalone) or asks to stop (menu)
        bool autopilot = false;    // the hero walks a fixed tour (stairs, platform, ledge, bridge)
        int crowd = 0;             // that many monsters chasing the hero (the load test of part 6)
        std::string capture_path;  // with freeze_after_ticks: one capture of a frozen frame
        long freeze_after_ticks = 0;
    };

    Level3DTest(moteur::Application& app, const Options& options, bool standalone);

    void update(double dt) override;
    void render(moteur::Renderer& renderer, double alpha) override;
    void draw_controls() override;
    bool stop_requested() const override { return stop_requested_; }

private:
    struct Actions {
        moteur::ActionId move_to, move, camera, zoom_in, zoom_out, follow, toggle_wall, spawn;
    };
    // A walker of the scene: goes between two points (the game's, not the engine's).
    struct Walker {
        glm::vec3 a{0.0f}, b{0.0f};
        bool to_b = true;
    };

    // A monster of the scene: chases the hero (the game's, not the engine's).
    struct Monster {};

    void build_level();
    void build_collision();  // the scene from the level and the crates
    entt::entity spawn_character(glm::vec3 at, glm::vec3 color, bool hero);
    void spawn_monsters(glm::vec3 around, int count);
    void toggle_crate(glm::vec3 at);
    void apply_input(float dt);
    void think();

    moteur::Application& app_;
    Options options_;
    bool standalone_;
    bool stop_requested_ = false;
    double elapsed_ = 0.0;
    long ticks_ = 0;
    bool capture_done_ = false;
    Actions actions_{};

    moteur::CollisionMeshData level_;
    moteur::CollisionScene scene_;
    moteur::NavMesh navmesh_;
    moteur::NavFlowField chase_;
    glm::vec3 chase_target_{1e9f};
    struct Crate {
        moteur::Aabb box;
        std::uint32_t obstacle = 0;
        entt::entity entity = entt::null;
    };
    std::vector<Crate> crates_;
    double nav_build_ms_ = 0.0, nav_rebuild_ms_ = 0.0, field_ms_ = 0.0, paths_ms_ = 0.0;
    moteur::ControllerSettings controller_;
    moteur::World world_;
    moteur::SpatialHash hash_;
    moteur::MovementStats movement_stats_;
    moteur::CollisionStats collision_stats_;
    double move_ms_ = 0.0, collide_ms_ = 0.0, build_ms_ = 0.0;

    entt::entity hero_ = entt::null;
    std::size_t tour_ = 0;  // autopilot: the next point of the tour
    std::optional<moteur::PickHit> pointed_;  // what the pointer is over, by the logic's rays
    entt::entity hovered_ = entt::null;        // what the id buffer says is under the pointer (display)
    bool follow_camera_ = true;

    moteur::Asset<moteur::Mesh> cube_;
    std::optional<moteur::Environment> sky_;
    moteur::Camera3D camera_;
    moteur::WorldDebugFlags debug_flags_;
};
