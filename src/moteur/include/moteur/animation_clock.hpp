#pragma once

#include <cstdint>
#include <vector>

namespace moteur {

// The time of a playing clip, shared by the sprite animations of milestone 2 (AnimationPlayer) and
// the skeletal ones of milestone 5 (Animator): whole ticks of the fixed step, a speed in
// thousandths, and marks (events) crossed exactly once. Integers only, so a clip plays the same
// on every machine and every OS, whatever the frame rate.
//
// A clip lasts `cycle_ticks` ticks. It plays once (and stays at its end) or loops. Its marks are
// points of a cycle, in ticks from its start (a frame change, a footstep): advance() reports
// every mark it crosses, even when a big step skips several, or whole loops.
class ClipClock {
public:
    // Time is kept in thousandths of a tick, and the speed in thousandths: 1000 means x1.
    static constexpr std::int64_t kOne = 1000;

    ClipClock() = default;
    // Throws std::invalid_argument if `cycle_ticks` is less than 1.
    ClipClock(int cycle_ticks, bool once);

    // Back to the start, as a new clip: the marks at 0 fire on the next advance.
    void restart();

    // Throws std::invalid_argument if negative (0 pauses). Rounded to a thousandth.
    void set_speed(double multiplier);
    double speed() const { return static_cast<double>(speed_) / static_cast<double>(kOne); }
    std::int64_t speed_thousandths() const { return speed_; }
    void set_speed_thousandths(std::int64_t speed) { speed_ = speed < 0 ? 0 : speed; }

    // Moves forward by `ticks` steps, scaled by the speed. Calls `on_mark(index)` for each entry of
    // `marks` (ticks from the start of a cycle, sorted, each within [0, cycle_ticks)) whose point
    // is crossed, in order, each exactly once: the points in (previous time, new time], and the
    // one at the very start right after restart(). Returns false when nothing moved.
    template <typename OnMark>
    bool advance(int ticks, const std::vector<int>& marks, OnMark&& on_mark);
    bool advance(int ticks) { return advance(ticks, {}, [](std::size_t) {}); }

    // Time since the start, in thousandths of a tick (not wrapped: a loop keeps counting).
    std::int64_t time() const { return time_; }
    // Puts the clock at `time` (clamped for a clip played once). The marks at that exact time are
    // not "crossed": they do not fire on the next advance.
    void set_time(std::int64_t time);

    int cycle_ticks() const { return cycle_ticks_; }
    std::int64_t cycle_length() const { return static_cast<std::int64_t>(cycle_ticks_) * kOne; }
    bool once() const { return once_; }
    // Where the clock is in the current cycle, in thousandths of a tick: [0, cycle_length()), or
    // exactly cycle_length() for a clip played once and finished.
    std::int64_t cycle_time() const;
    // Only a clip played once finishes.
    bool finished() const { return once_ && time_ >= cycle_length(); }
    bool started() const { return started_; }

    // Everything the clock is (saves, milestone 7): written and given back exactly.
    struct State {
        int cycle_ticks = 1;
        bool once = false;
        std::int64_t time = 0;
        std::int64_t speed = kOne;
        bool started = false;
    };
    State state() const { return {cycle_ticks_, once_, time_, speed_, started_}; }
    void set_state(const State& state) {
        cycle_ticks_ = state.cycle_ticks > 0 ? state.cycle_ticks : 1;
        once_ = state.once;
        time_ = state.time;
        speed_ = state.speed < 0 ? 0 : state.speed;
        started_ = state.started;
    }

private:
    int cycle_ticks_ = 1;
    bool once_ = false;
    std::int64_t time_ = 0;
    std::int64_t speed_ = kOne;
    bool started_ = false;  // false until the marks at 0 have had their chance to fire
};

template <typename OnMark>
bool ClipClock::advance(int ticks, const std::vector<int>& marks, OnMark&& on_mark) {
    const std::int64_t previous = time_;
    const std::int64_t cycle = cycle_length();
    time_ += static_cast<std::int64_t>(ticks > 0 ? ticks : 0) * speed_;
    if (once_ && time_ > cycle) {
        time_ = cycle;
    }
    const bool include_previous = !started_;
    started_ = true;
    if (marks.empty()) {
        return time_ != previous;
    }
    // Every mark in (previous, time_], or [previous, time_] right after a restart: the intervals of
    // successive advances touch without overlapping, so each point is seen once.
    for (std::int64_t cycle_index = previous / cycle;; ++cycle_index) {
        const std::int64_t cycle_start = cycle_index * cycle;
        if ((once_ && cycle_index > 0) || cycle_start > time_) {
            break;
        }
        bool past = false;
        for (std::size_t m = 0; m < marks.size(); ++m) {
            const std::int64_t point = cycle_start + static_cast<std::int64_t>(marks[m]) * kOne;
            if (point > time_) {
                past = true;
                break;
            }
            if (point < previous || (point == previous && !include_previous)) {
                continue;
            }
            on_mark(m);
        }
        if (past) {
            break;
        }
    }
    return time_ != previous;
}

}  // namespace moteur
