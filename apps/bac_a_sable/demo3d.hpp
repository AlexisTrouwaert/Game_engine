#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "moteur/camera3d.hpp"
#include "moteur/collision.hpp"
#include "moteur/environment.hpp"
#include "moteur/font.hpp"
#include "moteur/input.hpp"
#include "moteur/map_data.hpp"
#include "moteur/material.hpp"
#include "moteur/mesh.hpp"
#include "moteur/model.hpp"
#include "moteur/movement.hpp"
#include "moteur/nav_grid.hpp"
#include "moteur/particles.hpp"
#include "moteur/pathfinding.hpp"
#include "moteur/renderer.hpp"
#include "moteur/spatial_hash.hpp"
#include "moteur/tilemap.hpp"
#include "moteur/visibility.hpp"
#include "moteur/world_debug.hpp"
#include "moteur/save_file.hpp"
#include "moteur/world_save.hpp"
#include "moteur/world.hpp"

#include "sandbox_scene.hpp"

// The 3D demonstration scene (milestone 3, part 11): the map of the 2D demo built in 3D, a few
// thousand pieces of decor, a crowd of creatures that turn back before walls, the sun and torches
// with shadows, health bars, and the camera of the game. Everything the milestone added, at once.
//
// The world is made of entities (milestone 4, part 5): the floor, walls and decor are still
// entities, the creatures move (a root entity with a body and a head attached to it), the torches
// carry a flame, a light and a halo, and the ring under the selected creature is attached to it.
//
// Controls: actions (milestone 4, part 4), bound in assets/input/demo3d.json, two profiles:
// - "clic": left click sends the selected creature to the pointed ground (held: it follows the
//   pointer), A Z E R T and right click are skills 1 to 6;
// - "zqsd": Z Q S D move it, A E R F and right click are skills 1 to 5.
// In both: the wheel zooms, the arrows (or the right stick) move the camera, middle click selects
// the creature nearest to the pointed ground, Tab the next one, Space follows it, P switches the
// projection. Keys are named as on an AZERTY keyboard; they are bound by position, so a QWERTY
// keyboard gets the same places (Q W E R T...). A gamepad works in both profiles.
class Demo3D final : public SandboxScene {
public:
    struct Options {
        std::uint32_t seed = 12345;  // map decor, creatures and their moves
        int map_size = 100;          // the map has map_size x map_size cells of 1 m
        int creatures = 300;
        int decor = 3000;            // rocks, trees, crates and barrels
        bool merged_floor = true;    // the floor as one mesh per 10 x 10 block (false: one tile per cell, to compare)
        int point_budget = -1;       // torches with a shadow per frame (-1: the engine's default)
        long freeze_after_ticks = 0; // > 0 stops all motion after this many ticks (captures)
        std::string capture_path;    // non-empty: write a PNG once frozen
        double run_seconds = 0.0;    // > 0 quits by itself (standalone) or asks to stop (menu)
        bool no_input = false;       // ignore the keyboard and mouse (except Escape)
        bool fake_mouse = false;     // the mouse stays at fake_mouse_position (window pixels)
        glm::vec2 fake_mouse_position{-1.0f};
        // The playable slice (milestone 4, part 9): a character of its own (the hero), followed by
        // the camera, who strikes the creatures, with footsteps, impacts and an ambience. Off, the
        // demo of milestone 3 (a creature to choose and send), whose captures do not change.
        // Milestone 5, part 11: the hero is the animated KayKit knight, sword in hand, and the
        // creatures are animated KayKit skeletons (when tools/models/fetch_test_characters.py ran).
        // Milestone 6, part 11: the slice's map is assets/maps/tranche.json (the same rooms), the
        // hero walks by paths and slides on walls, the creatures collide, see the hero, chase and
        // strike it; braziers burn, blows spark and bleed, the dead vanish in smoke; the fog of war
        // hides what the hero has not seen, and walls before the hero fade.
        // Milestone 7, part 2: the hero and the creatures are rows of the "personnages" table
        // (assets/data/personnages/), read again every tick: edited while the game runs, they change.
        bool hero = false;
        // Milestone 7, part 6 (the slice): after tick `save_at_tick`, the game saved into
        // `save_path`; or, with `load_path`, the game loaded from that save instead of a new one.
        std::string save_path;
        long save_at_tick = 0;
        bool save_cbor = false;  // the compact binary form of the shipped game, instead of JSON
        std::string load_path;
        // Milestone 7, part 11: a save already read (the menus read slots, with their backup):
        // the slice starts from it, as from `load_path`.
        std::shared_ptr<const moteur::SaveGame> saved_game;
        // Milestone 7, part 8: the decor and creatures drawn at random written as objects of the
        // slice's map into this file (a v2 map), then the program stops. Once the map has objects,
        // the slice places those instead of drawing.
        std::string export_objects_path;
    };

    // The assets it loads from files (a loading screen loads them ahead: see StatesDemo).
    static constexpr const char* kFont = "fonts/Inter-Regular.ttf";
    static constexpr float kFontPixelHeight = 20.0f;
    static constexpr const char* kBarrel = "models/polyhaven/wine_barrel_01/wine_barrel_01_1k.gltf";
    // The hero's sounds (tools/audio/fetch_test_sounds.py; without them, the slice is silent).
    static constexpr const char* kSteps[5] = {
        "audio/kenney_impact/footstep_concrete_000.ogg", "audio/kenney_impact/footstep_concrete_001.ogg",
        "audio/kenney_impact/footstep_concrete_002.ogg", "audio/kenney_impact/footstep_concrete_003.ogg",
        "audio/kenney_impact/footstep_concrete_004.ogg"};
    static constexpr const char* kImpacts[5] = {
        "audio/kenney_impact/impactMetal_light_000.ogg", "audio/kenney_impact/impactMetal_light_001.ogg",
        "audio/kenney_impact/impactMetal_light_002.ogg", "audio/kenney_impact/impactMetal_light_003.ogg",
        "audio/kenney_impact/impactMetal_light_004.ogg"};
    static constexpr const char* kAmbience = "audio/ambience/forgotten_tombs.mp3";
    // The slice's animated characters (milestone 5): their models, skeletons and clips are in the
    // same files; the description of their animations is shared (the KayKit rig).
    static constexpr const char* kKnight = "models/characters/kaykit_adventurers/Knight.glb";
    static constexpr const char* kSword = "models/characters/kaykit_adventurers/sword_1handed.gltf";
    static constexpr const char* kSkeletons[2] = {"models/characters/kaykit_skeletons/Skeleton_Warrior.glb",
                                                  "models/characters/kaykit_skeletons/Skeleton_Minion.glb"};
    static constexpr const char* kAnimationSet = "animations/kaykit.json";
    // Whether the slice has its animated characters (else the bodies of primitives of milestone 4).
    static bool characters_available(moteur::Assets& assets);

    // Declares the demo's actions, and the menus' (menu_up, menu_down, menu_confirm), and reads their
    // bindings (assets/input/demo3d.json, then the player's file). The constructor does it; the
    // states around the demo call it first, so that their menus have actions before the demo exists.
    static void declare_actions(moteur::Application& app);

    // standalone: the scene is the whole program, so Escape and --run-seconds quit it.
    Demo3D(moteur::Application& app, const Options& options, bool standalone);
    ~Demo3D() override;

    void on_event(const SDL_Event& event) override;
    void update(double dt) override;
    void render(moteur::Renderer& renderer, double alpha) override;
    void draw_controls() override;
    bool stop_requested() const override { return stop_requested_; }

    // The slice: blows landed on creatures.
    int hits() const { return hits_; }

    // The slice's whole state as a save (milestone 7, part 6): the persistent entities (hero,
    // creatures) and the scene's own state (ticks, camera, exploration...). Between ticks.
    moteur::SaveGame save() const;

    // What a save's header says of the game (the slots' menu): the hero's health (0 to 1), the
    // creatures still standing.
    float hero_health() const;
    int creatures_standing() const;

    // For the command line report.
    double elapsed() const { return elapsed_; }
    long ticks() const { return ticks_; }
    long frames() const { return frames_; }
    // Mean CPU time of the world's collection for the renderer (entities -> draws, lights,
    // billboards), in milliseconds per frame.
    double collect_ms() const { return frames_ > 0 ? collect_seconds_ * 1000.0 / static_cast<double>(frames_) : 0.0; }
    std::optional<glm::ivec2> hovered_cell() const { return hovered_cell_; }
    // The "pause" action was pressed this tick (Escape, Start). The demo itself ignores it: a game
    // state running it pushes its pause over it.
    bool pause_pressed() const { return app_.input().pressed(actions_.pause); }

private:
    void build_map();
    void build_floor();
    void build_decor();
    void spawn_creatures();
    // The hero, at the start of the first room, selected for good.
    void spawn_hero();
    // The hero hits a creature: an impact where it is, a quarter of its health.
    void strike(entt::entity creature);
    // A creature loses `blow` of its health (a share of 1): its blow taken, or its fall (effects,
    // collider gone). What a strike and "kill all" share.
    void hurt(entt::entity creature, float blow);
    // What each creature is: drawn, read from the "monstre" objects of the map, or asked for by
    // the console ("spawn").
    struct CreatureSpawn {
        glm::vec2 position, direction;
        float pace;
        glm::vec3 color;
        float health;
        std::string character;  // row of the characters' table (the slice)
    };
    entt::entity spawn_creature(const CreatureSpawn& spawn, int index);
    // Milestone 7, part 11: the slice's console commands (game commands: queued, run at the start
    // of a tick, recorded in replays).
    void register_commands();
    // The hero starts a blow at `target` (null: in the air): with an animated hero, the blow lands
    // on the attack's "impact" event, on the target if it is still within reach then, or else on
    // the nearest creature within reach; a blow already under way is not started again. Without
    // animation, at once, as in milestone 4.
    void attack(entt::entity target);
    // The animated characters: they face where they go, their clips follow their speed, then their
    // clocks advance and their events (footsteps, impacts) are acted on.
    void animate_characters(float dt);
    // An animated character of the slice (a model, its Animator), placed at `foot`, `height` metres tall.
    entt::entity spawn_animated(const char* path, const moteur::Transform& foot, float height,
                                const std::vector<std::string>& hidden_nodes);
    // How high the top of a character is (its bar goes a little above): its Stature, or the height
    // of the bodies of primitives.
    float top_of(entt::entity entity) const;
    // The creature nearest to the hero within reach, or null.
    entt::entity creature_in_reach() const;
    bool in_reach(entt::entity creature) const;
    // Footsteps, while the hero walks.
    void play_steps(glm::vec2 before);
    void light_braziers();
    // A still entity: floor, wall, decor.
    void add_static(const moteur::Asset<moteur::Mesh>& mesh, const moteur::Transform& transform, const moteur::Material& material);
    void move_creatures(float dt);
    // The goal marker where the selected creature is sent, and the ring under it.
    void show_selection();
    void move_camera(glm::vec2 direction, float dt);
    // Reads the actions of this tick and applies them.
    void apply_input(float dt);
    // The player's bindings file (the profile chosen, keys changed).
    std::string user_bindings_path() const;
    bool walkable(glm::vec2 cell_position) const;
    // The ground forward and right of the camera (for moves relative to the screen).
    void screen_axes(glm::vec3& ahead, glm::vec3& right) const;
    entt::entity creature_near(glm::vec3 ground, float radius) const;
    // The creature created after `creature` (the first one after the last).
    entt::entity next_creature(entt::entity creature) const;
    // A creature's position on the map, in cells (x along X, y along Z), at the current tick.
    glm::vec2 cell_position(entt::entity creature) const;
    void draw_overlay(moteur::Renderer& renderer, const moteur::Camera3D& camera, float blend);
    // Milestone 6 (the slice only): the grid of the file map, the hunt of the creatures, the moves
    // (paths, flow field, collisions), the field of view and the fog, the effects.
    void hunt(float dt);
    // The hero's moves of this tick: a click walks by a path (held: follows the pointer), a click on
    // a creature strikes it or walks up to it, keys and stick walk straight and slide on walls.
    void steer_hero(const std::optional<glm::vec3>& ground);
    // A creature's blow lands (its attack's "impact" event, or at once without animation).
    void creature_blow(entt::entity creature);
    void move_characters(float dt);
    void update_sight();
    void play_effect(const char* path, glm::vec3 at, float scale = 1.0f);
    bool is_wall(int i, int j) const;
    // Milestone 7, part 6: what saves need.
    void setup_serializers();
    entt::entity spawn_from_record(entt::registry& registry, const nlohmann::json& record);
    void load(const moteur::SaveGame& save);
    void create_selection_marks();

    // The actions of the scene.
    struct Actions {
        moteur::ActionId move_to, move, camera, zoom_in, zoom_out, select, next, follow, projection;
        moteur::ActionId skills[6];
        moteur::ActionId pause;  // not read by the demo: by the state that runs it (StatesDemo)
    };
    static constexpr int kSkills = 6;
    // The actions of the scene, as declared by declare_actions() (found by their names).
    static Actions find_actions(const moteur::Input& input);
    // A skill just used: its number shown over the creature for a moment.
    struct SkillFlash {
        int skill;
        entt::entity creature;
        long until_tick;
    };

    moteur::Application& app_;
    Actions actions_{};
    std::vector<SkillFlash> flashes_;
    int skill_uses_[kSkills] = {};
    Options options_;
    bool standalone_;
    bool stop_requested_ = false;
    double elapsed_ = 0.0;
    long ticks_ = 0;
    long live_ticks_ = 0;  // ticks not frozen: what timed effects count, so that they stop with the scene
    long frames_ = 0;
    double collect_seconds_ = 0.0;
    bool capture_done_ = false;
    long frozen_frames_ = 0;     // frames drawn since the freeze (--freeze-after)
    bool stats_frozen_ = false;  // last_stats_ come from one of them

    moteur::Tileset tileset_;
    moteur::TileId ground_ = moteur::kNoTile;
    moteur::TileId wall_ = moteur::kNoTile;
    std::optional<moteur::TileMap> map_;

    moteur::Asset<moteur::Mesh> cube_;
    moteur::Asset<moteur::Mesh> tile_;
    moteur::Asset<moteur::Mesh> sphere_;
    moteur::Asset<moteur::Mesh> rock_;
    moteur::Asset<moteur::Model> barrel_;     // Poly Haven's wine barrel, when downloaded
    std::optional<moteur::Environment> sky_;
    moteur::Asset<moteur::Font> font_;
    moteur::Asset<moteur::Texture> glow_;
    moteur::Texture white_;

    // The scene's entities (one registry per scene).
    moteur::World world_;
    std::size_t static_draws_ = 0;  // draws of the still entities (a model counts its parts)
    std::size_t creatures_ = 0;
    std::vector<glm::vec3> braziers_;  // where the torches go (once the creatures exist)
    std::size_t torches_ = 0;
    entt::entity selected_ = entt::null;
    entt::entity reported_selection_ = entt::null;  // the last one given to the inspector
    entt::entity ring_ = entt::null;   // under the selected creature (attached to it)
    entt::entity goal_ = entt::null;   // where it is sent (hidden when it is not)
    bool follow_ = false;

    // The slice (Options::hero).
    entt::entity hero_ = entt::null;
    bool pressed_on_creature_ = false;  // the click began on a creature: holding it does not walk
    float step_distance_ = 0.0f;        // walked since the last footstep (hero without animation)
    bool animated_ = false;             // the slice has its animated characters
    entt::entity attack_target_ = entt::null;  // the blow under way lands on it at its impact
    std::vector<moteur::AnimatorEvent> events_;  // of the last tick
    int hits_ = 0;
    std::vector<moteur::Asset<moteur::Sound>> steps_;
    std::vector<moteur::Asset<moteur::Sound>> impacts_;
    moteur::Asset<moteur::Music> ambience_;
    moteur::SoundId ambience_voice_ = moteur::kNoSound;

    // Milestone 6 (the slice).
    moteur::Asset<moteur::MapData> slice_map_;
    moteur::NavGrid grid_;
    moteur::ClearanceMap clearance_;
    moteur::SpatialHash hash_;
    moteur::FlowField field_;  // towards the hero, for the creatures that chase it
    glm::ivec2 field_cell_{-1};
    moteur::FieldOfView view_;
    moteur::ExploredMap explored_;
    glm::ivec2 view_cell_{-1};
    std::vector<std::uint8_t> fog_cells_;
    moteur::ParticleSystem particles_;
    std::map<std::string, moteur::Asset<moteur::ParticleEffect>> effects_;
    std::uint64_t last_frame_ns_ = 0;
    bool fog_on_ = true;
    moteur::WorldDebugFlags debug_flags_;  // the overlays of the world, console variables "debug.*"
    bool pressed_on_ground_ = false;
    int follow_repath_ = 0;
    int blows_taken_ = 0;
    glm::vec2 field_target_{0.0f};  // where the hero was when the flow field was computed
    std::uint64_t next_persistent_id_ = 1;
    moteur::ComponentSerializers serializers_;
    moteur::Variable* effects_on_ = nullptr;  // "effects.enabled"
    std::vector<moteur::MapObject>* recording_ = nullptr;  // the draws, as objects (export)
    bool stop_after_export_ = false;
    bool god_ = false;  // "god": the hero takes no blow
    bool from_objects() const;  // the slice whose map has objects: no draws

    moteur::Camera3D camera_;
    glm::vec2 mouse_{-1.0f};
    glm::vec2 live_pointer_{-1.0f};  // the pointer at the last tick not frozen
    std::optional<glm::ivec2> hovered_cell_;
    moteur::Camera3D drawn_camera_;  // the camera of the last frame drawn (clicks pick with it)
    moteur::RenderStats last_stats_;

    // Settings of the panel.
    float sun_intensity_ = 3.0f;
    float sun_yaw_ = 60.0f;
    float sun_elevation_ = 55.0f;
    int torch_lights_ = 24;       // point lights sent per frame, nearest to the camera first
    bool torch_shadows_ = true;
    bool health_bars_ = true;
};
