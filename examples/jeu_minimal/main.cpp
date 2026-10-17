// The smallest program built on the engine (milestone 8, part 3): a window, a lit cube that the
// arrow keys (or the left stick) turn, a beep on Space (or the A button), Escape to quit. Its
// CMakeLists.txt shows how a game consumes the engine; moteur_doc/GUIDE.md explains each step.
//
//   jeu_minimal [--run-seconds S]

#include <SDL3/SDL.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "moteur/application.hpp"
#include "moteur/camera3d.hpp"
#include "moteur/color.hpp"
#include "moteur/mesh.hpp"
#include "moteur/mesh_renderer.hpp"
#include "moteur/paths.hpp"
#include "moteur/version.hpp"

namespace {

constexpr float kTurnSpeed = 2.0f;  // radians per second at full tilt

class JeuMinimal : public moteur::Game {
public:
    JeuMinimal(moteur::Application& app, double run_seconds)
        : app_(app),
          run_seconds_(run_seconds),
          cube_(moteur::Mesh::create(app.renderer(), moteur::make_cube(glm::vec3(1.0f)), "cube")),
          ground_(moteur::Mesh::create(app.renderer(), moteur::make_plane(glm::vec2(8.0f)), "sol")),
          beep_(app.assets().sound("audio/bip.wav")) {
        // 1. The actions the game reads, then their bindings (a file of the game's assets).
        moteur::Input& input = app.input();
        turn_ = input.add_axis("tourner");
        beep_action_ = input.add_button("bip");
        quit_ = input.add_button("quitter");
        input.load_bindings(moteur::read_text_file(app.assets().file_path("input/jeu_minimal.json")),
                            "input/jeu_minimal.json");

        // 2. The scene: a camera looking down at the cube, the sun.
        camera_.set_projection(moteur::Projection::Perspective);
        camera_.set_angles(45.0f, 35.0f);
        camera_.set_field_of_view(30.0f);
        camera_.set_target(glm::vec3(0.0f, 0.5f, 0.0f));
        camera_.set_visible_height(4.0f);
        app.renderer().meshes().set_sun(glm::normalize(glm::vec3(-0.4f, 1.0f, 0.3f)), glm::vec3(1.0f), 3.0f);

        cube_material_.base_color = glm::vec4(moteur::srgb_to_linear(glm::vec3(0.85f, 0.45f, 0.2f)), 1.0f);
        cube_material_.roughness = 0.4f;
        ground_material_.base_color = glm::vec4(moteur::srgb_to_linear(glm::vec3(0.35f)), 1.0f);
    }

    // 3. The logic, at a fixed rate: read the actions, change the state.
    void update(double dt) override {
        const moteur::Input& input = app_.input();
        previous_angle_ = angle_;
        angle_ += input.axis(turn_).x * kTurnSpeed * static_cast<float>(dt);
        if (input.pressed(beep_action_)) {
            app_.audio().play(beep_);
        }
        elapsed_ += dt;
        if (input.pressed(quit_) || (run_seconds_ > 0.0 && elapsed_ >= run_seconds_)) {
            app_.quit();
        }
    }

    // 4. The drawing, once per frame, between two states of the logic (alpha).
    void render(moteur::Renderer& renderer, double alpha) override {
        renderer.set_clear_color(0.05f, 0.06f, 0.08f);
        camera_.set_viewport({static_cast<float>(renderer.width()), static_cast<float>(renderer.height())});
        moteur::MeshRenderer& meshes = renderer.meshes();
        meshes.set_camera(camera_.view_projection(), camera_.position());
        const float angle = previous_angle_ + (angle_ - previous_angle_) * static_cast<float>(alpha);
        const glm::mat4 world = glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f, 0.0f)), angle,
                                            glm::vec3(0.0f, 1.0f, 0.0f));
        meshes.draw(cube_, world, cube_material_);
        meshes.draw(ground_, glm::mat4(1.0f), ground_material_);
    }

private:
    moteur::Application& app_;
    double run_seconds_;
    double elapsed_ = 0.0;
    moteur::Mesh cube_;
    moteur::Mesh ground_;
    moteur::Material cube_material_;
    moteur::Material ground_material_;
    moteur::Asset<moteur::Sound> beep_;
    moteur::Camera3D camera_;
    moteur::ActionId turn_ = moteur::kNoAction;
    moteur::ActionId beep_action_ = moteur::kNoAction;
    moteur::ActionId quit_ = moteur::kNoAction;
    float angle_ = 0.6f;
    float previous_angle_ = 0.6f;
};

}  // namespace

int main(int argc, char* argv[]) {
    double run_seconds = 0.0;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--run-seconds" && i + 1 < argc) {
            run_seconds = std::atof(argv[++i]);
        }
    }
    std::cout << moteur::build_description() << '\n';
    try {
        moteur::ApplicationConfig config;
        config.title = "Jeu minimal";
        config.organization = "moteur";
        config.application = "jeu_minimal";  // the player's files: SDL_GetPrefPath("moteur", "jeu_minimal")
        moteur::Application app(config);
        JeuMinimal game(app, run_seconds);
        app.run(game);
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Jeu minimal", e.what(), nullptr);
        return 1;
    }
    return 0;
}
