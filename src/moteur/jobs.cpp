#include "moteur/jobs.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace moteur {

struct JobHandle::State {
    std::function<void()> job;
    std::atomic<bool> done{false};
    std::exception_ptr error;
};

bool JobHandle::done() const {
    return state_ == nullptr || state_->done.load(std::memory_order_acquire);
}

struct JobSystem::Shared {
    std::mutex mutex;
    std::condition_variable work;      // a job was queued, or the system stops
    std::condition_variable finished;  // a job finished
    std::deque<std::shared_ptr<JobHandle::State>> queue;
    std::vector<std::thread> threads;
    bool stopping = false;

    // Runs one queued job if there is one (lock held on entry and on return).
    bool run_one(std::unique_lock<std::mutex>& lock) {
        if (queue.empty()) {
            return false;
        }
        std::shared_ptr<JobHandle::State> state = std::move(queue.front());
        queue.pop_front();
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
            work.wait(lock, [this] { return stopping || !queue.empty(); });
            if (!run_one(lock) && stopping) {
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

JobHandle JobSystem::submit(std::function<void()> job) {
    auto state = std::make_shared<JobHandle::State>();
    state->job = std::move(job);
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        shared_->queue.push_back(state);
    }
    shared_->work.notify_one();
    return JobHandle(std::move(state));
}

void JobSystem::wait(const JobHandle& job) {
    if (!job.valid()) {
        return;
    }
    std::unique_lock<std::mutex> lock(shared_->mutex);
    while (!job.state_->done.load(std::memory_order_acquire)) {
        if (!shared_->run_one(lock)) {
            shared_->finished.wait(lock, [&job, this] {
                return job.state_->done.load(std::memory_order_acquire) || !shared_->queue.empty();
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
        chunks.push_back(submit([&body, begin, end] {
            for (std::size_t i = begin; i < end; ++i) {
                body(i);
            }
        }));
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

JobSystem& jobs() {
    static JobSystem system;
    return system;
}

}  // namespace moteur
