#include "moteur/application.hpp"

#include <cstdint>
#include <cstdio>
#include <stdexcept>

#include "moteur/debug_ui.hpp"
#include "moteur/fixed_timestep.hpp"
#include "moteur/frame_stats.hpp"

namespace moteur {

namespace {

// Frames ignored at the start of the performance summary (pipeline warm-up, first uploads).
constexpr int kWarmupFrames = 60;

}  // namespace

Application::Application(const ApplicationConfig& config) : config_(config) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }

    window_ = SDL_CreateWindow(config_.title.c_str(), config_.width, config_.height,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window_ == nullptr) {
        const std::string error = SDL_GetError();
        SDL_Quit();
        throw std::runtime_error("SDL_CreateWindow failed: " + error);
    }

    int w = 0, h = 0, pixel_w = 0, pixel_h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    SDL_GetWindowSizeInPixels(window_, &pixel_w, &pixel_h);
    SDL_Log("Window: %dx%d points, %dx%d pixels", w, h, pixel_w, pixel_h);

    try {
        RendererConfig renderer_config;
        renderer_config.debug = config_.gpu_debug;
        renderer_config.vsync = config_.vsync;
        renderer_config.debug_ui = config_.debug_ui;
        renderer_config.debug_ui_font = config_.debug_ui_font;
        renderer_config.debug_ui_font_size = config_.debug_ui_font_size;
        renderer_ = std::make_unique<Renderer>(window_, renderer_config);
    } catch (...) {
        SDL_DestroyWindow(window_);
        SDL_Quit();
        throw;
    }
}

Application::~Application() {
    // The renderer must be released before the window it draws to.
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

    std::uint64_t previous = SDL_GetPerformanceCounter();

    running_ = true;
    while (running_) {
        const std::uint64_t iteration_start = SDL_GetPerformanceCounter();
        const double frame_time = static_cast<double>(iteration_start - previous) / frequency;
        previous = iteration_start;

        DebugUi* ui = renderer_->debug_ui();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                quit();
            }
            if (ui != nullptr) {
                ui->process_event(event);
                if (ui->captures(event)) {
                    continue;
                }
            }
            game.on_event(event);
        }

        const int steps = timestep.advance(frame_time);
        for (int i = 0; i < steps; ++i) {
            game.update(timestep.step());
        }
        stats_ticks += steps;
        const std::uint64_t update_end = SDL_GetPerformanceCounter();

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
            }
            ++drawn_frames;
        } else {
            // Nothing to draw (minimized window): avoid spinning at full speed.
            SDL_Delay(10);
        }

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

    if (config_.report_performance && cpu_stats.count() > 0) {
        const auto measured = static_cast<double>(cpu_stats.count());
        SDL_Log("perf: %zu frames measured (first %d skipped)", cpu_stats.count(), kWarmupFrames);
        SDL_Log("perf: cpu ms   mean %.3f  p99 %.3f  max %.3f", cpu_stats.mean(), cpu_stats.percentile(99),
                cpu_stats.max());
        SDL_Log("perf: phases   update %.3f  record %.3f  submit %.3f  (means, ms)", update_stats.mean(),
                record_stats.mean(), submit_stats.mean());
        SDL_Log("perf: per frame  %.0f sprites, %.0f draw calls, %.1f KiB uploaded", sprites_total / measured,
                draw_calls_total / measured, bytes_total / measured / 1024.0);
    }
}

}  // namespace moteur
