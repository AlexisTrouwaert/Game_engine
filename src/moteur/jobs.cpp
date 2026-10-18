#include "moteur/jobs.hpp"

#include "moteur/profiler.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace moteur {

struct JobHandle::State {
    std::function<void()> job;
    std::atomic<bool> done{false};
    std::exception_ptr error;
    JobPriority priority = JobPriority::Background;
};

bool JobHandle::done() const {
    return state_ == nullptr || state_->done.load(std::memory_order_acquire);
}

struct JobSystem::Shared {
    std::mutex mutex;
    std::condition_variable work;      // a job was queued, or the system stops
    std::condition_variable finished;  // a job finished
    std::deque<std::shared_ptr<JobHandle::State>> frame;       // first
    std::deque<std::shared_ptr<JobHandle::State>> background;  // when no frame job waits
    std::vector<std::thread> threads;
    bool stopping = false;

    bool has_work(bool background_too) const { return !frame.empty() || (background_too && !background.empty()); }

    // Runs one queued job if there is one it may take (lock held on entry and on return).
    bool run_one(std::unique_lock<std::mutex>& lock, bool background_too) {
        std::deque<std::shared_ptr<JobHandle::State>>* queue = !frame.empty() ? &frame
                                                              : (background_too && !background.empty()) ? &background
                                                                                                          : nullptr;
        if (queue == nullptr) {
            return false;
        }
        std::shared_ptr<JobHandle::State> state = std::move(queue->front());
        queue->pop_front();
        lock.unlock();
        try {
            state->job();
        } catch (...) {
            state->error = std::current_exception();
        }
        state->job = nullptr;  // what it captured goes now, not with the last handle
        lock.lock();
        state->done.store(true, std::memory_order_release);
        finished.notify_all();
        return true;
    }

    void worker() {
        std::unique_lock<std::mutex> lock(mutex);
        while (true) {
            work.wait(lock, [this] { return stopping || has_work(true); });
            if (!run_one(lock, true) && stopping) {
                return;
            }
        }
    }
};

JobSystem::JobSystem(int workers) : shared_(std::make_unique<Shared>()) {
    if (workers < 0) {
        workers = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    }
    for (int i = 0; i < workers; ++i) {
        shared_->threads.emplace_back([shared = shared_.get()] { shared->worker(); });
    }
}

JobSystem::~JobSystem() {
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        shared_->stopping = true;
    }
    shared_->work.notify_all();
    for (std::thread& thread : shared_->threads) {
        thread.join();
    }
}

JobHandle JobSystem::submit(std::function<void()> job, JobPriority priority) {
    auto state = std::make_shared<JobHandle::State>();
    state->job = std::move(job);
    state->priority = priority;
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        (priority == JobPriority::Frame ? shared_->frame : shared_->background).push_back(state);
    }
    shared_->work.notify_one();
    return JobHandle(std::move(state));
}

void JobSystem::wait(const JobHandle& job) {
    if (!job.valid()) {
        return;
    }
    // Waiting for frame work: never pick up a load, which may take far longer.
    const bool background_too = job.state_->priority == JobPriority::Background;
    std::unique_lock<std::mutex> lock(shared_->mutex);
    while (!job.state_->done.load(std::memory_order_acquire)) {
        if (!shared_->run_one(lock, background_too)) {
            shared_->finished.wait(lock, [&job, background_too, this] {
                return job.state_->done.load(std::memory_order_acquire) || shared_->has_work(background_too);
            });
        }
    }
    lock.unlock();
    if (job.state_->error) {
        std::rethrow_exception(job.state_->error);
    }
}

void JobSystem::parallel_for(std::size_t count, const std::function<void(std::size_t)>& body, std::size_t grain) {
    if (count == 0) {
        return;
    }
    grain = std::max<std::size_t>(grain, 1);
    std::vector<JobHandle> chunks;
    chunks.reserve((count + grain - 1) / grain);
    for (std::size_t begin = 0; begin < count; begin += grain) {
        const std::size_t end = std::min(count, begin + grain);
        chunks.push_back(submit(
            [&body, begin, end] {
                for (std::size_t i = begin; i < end; ++i) {
                    body(i);
                }
            },
            JobPriority::Frame));
    }
    std::exception_ptr first;
    for (const JobHandle& chunk : chunks) {
        try {
            wait(chunk);
        } catch (...) {
            if (!first) {
                first = std::current_exception();
            }
        }
    }
    if (first) {
        std::rethrow_exception(first);
    }
}

int JobSystem::worker_count() const {
    return static_cast<int>(shared_->threads.size());
}

TaskGraph::TaskId TaskGraph::add(std::string name, std::function<void()> work, std::vector<TaskId> after) {
    const TaskId id = tasks_.size();
    for (const TaskId dependency : after) {
        if (dependency >= id) {
            throw std::logic_error("TaskGraph: '" + name + "' comes after a task added later");
        }
        tasks_[dependency].before.push_back(id);
    }
    tasks_.push_back({std::move(name), std::move(work), std::move(after), {}, 0.0});
    return id;
}

void TaskGraph::clear() {
    tasks_.clear();
}

void TaskGraph::run(JobSystem& system) {
    const std::size_t count = tasks_.size();
    if (count == 0) {
        return;
    }
    // Each task waits for its count of unfinished dependencies; the last of them submits it.
    std::vector<std::atomic<int>> remaining(count);
    std::vector<JobHandle> handles(count);
    std::mutex handles_mutex;
    std::atomic<bool> failed{false};
    std::function<void(TaskId)> submit_task;
    submit_task = [&](TaskId id) {
        JobHandle handle = system.submit(
            [&, id] {
                Task& task = tasks_[id];
#if defined(MOTEUR_TRACY) && !defined(MOTEUR_SHIPPING)
                // A zone per task, on the thread that runs it, named after it.
                ZoneScopedN("task");
                ZoneName(task.name.data(), task.name.size());
#endif
                const auto start = std::chrono::steady_clock::now();
                if (!failed.load(std::memory_order_acquire)) {
                    try {
                        task.work();
                    } catch (...) {
                        failed.store(true, std::memory_order_release);
                        task.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                        for (const TaskId next : task.before) {
                            if (remaining[next].fetch_sub(1, std::memory_order_acq_rel) == 1) {
                                submit_task(next);
                            }
                        }
                        throw;
                    }
                }
                task.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                for (const TaskId next : task.before) {
                    if (remaining[next].fetch_sub(1, std::memory_order_acq_rel) == 1) {
                        submit_task(next);
                    }
                }
            },
            JobPriority::Frame);
        std::lock_guard<std::mutex> lock(handles_mutex);
        handles[id] = std::move(handle);
    };
    for (TaskId id = 0; id < count; ++id) {
        remaining[id].store(static_cast<int>(tasks_[id].after.size()), std::memory_order_relaxed);
    }
    for (TaskId id = 0; id < count; ++id) {
        if (tasks_[id].after.empty()) {
            submit_task(id);
        }
    }
    // In the order of the ids: when task `id` is waited for, every task it comes after was waited
    // for before it, and the last of them submitted it (inside its own job, before being done).
    std::exception_ptr first;
    for (TaskId id = 0; id < count; ++id) {
        JobHandle handle;
        {
            std::lock_guard<std::mutex> lock(handles_mutex);
            handle = handles[id];
        }
        try {
            system.wait(handle);
        } catch (...) {
            if (!first) {
                first = std::current_exception();
            }
        }
    }
    if (first) {
        std::rethrow_exception(first);
    }
}

JobSystem& jobs() {
    static JobSystem system;
    return system;
}

}  // namespace moteur
