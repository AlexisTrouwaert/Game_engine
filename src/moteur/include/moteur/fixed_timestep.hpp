#pragma once

namespace moteur {

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
