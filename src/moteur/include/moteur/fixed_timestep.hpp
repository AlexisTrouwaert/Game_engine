#pragma once

namespace moteur {

// A value between the previous tick (t = 0) and the current one (t = 1), for drawing between two
// fixed steps (FixedTimestep::alpha()). Computed as previous + (current - previous) * t, which is
// exactly `previous` when nothing moved, whatever t: glm::mix computes previous * (1 - t) +
// current * t, off by a rounding that changes with t, so a still scene would shift by a few 1e-7
// from frame to frame (enough to flip pixels on shadow edges and to break identical captures).
template <typename T>
T interpolate(const T& previous, const T& current, float t) {
    return previous + (current - previous) * t;
}

// Turns variable frame times into a whole number of fixed simulation steps.
//
//   const int steps = timestep.advance(frame_time);
//   for (int i = 0; i < steps; ++i) update(timestep.step());
//   render(timestep.alpha());
class FixedTimestep {
public:
    // step: duration of one update, in seconds.
    // max_frame_time: longest frame time taken into account, in seconds. It caps the catch-up
    // after a stall (debugger break, window drag) so the simulation cannot spiral out of control.
    // Throws std::invalid_argument if either value is not positive.
    FixedTimestep(double step, double max_frame_time);

    // Adds the real time elapsed since the previous frame and returns how many fixed updates
    // to run now. Negative times count as zero.
    int advance(double frame_time);

    double step() const { return step_; }

    // How far the current frame is between the last update and the next one, in [0, 1).
    double alpha() const { return accumulator_ / step_; }

private:
    double step_;
    double max_frame_time_;
    double accumulator_ = 0.0;
};

}  // namespace moteur
