#include "moteur/application.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <stdexcept>

#include "moteur/debug_ui.hpp"
#include "moteur/fixed_timestep.hpp"
#include "moteur/frame_stats.hpp"
#include "moteur/paths.hpp"

namespace moteur {

namespace {

// Frames ignored at the start of the performance summary (pipeline warm-up, first uploads).
constexpr int kWarmupFrames = 60;

}  // namespace

Application::Application(const ApplicationConfig& config) : config_(config) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }

    window_ = SDL_CreateWindow(config_.title.c_str(), config_.width, config_.height,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window_ == nullptr) {
        const std::string error = SDL_GetError();
        SDL_Quit();
        throw std::runtime_error("SDL_CreateWindow failed: " + error);
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
    SDL_Log("Window: %dx%d points, %dx%d pixels", w, h, pixel_w, pixel_h);
    if (config_.pixel_width > 0 && (pixel_w != config_.pixel_width || pixel_h != config_.pixel_height)) {
        SDL_Log("Window: %dx%d pixels asked, %dx%d obtained (screen density %.2f)", config_.pixel_width,
                config_.pixel_height, pixel_w, pixel_h, static_cast<double>(SDL_GetWindowPixelDensity(window_)));
    }

    try {
        RendererConfig renderer_config;
        renderer_config.debug = config_.gpu_debug;
        renderer_config.vsync = config_.vsync;
        renderer_config.debug_ui = config_.debug_ui;
        renderer_config.debug_ui_font = config_.debug_ui_font;
        renderer_config.debug_ui_font_size = config_.debug_ui_font_size;
        renderer_ = std::make_unique<Renderer>(window_, renderer_config);
        renderer_->set_gpu_timing(config_.gpu_timing);
        assets_ = std::make_unique<Assets>(*renderer_, asset_path(""));
        input_ = std::make_unique<Input>();
        AudioConfig audio_config;
        audio_config.device = config_.audio;
        audio_ = std::make_unique<Audio>(audio_config);
        load_audio_settings();
        if (!config_.assets_source_directory.empty()) {
            assets_->enable_hot_reload(config_.assets_source_directory);
        }
        if (DebugUi* ui = renderer_->debug_ui()) {
            // Its settings handler must exist before ImGui reads imgui.ini (at the first frame).
            debug_tools_ = std::make_unique<DebugTools>(*this);
            if (const std::string directory = preferences_directory(); !directory.empty()) {
                ui->set_settings_file(directory + "imgui.ini");
            }
        }
    } catch (...) {
        debug_tools_.reset();
        audio_.reset();
        input_.reset();
        assets_.reset();
        renderer_.reset();
        SDL_DestroyWindow(window_);
        SDL_Quit();
        throw;
    }
}

Application::~Application() {
    // Assets hold GPU resources: released before the renderer, itself before the window it draws to.
    save_audio_settings();
    debug_tools_.reset();  // writes imgui.ini while ImGui is still there
    audio_.reset();  // its voices hold sounds
    input_.reset();  // closes the gamepads
    assets_.reset();
    renderer_.reset();
    SDL_DestroyWindow(window_);
    SDL_Quit();
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

void Application::run(Game& game) {
    FixedTimestep timestep(1.0 / config_.fixed_hz, config_.max_frame_time);
    const auto frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    const auto elapsed_ms = [frequency](std::uint64_t from, std::uint64_t to) {
        return static_cast<double>(to - from) * 1000.0 / frequency;
    };

    // Summary over the whole run, all in milliseconds.
    FrameStats cpu_stats;     // everything except waiting for the display
    FrameStats update_stats;  // events + fixed updates
    FrameStats record_stats;  // Game::render
    FrameStats submit_stats;  // Renderer::end_frame: uploads, draw calls, submission
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
    std::size_t tick = 0;

    running_ = true;
    while (running_) {
        const std::uint64_t iteration_start = SDL_GetPerformanceCounter();
        const double frame_time = static_cast<double>(iteration_start - previous) / frequency;
        previous = iteration_start;

        assets_->update();  // hot reload, between two frames
        if (debug_tools_) {
            debug_tools_->between_frames();  // the tools' reloads and frees, also between two frames
        }

        DebugUi* ui = renderer_->debug_ui();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
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
            if (ui != nullptr) {
                ui->process_event(event);
                if (ui->captures(event)) {
                    if (release) {
                        input_->process_event(event);
                    }
                    continue;
                }
            }
            if (event.type == SDL_EVENT_MOUSE_MOTION) {
                input_->set_pointer(to_pixels({event.motion.x, event.motion.y}));
            } else if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE) {
                input_->set_pointer(glm::vec2(-1.0f));
            }
            input_->process_event(event);
            game.on_event(event);
        }

        const int steps = timestep.advance(frame_time);
        for (int i = 0; i < steps; ++i) {
            if (replay) {
                input_->set_frame(*replay, tick);
            } else if (!config_.record_input_path.empty()) {
                if (!recording) {
                    recording = input_->start_recording();  // the game has declared its actions by now
                }
                recording->frames.push_back(input_->frame());
            }
            game.update(timestep.step());
            input_->end_tick();  // what this tick has seen is consumed
            ++tick;
        }
        stats_ticks += steps;
        const std::uint64_t update_end = SDL_GetPerformanceCounter();
        if (restart_clock_) {
            // The next frame time starts here: the load that just ran is not owed to the logic.
            previous = update_end;
            restart_clock_ = false;
        }

        // begin_frame() blocks until the display can take a new image: that is waiting, not work.
        const bool drawable = renderer_->begin_frame();
        const std::uint64_t wait_end = SDL_GetPerformanceCounter();

        if (drawable) {
            if (ui != nullptr) {
                ui->new_frame();
            }
            game.render(*renderer_, timestep.alpha());
            const std::uint64_t record_end = SDL_GetPerformanceCounter();
            renderer_->end_frame();
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
        audio_->update();

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
}

}  // namespace moteur
