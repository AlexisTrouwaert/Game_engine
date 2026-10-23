#pragma once

#include <SDL3/SDL.h>

#include <glm/glm.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "moteur/assets.hpp"
#include "moteur/audio.hpp"
#include "moteur/console.hpp"
#include "moteur/localization.hpp"
#include "moteur/data_table.hpp"
#include "moteur/debug_tools.hpp"
#include "moteur/display.hpp"
#include "moteur/input.hpp"
#include "moteur/log_file.hpp"
#include "moteur/profiler.hpp"
#include "moteur/renderer.hpp"

namespace moteur {

/// How the Application is created: read once, by its constructor (a copy is kept). Every field has
/// a default that suits a game in development; a game sets what it needs, then passes it to
/// Application::Application().
struct ApplicationConfig {
    /// The window's title (the FPS and frame times are appended to it once per second), and the
    /// title of the crash reporter's message box.
    std::string title = "moteur";
    /// The window's size at creation, in points (see pixel_width): resizable afterwards.
    ///@{
    int width = 1280;
    int height = 720;
    ///@}
    /// > 0 (both): the window is sized so that it has exactly this many pixels, whatever the
    /// screen's density (a 2x Retina screen gets a window of half as many points), instead of
    /// width and height. For captures compared between machines. Exact for whole densities (1, 2);
    /// other densities round, and say so in the log.
    ///@{
    int pixel_width = 0;
    int pixel_height = 0;
    ///@}
    /// The screen the window opens on, as an index into the connected screens (0: the first);
    /// negative: the system's choice. A missing screen falls back to the system's choice (logged).
    /// The environment variable MOTEUR_WINDOW_DISPLAY, when set to an index, replaces it: the
    /// checks and tools set it to open their windows on a screen of their own (the verification
    /// scripts use the second screen when there is one). The player's display settings
    /// (display_settings), applied afterwards, still choose their screen.
    int window_display = -1;
    int tick_rate = 60;            ///< logic ticks per second (see moteur::tick_rate()): 1 to 1000
    double max_frame_time = 0.25;  ///< seconds; caps catch-up after a stall
    /// Waits for the screen's refresh to show each frame (RendererConfig::vsync). With
    /// display_settings, the player's choice (DisplaySettings::vsync) replaces it.
    bool vsync = true;
    /// The GPU backend's validation layer (RendererConfig::debug): on by default in the builds
    /// without NDEBUG (Debug), off otherwise. Slows the GPU calls down.
#ifdef NDEBUG
    bool gpu_debug = false;
#else
    bool gpu_debug = true;
#endif
    /// Logs a summary of CPU time per frame, and the asset statistics, when the loop ends (see
    /// Application::run()).
    bool report_performance = false;
    /// Writes the measurement report of the run into this JSON file when the loop ends (milestone 8,
    /// part 4): CPU time and frame interval (mean, percentiles, maximum, frames over 16.7 and 33.3 ms),
    /// phases, profiler zones, draw counters, GPU time per pass (with gpu_timing), process and GPU
    /// memory (peaks), and the game's own values (Application::report_value()), under English keys
    /// (`"version": 2`; version 1 had French ones). A failure to write it is logged. Empty: none.
    std::string report_json_path;
    /// Measures the GPU time of each part of the frame (Renderer::set_gpu_timing(): slows it down).
    bool gpu_timing = false;
    /// Dear ImGui for debug windows and menus (see DebugUi). The game calls ImGui:: from render().
    /// Ignored in the shipping configuration (MOTEUR_SHIPPING), which has no ImGui at all.
    bool debug_ui = false;
    std::string debug_ui_font;  ///< TrueType file; empty for ImGui's built-in font (ASCII only)
    float debug_ui_font_size = 16.0f;  ///< of debug_ui_font, in pixels
    /// Hot reload of the assets (development): the assets/ folder of the source tree, whose changes
    /// are copied next to the executable and reloaded (see Assets). Empty: no hot reload. Also where
    /// the `.meta` files of new assets are written (see AssetRegistry::scan()).
    std::string assets_source_directory;
    /// Other folders of assets, read in place without being copied (development; ignored in the
    /// shipping configuration, whose archives hold what was prepared from them): another game's
    /// assets that this program uses, as the slice uses the ARPG's (milestone 9, part 4). Mounted
    /// with mount_asset_folder(), identified with the others, hot reloaded in place.
    std::vector<std::string> asset_folders;
    /// The name of the frozen asset registry the preparation writes (see AssetRegistry), read from
    /// the archives in the shipped game.
    static constexpr const char* kRegistryFile = "asset_registry.mreg";
    /// The player's files (key bindings, settings, log, crash reports) go into
    /// SDL_GetPrefPath(organization, application) (see Application::preferences_directory()): on
    /// Windows `%APPDATA%/organization/application/`. Set both before shipping: they name the
    /// player's folder for good.
    ///@{
    std::string organization = "moteur";
    std::string application = "moteur";
    ///@}
    /// Records what update() reads from the input, tick after tick (with the console's game commands
    /// of each tick), into this JSON file when the loop ends. Ignored while replaying. Empty: none.
    std::string record_input_path;
    /// Replays such a recording instead of the real input (which is then ignored). Application::run()
    /// throws if the file cannot be read or is not a valid recording. Empty: the real input.
    std::string replay_input_path;
    /// The replay starts at this tick of the recording (a save loaded at that tick goes on from there).
    std::size_t replay_start = 0;
    /// false: no sound device is opened (the Audio works, silently).
    bool audio = true;
    /// Console lines run before the first tick (--exec, +set): game commands among them run at the
    /// first tick and are recorded like the others. In the shipping configuration only the `set`
    /// lines are run (the others are refused with a warning): the shipped game has no console.
    std::vector<std::string> startup_commands;
    /// The log written to log.txt in the player's preferences (see LogFile).
    bool log_file = true;
    /// Crash reports in the player's preferences, crash_reports/ (see install_crash_reporter()).
    bool crash_reports = true;
    /// What the player reads after a crash, in the player's language: the game's text, `{report}`
    /// standing for the report's path. Empty: an English default (kDefaultCrashMessage).
    std::string crash_message;
    /// What the player reads when the GPU is lost (see Renderer::device_lost()), in the player's
    /// language, `{report}` for the report's path. Empty: an English default (kDefaultGpuLostMessage).
    std::string gpu_lost_message;
    /// The player's display settings (display.json in the preferences, see DisplaySettings):
    /// read and applied at the start (affichage.json, the file's name until 2026-10-17, is read when
    /// display.json is missing), written by Application::apply_display(). For the game; the tools
    /// and the checks leave it off (their captures must not depend on the player's settings).
    bool display_settings = false;
    /// The archives of the game (milestone 8, part 7): every .pak of this folder of the program
    /// (base_path() + this) is mounted before anything is loaded, unless archives are mounted
    /// already. A damaged archive makes the constructor throw. In the shipping configuration, once
    /// archives are there, the program's files are read from them only (set_archives_only()).
    /// A missing folder, or empty: none.
    std::string archives_directory = "archives";
    /// The language of the texts (milestone 9 bis, part 5), whatever the player chose: the checks
    /// and captures, which must not depend on the machine (`--language` of the programs). Empty: the
    /// player's choice (the archived variable `language`), else the system's preference among the
    /// languages offered (Localization::preferred_language()).
    std::string language;
};

/// The import cache of a program in development: `.imported/` next to its assets source folder
/// (`game/assets` -> `game/.imported/`), shared by its builds. See Assets::set_import_cache().
std::string import_cache_folder(const std::string& assets_source_directory);

/// Implemented by the program driving the engine: Application::run() calls it, on the main thread,
/// every frame (on_event(), then update() for each tick due, then render()). A StateStack is a Game
/// made of screens.
class Game {
public:
    virtual ~Game() = default;

    /// Raw SDL event, for what actions do not cover (text input, window events). Game controls go
    /// through Application::input(). Events used by the debug interface (a click on one of its
    /// windows, the console's key) do not reach the game. Called before the frame's ticks.
    virtual void on_event(const SDL_Event&) {}

    /// Called at a fixed rate, always with the same dt (seconds: 1 / tick_rate()). This is where
    /// actions are read (Application::input()): what happened since the previous update, each press
    /// seen once.
    virtual void update(double dt) = 0;

    /// Asked before each tick: true when that tick only waits for work in the background (a loading
    /// screen waiting for files decoded on the job system). Such a tick still calls update(), but
    /// reads no input, runs no console game command, is neither recorded nor replayed and does not
    /// count: how many there are depends on the machine, and the replays and saves must not
    /// (milestone 8, part 5).
    virtual bool waiting() { return false; }

    /// Called once per drawable frame (not while the window is minimized). Only *record* what to
    /// draw here, for example with renderer.sprites().draw(...): nothing is sent to the GPU until
    /// the frame ends. With ApplicationConfig::debug_ui, ImGui:: calls are allowed here too.
    /// `alpha` in [0, 1) is how far the frame is between the last update and the next one,
    /// for render interpolation (see interpolate()).
    virtual void render(Renderer& renderer, double alpha) = 0;
};

/// The engine's entry point: owns SDL, the window, the Renderer, and every engine service a game
/// uses (Assets, DataTables, Input, Audio, Console, Profiler, the log and the crash reports), and
/// runs the main loop (run()). One per process, created and used on the main thread; the
/// references it gives stay valid until it is destroyed.
///
/// @code{.cpp}
/// moteur::ApplicationConfig config;
/// config.title = "My game";
/// moteur::Application app(config);
/// MyGame game(app);
/// app.run(game);
/// return app.ended_on_error() ? 1 : 0;
/// @endcode
class Application {
public:
    /// Initializes SDL, opens the log and installs the crash reporter (ApplicationConfig::log_file,
    /// ApplicationConfig::crash_reports), mounts the archives, sets tick_rate(), creates the
    /// window, the Renderer and the services, reads the player's settings (console variables, audio,
    /// display) and applies them. Throws std::runtime_error if SDL, the window or the GPU cannot be
    /// created, or a game archive is damaged; std::invalid_argument for a tick_rate outside [1,
    /// 1000]. A failure undoes what was set up (SDL included, and the crash reporter if it installed
    /// it) before the exception leaves. The crash reporter is not installed again when the game (or
    /// an earlier Application) already installed it.
    explicit Application(const ApplicationConfig& config);
    /// Writes the player's settings back (audio.json if they changed, variables.cfg, imgui.ini),
    /// releases the services, then the Renderer, the window and SDL, and last uninstalls the crash
    /// reporter if it installed it (uninstall_crash_reporter()). Every Asset handle and engine
    /// object the game made must be gone by then.
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    /// Runs the main loop until quit() is called, the window is closed, or a fatal error ends it
    /// (see ended_on_error()). Each frame: hot reload, events, the ticks due (see
    /// Game::update()), then Game::render() and the frame's submission, then the audio. The
    /// startup commands run first; the input recording is written, and the reports, when the loop
    /// ends.
    ///
    /// Error policy (milestone 9, part 2; GUIDE.md, chapter 0): exceptions belong to loading. Before
    /// the loop, run() may throw (std::runtime_error for a replay that cannot be read); inside it,
    /// no exception crosses a frame. One escaping the game (on_event(), update(), waiting(),
    /// render()) or a service between two frames is a bug: it is a controlled crash, reported like
    /// one (report_fatal_error(), with ApplicationConfig::crash_message), and the loop ends with
    /// ended_on_error(). A GPU failure inside a frame skips what it concerns for that frame only
    /// (Renderer::end_frame()); a lost device ends the loop the same way.
    ///
    /// The CPU time of each frame is measured, excluding the time spent waiting for the display
    /// (vsync). The window title shows the FPS and the latest averages once per second. With
    /// report_performance, a summary (mean, 99th percentile and maximum, per phase) is logged at
    /// the end. The first frames (60) are left out of the summary: they include one-time warm-up work.
    void run(Game& game);
    /// Ends run() after the current frame. Also the console's `quit` command.
    void quit() { running_ = false; }
    /// True when run() ended on a fatal error (the GPU device lost, or an exception escaping the
    /// game inside a frame; reported): the game's main returns a failure code.
    bool ended_on_error() const { return ended_on_error_; }
    /// The time spent until the end of the current updates does not count: after a long load in
    /// update(), the next frames do not run the ticks they would owe to catch up.
    void restart_clock() { restart_clock_ = true; }

    /// A value of the game for the measurement report (report_json_path), under `values`: a load
    /// time, a count. The last value given for a name is kept.
    void report_value(const std::string& name, double value) { report_values_[name] = value; }

    /// The display settings in force: the player's (ApplicationConfig::display_settings), or the
    /// defaults with ApplicationConfig::vsync, until apply_display() changes them.
    const DisplaySettings& display() const { return display_; }
    /// Applies these settings now (window mode, screen, vsync, frame limit, render scale,
    /// anti-aliasing, shadows) and, with ApplicationConfig::display_settings, writes them to
    /// display.json (a failure is logged). A screen index past the screens connected is brought to
    /// the last one. Between frames (from update() or an event, not from render()); it may move and
    /// resize the window, so ask the player to confirm what changes_screen() reports.
    void apply_display(const DisplaySettings& settings);

    /// The window, owned by the Application (for SDL calls the engine does not wrap).
    SDL_Window* window() const { return window_; }
    /// What draws the frames (see Renderer); the same object passed to Game::render().
    Renderer& renderer() { return *renderer_; }
    /// Every asset the game loads (the assets/ folder next to the executable).
    Assets& assets() { return *assets_; }
    /// The game's data tables (see DataTables): the game registers them, then loads them; a changed
    /// file of a table reloads it (hot reload).
    DataTables& data() { return *data_; }
    /// Where the time goes (see Profiler); its window is DEBUG > Profiler.
    Profiler& profiler() { return profiler_; }
    /// Commands and variables (see Console); its window is DEBUG > Console (key under Escape).
    Console& console() { return *console_; }
    /// Pauses the ticks (the frames go on); step() runs some ticks while paused. Tool commands
    /// `pause` and `step`: the logic itself is unchanged, only when it runs.
    void set_paused(bool paused) { paused_ = paused; }
    /// Whether the ticks are paused (set_paused(), or the console's `pause` and `step`).
    bool paused() const { return paused_; }
    /// While paused, runs `ticks` more ticks, at most 8 per frame. Ignored when not paused (unlike
    /// the console's `step`, it does not pause).
    void step(int ticks) { pending_steps_ += ticks; }
    /// The player's input, as actions (see Input).
    Input& input() { return *input_; }
    /// The texts of the player in their language (see Localization): `assets/locales/` read at the
    /// start (the engine's `moteur` strings and the game's resources listed in languages.json),
    /// read again when one of their files changes (development).
    ///@{
    Localization& localization() { return *localization_; }
    const Localization& localization() const { return *localization_; }
    ///@}
    /// Changes the language at once (the interface redoes its texts: Localization::generation())
    /// and keeps the player's choice (variable `language`). false for a language not offered.
    bool set_language(std::string_view language);
    /// Sound and music (see Audio). Its settings (volumes) are the player's: read from audio.json in
    /// preferences_directory() at the start, written back at the end if they changed.
    Audio& audio() { return *audio_; }
    /// The debug tools (inspector, assets, inputs, audio, states), or null without
    /// ApplicationConfig::debug_ui, and always null in the shipping configuration. The game puts
    /// them in its menu and draws them (see DebugTools).
#if defined(MOTEUR_SHIPPING)
    DebugTools* debug_tools() { return nullptr; }  // the shipped game has no debug tools
#else
    DebugTools* debug_tools() { return debug_tools_.get(); }
#endif
    /// Where the player's files go (ends with a separator; see ApplicationConfig::organization);
    /// created if needed. Empty, with a log line, if SDL cannot give one. Asks SDL at each call.
    std::string preferences_directory() const;

    /// Converts a position in window points (as reported for the mouse) to pixels (as drawn). The two
    /// are equal on a normal screen and differ by a factor (2 on many Macs) on a high-density one.
    /// Uses the window's current sizes (see window_to_pixels()).
    glm::vec2 to_pixels(glm::vec2 window_point) const;

    /// Writes the player's audio settings (`audio.json` in the preferences: Audio::settings_json())
    /// now, if they changed since they were loaded or last written: an options menu calls it when the
    /// player leaves it, so that a crash loses nothing. The Application also writes them when it
    /// ends. A failure is logged, never thrown.
    void save_audio_settings();

private:
    std::string audio_settings_path() const;
    void load_audio_settings();
    void add_console_commands();
    std::string variables_path() const;
    void build_asset_registry();
    /// Runs `body`; an exception escaping it is a fatal error of the frame (see run()): reported,
    /// the loop ends. Returns false then.
    template <typename Body>
    bool guarded(const char* phase, Body&& body);
    void fail(const std::string& what);

    ApplicationConfig config_;
    SDL_Window* window_ = nullptr;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<Assets> assets_;  ///< after the renderer: released before it
    std::unique_ptr<DataTables> data_;
    std::unique_ptr<Console> console_;
    LogFile log_;
    Profiler profiler_;
    Variable* time_scale_ = nullptr;  ///< "time.scale"
    Variable* profile_enabled_ = nullptr;  ///< "profile.enabled"
    bool paused_ = false;
    int pending_steps_ = 0;
    bool swallow_text_ = false;  ///< the console's key was pressed: its character is not typed
    std::unique_ptr<Localization> localization_;
    /// The crash and GPU-lost messages in the player's language (`moteur-crash-message`,
    /// `moteur-gpu-lost-message`), when the strings have them.
    void localize_fatal_messages();
    std::string gpu_lost_message_;
    Variable* language_ = nullptr;  ///< "language": the player's choice (archived)
    std::unique_ptr<Input> input_;
    std::unique_ptr<Audio> audio_;  ///< after the assets: its voices hold sounds, released first
#if !defined(MOTEUR_SHIPPING)
    std::unique_ptr<DebugTools> debug_tools_;  ///< refers to all of the above, released first
#endif
    bool running_ = false;
    bool ended_on_error_ = false;
    bool restart_clock_ = false;
    std::map<std::string, double> report_values_;
    DisplaySettings display_;
    bool display_applied_ = false;
    std::string display_path() const;
};

}  // namespace moteur
