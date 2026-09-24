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
#include "moteur/material.hpp"
#include "moteur/mesh.hpp"
#include "moteur/model.hpp"
#include "moteur/renderer.hpp"
#include "moteur/tilemap.hpp"

#include "sandbox_scene.hpp"

// The 3D demonstration scene (milestone 3, part 11): the map of the 2D demo built in 3D, a few
// thousand pieces of decor, a crowd of creatures that turn back before walls, the sun and torches
// with shadows, health bars, and the camera of the game. Everything the milestone added, at once.
//
// Controls: arrows or ZQSD move the camera, the wheel zooms, P switches the projection. Right click
// selects the creature nearest to the pointed ground, left click sends it there (in a straight
// line: the pathfinding comes later), Tab selects the next one, F follows it.
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
    };

    // standalone: the scene is the whole program, so Escape and --run-seconds quit it.
    Demo3D(moteur::Application& app, const Options& options, bool standalone);
    ~Demo3D() override;

    void on_event(const SDL_Event& event) override;
    void update(double dt) override;
    void render(moteur::Renderer& renderer, double alpha) override;
    void draw_controls() override;
    bool stop_requested() const override { return stop_requested_; }

    // For the command line report.
    double elapsed() const { return elapsed_; }
    long ticks() const { return ticks_; }
    long frames() const { return frames_; }
    std::optional<glm::ivec2> hovered_cell() const { return hovered_cell_; }

private:
    // A draw that never changes, with its box in the world computed once.
    struct StaticDraw {
        const moteur::Mesh* mesh;
        glm::mat4 world;
        moteur::Material material;
        moteur::Aabb bounds;
    };
    struct Creature {
        glm::vec2 position;   // on the map, in cells (x along X, y along Z)
        glm::vec2 previous;   // at the previous tick, for interpolation
        glm::vec2 velocity;   // cells per second
        glm::vec3 color;      // linear
        float health;         // [0, 1]
        std::optional<glm::vec2> goal;  // sent there by a click
    };
    struct Torch {
        glm::vec3 position;
        glm::vec3 color;
    };

    void build_map();
    void build_floor();
    void build_decor();
    void spawn_creatures();
    void add_static(const moteur::Mesh& mesh, const glm::mat4& world, const moteur::Material& material);
    void move_creatures(float dt);
    void move_camera(float dt);
    bool walkable(glm::vec2 cell_position) const;
    int creature_near(glm::vec3 ground, float radius) const;
    void draw_overlay(moteur::Renderer& renderer, const moteur::Camera3D& camera, float blend);

    moteur::Application& app_;
    Options options_;
    bool standalone_;
    bool stop_requested_ = false;
    double elapsed_ = 0.0;
    long ticks_ = 0;
    long frames_ = 0;
    bool capture_done_ = false;

    moteur::Tileset tileset_;
    moteur::TileId ground_ = moteur::kNoTile;
    moteur::TileId wall_ = moteur::kNoTile;
    std::optional<moteur::TileMap> map_;

    moteur::Mesh cube_;
    moteur::Mesh tile_;
    moteur::Mesh sphere_;
    moteur::Mesh rock_;
    std::vector<moteur::Mesh> floor_blocks_;  // with merged_floor
    std::optional<moteur::Model> barrel_;     // Poly Haven's wine barrel, when downloaded
    std::optional<moteur::Environment> sky_;
    std::optional<moteur::Font> font_;
    moteur::Texture glow_;
    moteur::Texture white_;

    std::vector<StaticDraw> statics_;
    std::size_t floor_draws_ = 0;  // how many of statics_ are the floor
    std::vector<Creature> creatures_;
    std::vector<Torch> torches_;
    int selected_ = 0;       // creature index
    bool follow_ = false;

    moteur::Camera3D camera_;
    glm::vec2 mouse_{-1.0f};
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
