#pragma once

#include <string>
#include <vector>

#include "moteur/application.hpp"
#include "moteur/camera3d.hpp"
#include "moteur/world.hpp"

#include "sandbox_scene.hpp"

// The pose compared with Blender (milestone 5, part 9): the KayKit knight alone, in one clip at one
// moment, in the "base color" view (no light: only the pose and the textures count), seen by a
// fixed camera. tools/blender/compare_pose.py poses the same file in Blender from the JSON written
// next to the capture, and tools/blender/compare_pose_images.py compares the two silhouettes.
class AnimCompare final : public SandboxScene {
public:
    struct Options {
        std::string clip = "1H_Melee_Attack_Chop";
        double seconds = 0.4;      // from the start of the clip
        std::string capture_path;  // non-empty: a PNG of the 10th frame, and <same name>.json beside it
        double run_seconds = 0.0;  // > 0 quits by itself
    };

    AnimCompare(moteur::Application& app, const Options& options);
    ~AnimCompare() override;

    void update(double dt) override;
    void render(moteur::Renderer& renderer, double alpha) override;
    void draw_controls() override;
    bool stop_requested() const override { return false; }

private:
    void write_description(const std::string& path, const moteur::Renderer& renderer) const;

    moteur::Application& app_;
    Options options_;
    double elapsed_ = 0.0;
    long frames_ = 0;
    moteur::World world_;
    entt::entity knight_ = entt::null;
    moteur::Camera3D camera_;
};
