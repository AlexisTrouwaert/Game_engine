#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "moteur/camera3d.hpp"
#include "moteur/environment.hpp"
#include "moteur/font.hpp"
#include "moteur/input.hpp"
#include "moteur/material.hpp"
#include "moteur/mesh.hpp"
#include "moteur/model.hpp"
#include "moteur/renderer.hpp"
#include "moteur/tilemap.hpp"
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
        bool hero = false;
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
    float step_distance_ = 0.0f;        // walked since the last footstep
    int hits_ = 0;
    std::vector<moteur::Asset<moteur::Sound>> steps_;
    std::vector<moteur::Asset<moteur::Sound>> impacts_;
    moteur::Asset<moteur::Music> ambience_;
    moteur::SoundId ambience_voice_ = moteur::kNoSound;

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
