#pragma once

#include <cstddef>
#include <exception>
#include <functional>
#include <memory>

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

/// A small pool of worker threads (milestone 8, part 5): loading files in the background, and
/// splitting long computations (the prefiltering of an environment) over the cores. Jobs run in
/// the order they were submitted (one queue, first in first out), each on whichever thread is free.
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
    JobHandle submit(std::function<void()> job);

    /// Until the job is done, the calling thread runs other queued jobs (it never sleeps while
    /// there is work). Rethrows the job's exception (each wait() on that job rethrows it). Returns
    /// at once for an empty handle.
    void wait(const JobHandle& job);

    /// body(i) for every i in [0, count), in chunks of `grain` (0 counts as 1), over the workers and
    /// the calling thread; returns once all ran. The first exception thrown is rethrown (the others
    /// are lost), once every chunk has finished. `body` is called concurrently: it must be safe to
    /// call from several threads at once.
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

}  // namespace moteur
