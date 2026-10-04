#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "moteur/application.hpp"
#include "moteur/camera3d.hpp"
#include "moteur/collision.hpp"
#include "moteur/environment.hpp"
#include "moteur/input.hpp"
#include "moteur/map_data.hpp"
#include "moteur/mesh.hpp"
#include "moteur/movement.hpp"
#include "moteur/nav_grid.hpp"
#include "moteur/particles.hpp"
#include "moteur/pathfinding.hpp"
#include "moteur/spatial_hash.hpp"
#include "moteur/visibility.hpp"
#include "moteur/world.hpp"
#include "moteur/world_debug.hpp"

#include "map_objects.hpp"
#include "sandbox_scene.hpp"

// The "Monde" test (milestone 6): a test map of assets/maps (a MapData asset) built in 3D, with
// what the milestone brings, each part visible:
// - a hero (a column) moved by click (A* path, smoothed; held: follows the pointer) or by keys
//   (sliding on the walls), its Collider and Mover; the camera follows it (Space);
// - monsters (M under the pointer, B a big one) that see the hero (line of sight), chase it along
//   a flow field (one per size), push each other apart and surround it;
// - the fog of war (field of view of the hero, explored cells) and walls dithered before the hero;
// - particle effects: fires on the map's "but" points, an effect under the pointer (E), a trail;
// - overlays: grid, clearance, colliders, paths, flow field, field of view, queries, a ray;
// - the systems' times and counts.
// A right click puts or takes away a wall (the grid's version changes: clearance, fields and sight
// are computed again). Editing the map's file reloads it (hot reload) and the scene is built again.
//
// Standalone runs (no menu): Options::autopilot sends the hero to the map's "but" point with
// monsters on the "monstres" points (a reproducible run, for captures); Options::crowd is the load
// test (part 10): that many monsters chasing the hero on the map, the times printed with report.
class WorldTest final : public SandboxScene {
public:
    struct Options {
        std::string map = "salle_portes";  // a file of assets/maps, without ".json"
        double run_seconds = 0.0;          // > 0 quits by itself (standalone) or asks to stop (menu)
        bool autopilot = false;
        int crowd = 0;
        int fires = 0;  // load test of the particles: that many fires around the hero
        bool report = false;
        std::string capture_path;  // with freeze_after_ticks: one capture of a frozen frame
        long freeze_after_ticks = 0;
        // The map editor's "Essayer": this map instead of a file (not saved yet), the hero at
        // `start` (metres on the ground) instead of the "depart" point.
        std::shared_ptr<const moteur::MapData> data;
        std::optional<glm::vec2> start;
    };

    WorldTest(moteur::Application& app, const Options& options, bool standalone);
    ~WorldTest() override;

    void on_event(const SDL_Event& event) override;
    void update(double dt) override;
    void render(moteur::Renderer& renderer, double alpha) override;
    void draw_controls() override;
    bool stop_requested() const override { return stop_requested_; }

    // The maps of assets/maps, without ".json", sorted.
    static std::vector<std::string> map_names(const moteur::Assets& assets);

private:
    struct Actions {
        moteur::ActionId move_to, move, camera, zoom_in, zoom_out, toggle_wall, spawn, spawn_big, effect, follow,
            projection;
    };
    // The scene's own component: a monster's state (the game's, not the engine's).
    struct Monster {
        bool big = false;
        bool chasing = false;
        bool always_chase = false;  // the load test: no need to see the hero
        int lost_ticks = 0;         // ticks without seeing the hero while chasing
        int strike_ticks = 0;       // ticks until its next blow while in reach
    };
    // Mean times of the systems, in milliseconds per tick (and per frame for the particles).
    struct Timings {
        double ai = 0, paths = 0, fields = 0, moves = 0, collisions = 0, sight = 0, particles = 0;
        long ticks = 0, frames = 0;
    };

    void load_map(const std::string& name);
    // The scene's copy of the map (with the walls put or taken away), its grid, then everything.
    void reset_tiles();
    void build_decor();
    void add_static(const moteur::Asset<moteur::Mesh>& mesh, const moteur::Transform& transform,
                    const moteur::Material& material);
    void spawn_hero(glm::vec2 at);
    entt::entity spawn_monster(glm::vec2 at, bool big);
    void spawn_group(glm::vec2 around, int count, bool big);
    void start_fires();
    void toggle_wall(glm::ivec2 cell);
    void grid_changed();
    std::string point_at(glm::ivec2 cell) const;
    void apply_input(float dt);
    void think(float dt);
    void update_fields();
    void update_sight();
    void draw_overlays(moteur::Renderer& renderer, const moteur::Camera3D& camera, float alpha);
    glm::vec2 hero_position() const;

    moteur::Application& app_;
    Options options_;
    bool standalone_;
    bool stop_requested_ = false;
    double elapsed_ = 0.0;
    long ticks_ = 0;
    bool capture_done_ = false;
    Actions actions_{};
    Random random_{6};

    std::vector<std::string> names_;
    int chosen_ = 0;  // in names_
    std::string error_;
    moteur::Asset<moteur::MapData> map_;
    std::uint64_t built_revision_ = 0;
    moteur::TileMap tiles_{1, 1};
    moteur::NavGrid grid_;
    moteur::ClearanceMap clearance_;
    int toggled_ = 0;

    moteur::World world_;
    moteur::SpatialHash hash_;
    moteur::FlowField fields_[2];  // small creatures, big ones
    glm::ivec2 field_cell_{-1};
    std::uint64_t field_version_ = ~0ull;
    moteur::FieldOfView view_;
    moteur::ExploredMap explored_;
    glm::ivec2 view_cell_{-1};
    std::uint64_t view_version_ = ~0ull;
    std::vector<std::uint8_t> fog_cells_;
    moteur::MovementStats movement_stats_;
    moteur::CollisionStats collision_stats_;
    Timings timings_;
    Timings shown_;  // the means shown, refreshed twice a second
    long shown_at_ = 0;

    entt::entity hero_ = entt::null;
    bool pressed_on_ground_ = false;
    int follow_repath_ = 0;  // ticks until the held click may ask for a new path
    bool follow_camera_ = true;

    moteur::ParticleSystem particles_;
    std::vector<moteur::EffectId> fires_;
    moteur::EffectId trail_ = moteur::kNoEffect;
    moteur::Asset<moteur::ParticleEffect> sparks_;  // the monsters' blows (empty without the file)
    std::vector<std::string> effect_names_;
    int effect_choice_ = 0;
    std::uint64_t last_frame_ns_ = 0;

    moteur::Asset<moteur::Mesh> cube_;
    moteur::Asset<moteur::Mesh> tile_;
    ObjectMeshes object_meshes_;  // the map's objects (milestone 7)
    std::optional<moteur::Environment> sky_;
    moteur::Camera3D camera_;
    std::optional<glm::ivec2> hovered_;
    std::optional<glm::vec3> pointer_ground_;

    // What the panel shows.
    bool show_axes_ = true;
    bool show_points_ = true;
    bool show_ascii_ = false;
    moteur::WorldDebugFlags debug_flags_;  // the overlays, console variables "debug.*"
    bool show_query_ = false;
    bool show_ray_ = false;
    bool fog_on_ = true;
    bool cutout_on_ = true;
    bool fires_on_ = true;
    bool trail_on_ = false;
};
