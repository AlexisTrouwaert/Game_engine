#include "moteur/fixed_timestep.hpp"

#include <algorithm>
#include <stdexcept>

namespace moteur {

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
