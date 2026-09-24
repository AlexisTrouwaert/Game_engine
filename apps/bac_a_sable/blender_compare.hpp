#pragma once

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <vector>

#include "moteur/camera3d.hpp"
#include "moteur/environment.hpp"
#include "moteur/material.hpp"
#include "moteur/mesh.hpp"
#include "moteur/model.hpp"
#include "moteur/renderer.hpp"

#include "sandbox_scene.hpp"

// The scene compared with Blender (milestone 3, part 7): the Poly Haven test models and two
// spheres of known materials in a row, lit only by the test environment (no sun, no point light,
// no floor), seen by a fixed camera. tools/blender/compare_render.py builds the same scene in
// Blender from the JSON file written next to the capture, so that both images can be compared.
class BlenderCompare final : public SandboxScene {
public:
    struct Options {
        std::string capture_path;  // non-empty: a PNG of the 10th frame, and <same name>.json beside it
        double run_seconds = 0.0;  // > 0 quits by itself (standalone) or asks to stop (menu)
    };

    // What the scene shows, shared with Blender through the JSON file.
    struct Sphere {
        glm::vec3 center;
        float radius;
        glm::vec3 color;  // linear
        float metallic;
        float roughness;
    };

    BlenderCompare(moteur::Application& app, const Options& options, bool standalone);

    void update(double dt) override;
    void render(moteur::Renderer& renderer, double alpha) override;
    void draw_controls() override;
    bool stop_requested() const override { return stop_requested_; }

private:
    struct Placed {
        std::string path;  // relative to assets/
        moteur::Model model;
        glm::vec3 offset;  // where its origin is put
    };

    void write_description(const std::string& path, const moteur::Renderer& renderer) const;

    moteur::Application& app_;
    Options options_;
    bool standalone_;
    bool stop_requested_ = false;
    double elapsed_ = 0.0;
    long frames_ = 0;

    std::vector<Placed> models_;
    std::vector<Sphere> spheres_;
    moteur::Mesh sphere_mesh_;
    std::optional<moteur::Environment> environment_;
    std::string environment_path_;  // relative to assets/, empty without the test environment
    moteur::Camera3D camera_;
    float environment_intensity_ = 1.0f;
};
