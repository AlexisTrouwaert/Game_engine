#pragma once

#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace moteur {

/// A job submitted to a JobSystem: done() once it ran (or threw). Cheap to copy (a shared
/// reference); the job runs whether or not a handle to it is kept.
class JobHandle {
public:
    /// An empty handle, standing for no job: valid() is false, done() true.
    JobHandle() = default;
    /// Whether the handle stands for a job (it came from JobSystem::submit()).
    bool valid() const { return state_ != nullptr; }
    /// Whether the job has finished running, normally or by throwing (its exception is then given
    /// by JobSystem::wait()); true for an empty handle. Never blocks; any thread.
    bool done() const;

private:
    friend class JobSystem;
    struct State;
    explicit JobHandle(std::shared_ptr<State> state) : state_(std::move(state)) {}
    std::shared_ptr<State> state_;
};

/// How urgent a job is (milestone 9, part 9). The frame's work never waits behind a load: workers
/// take the frame's jobs first, and a thread waiting for a frame job helps only with frame jobs (a
/// load it would pick up could take far longer than the frame).
enum class JobPriority {
    Frame,       ///< the work of the current frame (parallel_for(), TaskGraph): first
    Background,  ///< loading, importing, baking: when no frame work waits
};

/// A small pool of worker threads (milestone 8, part 5): loading files in the background, and
/// splitting the frame's computations over the cores (milestone 9, part 9). Jobs of one priority run
/// in the order they were submitted (first in first out), each on whichever thread is free; frame
/// jobs before background ones.
///
/// Rules of the engine:
/// - the game's logic never runs on it: the order of its computations must not depend on threads
///   (determinism of the replays and of the saves);
/// - a job never touches the GPU, the asset caches nor the ECS registries: it computes data that
///   the main thread then uses;
/// - a job that throws is done; wait() rethrows its exception on the waiting thread.
///
/// submit(), wait() and parallel_for() may be called from any thread, jobs included.
class JobSystem {
public:
    /// Starts `workers` threads; -1: the hardware threads minus one (the main thread), at least
    /// one. With 0, jobs only run inside wait() and parallel_for(), on the calling thread.
    explicit JobSystem(int workers = -1);
    /// Finishes the queued jobs, then stops the threads (blocks until then). Without workers, the
    /// jobs nobody waited for are dropped without running.
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    /// Queues `job` and returns at once, with its handle. What the job captures is released once it
    /// ran, even while handles to it remain: references it captures must stay valid until then.
    /// Background by default (what loads); the frame's work is JobPriority::Frame.
    JobHandle submit(std::function<void()> job, JobPriority priority = JobPriority::Background);

    /// Until the job is done, the calling thread runs other queued jobs (it never sleeps while
    /// there is work it may do): frame jobs, and background ones only when it waits for a background
    /// job. Rethrows the job's exception (each wait() on that job rethrows it). Returns at once for
    /// an empty handle.
    void wait(const JobHandle& job);

    /// body(i) for every i in [0, count), in chunks of `grain` (0 counts as 1), over the workers and
    /// the calling thread, as frame jobs; returns once all ran. The first exception thrown is
    /// rethrown (the others are lost), once every chunk has finished. `body` is called concurrently:
    /// it must be safe to call from several threads at once.
    void parallel_for(std::size_t count, const std::function<void(std::size_t)>& body, std::size_t grain = 1);

    /// The number of worker threads (the calling threads that help in wait() not counted).
    int worker_count() const;

private:
    struct Shared;
    std::unique_ptr<Shared> shared_;
};

/// The process's job system, created at its first use (thread-safe) with the default number of
/// workers, and stopped when the program exits (after main() returns). The engine's background
/// loading (Assets::prefetch_model()...) uses this one.
JobSystem& jobs();

/// A frame's work as tasks with dependencies (milestone 9, part 9): each task runs once those it
/// comes after are done, on the job system's workers and the calling thread (which helps until
/// all are done), as frame jobs. The engine builds one for the preparation of the rendering; a game
/// adds its own systems the same way. The order of the results never depends on the threads: a
/// task writes only its own outputs, which the code after run() reads in a fixed order.
///
/// ```
/// moteur::TaskGraph graph;
/// const auto lods = graph.add("lods", [&] { select_lods(); });
/// graph.add("scene batches", [&] { build_scene(); }, {lods});
/// graph.add("shadow batches", [&] { build_shadows(); }, {lods});
/// graph.run();
/// ```
class TaskGraph {
public:
    /// A task of the graph: its index (the order it was added in).
    using TaskId = std::size_t;

    /// Adds a task that runs after every task of `after` (added before it). Returns its id.
    TaskId add(std::string name, std::function<void()> work, std::vector<TaskId> after = {});
    /// Runs every task and returns when all are done; rethrows the first exception thrown (once
    /// a task threw, the tasks not started yet do not run: those after it never do). The tasks stay: run() again runs them again.
    void run(JobSystem& system = jobs());
    /// Removes every task.
    void clear();
    /// The number of tasks.
    std::size_t size() const { return tasks_.size(); }
    /// A task's name, and how long it took at the last run(), in milliseconds (for the profiler's
    /// panel: the zones of other threads than the main one are Tracy's).
    const std::string& name(TaskId task) const { return tasks_[task].name; }
    /// See name().
    double milliseconds(TaskId task) const { return tasks_[task].milliseconds; }

private:
    struct Task {
        std::string name;
        std::function<void()> work;
        std::vector<TaskId> after;
        std::vector<TaskId> before;  ///< the tasks waiting for it
        double milliseconds = 0.0;
    };
    std::vector<Task> tasks_;
};

}  // namespace moteur
