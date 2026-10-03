#include "moteur/animation_clock.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace moteur {

ClipClock::ClipClock(int cycle_ticks, bool once) : cycle_ticks_(cycle_ticks), once_(once) {
    if (cycle_ticks < 1) {
        throw std::invalid_argument("a clip lasts at least one tick, not " + std::to_string(cycle_ticks));
    }
}

void ClipClock::restart() {
    time_ = 0;
    started_ = false;
}

void ClipClock::set_speed(double multiplier) {
    if (!(multiplier >= 0.0)) {  // also rejects NaN
        throw std::invalid_argument("animation speed must be zero or positive, got " + std::to_string(multiplier));
    }
    speed_ = std::llround(multiplier * static_cast<double>(kOne));
}

void ClipClock::set_time(std::int64_t time) {
    time_ = std::max<std::int64_t>(time, 0);
    if (once_) {
        time_ = std::min(time_, cycle_length());
    }
    started_ = true;
}

std::int64_t ClipClock::cycle_time() const {
    if (once_) {
        return std::min(time_, cycle_length());
    }
    return time_ % cycle_length();
}

}  // namespace moteur
