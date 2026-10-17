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

/// Where the time goes (milestone 7, part 4): zones measured by MOTEUR_PROFILE("name"), nested, in a
/// tree per frame; the last frames kept for the DEBUG > Profiler window (graph of the frame times,
/// the tree of a chosen frame, statistics per zone), a capture written in the Chrome Trace format
/// (Perfetto, chrome://tracing), and the same zones sent to Tracy when built with MOTEUR_TRACY.
///
/// One zone per system, not per entity: measuring costs. Only the main thread is measured (the
/// thread that made the Profiler: zones on other threads are ignored, Tracy sees them). The names
/// must live for the whole run (literals).
///
/// ```
/// void separate_colliders(...) {
///     MOTEUR_PROFILE("collisions");
///     ...
/// }
/// ```
///
/// Times are in performance counter ticks (SDL_GetPerformanceCounter()); milliseconds() converts.
class Profiler {
public:
    /// One measured zone of a frame.
    struct Zone {
        const char* name;      ///< as given to MOTEUR_PROFILE (a literal)
        std::uint64_t start;   ///< performance counter
        std::uint64_t end;     ///< performance counter; a zone left open ends with its frame
        std::uint32_t parent;  ///< index in the frame's zones; kNoParent for a root
        std::uint16_t depth;   ///< 0 for a root, its parent's depth + 1 otherwise
    };
    /// Zone::parent of a root zone.
    static constexpr std::uint32_t kNoParent = 0xFFFFFFFFu;
    /// One measured frame: from begin_frame() to end_frame().
    struct Frame {
        std::uint64_t index = 0;  ///< frames are numbered from 0, in order (unmeasured ones included)
        std::uint64_t start = 0;  ///< performance counter, at begin_frame()
        std::uint64_t end = 0;    ///< performance counter, at end_frame()
        std::vector<Zone> zones;  ///< in the order they began
    };
    /// Statistics of the zones of one name (a zone run several times in a frame adds up).
    struct Summary {
        std::string name;       ///< the zone's name
        double mean_ms = 0.0;   ///< per frame where it ran
        double p99_ms = 0.0;    ///< the 99th percentile, per frame where it ran
        double max_ms = 0.0;    ///< the longest, per frame where it ran
        double calls = 0.0;     ///< per frame, mean
    };

    /// A profiler measuring the calling thread; not yet current().
    Profiler();
    /// Stops being current() if it was.
    ~Profiler();
    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    /// The profiler the macros feed (the Application's); null: they do nothing.
    static Profiler* current() { return current_; }
    /// Makes `profiler` the one the macros feed (null: none). Not thread-safe: at start-up.
    static void set_current(Profiler* profiler) { current_ = profiler; }

    /// Off: the zones are not recorded (the macros cost a test). On by default. Takes effect at the
    /// next begin_frame().
    void set_enabled(bool enabled) { enabled_ = enabled; }
    /// Whether the zones are recorded (set_enabled()).
    bool enabled() const { return enabled_; }
    /// Frozen: frames are still measured (the totals and a capture go on), but the history stops
    /// (to look at it).
    void set_frozen(bool frozen) { frozen_ = frozen; }
    /// Whether the history is stopped (set_frozen()).
    bool frozen() const { return frozen_; }

    /// Starts a frame (the Application, at the top of its loop); measures nothing while disabled.
    void begin_frame();
    /// Ends the frame: closes the zones left open, adds the frame to the totals, the capture and
    /// (unless frozen) the history.
    void end_frame();
    /// Opens a zone inside the current one; prefer MOTEUR_PROFILE, which closes it. `name` must live
    /// for the whole run. Ignored outside a frame and on another thread than the main one.
    void begin_zone(const char* name);
    /// Closes the zone opened last; ignored when none is open, outside a frame and on another thread.
    void end_zone();

    /// How many frames history() keeps.
    static constexpr std::size_t kHistory = 600;
    /// The last frames, oldest first (at most kHistory). Changed by end_frame().
    const std::deque<Frame>& history() const { return history_; }
    /// Statistics per zone over the frames of the history, sorted by mean time (largest first).
    /// Walks every zone of the history: not for every frame.
    std::vector<Summary> summary() const;
    /// Statistics of the whole run (since the start or reset_totals()), for --report.
    std::vector<Summary> totals() const;
    /// Restarts the statistics of totals().
    void reset_totals() { totals_.clear(); }

    /// Capture: every frame from start_capture() to stop_capture(), written as Chrome Trace JSON.
    /// Starting again drops what was captured. The frames are kept in memory until then.
    void start_capture();
    /// Whether a capture is running (start_capture()).
    bool capturing() const { return capturing_; }
    /// Ends the capture and writes the captured frames to `path` (UTF-8; the folders are made).
    /// False if it cannot be written.
    bool stop_capture(const std::string& path);
    /// The Chrome Trace JSON of some frames (for tests and stop_capture), times in microseconds from
    /// the first frame's start.
    static std::string chrome_trace(const std::vector<Frame>& frames);

    /// A duration in performance counter ticks, in milliseconds.
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
    /// The pointer standing for each name text: the first one seen with that text. Two literals of
    /// the same text may have different addresses (other translation units): they are one zone.
    const char* canonical_name(const char* name);
    std::unordered_map<const char*, const char*> canonical_;  ///< any pointer seen -> its text's pointer
    std::unordered_map<std::string, const char*> by_text_;   ///< name text -> its pointer
    /// By canonical name pointer (cheap per frame): one entry per name text.
    std::unordered_map<const char*, Totals> totals_;
    std::vector<std::pair<const char*, std::pair<double, int>>> per_frame_;  ///< reused each frame
    bool capturing_ = false;
    std::vector<Frame> capture_;
    std::uint64_t next_index_ = 0;
};

/// Measures the enclosing scope as a zone (see Profiler); what MOTEUR_PROFILE declares.
class ProfileScope {
public:
    /// Opens the zone `name` in Profiler::current(), if there is one and it is enabled; the
    /// destructor closes it. `name` must live for the whole run (a literal).
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

/// Pastes two tokens after expanding them (MOTEUR_PROFILE names its variable with the line).
///@{
#define MOTEUR_PROFILE_CONCAT_INNER(a, b) a##b
#define MOTEUR_PROFILE_CONCAT(a, b) MOTEUR_PROFILE_CONCAT_INNER(a, b)
///@}

/// \def MOTEUR_PROFILE(name)
/// Measures the rest of the enclosing scope as the zone `name` (a string literal): a
/// moteur::ProfileScope, plus a Tracy zone when built with MOTEUR_TRACY. A statement; at most one
/// per line. Nothing at all in a MOTEUR_SHIPPING build.

/// \def MOTEUR_PROFILE_FRAME()
/// Marks the end of a frame for Tracy (Profiler::end_frame() does it); nothing without MOTEUR_TRACY.

#if defined(MOTEUR_SHIPPING)
// The shipped game measures nothing (milestone 8, part 6): the zones cost nothing at all.
#define MOTEUR_PROFILE(name) ((void)0)
#define MOTEUR_PROFILE_FRAME() ((void)0)
#elif defined(MOTEUR_TRACY)
#define MOTEUR_PROFILE(name)                                                                  \
    ZoneScopedN(name);                                                                        \
    ::moteur::ProfileScope MOTEUR_PROFILE_CONCAT(moteur_profile_scope_, __LINE__)(name)
#define MOTEUR_PROFILE_FRAME() FrameMark
#else
#define MOTEUR_PROFILE(name) ::moteur::ProfileScope MOTEUR_PROFILE_CONCAT(moteur_profile_scope_, __LINE__)(name)
#define MOTEUR_PROFILE_FRAME() ((void)0)
#endif
