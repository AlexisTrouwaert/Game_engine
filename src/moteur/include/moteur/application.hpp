#pragma once

#include <SDL3/SDL.h>

#include <glm/glm.hpp>

#include <memory>
#include <string>

#include "moteur/assets.hpp"
#include "moteur/audio.hpp"
#include "moteur/camera.hpp"
#include "moteur/debug_tools.hpp"
#include "moteur/input.hpp"
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
    // Hot reload of the assets (development): the assets/ folder of the source tree, whose changes
    // are copied next to the executable and reloaded (see Assets). Empty: no hot reload.
    std::string assets_source_directory;
    // The player's files (key bindings, settings) go into SDL_GetPrefPath(organization, application).
    std::string organization = "moteur";
    std::string application = "moteur";
    // Records what update() reads from the input, tick after tick, into this file when the loop ends.
    std::string record_input_path;
    // Replays such a recording instead of the real input (which is then ignored).
    std::string replay_input_path;
    // false: no sound device is opened (the Audio works, silently).
    bool audio = true;
};

// Implemented by the program driving the engine.
class Game {
public:
    virtual ~Game() = default;

    // Raw SDL event, for what actions do not cover (text input, window events). Game controls go
    // through Application::input(). Events used by the debug interface (a click on one of its
    // windows) do not reach the game.
    virtual void on_event(const SDL_Event&) {}

    // Called at a fixed rate, always with the same dt (seconds). This is where actions are read
    // (Application::input()): what happened since the previous update, each press seen once.
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
    // The time spent until the end of the current updates does not count: after a long load in
    // update(), the next frames do not run the ticks they would owe to catch up.
    void restart_clock() { restart_clock_ = true; }

    SDL_Window* window() const { return window_; }
    Renderer& renderer() { return *renderer_; }
    // Every asset the game loads (the assets/ folder next to the executable).
    Assets& assets() { return *assets_; }
    // The player's input, as actions (see Input).
    Input& input() { return *input_; }
    // Sound and music (see Audio). Its settings (volumes) are the player's: read from audio.json in
    // preferences_directory() at the start, written back at the end if they changed.
    Audio& audio() { return *audio_; }
    // The debug tools (inspector, assets, inputs, audio, states), or null without
    // ApplicationConfig::debug_ui. The game puts them in its menu and draws them (see DebugTools).
    DebugTools* debug_tools() { return debug_tools_.get(); }
    // Where the player's files go (ends with a separator); created if needed. Empty if SDL cannot
    // give one.
    std::string preferences_directory() const;

    // Converts a position in window points (as reported for the mouse) to pixels (as drawn). The two
    // are equal on a normal screen and differ by a factor (2 on many Macs) on a high-density one.
    glm::vec2 to_pixels(glm::vec2 window_point) const;

private:
    std::string audio_settings_path() const;
    void load_audio_settings();
    void save_audio_settings();

    ApplicationConfig config_;
    SDL_Window* window_ = nullptr;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<Assets> assets_;  // after the renderer: released before it
    std::unique_ptr<Input> input_;
    std::unique_ptr<Audio> audio_;  // after the assets: its voices hold sounds, released first
    std::unique_ptr<DebugTools> debug_tools_;  // refers to all of the above, released first
    bool running_ = false;
    bool restart_clock_ = false;
};

}  // namespace moteur
