#pragma once

#include <SDL3/SDL.h>

#include <glm/glm.hpp>

#include <memory>
#include <string>

#include "moteur/camera.hpp"
#include "moteur/renderer.hpp"

namespace moteur {

struct ApplicationConfig {
    std::string title = "moteur";
    int width = 1280;   // points (see pixel_width)
    int height = 720;
    // > 0: the window is sized so that it has exactly this many pixels, whatever the screen's
    // density (a 2x Retina screen gets a window of half as many points). For captures compared
    // between machines. Exact for whole densities (1, 2); other densities round, and say so.
    int pixel_width = 0;
    int pixel_height = 0;
    double fixed_hz = 60.0;        // logic update rate
    double max_frame_time = 0.25;  // seconds; caps catch-up after a stall
    bool vsync = true;
#ifdef NDEBUG
    bool gpu_debug = false;
#else
    bool gpu_debug = true;
#endif
    // Logs a summary of CPU time per frame when the loop ends (see Application::run).
    bool report_performance = false;
    // Measures the GPU time of each part of the frame (Renderer::set_gpu_timing(): slows it down).
    bool gpu_timing = false;
    // Dear ImGui for debug windows and menus (see DebugUi). The game calls ImGui:: from render().
    bool debug_ui = false;
    std::string debug_ui_font;  // TrueType file; empty for ImGui's built-in font (ASCII only)
    float debug_ui_font_size = 16.0f;
};

// Implemented by the program driving the engine.
class Game {
public:
    virtual ~Game() = default;

    // Raw SDL event. Temporary: an input abstraction will replace it. Events used by the debug
    // interface (a click on one of its windows) do not reach the game.
    virtual void on_event(const SDL_Event&) {}

    // Called at a fixed rate, always with the same dt (seconds).
    virtual void update(double dt) = 0;

    // Called once per drawable frame. Only *record* what to draw here, for example with
    // renderer.sprites().draw(...): nothing is sent to the GPU until the frame ends. With
    // ApplicationConfig::debug_ui, ImGui:: calls are allowed here too.
    // alpha in [0, 1) is how far the frame is between the last update and the next one,
    // for render interpolation.
    virtual void render(Renderer& renderer, double alpha) = 0;
};

class Application {
public:
    // Throws std::runtime_error if SDL, the window or the GPU cannot be created.
    explicit Application(const ApplicationConfig& config);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Runs the main loop until quit() is called or the window is closed.
    //
    // The CPU time of each frame is measured, excluding the time spent waiting for the display
    // (vsync). The window title shows the FPS and the latest averages once per second. With
    // report_performance, a summary (mean, 99th percentile and maximum, per phase) is logged at
    // the end. The first frames are left out of the summary: they include one-time warm-up work.
    void run(Game& game);
    void quit() { running_ = false; }

    SDL_Window* window() const { return window_; }
    Renderer& renderer() { return *renderer_; }

    // Converts a position in window points (as reported for the mouse) to pixels (as drawn). The two
    // are equal on a normal screen and differ by a factor (2 on many Macs) on a high-density one.
    glm::vec2 to_pixels(glm::vec2 window_point) const;

private:
    ApplicationConfig config_;
    SDL_Window* window_ = nullptr;
    std::unique_ptr<Renderer> renderer_;
    bool running_ = false;
};

}  // namespace moteur
