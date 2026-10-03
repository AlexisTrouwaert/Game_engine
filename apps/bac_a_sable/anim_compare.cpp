#include "anim_compare.hpp"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <SDL3/SDL.h>

#include <fstream>
#include <stdexcept>

#include "moteur/animator.hpp"
#include "moteur/mesh_renderer.hpp"
#include "moteur/paths.hpp"

namespace {

constexpr const char* kKnight = "models/characters/kaykit_adventurers/Knight.glb";
// The weapons the knight carries in its file: one sword and one shield are shown, as everywhere.
const std::vector<std::string> kHidden = {"1H_Sword_Offhand", "2H_Sword", "Badge_Shield", "Rectangle_Shield", "Spike_Shield"};
constexpr float kYaw = 35.0f;          // degrees: three quarters, the sword arm towards the camera
constexpr float kPitch = 10.0f;
constexpr float kFieldOfView = 30.0f;
constexpr int kCaptureFrame = 10;      // everything is loaded and nothing moves: any frame would do
// The background (sRGB): a green the knight does not have (its armour is close to a mid grey), so
// that its silhouette is every pixel of another colour. Blender's render is transparent.
constexpr float kClear[3] = {0.0f, 0.75f, 0.0f};

nlohmann::json vec3(glm::vec3 v) {
    return nlohmann::json::array({v.x, v.y, v.z});
}

}  // namespace

AnimCompare::AnimCompare(moteur::Application& app, const Options& options) : app_(app), options_(options) {
    moteur::Assets& assets = app.assets();
    if (!assets.exists(kKnight)) {
        throw std::runtime_error("--anim-compare: no knight (python tools/models/fetch_test_characters.py)");
    }
    entt::registry& registry = world_.registry();
    knight_ = registry.create();
    registry.emplace<moteur::Transform>(knight_);
    moteur::ModelComponent model{assets.model(kKnight), {}};
    for (const std::string& node : kHidden) {
        model.hide(node);
    }
    const moteur::Aabb bounds = model.model->bounds;
    registry.emplace<moteur::ModelComponent>(knight_, std::move(model));
    // The clip, still, at its moment: seek() puts the clock there, no tick ever moves it.
    moteur::Animator animator = moteur::Animator::create(assets.skeleton(kKnight), assets.clips(kKnight));
    animator.play(options_.clip, {.fade_ticks = 0, .loop = false});
    animator.set_speed(0.0);
    animator.seek(options_.seconds * moteur::kClipTicksPerSecond);
    registry.emplace<moteur::Animator>(knight_, std::move(animator));

    app.renderer().meshes().set_view(moteur::MeshView::BaseColor);
    camera_.set_projection(moteur::Projection::Perspective);
    camera_.set_angles(kYaw, kPitch);
    camera_.set_field_of_view(kFieldOfView);
    camera_.set_target({0.0f, bounds.size().y * 0.5f, 0.0f});
    camera_.set_visible_height(bounds.size().y * 1.5f);
}

AnimCompare::~AnimCompare() {
    app_.renderer().meshes().set_view(moteur::MeshView::Lit);
}

void AnimCompare::update(double dt) {
    elapsed_ += dt;
    if (options_.run_seconds > 0.0 && elapsed_ >= options_.run_seconds) {
        app_.quit();
    }
}

void AnimCompare::render(moteur::Renderer& renderer, double /*alpha*/) {
    ++frames_;
    renderer.set_clear_color(kClear[0], kClear[1], kClear[2]);
    camera_.set_viewport({static_cast<float>(renderer.width()), static_cast<float>(renderer.height())});
    if (!options_.capture_path.empty() && frames_ == kCaptureFrame) {
        renderer.request_capture(options_.capture_path);
        write_description(options_.capture_path + ".json", renderer);
    }
    moteur::MeshRenderer& meshes = renderer.meshes();
    meshes.set_camera(camera_.view_projection(), camera_.position());
    world_.submit(renderer, 1.0f);
}

void AnimCompare::draw_controls() {
    ImGui::TextWrapped("Le chevalier dans « %s » à %.3f s, en couleur de base, comme dans tools/blender/compare_pose.py.",
                       options_.clip.c_str(), options_.seconds);
}

// Everything Blender needs to pose and see the same knight, in the engine's conventions (Y up);
// compare_pose.py converts to Blender's (Z up).
void AnimCompare::write_description(const std::string& path, const moteur::Renderer& renderer) const {
    nlohmann::json doc;
    doc["assets_dir"] = moteur::asset_path("");
    doc["model"] = kKnight;
    doc["hidden_nodes"] = kHidden;
    doc["clip"] = options_.clip;
    doc["seconds"] = options_.seconds;
    doc["image"] = {{"width", renderer.width()}, {"height", renderer.height()}, {"background_srgb", {kClear[0], kClear[1], kClear[2]}}};
    doc["camera"] = {{"eye", vec3(camera_.position())},
                     {"target", vec3(camera_.target())},
                     {"vertical_fov_degrees", camera_.field_of_view()}};
    std::ofstream out(path);
    out << doc.dump(2) << '\n';
    SDL_Log("Pose comparison: %s", path.c_str());
}
