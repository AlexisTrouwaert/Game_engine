#include "blender_compare.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <fstream>

#include "moteur/mesh_renderer.hpp"
#include "moteur/paths.hpp"

namespace {

// Relative to assets/, in the order of the row, from left to right.
constexpr const char* kModels[] = {
    "models/polyhaven/wine_barrel_01/wine_barrel_01_1k.gltf",
    "models/polyhaven/boulder_01/boulder_01_1k.gltf",
    "models/polyhaven/Lantern_01/Lantern_01_1k.gltf",
    "models/polyhaven/antique_estoc/antique_estoc_1k.gltf",
};
constexpr const char* kEnvironment = "environments/studio_small_09_1k.hdr";
constexpr float kGap = 0.3f;         // metres between two objects of the row
constexpr float kPitch = 15.0f;      // degrees below the horizon: the objects seen a little from above
constexpr float kFieldOfView = 30.0f;
constexpr int kCaptureFrame = 10;    // everything is loaded and nothing moves: any frame would do
// The background, as the tone-mapped image shows it (sRGB): a neutral grey, the one Blender's
// transparent render is laid on for the comparison.
constexpr float kClearGrey = 0.5f;

nlohmann::json vec3(glm::vec3 v) {
    return nlohmann::json::array({v.x, v.y, v.z});
}

}  // namespace

BlenderCompare::BlenderCompare(moteur::Application& app, const Options& options, bool standalone)
    : app_(app), options_(options), standalone_(standalone) {
    moteur::Renderer& renderer = app.renderer();
    sphere_mesh_ = moteur::Mesh::create(renderer, moteur::make_sphere(1.0f, 64, 32), "compare.sphere");
    // Missing test files are left out (a placeholder would spoil the comparison).
    if (app.assets().exists(kEnvironment)) {
        environment_ = app.assets().environment(kEnvironment);
        environment_path_ = kEnvironment;
    } else {
        SDL_Log("Blender comparison: no '%s' (python tools/models/fetch_test_models.py downloads it)", kEnvironment);
    }

    // The row: each object standing on y = 0, centred in depth, kGap apart.
    float cursor = 0.0f;
    for (const char* path : kModels) {
        if (!app.assets().exists(path)) {
            SDL_Log("Blender comparison: no '%s'", path);
            continue;
        }
        const moteur::Asset<moteur::Model> model = app.assets().model(path);
        const moteur::Aabb bounds = model->bounds;
        const glm::vec3 offset(cursor - bounds.min.x, -bounds.min.y, -bounds.center().z);
        cursor += bounds.size().x + kGap;
        models_.push_back({path, model, offset});
    }
    // Two spheres whose look is known: a matte white plastic and a polished gold (the glTF sample
    // values: base color of gold in linear terms).
    const float radius = 0.25f;
    spheres_.push_back({{cursor + radius, radius, 0.0f}, radius, {0.8f, 0.8f, 0.8f}, 0.0f, 0.5f});
    cursor += 2.0f * radius + kGap;
    spheres_.push_back({{cursor + radius, radius, 0.0f}, radius, {1.0f, 0.766f, 0.336f}, 1.0f, 0.3f});
    cursor += 2.0f * radius;

    // Centred on x = 0.
    const float half = cursor * 0.5f;
    float height = 2.0f * radius;
    for (Placed& placed : models_) {
        placed.offset.x -= half;
        height = std::max(height, placed.model->bounds.size().y);
    }
    for (Sphere& sphere : spheres_) {
        sphere.center.x -= half;
    }

    camera_.set_projection(moteur::Projection::Perspective);
    camera_.set_angles(0.0f, kPitch);  // from +Z, looking towards -Z
    camera_.set_field_of_view(kFieldOfView);
    camera_.set_target({0.0f, height * 0.4f, 0.0f});
    camera_.set_visible_height(std::max(height * 1.5f, cursor * 1.15f * 9.0f / 16.0f));
}

void BlenderCompare::update(double dt) {
    elapsed_ += dt;
    if (options_.run_seconds > 0.0 && elapsed_ >= options_.run_seconds) {
        if (standalone_) {
            app_.quit();
        } else {
            stop_requested_ = true;
        }
    }
}

void BlenderCompare::render(moteur::Renderer& renderer, double /*alpha*/) {
    ++frames_;
    if (!options_.capture_path.empty() && frames_ == kCaptureFrame) {
        renderer.request_capture(options_.capture_path);
        write_description(options_.capture_path + ".json", renderer);
    }
    renderer.set_clear_color(kClearGrey, kClearGrey, kClearGrey);
    camera_.set_viewport({static_cast<float>(renderer.width()), static_cast<float>(renderer.height())});

    moteur::MeshRenderer& meshes = renderer.meshes();
    meshes.set_camera(camera_.view_projection(), camera_.position());
    meshes.set_sun({0.0f, 1.0f, 0.0f}, glm::vec3(1.0f), 0.0f);  // the environment's light only
    meshes.set_environment(environment_ ? &*environment_ : nullptr, environment_intensity_);
    for (const Placed& placed : models_) {
        meshes.draw(*placed.model, glm::translate(glm::mat4(1.0f), placed.offset));
    }
    for (const Sphere& sphere : spheres_) {
        moteur::Material material;
        material.base_color = glm::vec4(sphere.color, 1.0f);
        material.metallic = sphere.metallic;
        material.roughness = sphere.roughness;
        const glm::mat4 world = glm::scale(glm::translate(glm::mat4(1.0f), sphere.center), glm::vec3(sphere.radius));
        meshes.draw(sphere_mesh_, world, material);
    }
}

void BlenderCompare::draw_controls() {
    ImGui::TextWrapped("Les modèles de test et deux sphères, éclairés par l'environnement seul, comme dans "
                       "tools/blender/compare_render.py.");
    if (!environment_) {
        ImGui::TextWrapped("Environnement absent : python tools/models/fetch_test_models.py");
    }
    ImGui::PushItemWidth(200.0f);
    ImGui::SliderFloat("Intensité de l'environnement", &environment_intensity_, 0.0f, 4.0f, "%.2f");
    anti_aliasing_combo(app_.renderer());
    ImGui::PopItemWidth();
}

// Everything Blender needs to build the same image, in the engine's conventions (Y up, metres,
// linear colors); tools/blender/compare_render.py converts to Blender's (Z up).
void BlenderCompare::write_description(const std::string& path, const moteur::Renderer& renderer) const {
    nlohmann::json doc;
    doc["assets_dir"] = moteur::asset_path("");
    doc["image"] = {{"width", renderer.width()}, {"height", renderer.height()}, {"background_srgb", kClearGrey}};
    doc["tone_mapping"] = {{"view_transform", "Khronos PBR Neutral"}, {"exposure", renderer.exposure()}};
    doc["environment"] = {{"path", environment_path_}, {"intensity", environment_intensity_}};
    doc["camera"] = {{"eye", vec3(camera_.position())},
                     {"target", vec3(camera_.target())},
                     {"vertical_fov_degrees", camera_.field_of_view()}};
    nlohmann::json models = nlohmann::json::array();
    for (const Placed& placed : models_) {
        models.push_back({{"path", placed.path}, {"offset", vec3(placed.offset)}});
    }
    doc["models"] = models;
    nlohmann::json spheres = nlohmann::json::array();
    for (const Sphere& sphere : spheres_) {
        spheres.push_back({{"center", vec3(sphere.center)},
                           {"radius", sphere.radius},
                           {"base_color_linear", vec3(sphere.color)},
                           {"metallic", sphere.metallic},
                           {"roughness", sphere.roughness}});
    }
    doc["spheres"] = spheres;
    std::ofstream file(path);
    file << doc.dump(2) << '\n';
    if (!file) {
        SDL_Log("Blender comparison: could not write %s", path.c_str());
    }
}
