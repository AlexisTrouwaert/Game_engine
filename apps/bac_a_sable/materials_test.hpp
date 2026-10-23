#pragma once

#include <optional>
#include <string>

#include "moteur/application.hpp"
#include "moteur/camera3d.hpp"
#include "moteur/environment.hpp"
#include "moteur/mesh.hpp"
#include "moteur/world.hpp"

#include "sandbox_scene.hpp"

// The "Matériaux" test (milestone 9 bis, part 2), what the part brings side by side:
// - a stone pillar and the slice's ghost going round it (models/fantome/fantome.glb: glTF BLEND),
//   in front of it and behind it; panes of coloured glass crossing each other's sight;
// - dissolves at 0, 35 % and 70 %, and one that goes round from whole to gone (its shadow with it);
// - a flash on a sphere every second;
// - a material file (materials/test/lave.material.json): its uv scroll and its beating emission;
// - a model's material replaced by name (ModelComponent::materials): the ghost's veil turned red.
// Standalone with --materials; captured frozen with --freeze-after (check_references.py).
class MaterialsTest final : public SandboxScene {
public:
    struct Options {
        double run_seconds = 0.0;
        std::string capture_path;  // with freeze_after_ticks: one capture of a frozen frame
        long freeze_after_ticks = 0;
    };

    MaterialsTest(moteur::Application& app, const Options& options, bool standalone);

    void update(double dt) override;
    void render(moteur::Renderer& renderer, double alpha) override;
    void draw_controls() override;
    bool stop_requested() const override { return stop_requested_; }

private:
    entt::entity add_mesh(const moteur::Asset<moteur::Mesh>& mesh, glm::vec3 position, glm::vec3 scale, const moteur::Material& material);

    moteur::Application& app_;
    Options options_;
    bool standalone_;
    bool stop_requested_ = false;
    bool capture_done_ = false;
    long ticks_ = 0;
    double elapsed_ = 0.0;
    moteur::World world_;
    moteur::Asset<moteur::Mesh> cube_;
    moteur::Asset<moteur::Mesh> sphere_;
    moteur::Asset<moteur::Mesh> plane_;
    std::optional<moteur::Environment> sky_;
    moteur::Camera3D camera_;
    entt::entity ghost_ = entt::null;
    entt::entity red_ghost_ = entt::null;
    entt::entity cycling_ = entt::null;   // dissolves from whole to gone, round and round
    entt::entity flashing_ = entt::null;  // flashes every second
};
