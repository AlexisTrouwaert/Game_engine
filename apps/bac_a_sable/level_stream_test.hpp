#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <string>

#include "moteur/application.hpp"
#include "moteur/camera3d.hpp"
#include "moteur/environment.hpp"
#include "moteur/level_streamer.hpp"
#include "moteur/movement.hpp"
#include "moteur/spatial_hash.hpp"
#include "moteur/world.hpp"

#include "sandbox_scene.hpp"

// The "Level streaming" test (milestone 9, part 8): a level of 512 x 512 m made of blockout pieces
// (a ground of 8 m tiles, walls, pillars, crates: thousands of placed prefabs) generated and baked
// at the start, then crossed by the hero from one side to the other while the chunks around it load
// (in the background, their entities a few at a time) and the far ones unload: the whole level never
// is in memory (StreamingSettings::max_chunks). Measures the main thread's share of the streaming and
// the frames (report). Also the level of a file (Options::level): any level of assets/levels.
class LevelStreamTest final : public SandboxScene {
public:
    struct Options {
        double run_seconds = 0.0;
        std::string level;            // a level of the assets; empty: the generated one
        float speed = 12.0f;          // metres per second of the crossing
        std::string capture_path;     // with freeze_after_ticks
        long freeze_after_ticks = 0;
    };

    LevelStreamTest(moteur::Application& app, const Options& options, bool standalone);
    ~LevelStreamTest() override;

    void update(double dt) override;
    void render(moteur::Renderer& renderer, double alpha) override;
    void draw_controls() override;
    bool stop_requested() const override { return stop_requested_; }

private:
    moteur::Application& app_;
    Options options_;
    bool standalone_;
    bool stop_requested_ = false;
    double elapsed_ = 0.0;
    long ticks_ = 0;
    bool capture_done_ = false;
    moteur::World world_;
    std::unique_ptr<moteur::LevelStreamer> streamer_;
    moteur::SpatialHash hash_;
    entt::entity hero_ = entt::null;
    glm::vec3 start_{0.0f};
    double bake_ms_ = 0.0;
    int max_loaded_ = 0;
    int max_instances_ = 0;
    long waiting_ticks_ = 0;  // ticks the hero waited for the chunk under it
    std::optional<moteur::Environment> sky_;
    moteur::Camera3D camera_;
};
