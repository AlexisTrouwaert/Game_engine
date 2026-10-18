#include "moteur/assert.hpp"
#include "moteur/application.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <stdexcept>

#include <nlohmann/json.hpp>

#if !defined(MOTEUR_SHIPPING)
#include "moteur/debug_ui.hpp"
#endif
#include "moteur/fixed_timestep.hpp"
#include "moteur/frame_stats.hpp"
#include "moteur/crash.hpp"
#include "moteur/mesh_renderer.hpp"
#include "moteur/paths.hpp"
#include "moteur/process_memory.hpp"
#include "moteur/version.hpp"

namespace moteur {

namespace {

// Frames ignored at the start of the performance summary (pipeline warm-up, first uploads).
constexpr int kWarmupFrames = 60;

// The Application that installed the crash reporter (there is one reporter per process), which
// uninstalls it when it fails to start or ends; null when the game installed it itself, or nobody.
const Application* crash_reporter_owner = nullptr;

void release_crash_reporter(const Application* application) {
    if (crash_reporter_owner == application) {
        uninstall_crash_reporter();
        crash_reporter_owner = nullptr;
    }
}

}  // namespace

Application::Application(const ApplicationConfig& config) : config_(config) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }
    // From here, a failure undoes what was set up (the catch below), SDL included: the program can
    // report the error and end, or try again.
    const int previous_tick_rate = tick_rate();
    const bool mounts_archives = !config_.archives_directory.empty() && mounted_archives() == 0;
    const bool previous_archives_only = archives_only();
    try {
        // The log first, so that everything after goes to the file; the console sees it too.
        console_ = std::make_unique<Console>();
        if (config_.log_file) {
            if (const std::string directory = preferences_directory(); !directory.empty()) {
                log_.open(directory);
            }
        }
        log_.set_console(console_.get());
        SDL_Log("%s, SDL %s, %s", build_description().c_str(), sdl_version().c_str(), SDL_GetPlatform());
        if (config_.crash_reports) {
            if (const std::string directory = preferences_directory(); !directory.empty() && crash_reports_directory().empty()) {
                install_crash_reporter(directory + "crash_reports/", config_.title, config_.crash_message);
                crash_reporter_owner = this;
            }
            set_crash_context("engine", build_description());
            set_crash_context("system", std::string(SDL_GetPlatform()) + ", " + std::to_string(SDL_GetNumLogicalCPUCores()) +
                                            " logical cores, " + std::to_string(SDL_GetSystemRAM()) + " MB of memory");
        }
        if (mounts_archives) {
            const std::string directory = base_path() + config_.archives_directory;
            std::vector<std::string> errors;
            const std::size_t mounted = mount_archives(directory, errors);
            for (const std::string& error : errors) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.c_str());
            }
            if (!errors.empty()) {
                throw std::runtime_error("Damaged game archives: " + errors.front() +
                                         ". Reinstalling the game (or verifying its files) should repair them.");
            }
#if defined(MOTEUR_SHIPPING)
            if (mounted > 0) {
                set_archives_only(true);  // what was not prepared does not exist
            } else {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Archives: none in '%s': the files of the folder are read (a trial, not a delivery)",
                            directory.c_str());
            }
#else
            (void)mounted;
#endif
        }
        set_tick_rate(config_.tick_rate);  // before anything converts a duration into ticks
        Profiler::set_current(&profiler_);
        time_scale_ = &console_->variables().add_float("time.scale", 1.0f, 0.05f, 16.0f,
                                                      "speed of time (ticks keep their length; tool)");
        profile_enabled_ = &console_->variables().add_bool("profile.enabled", true,
                                                           "measures the zones every frame (Profiler window, --report, profile start)");
        if (const std::string path = variables_path(); !path.empty() && SDL_GetPathInfo(path.c_str(), nullptr)) {
            try {
                console_->variables().set_pending(read_text_file(path));
            } catch (const std::exception& e) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Variables: %s", e.what());
            }
        }

        // The screen asked for (the config, or MOTEUR_WINDOW_DISPLAY for the checks): the window is
        // created hidden, centered on it, then shown, so that it never flashes on another screen.
        int display_index = config_.window_display;
        if (const char* forced = SDL_getenv("MOTEUR_WINDOW_DISPLAY"); forced != nullptr && *forced != '\0') {
            display_index = SDL_atoi(forced);
        }
        SDL_DisplayID window_display = 0;
        if (display_index >= 0) {
            int count = 0;
            if (SDL_DisplayID* ids = SDL_GetDisplays(&count)) {
                if (display_index < count) {
                    window_display = ids[display_index];
                } else {
                    SDL_Log("Window: screen %d asked for, %d connected: the system chooses", display_index, count);
                }
                SDL_free(ids);
            }
        }
        window_ = SDL_CreateWindow(config_.title.c_str(), config_.width, config_.height,
                                   SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                       (window_display != 0 ? SDL_WINDOW_HIDDEN : 0));
        if (window_ == nullptr) {
            throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        }
        if (window_display != 0) {
            SDL_SetWindowPosition(window_, static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(window_display)),
                                  static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(window_display)));
            SDL_ShowWindow(window_);
            SDL_SyncWindow(window_);
        }

        if (config_.pixel_width > 0 && config_.pixel_height > 0) {
            const float density = SDL_GetWindowPixelDensity(window_);
            const float scale = density > 0.0f ? density : 1.0f;
            SDL_SetWindowSize(window_, static_cast<int>(std::lround(static_cast<float>(config_.pixel_width) / scale)),
                              static_cast<int>(std::lround(static_cast<float>(config_.pixel_height) / scale)));
            SDL_SyncWindow(window_);
        }
        int w = 0, h = 0, pixel_w = 0, pixel_h = 0;
        SDL_GetWindowSize(window_, &w, &h);
        SDL_GetWindowSizeInPixels(window_, &pixel_w, &pixel_h);
        const char* screen = SDL_GetDisplayName(SDL_GetDisplayForWindow(window_));
        SDL_Log("Window: %dx%d points, %dx%d pixels, on '%s'", w, h, pixel_w, pixel_h, screen != nullptr ? screen : "?");
        if (config_.pixel_width > 0 && (pixel_w != config_.pixel_width || pixel_h != config_.pixel_height)) {
            SDL_Log("Window: %dx%d pixels asked, %dx%d obtained (screen density %.2f)", config_.pixel_width,
                    config_.pixel_height, pixel_w, pixel_h, static_cast<double>(SDL_GetWindowPixelDensity(window_)));
        }

        RendererConfig renderer_config;
        renderer_config.debug = config_.gpu_debug;
        renderer_config.vsync = config_.vsync;
#if defined(MOTEUR_SHIPPING)
        renderer_config.debug_ui = false;
#else
        renderer_config.debug_ui = config_.debug_ui;
#endif
        renderer_config.debug_ui_font = config_.debug_ui_font;
        renderer_config.debug_ui_font_size = config_.debug_ui_font_size;
        renderer_ = std::make_unique<Renderer>(window_, renderer_config);
        renderer_->set_gpu_timing(config_.gpu_timing);
        set_crash_context("gpu", renderer_->device_description());
        display_.vsync = config_.vsync;
        if (config_.display_settings) {
            // The player's settings: read, then applied once everything exists (end of the constructor).
            std::string path = display_path();
            if (const std::string directory = preferences_directory();
                !path.empty() && !SDL_GetPathInfo(path.c_str(), nullptr) && !directory.empty()) {
                path = directory + "affichage.json";  // its name until 2026-10-17: read, then written as display.json
            }
            if (!path.empty() && SDL_GetPathInfo(path.c_str(), nullptr)) {
                std::vector<std::string> warnings;
                display_ = DisplaySettings::from_json(read_text_file(path), warnings, display_);  // vsync: the config's
                for (const std::string& warning : warnings) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Display: %s", warning.c_str());
                }
            }
        }
        assets_ = std::make_unique<Assets>(*renderer_, asset_path(""));
        build_asset_registry();
#if !defined(MOTEUR_SHIPPING)
        if (!config_.assets_source_directory.empty()) {
            // The import cache next to the sources: shared by every build (Debug, Release...).
            assets_->set_import_cache(import_cache_folder(config_.assets_source_directory));
        }
#endif
        data_ = std::make_unique<DataTables>();
        assets_->add_file_listener([this](const std::string& key) { data_->reload_file(assets_->root(), key); });
        add_console_commands();
        input_ = std::make_unique<Input>();
        AudioConfig audio_config;
        audio_config.device = config_.audio;
        audio_ = std::make_unique<Audio>(audio_config);
        load_audio_settings();
        if (!config_.assets_source_directory.empty()) {
            assets_->enable_hot_reload(config_.assets_source_directory);
#if !defined(MOTEUR_SHIPPING)
            for (const std::string& folder : config_.asset_folders) {
                assets_->watch_mounted_folder(folder);
            }
#endif
        }
#if !defined(MOTEUR_SHIPPING)
        if (DebugUi* ui = renderer_->debug_ui()) {
            // Its settings handler must exist before ImGui reads imgui.ini (at the first frame).
            debug_tools_ = std::make_unique<DebugTools>(*this);
            if (const std::string directory = preferences_directory(); !directory.empty()) {
                ui->set_settings_file(directory + "imgui.ini");
            }
        }
#endif
        if (config_.display_settings) {
            apply_display(display_);
        }
    } catch (...) {
#if !defined(MOTEUR_SHIPPING)
        debug_tools_.reset();
#endif
        audio_.reset();
        input_.reset();
        assets_.reset();
        renderer_.reset();
        if (window_ != nullptr) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        if (mounts_archives) {
            unmount_archives();  // the archives of a damaged set mounted before the damaged one
            set_archives_only(previous_archives_only);
        }
        set_tick_rate(previous_tick_rate);
        if (Profiler::current() == &profiler_) {
            Profiler::set_current(nullptr);
        }
        log_.close();  // gives SDL its log output back while SDL is still there
        SDL_Quit();
        release_crash_reporter(this);  // last: a crash while undoing the rest is still reported
        throw;
    }
}

std::string Application::display_path() const {
    const std::string directory = preferences_directory();
    return directory.empty() ? std::string() : directory + "display.json";
}

void Application::apply_display(const DisplaySettings& settings) {
    const DisplaySettings previous = display_;
    display_ = settings;
    // The screen first: the window goes there, then (fullscreen) covers it.
    int count = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&count);
    if (ids != nullptr && count > 0) {
        display_.display = std::clamp(display_.display, 0, count - 1);
        const SDL_DisplayID target = ids[display_.display];
        if (SDL_GetDisplayForWindow(window_) != target) {
            SDL_SetWindowFullscreen(window_, false);
            SDL_SetWindowPosition(window_, static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(target)),
                                  static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(target)));
        }
    }
    SDL_free(ids);
    SDL_SetWindowFullscreenMode(window_, nullptr);  // fullscreen windowed: the desktop's resolution
    SDL_SetWindowFullscreen(window_, display_.mode == WindowMode::Fullscreen);
    SDL_SyncWindow(window_);
    if (display_.vsync != previous.vsync || !display_applied_) {
        renderer_->set_vsync(display_.vsync);
    }
    renderer_->set_render_scale(display_.render_scale);
    renderer_->set_anti_aliasing(display_.anti_aliasing);
    const ShadowCaps caps = shadow_caps(display_.shadows);
    renderer_->meshes().set_shadow_caps(caps.resolution, caps.point_budget, caps.point_resolution);
    display_applied_ = true;
    SDL_Log("Display: %s, screen %d, vsync %s, limit %d, render scale %.2f, %s, shadows %s", window_mode_name(display_.mode),
            display_.display, display_.vsync ? "on" : "off", display_.frame_limit, display_.render_scale,
            anti_aliasing_name(display_.anti_aliasing), shadow_quality_name(display_.shadows));
    if (config_.display_settings) {
        if (const std::string path = display_path(); !path.empty()) {
            const std::string text = display_.to_json();
            if (!SDL_SaveFile(path.c_str(), text.data(), text.size())) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Display: cannot write '%s': %s", path.c_str(), SDL_GetError());
            }
        }
    }
}

Application::~Application() {
    // Assets hold GPU resources: released before the renderer, itself before the window it draws to.
    save_audio_settings();
    if (const std::string path = variables_path(); !path.empty()) {
        const std::string text = console_->variables().archived_text();
        if (!SDL_SaveFile(path.c_str(), text.data(), text.size())) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Variables: cannot write '%s': %s", path.c_str(), SDL_GetError());
        }
    }
#if !defined(MOTEUR_SHIPPING)
    debug_tools_.reset();  // writes imgui.ini while ImGui is still there
#endif
    audio_.reset();  // its voices hold sounds
    input_.reset();  // closes the gamepads
    assets_.reset();
    renderer_.reset();
    log_.close();
    SDL_DestroyWindow(window_);
    SDL_Quit();
    release_crash_reporter(this);  // last: a crash while ending is still reported
}

std::string Application::variables_path() const {
    const std::string directory = preferences_directory();
    return directory.empty() ? std::string() : directory + "variables.cfg";
}

void Application::add_console_commands() {
    Console& c = *console_;
    c.add("quit", "", "quits the program", Console::Kind::Tool, [this](const Console::Args&, Console&) { quit(); });
    c.add("crash", "[acces|abort|exception|assertion|gpu|frame]",
          "crashes on purpose (checks the crash reports; frame: a GPU failure inside a frame, which must not end the game)",
          Console::Kind::Tool,
          [this](const Console::Args& args, Console&) {
              const std::string kind = args.empty() ? "acces" : args[0];
              if (kind == "gpu") {
                  renderer_->simulate_device_lost();  // reported at the next frame, then a clean end
                  return;
              }
              if (kind == "frame") {
                  renderer_->simulate_frame_failure();  // the next frame without its meshes, then normal
                  return;
              }
              crash_now(kind == "abort"       ? CrashKind::Abort
                        : kind == "exception" ? CrashKind::UncaughtException
                        : kind == "assertion" ? CrashKind::Assertion
                                              : CrashKind::AccessViolation);
          });
    c.add("pause", "[0|1]", "pauses the ticks (frames go on)", Console::Kind::Tool,
          [this](const Console::Args& args, Console& out) {
              paused_ = args.empty() ? !paused_ : args[0] != "0";
              out.print(paused_ ? "paused" : "resumed");
          });
    c.add("step", "[ticks]", "while paused, runs a few ticks (1 by default)", Console::Kind::Tool,
          [this](const Console::Args& args, Console& out) {
              const int n = args.empty() ? 1 : std::max(1, std::atoi(args[0].c_str()));
              paused_ = true;
              pending_steps_ += n;
              out.print(std::to_string(n) + " tick(s)");
          });
    c.add("reload", "<asset file | table>", "reloads an asset (every one made from that file) or a data table",
          Console::Kind::Tool,
          [this](const Console::Args& args, Console& out) {
              if (args.size() != 1) {
                  out.error("usage: reload <file | table>");
                  return;
              }
              if (data_->find(args[0]) != nullptr) {
                  out.print(data_->reload(assets_->root(), args[0]) ? "table reloaded" : "table not reloaded (see the log)");
                  return;
              }
              const std::size_t types = assets_->stats().size();
              int reloaded = 0;
              for (std::size_t type = 0; type < types; ++type) {
                  for (const AssetInfo& info : assets_->infos()[type]) {
                      if (info.key == args[0] || info.key.rfind(args[0] + "#", 0) == 0) {
                          reloaded += assets_->reload(static_cast<AssetType>(type), info.key) ? 1 : 0;
                      }
                  }
              }
              out.print(std::to_string(reloaded) + " asset(s) reloaded");
          },
          [this](std::size_t index, const std::string&) {
              std::vector<std::string> names;
              if (index == 0) {
                  for (const auto& table : data_->tables()) {
                      names.push_back(table->name());
                  }
                  for (const auto& type : assets_->infos()) {
                      for (const AssetInfo& info : type) {
                          names.push_back(info.key.substr(0, info.key.find('#')));
                      }
                  }
              }
              return names;
          });
    c.add("assets", "", "asset statistics", Console::Kind::Tool, [this](const Console::Args&, Console& out) {
        for (const AssetTypeStats& type : assets_->stats()) {
            out.print(type.type + " : " + std::to_string(type.count) + " (" + std::to_string(type.bytes / 1024) + " KiB), " +
                      std::to_string(type.loads) + " load(s), " + std::to_string(type.failures) + " failure(s)");
        }
    });
    c.add("tables", "", "the data tables", Console::Kind::Tool, [this](const Console::Args&, Console& out) {
        for (const auto& table : data_->tables()) {
            out.print(table->name() + " : " + std::to_string(table->size()) + " row(s), reloaded " +
                      std::to_string(table->revision()) + " time(s)");
        }
    });
    c.add("exec", "<file>", "runs the lines of a file (a path, or a file of the assets)", Console::Kind::Tool,
          [this](const Console::Args& args, Console& out) {
              if (args.size() != 1) {
                  out.error("usage: exec <file>");
                  return;
              }
              std::string path = args[0];
              if (!SDL_GetPathInfo(path.c_str(), nullptr)) {
                  path = assets_->file_path(args[0]);
              }
              try {
                  out.run_script(read_text_file(path));
              } catch (const std::exception& e) {
                  out.error(e.what());
              }
          });
    c.add("profile", "start | stop [file.json]", "captures the profile of the frames between start and stop (Chrome Trace format)",
          Console::Kind::Tool,
          [this](const Console::Args& args, Console& out) {
              if (args.empty() || (args[0] != "start" && args[0] != "stop")) {
                  out.error("usage: profile start | stop [file.json]");
                  return;
              }
              if (args[0] == "start") {
                  profiler_.start_capture();
                  out.print("profile capture started");
                  return;
              }
              const std::string directory = preferences_directory();
              const std::string path = args.size() > 1 ? args[1] : directory + "profiles/profile.json";
              out.print(profiler_.stop_capture(path) ? "profile written: " + path + " (open it in ui.perfetto.dev)"
                                                     : "cannot write: " + path);
          },
          [](std::size_t index, const std::string&) {
              return index == 0 ? std::vector<std::string>{"start", "stop"} : std::vector<std::string>{};
          });
    c.add("screenshot", "<file.png>", "captures the next frame", Console::Kind::Tool,
          [this](const Console::Args& args, Console& out) {
              if (args.size() != 1) {
                  out.error("usage: screenshot <file.png>");
                  return;
              }
              renderer_->request_capture(args[0]);
              out.print("capture requested: " + args[0]);
          });
}

glm::vec2 Application::to_pixels(glm::vec2 window_point) const {
    int points_w = 0, points_h = 0, pixels_w = 0, pixels_h = 0;
    SDL_GetWindowSize(window_, &points_w, &points_h);
    SDL_GetWindowSizeInPixels(window_, &pixels_w, &pixels_h);
    return window_to_pixels(window_point, {static_cast<float>(points_w), static_cast<float>(points_h)},
                            {static_cast<float>(pixels_w), static_cast<float>(pixels_h)});
}

std::string Application::audio_settings_path() const {
    const std::string directory = preferences_directory();
    return directory.empty() ? std::string() : directory + "audio.json";
}

void Application::load_audio_settings() {
    const std::string path = audio_settings_path();
    if (path.empty() || !SDL_GetPathInfo(path.c_str(), nullptr)) {
        return;
    }
    try {
        audio_->apply_settings_json(read_text_file(path), path);
    } catch (const std::exception& e) {
        SDL_Log("%s (the default volumes are used)", e.what());
    }
}

void Application::save_audio_settings() {
    if (!audio_ || !audio_->settings_changed()) {
        return;
    }
    const std::string path = audio_settings_path();
    const std::string text = audio_->settings_json();
    if (!path.empty() && SDL_SaveFile(path.c_str(), text.data(), text.size())) {
        audio_->mark_settings_saved();
    } else {
        SDL_Log("Audio: cannot write the settings '%s': %s", path.c_str(), SDL_GetError());
    }
}

std::string Application::preferences_directory() const {
    char* path = SDL_GetPrefPath(config_.organization.c_str(), config_.application.c_str());
    if (path == nullptr) {
        SDL_Log("No preferences directory: %s", SDL_GetError());
        return {};
    }
    std::string directory = path;
    SDL_free(path);
    return directory;
}

std::string import_cache_folder(const std::string& assets_source_directory) {
    std::string folder = assets_source_directory;
    while (!folder.empty() && (folder.back() == '/' || folder.back() == '\\')) {
        folder.pop_back();
    }
    const std::size_t slash = folder.find_last_of("/\\");
    return (slash == std::string::npos ? std::string() : folder.substr(0, slash + 1)) + ".imported/";
}

void Application::build_asset_registry() {
    // The shipped game: the registry frozen by the preparation, in the archives.
    const std::string frozen = asset_path(ApplicationConfig::kRegistryFile);
    if (file_exists(frozen)) {
        const FileData file = read_file(frozen);
        const auto* bytes = static_cast<const std::uint8_t*>(file.data());
        assets_->registry() = AssetRegistry::decode(std::vector<std::uint8_t>(bytes, bytes + file.size()), frozen);
        SDL_Log("Assets: %zu identified (frozen registry)", assets_->registry().size());
        return;
    }
    // Development: the folders and their .meta files; a new asset gets its .meta in the sources
    // (never in the copy next to the executable, which the next build replaces).
    std::vector<std::string> roots;
    bool write = false;
    if (!config_.assets_source_directory.empty()) {
        std::string source = config_.assets_source_directory;
        if (source.back() != '/' && source.back() != '\\') {
            source += '/';
        }
        roots.push_back(source);
        write = true;
#if defined(MOTEUR_ENGINE_ASSETS_DIR)
        roots.emplace_back(MOTEUR_ENGINE_ASSETS_DIR);  // copied under the game's: the game's file wins
#endif
    } else {
        roots.push_back(assets_->root());
    }
#if !defined(MOTEUR_SHIPPING)
    for (const std::string& folder : config_.asset_folders) {
        mount_asset_folder(folder);
    }
    for (const std::string& folder : mounted_asset_folders()) {
        roots.push_back(folder);
    }
#endif
    const Uint64 start = SDL_GetPerformanceCounter();
    const AssetScanReport report = assets_->registry().scan(roots, write);
    SDL_Log("Assets: %zu identified in %.1f ms (%zu new .meta%s)", report.assets,
            static_cast<double>(SDL_GetPerformanceCounter() - start) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency()),
            report.created.size(), write ? " written" : ", not written: no source folder");
    for (const std::string& key : report.reassigned) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Assets: new identifier for %s", key.c_str());
    }
    for (const std::string& orphan : report.orphans) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Assets: '%s' has no asset (moved without it?)", orphan.c_str());
    }
    for (const std::string& problem : report.problems) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Assets: %s", problem.c_str());
    }
    if (!report.shadowed.empty()) {
        SDL_Log("Assets: %zu file(s) of the mounted folders hidden by the program's own (%s%s)", report.shadowed.size(),
                report.shadowed.front().c_str(), report.shadowed.size() > 1 ? "..." : "");
    }
}

void Application::fail(const std::string& what) {
    report_fatal_error(what, config_.crash_message.empty() ? std::string(kDefaultCrashMessage) : config_.crash_message);
    ended_on_error_ = true;
    running_ = false;
}

template <typename Body>
bool Application::guarded(const char* phase, Body&& body) {
    try {
        body();
        return true;
    } catch (const std::exception& e) {
        fail(std::string("uncaught exception in ") + phase + ": " + e.what());
    } catch (...) {
        fail(std::string("uncaught exception in ") + phase + " (not a std::exception)");
    }
    return false;
}

void Application::run(Game& game) {
    MOTEUR_ASSERT_MAIN_THREAD("Application::run()");
    MOTEUR_ASSERT(!running_, "Application::run() called from inside run()");
    FixedTimestep timestep(1.0 / static_cast<double>(tick_rate()), config_.max_frame_time);
    const auto frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    const auto elapsed_ms = [frequency](std::uint64_t from, std::uint64_t to) {
        return static_cast<double>(to - from) * 1000.0 / frequency;
    };

    // Summary over the whole run, all in milliseconds.
    FrameStats cpu_stats;     // everything except waiting for the display
    FrameStats update_stats;  // events + fixed updates
    FrameStats record_stats;  // Game::render
    FrameStats submit_stats;  // Renderer::end_frame: uploads, draw calls, submission
    FrameStats interval_stats;  // time between two frames (waiting for the display included)
    long long gpu_memory_peak = 0;
    double sprites_total = 0.0;
    double draw_calls_total = 0.0;
    double bytes_total = 0.0;
    long drawn_frames = 0;

    // Statistics shown in the window title, refreshed once per second.
    double stats_time = 0.0;
    int stats_frames = 0;
    int stats_ticks = 0;
    double stats_cpu_ms = 0.0;
    int last_sprites = 0;
    int last_draw_calls = 0;
    // Sums of the 3D counters of the measured frames, per pass.
    struct MeshTotals {
        double submitted = 0.0, drawn = 0.0, triangles = 0.0, draw_calls = 0.0;
        double shadow_submitted = 0.0, shadow_drawn = 0.0, shadow_triangles = 0.0, shadow_draw_calls = 0.0;
        double point_lights = 0.0, point_updates = 0.0, point_drawn = 0.0, point_triangles = 0.0, point_draw_calls = 0.0;
        double gpu_ms[kGpuTimeCount] = {};
        double gpu_frames = 0.0;
        void add(const RenderStats& stats) {
            submitted += stats.meshes_submitted;
            drawn += stats.meshes;
            triangles += static_cast<double>(stats.triangles);
            draw_calls += stats.mesh_draw_calls;
            shadow_submitted += stats.shadow_casters_submitted;
            shadow_drawn += stats.shadow_casters;
            shadow_triangles += static_cast<double>(stats.shadow_triangles);
            shadow_draw_calls += stats.shadow_draw_calls;
            point_lights += stats.point_shadow_lights;
            point_updates += stats.point_shadow_updates;
            point_drawn += stats.point_shadow_casters;
            point_triangles += static_cast<double>(stats.point_shadow_triangles);
            point_draw_calls += stats.point_shadow_draw_calls;
            if (stats.gpu_timed) {
                for (int i = 0; i < kGpuTimeCount; ++i) {
                    gpu_ms[i] += stats.gpu_ms[i];
                }
                gpu_frames += 1.0;
            }
        }
    } mesh_totals;

    std::uint64_t previous = SDL_GetPerformanceCounter();

    // Input replay or recording (see ApplicationConfig).
    std::optional<InputRecording> replay;
    if (!config_.replay_input_path.empty()) {
        replay = input_recording_from_json(read_text_file(config_.replay_input_path), config_.replay_input_path);
        input_->set_enabled(false);
        SDL_Log("Input: replaying '%s' (%zu ticks)", config_.replay_input_path.c_str(), replay->frames.size());
    }
    std::optional<InputRecording> recording;
    std::size_t tick = replay ? config_.replay_start : 0;
    for (const std::string& line : config_.startup_commands) {
#if defined(MOTEUR_SHIPPING)
        // The shipped game has no console: only its variables can be set at launch (+set name value),
        // never a game command (decision of milestone 8, part 6).
        if (!line.starts_with("set ")) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Console: '%s' refused (only 'set' at launch in the shipped game)",
                        line.c_str());
            continue;
        }
#endif
        console_->submit(line);
    }

    running_ = true;
    while (running_) {
        const std::uint64_t iteration_start = SDL_GetPerformanceCounter();
        const double frame_time = static_cast<double>(iteration_start - previous) / frequency;
        previous = iteration_start;
#if !defined(MOTEUR_SHIPPING)  // the shipped game has no profiler (milestone 7): no zone, no frame
        profiler_.set_enabled(profile_enabled_->as_bool());
        profiler_.begin_frame();
#endif

        {
            MOTEUR_PROFILE("assets");
            const bool ok = guarded("the hot reload", [this] {
                assets_->update();  // hot reload, between two frames
#if !defined(MOTEUR_SHIPPING)
                if (debug_tools_) {
                    debug_tools_->between_frames();  // the tools' reloads and frees, also between two frames
                }
#endif
            });
            if (!ok) {
                break;
            }
        }

#if !defined(MOTEUR_SHIPPING)
        DebugUi* ui = renderer_->debug_ui();
#endif
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
#if !defined(MOTEUR_SHIPPING)
            // The console's key (under Escape, by position): opens and closes it, never typed.
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_GRAVE && debug_tools_) {
                if (!event.key.repeat) {
                    debug_tools_->toggle_console();
                }
                swallow_text_ = true;
                continue;
            }
#endif
            if (event.type == SDL_EVENT_TEXT_INPUT && swallow_text_) {
                swallow_text_ = false;
                continue;
            }
            if (event.type == SDL_EVENT_QUIT) {
                quit();
            } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                audio_->set_focus(false);
            } else if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
                audio_->set_focus(true);
            }
            // A release always reaches the input, even over the debug interface: a button pressed in
            // the game and released over a window must not stay held.
            const bool release = event.type == SDL_EVENT_KEY_UP || event.type == SDL_EVENT_MOUSE_BUTTON_UP;
#if !defined(MOTEUR_SHIPPING)
            if (ui != nullptr) {
                ui->process_event(event);
                if (ui->captures(event)) {
                    if (release) {
                        input_->process_event(event);
                    }
                    continue;
                }
            }
#else
            (void)release;
#endif
            if (event.type == SDL_EVENT_MOUSE_MOTION) {
                input_->set_pointer(to_pixels({event.motion.x, event.motion.y}));
            } else if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE) {
                input_->set_pointer(glm::vec2(-1.0f));
            }
            input_->process_event(event);
            if (!guarded("Game::on_event", [&] { game.on_event(event); })) {
                break;
            }
        }
        if (!running_ && ended_on_error_) {
            break;
        }

        console_->flush_log();
        // Paused: only the ticks asked for by "step"; otherwise the time, scaled by "time.scale".
        int steps = 0;
        if (paused_) {
            steps = std::min(pending_steps_, 8);
            pending_steps_ -= steps;
            timestep.advance(0.0);
        } else {
            pending_steps_ = 0;
            steps = timestep.advance(frame_time * static_cast<double>(time_scale_->as_float()));
        }
        {
        MOTEUR_PROFILE("logic");
        for (int i = 0; i < steps && running_; ++i) {
            MOTEUR_PROFILE("tick");
            bool waiting = false;
            if (!guarded("Game::waiting", [&] { waiting = game.waiting(); })) {
                break;
            }
            if (waiting) {
                guarded("Game::update", [&] { game.update(timestep.step()); });  // a tick that does not count: see Game::waiting()
                continue;
            }
            // The console's game commands of this tick: replayed, or typed and recorded.
            std::vector<std::string> commands;
            if (replay) {
                input_->set_frame(*replay, tick);
                if (tick < replay->frames.size()) {
                    commands = replay->frames[tick].commands;
                }
            } else {
                commands = console_->take_game_commands();
                if (!config_.record_input_path.empty()) {
                    if (!recording) {
                        recording = input_->start_recording();  // the game has declared its actions by now
                    }
                    InputFrame frame = input_->frame();
                    frame.commands = commands;
                    recording->frames.push_back(std::move(frame));
                }
            }
            for (const std::string& command : commands) {
                console_->run_game_command(command);
            }
            if (!guarded("Game::update", [&] { game.update(timestep.step()); })) {
                break;
            }
            input_->end_tick();  // what this tick has seen is consumed
            ++tick;
        }
        }
        if (ended_on_error_) {
            break;
        }
        stats_ticks += steps;
        const std::uint64_t update_end = SDL_GetPerformanceCounter();
        if (restart_clock_) {
            // The next frame time starts here: the load that just ran is not owed to the logic.
            previous = update_end;
            restart_clock_ = false;
        }

        // begin_frame() blocks until the display can take a new image: that is waiting, not work.
        bool drawable = false;
        {
            MOTEUR_PROFILE("waiting for the display");
            drawable = renderer_->begin_frame();
        }
        if (renderer_->device_lost()) {
            // Nothing can be drawn any more: the report, the player's message, a clean end.
            report_fatal_error("GPU device lost: " + renderer_->device_lost_reason(), config_.gpu_lost_message);
            ended_on_error_ = true;
            running_ = false;
            break;
        }
        const std::uint64_t wait_end = SDL_GetPerformanceCounter();

        if (drawable) {
#if !defined(MOTEUR_SHIPPING)
            if (ui != nullptr) {
                ui->new_frame();
            }
#endif
            {
                MOTEUR_PROFILE("render: record");
                // A failure here still ends the frame below (the swapchain image acquired must be
                // submitted), then the loop.
                guarded("Game::render", [&] { game.render(*renderer_, timestep.alpha()); });
            }
            const std::uint64_t record_end = SDL_GetPerformanceCounter();
            {
                MOTEUR_PROFILE("render: submit");
                renderer_->end_frame();  // never throws (see Renderer::end_frame())
            }
            if (ended_on_error_) {
                break;
            }
            const std::uint64_t submit_end = SDL_GetPerformanceCounter();
            ++stats_frames;

            const double update_ms = elapsed_ms(iteration_start, update_end);
            const double record_ms = elapsed_ms(wait_end, record_end);
            const double submit_ms = elapsed_ms(record_end, submit_end);
            const double cpu_ms = update_ms + record_ms + submit_ms;
            stats_cpu_ms += cpu_ms;

            last_sprites = renderer_->stats().sprites;
            last_draw_calls = renderer_->stats().draw_calls;

            if (drawn_frames >= kWarmupFrames) {
                cpu_stats.add(cpu_ms);
                interval_stats.add(frame_time * 1000.0);
                gpu_memory_peak = std::max(gpu_memory_peak, gpu_memory().total());
                update_stats.add(update_ms);
                record_stats.add(record_ms);
                submit_stats.add(submit_ms);
                sprites_total += last_sprites;
                draw_calls_total += last_draw_calls;
                bytes_total += static_cast<double>(renderer_->stats().bytes_uploaded);
                mesh_totals.add(renderer_->stats());
            }
            ++drawn_frames;
        } else {
            // Nothing to draw (minimized window): avoid spinning at full speed.
            SDL_Delay(10);
        }
        // The sounds the updates asked for, at the end of the frame (see Audio).
        {
            MOTEUR_PROFILE("audio");
            if (!guarded("the audio", [this] { audio_->update(); })) {
                break;
            }
        }
        // The player's frame limit, without vsync (DisplaySettings::frame_limit).
        if (!display_.vsync && display_.frame_limit > 0) {
            const Uint64 frame_ns = SDL_NS_PER_SECOND / static_cast<Uint64>(display_.frame_limit);
            const auto spent_ns = static_cast<Uint64>(static_cast<double>(SDL_GetPerformanceCounter() - iteration_start) /
                                                      frequency * 1e9);
            if (spent_ns < frame_ns) {
                SDL_DelayPrecise(frame_ns - spent_ns);
            }
        }
#if !defined(MOTEUR_SHIPPING)
        profiler_.end_frame();
#endif

        stats_time += frame_time;
        if (stats_time >= 1.0) {
            char title[160];
            std::snprintf(title, sizeof(title), "%s - %d FPS, %d ticks/s, %.2f ms cpu, %d sprites, %d draws",
                          config_.title.c_str(), stats_frames, stats_ticks,
                          stats_frames > 0 ? stats_cpu_ms / stats_frames : 0.0, last_sprites, last_draw_calls);
            SDL_SetWindowTitle(window_, title);
            stats_time -= 1.0;
            stats_frames = 0;
            stats_ticks = 0;
            stats_cpu_ms = 0.0;
        }
    }

    if (recording) {
        const std::string text = input_recording_to_json(*recording);
        if (SDL_SaveFile(config_.record_input_path.c_str(), text.data(), text.size())) {
            SDL_Log("Input: %zu ticks recorded into '%s'", recording->frames.size(), config_.record_input_path.c_str());
        } else {
            SDL_Log("Input: cannot write '%s': %s", config_.record_input_path.c_str(), SDL_GetError());
        }
    }

    if (config_.report_performance) {
        for (const AssetTypeStats& type : assets_->stats()) {
            if (type.count > 0) {
                SDL_Log("Assets: %-11s %4zu in memory, %7.2f MB on the GPU, %zu loads, %zu failures", type.type.c_str(),
                        type.count, static_cast<double>(type.bytes) / (1024.0 * 1024.0), type.loads, type.failures);
            }
        }
    }
    if (config_.report_performance && cpu_stats.count() > 0) {
        const auto measured = static_cast<double>(cpu_stats.count());
        SDL_Log("perf: %zu frames measured (first %d skipped)", cpu_stats.count(), kWarmupFrames);
        SDL_Log("perf: cpu ms   mean %.3f  p99 %.3f  max %.3f", cpu_stats.mean(), cpu_stats.percentile(99),
                cpu_stats.max());
        SDL_Log("perf: phases   update %.3f  record %.3f  submit %.3f  (means, ms)", update_stats.mean(),
                record_stats.mean(), submit_stats.mean());
        // The profiler's zones over the whole run (per frame where they ran).
        int shown = 0;
        for (const Profiler::Summary& zone : profiler_.totals()) {
            if (++shown > 16) {
                break;
            }
            SDL_Log("perf: zone %-28s mean %.3f  p99 %.3f  max %.3f  (%.1f per frame)", zone.name.c_str(), zone.mean_ms,
                    zone.p99_ms, zone.max_ms, zone.calls);
        }
        SDL_Log("perf: per frame  %.0f sprites, %.0f draw calls, %.1f KiB uploaded", sprites_total / measured,
                draw_calls_total / measured, bytes_total / measured / 1024.0);
        if (mesh_totals.submitted > 0.0) {
            SDL_Log("perf: scene    %.0f meshes recorded, %.0f drawn, %.0f triangles, %.0f draw calls",
                    mesh_totals.submitted / measured, mesh_totals.drawn / measured, mesh_totals.triangles / measured,
                    mesh_totals.draw_calls / measured);
            SDL_Log("perf: shadow   %.0f casters recorded, %.0f drawn, %.0f triangles, %.0f draw calls",
                    mesh_totals.shadow_submitted / measured, mesh_totals.shadow_drawn / measured,
                    mesh_totals.shadow_triangles / measured, mesh_totals.shadow_draw_calls / measured);
            SDL_Log("perf: points   %.2f lights shadowed, %.2f redrawn, %.0f meshes drawn, %.0f triangles, %.0f draw calls",
                    mesh_totals.point_lights / measured, mesh_totals.point_updates / measured,
                    mesh_totals.point_drawn / measured, mesh_totals.point_triangles / measured,
                    mesh_totals.point_draw_calls / measured);
        }
        if (mesh_totals.gpu_frames > 0.0) {
            const double n = mesh_totals.gpu_frames;
            const double* g = mesh_totals.gpu_ms;
            SDL_Log("perf: gpu ms (approx.)  uploads %.3f  shadow %.3f  point shadows %.3f  scene %.3f  compose %.3f  total %.3f",
                    g[kGpuUpload] / n, g[kGpuShadow] / n, g[kGpuPointShadows] / n, g[kGpuScene] / n, g[kGpuCompose] / n,
                    (g[kGpuUpload] + g[kGpuShadow] + g[kGpuPointShadows] + g[kGpuScene] + g[kGpuCompose]) / n);
        }
    }

    if (!config_.report_json_path.empty()) {
        const auto summary = [](const FrameStats& stats) {
            return nlohmann::json{{"mean", stats.mean()}, {"p50", stats.percentile(50)},
                                  {"p99", stats.percentile(99)}, {"max", stats.max()}};
        };
        const auto over = [](const FrameStats& stats, double limit) {
            std::size_t count = 0;
            for (std::size_t i = 0; i < stats.count(); ++i) {
                count += stats.sample(i) > limit ? 1 : 0;
            }
            return count;
        };
        const double measured = std::max(1.0, static_cast<double>(cpu_stats.count()));
        nlohmann::json report;
        report["version"] = 2;  // 2: English keys (version 1 had French ones)
        report["engine"] = build_description();
        report["platform"] = SDL_GetPlatform();
        report["gpu"] = renderer_->device_description();
        report["window_pixels"] = {renderer_->width(), renderer_->height()};
        report["vsync"] = display_.vsync;  // as applied (the player's settings, or the config)
        report["frames_measured"] = cpu_stats.count();
        report["frames_skipped_at_start"] = kWarmupFrames;
        report["ticks"] = tick;
        report["cpu_ms"] = summary(cpu_stats);
        report["interval_ms"] = summary(interval_stats);
        report["phases_ms"] = {{"logic", update_stats.mean()}, {"record", record_stats.mean()},
                               {"submit", submit_stats.mean()}};
        report["frames_over"] = {{"cpu_16_7_ms", over(cpu_stats, 1000.0 / 60.0)},
                                 {"cpu_33_3_ms", over(cpu_stats, 1000.0 / 30.0)},
                                 {"interval_16_7_ms", over(interval_stats, 1000.0 / 60.0 + 0.5)},
                                 {"interval_33_3_ms", over(interval_stats, 1000.0 / 30.0)}};
        nlohmann::json zones = nlohmann::json::array();
        for (const Profiler::Summary& zone : profiler_.totals()) {
            zones.push_back({{"name", zone.name}, {"mean", zone.mean_ms}, {"p99", zone.p99_ms}, {"max", zone.max_ms},
                             {"per_frame", zone.calls}});
        }
        report["zones"] = zones;
        report["per_frame"] = {{"sprites", sprites_total / measured},
                               {"draw_calls", draw_calls_total / measured},
                               {"point_shadow_draw_calls", mesh_totals.point_draw_calls / measured},
                               {"point_shadows_redrawn", mesh_totals.point_updates / measured},
                               {"meshes_drawn", mesh_totals.drawn / measured},
                               {"triangles", mesh_totals.triangles / measured},
                               {"scene_draw_calls", mesh_totals.draw_calls / measured},
                               {"shadow_draw_calls", mesh_totals.shadow_draw_calls / measured},
                               {"kib_uploaded", bytes_total / measured / 1024.0}};
        if (mesh_totals.gpu_frames > 0.0) {
            const double n = mesh_totals.gpu_frames;
            const double* g = mesh_totals.gpu_ms;
            report["gpu_ms"] = {{"uploads", g[kGpuUpload] / n},  {"shadow", g[kGpuShadow] / n},
                                {"point_shadows", g[kGpuPointShadows] / n}, {"scene", g[kGpuScene] / n},
                                {"compose", g[kGpuCompose] / n},
                                {"total", (g[kGpuUpload] + g[kGpuShadow] + g[kGpuPointShadows] + g[kGpuScene] +
                                           g[kGpuCompose]) / n}};
        }
        const GpuMemory memory = gpu_memory();
        constexpr double kMiB = 1024.0 * 1024.0;
        report["memory_mib"] = {{"process", static_cast<double>(process_memory_bytes()) / kMiB},
                                {"process_peak", static_cast<double>(process_peak_memory_bytes()) / kMiB},
                                {"gpu_textures", static_cast<double>(memory.textures) / kMiB},
                                {"gpu_targets", static_cast<double>(memory.targets) / kMiB},
                                {"gpu_buffers", static_cast<double>(memory.buffers) / kMiB},
                                {"gpu_peak", static_cast<double>(gpu_memory_peak) / kMiB}};
        report["values"] = report_values_;
        const std::string text = report.dump(2) + "\n";
        if (SDL_SaveFile(config_.report_json_path.c_str(), text.data(), text.size())) {
            SDL_Log("perf: report written into '%s'", config_.report_json_path.c_str());
        } else {
            SDL_Log("perf: cannot write '%s': %s", config_.report_json_path.c_str(), SDL_GetError());
        }
    }
}

}  // namespace moteur
