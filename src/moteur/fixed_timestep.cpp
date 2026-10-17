#include "moteur/fixed_timestep.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>
#include <string>

namespace moteur {

namespace {

std::atomic<int> g_tick_rate{60};  // read by the job system's threads too (clip durations)

}  // namespace

int tick_rate() {
    return g_tick_rate.load(std::memory_order_relaxed);
}

void set_tick_rate(int ticks_per_second) {
    if (ticks_per_second < 1 || ticks_per_second > 1000) {
        throw std::invalid_argument("tick rate " + std::to_string(ticks_per_second) + " out of range [1, 1000]");
    }
    g_tick_rate.store(ticks_per_second, std::memory_order_relaxed);
}

std::int64_t ticks_from_seconds(double seconds) {
    return std::llround(seconds * static_cast<double>(tick_rate()));
}

FixedTimestep::FixedTimestep(double step, double max_frame_time) : step_(step), max_frame_time_(max_frame_time) {
    if (step <= 0.0 || max_frame_time <= 0.0) {
        throw std::invalid_argument("FixedTimestep: step and max_frame_time must be positive");
    }
}

int FixedTimestep::advance(double frame_time) {
    accumulator_ += std::clamp(frame_time, 0.0, max_frame_time_);

    int steps = 0;
    while (accumulator_ >= step_) {
        accumulator_ -= step_;
        ++steps;
    }
    return steps;
}

}  // namespace moteur
