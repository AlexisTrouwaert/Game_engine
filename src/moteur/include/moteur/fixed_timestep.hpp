#pragma once

#include <cstdint>

namespace moteur {

/// The rate of the logic, in ticks per second: one value for the whole program, 60 unless the game
/// sets ApplicationConfig::tick_rate (Application sets it before the first tick). Everything the
/// logic counts in ticks (animation clips and their events, timers of the engine and of the game)
/// is written in seconds and converted with ticks_from_seconds(): a game at another rate keeps its
/// durations. The replays and saves of a game assume the rate they were made at (review of
/// milestones 1 to 7, 6.3: before, 60 was assumed in several places). Any thread (an atomic value:
/// the job system's threads read it for clip durations).
int tick_rate();
/// Sets tick_rate(). Throws std::invalid_argument outside [1, 1000]. Not while ticks run: the
/// durations already converted would no longer match (Application calls it in its constructor).
void set_tick_rate(int ticks_per_second);
/// Seconds to ticks at tick_rate(), rounded to the nearest tick (halves away from zero); a negative
/// duration gives a negative count.
std::int64_t ticks_from_seconds(double seconds);

/// A value between the previous tick (t = 0) and the current one (t = 1), for drawing between two
/// fixed steps (FixedTimestep::alpha()). Computed as previous + (current - previous) * t, which is
/// exactly `previous` when nothing moved, whatever t: glm::mix computes previous * (1 - t) +
/// current * t, off by a rounding that changes with t, so a still scene would shift by a few 1e-7
/// from frame to frame (enough to flip pixels on shadow edges and to break identical captures).
/// `T` needs `+`, `-` and a multiplication by a float (float, glm vectors). Not for rotations
/// stored as quaternions: interpolate those with glm::slerp.
template <typename T>
T interpolate(const T& previous, const T& current, float t) {
    return previous + (current - previous) * t;
}

/// Turns variable frame times into a whole number of fixed simulation steps.
///
/// @code{.cpp}
/// const int steps = timestep.advance(frame_time);
/// for (int i = 0; i < steps; ++i) update(timestep.step());
/// render(timestep.alpha());
/// @endcode
///
/// Application::run() drives one for the game; a tool or a test may use its own.
class FixedTimestep {
public:
    /// `step`: duration of one update, in seconds.
    /// `max_frame_time`: longest frame time taken into account, in seconds. It caps the catch-up
    /// after a stall (debugger break, window drag) so the simulation cannot spiral out of control:
    /// at most about max_frame_time / step updates per frame.
    /// Throws std::invalid_argument if either value is not positive.
    FixedTimestep(double step, double max_frame_time);

    /// Adds the real time elapsed since the previous frame (in seconds, clamped to [0,
    /// max_frame_time]) and returns how many fixed updates to run now (0 or more). The remainder
    /// carries over to the next frame.
    int advance(double frame_time);

    /// The duration of one update, in seconds, as given to the constructor: the `dt` to pass to
    /// each update.
    double step() const { return step_; }

    /// How far the current frame is between the last update and the next one, in [0, 1).
    double alpha() const { return accumulator_ / step_; }

private:
    double step_;
    double max_frame_time_;
    double accumulator_ = 0.0;
};

}  // namespace moteur
