#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "moteur/frame_stats.hpp"

#if defined(MOTEUR_TRACY)
#include <tracy/Tracy.hpp>
#endif

namespace moteur {

// Where the time goes (milestone 7, part 4): zones measured by MOTEUR_PROFILE("name"), nested, in a
// tree per frame; the last frames kept for the DEBUG > Profiler window (graph of the frame times,
// the tree of a chosen frame, statistics per zone), a capture written in the Chrome Trace format
// (Perfetto, chrome://tracing), and the same zones sent to Tracy when built with MOTEUR_TRACY.
//
// One zone per system, not per entity: measuring costs. Only the main thread is measured (zones on
// other threads are ignored, Tracy sees them). The names must live for the whole run (literals).
//
//   void separate_colliders(...) {
//       MOTEUR_PROFILE("collisions");
//       ...
//   }
class Profiler {
public:
    struct Zone {
        const char* name;
        std::uint64_t start;   // performance counter
        std::uint64_t end;
        std::uint32_t parent;  // index in the frame's zones; kNoParent for a root
        std::uint16_t depth;
    };
    static constexpr std::uint32_t kNoParent = 0xFFFFFFFFu;
    struct Frame {
        std::uint64_t index = 0;
        std::uint64_t start = 0;
        std::uint64_t end = 0;
        std::vector<Zone> zones;  // in the order they began
    };
    struct Summary {
        std::string name;
        double mean_ms = 0.0;   // per frame where it ran
        double p99_ms = 0.0;
        double max_ms = 0.0;
        double calls = 0.0;     // per frame, mean
    };

    Profiler();
    ~Profiler();
    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    // The profiler the macros feed (the Application's); null: they do nothing.
    static Profiler* current() { return current_; }
    static void set_current(Profiler* profiler) { current_ = profiler; }

    // Off: the zones are not recorded (the macros cost a test). On by default.
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }
    // Frozen: frames are still measured for Tracy, but the history stops (to look at it).
    void set_frozen(bool frozen) { frozen_ = frozen; }
    bool frozen() const { return frozen_; }

    void begin_frame();
    void end_frame();
    void begin_zone(const char* name);
    void end_zone();

    // The last frames, oldest first (at most kHistory).
    static constexpr std::size_t kHistory = 600;
    const std::deque<Frame>& history() const { return history_; }
    // Statistics per zone over the frames of the history, sorted by mean time (largest first).
    std::vector<Summary> summary() const;
    // Statistics of the whole run (since the start or reset_totals()), for --report.
    std::vector<Summary> totals() const;
    void reset_totals() { totals_.clear(); }

    // Capture: every frame from start_capture() to stop_capture(), written as Chrome Trace JSON.
    void start_capture();
    bool capturing() const { return capturing_; }
    // Writes the captured frames to `path`. False if it cannot be written.
    bool stop_capture(const std::string& path);
    // The Chrome Trace JSON of some frames (for tests and stop_capture).
    static std::string chrome_trace(const std::vector<Frame>& frames);

    static double milliseconds(std::uint64_t counter_ticks);

private:
    struct Totals {
        FrameStats ms{65536};
        double calls = 0.0;
        double frames = 0.0;
    };
    static Profiler* current_;
    bool enabled_ = true;
    bool frozen_ = false;
    bool in_frame_ = false;
    std::thread::id main_thread_;
    Frame frame_;
    std::vector<std::uint32_t> stack_;
    std::deque<Frame> history_;
    // By name pointer (cheap per frame); merged by name text when read.
    std::unordered_map<const char*, Totals> totals_;
    std::vector<std::pair<const char*, std::pair<double, int>>> per_frame_;  // reused each frame
    bool capturing_ = false;
    std::vector<Frame> capture_;
    std::uint64_t next_index_ = 0;
};

// Measures the enclosing scope as a zone (see Profiler).
class ProfileScope {
public:
    explicit ProfileScope(const char* name) {
        Profiler* profiler = Profiler::current();
        if (profiler != nullptr && profiler->enabled()) {
            profiler_ = profiler;
            profiler_->begin_zone(name);
        }
    }
    ~ProfileScope() {
        if (profiler_ != nullptr) {
            profiler_->end_zone();
        }
    }
    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

private:
    Profiler* profiler_ = nullptr;
};

}  // namespace moteur

#define MOTEUR_PROFILE_CONCAT_INNER(a, b) a##b
#define MOTEUR_PROFILE_CONCAT(a, b) MOTEUR_PROFILE_CONCAT_INNER(a, b)
#if defined(MOTEUR_TRACY)
#define MOTEUR_PROFILE(name)                                                                  \
    ZoneScopedN(name);                                                                        \
    ::moteur::ProfileScope MOTEUR_PROFILE_CONCAT(moteur_profile_scope_, __LINE__)(name)
#define MOTEUR_PROFILE_FRAME() FrameMark
#else
#define MOTEUR_PROFILE(name) ::moteur::ProfileScope MOTEUR_PROFILE_CONCAT(moteur_profile_scope_, __LINE__)(name)
#define MOTEUR_PROFILE_FRAME() ((void)0)
#endif
