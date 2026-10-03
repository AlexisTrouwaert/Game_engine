#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "moteur/animator.hpp"
#include "moteur/application.hpp"
#include "moteur/camera3d.hpp"
#include "moteur/environment.hpp"
#include "moteur/mesh.hpp"
#include "moteur/world.hpp"

#include "sandbox_scene.hpp"

// The animation test (milestone 5): the animated test characters in a row, entities of a World
// with an Animator each, playing a clip of their own file in ticks, drawn between two ticks,
// skinned on the GPU; the skeleton is drawn over them in debug lines, and the rigid parts on joints
// (weapons, helmets) follow their joint. In front of the row, a knight walks around an ellipse
// (part 6): its speed drives the locomotion blend space of assets/animations/kaykit.json, it can
// attack with the upper body while walking and crossfade to any clip, and the scene measures how
// much its planted foot slides; its footsteps and blows sound on the events of its clips (part 7);
// it holds a sword of its own file and a burning torch on the attach points of its hands (part 8).
// The characters are those of tools/models/fetch_test_characters.py;
// the reference model is always there. With --fixed-hz 10, the "Interpolation" box shows what
// drawing between ticks is for.
//
// With Options::crowd (--skinned N, part 10), a load test instead: N KayKit characters on a grid,
// seen as in the game, each with its own clip, phase and speed, some changing clip every tick with
// a crossfade or attacking with the upper body; the animation's cost is shown and, with
// Options::report, printed at the end.
class AnimationTest final : public SandboxScene {
public:
    struct Options {
        double run_seconds = 0.0;  // > 0 quits by itself (standalone) or asks to stop (menu)
        int crowd = 0;             // > 0: the load test, with that many animated characters
        bool sun_shadows = true;   // the load test without the sun's shadows, to compare
        float crowd_zoom = 1.0f;   // > 1: the camera closer, part of the crowd out of view (culling)
        bool report = false;       // print the animation's cost at the end
    };

    AnimationTest(moteur::Application& app, const Options& options, bool standalone);
    ~AnimationTest() override;

    void update(double dt) override;
    void render(moteur::Renderer& renderer, double alpha) override;
    void draw_controls() override;
    bool stop_requested() const override { return stop_requested_; }

private:
    struct Character {
        std::string path;  // relative to assets/
        entt::entity entity = entt::null;
        std::vector<std::string> clip_names;
        int clip = 0;          // index in clip_names
        float height = 0.0f;   // metres, in the file
    };

    // The knight of the blends, walking around an ellipse in front of the row.
    struct Walker {
        entt::entity entity = entt::null;
        float scale = 1.0f;         // of the model: speeds of the file x scale = speeds in the scene
        float angle = 0.0f;         // on the ellipse, radians
        float speed = 0.0f;         // m/s in the scene, moving towards target_speed
        float target_speed = 0.0f;
        bool moving = true;         // false: walks on the spot (the feet slide: to compare)
        int motion = 0;             // in motions; 0: the locomotion blend space
        int attack = 0;             // in attacks
        int fade = 12;              // ticks
        std::vector<std::string> motions;
        std::vector<std::string> attacks;
        int feet[2] = {-1, -1};
        float max_speed = 3.0f;     // m/s in the scene: the run's ground speed, and some more
        std::string strides;        // measured ground speeds, for the panel
        // The planted foot's slide, measured from frame to frame.
        glm::vec3 foot_at[2] = {glm::vec3(0.0f), glm::vec3(0.0f)};
        int planted = -1;
        std::uint64_t last_ns = 0;
        float slide = 0.0f;         // m/s along the way, smoothed (+: forwards)
        float lowest = 1e9f;        // the lowest a foot went: the floor, for the feet
        // Part 8: what it holds, on the attach points of kaykit.json.
        entt::entity sword = entt::null;   // sword_1handed.gltf on "right_hand"
        entt::entity torch = entt::null;   // a stick on "left_hand", with its flame and light below
        entt::entity marker = entt::null;  // on "head", a little above: where a health bar would go
        bool own_sword = true;             // false: the sword of the knight's file instead
        bool torch_on = true;              // false: the shield instead
        bool head_markers = true;
    };

    void add(const std::string& path, const std::vector<std::string>& hidden_nodes, const std::string& first_clip);
    void add_walker();
    void build_crowd(int count);
    // A few characters of the crowd change what they do (crossfades, attacks): every tick.
    void stir_crowd();
    // Starts a random motion on a crowd member.
    void play_random(moteur::Animator& animator, int fade);
    void add_held_objects();
    void show_held_objects();
    void update_walker(double dt);
    void measure_slide(float blend);
    void draw_walker_controls();
    // The events of this tick: sounds, and the panel's list.
    void on_events();
    void place_in_row();
    // Plays the character's chosen clip again (from its start), or no clip for the rest pose.
    void restart(Character& character);
    // Every Animator of the scene at the speed of the panel (0 while paused).
    void apply_speed();
    // The palette entry of joint_ in the selected character's first skin, for the weights view.
    int weight_palette_index() const;

    moteur::Application& app_;
    Options options_;
    bool standalone_;
    bool stop_requested_ = false;
    double elapsed_ = 0.0;
    double tick_seconds_ = 0.0;  // the fixed step, as update() receives it

    moteur::World world_;
    std::vector<Character> characters_;
    std::vector<entt::entity> crowd_;
    std::uint32_t random_ = 42;  // the crowd's own generator (xorshift): the same run every time
    std::uint32_t next_random();
    // The animation's cost, summed over the frames measured (after a warm-up).
    struct Measured {
        long frames = 0;
        double animated = 0.0;
        double poses = 0.0;
        double palettes = 0.0;
        double matrices = 0.0;
        double sample_ms = 0.0;
        double palette_ms = 0.0;
    };
    Measured measured_;
    long frame_ = 0;
    Walker walker_;
    std::vector<moteur::AnimatorEvent> events_;  // of the last tick
    std::vector<std::string> event_log_;         // the latest, newest first
    std::int64_t tick_ = 0;
    std::vector<moteur::Asset<moteur::Sound>> steps_;
    std::vector<moteur::Asset<moteur::Sound>> impacts_;
    int selected_ = 0;
    bool playing_ = true;
    float speed_ = 1.0f;
    bool show_skeleton_ = true;
    bool rest_pose_ = false;
    bool interpolation_ = true;
    bool torch_on_ = true;
    bool step_ = false;         // paused: one tick asked for ("Tick suivant")
    bool axes_ = false;         // the joints' axes on the skeletons
    bool bind_pose_ = false;    // CollectOptions::bind_pose
    int joint_ = -1;            // the joint chosen on the selected character (-1: none): highlighted,
                                // and its weights in the weights view
    entt::entity torch_ = entt::null;  // a point light with shadows, in front of the row
    moteur::Mesh floor_;
    moteur::Asset<moteur::Mesh> stick_;
    moteur::Asset<moteur::Mesh> flame_;
    std::optional<moteur::Environment> sky_;
    moteur::Camera3D camera_;
};
