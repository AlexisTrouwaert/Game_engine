#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <numeric>
#include <stdexcept>
#include <vector>

#include "moteur/jobs.hpp"

TEST_CASE("jobs: every submitted job runs once, and wait() sees its result") {
    moteur::JobSystem system(3);
    std::vector<int> values(100, 0);
    std::vector<moteur::JobHandle> handles;
    for (int i = 0; i < 100; ++i) {
        handles.push_back(system.submit([&values, i] { values[static_cast<std::size_t>(i)] = i * i; }));
    }
    for (const moteur::JobHandle& handle : handles) {
        system.wait(handle);
        CHECK(handle.done());
    }
    for (int i = 0; i < 100; ++i) {
        CHECK(values[static_cast<std::size_t>(i)] == i * i);
    }
}

TEST_CASE("jobs: parallel_for covers every index exactly once, whatever the grain") {
    moteur::JobSystem system(4);
    for (std::size_t grain : {1u, 3u, 64u, 1000u}) {
        std::vector<std::atomic<int>> hits(997);
        system.parallel_for(hits.size(), [&hits](std::size_t i) { hits[i].fetch_add(1); }, grain);
        int wrong = 0;
        for (const std::atomic<int>& hit : hits) {
            wrong += hit.load() == 1 ? 0 : 1;
        }
        CHECK(wrong == 0);
    }
}

TEST_CASE("jobs: an exception reaches the waiting thread") {
    moteur::JobSystem system(2);
    const moteur::JobHandle failing = system.submit([] { throw std::runtime_error("raté"); });
    CHECK_THROWS_WITH_AS(system.wait(failing), "raté", std::runtime_error);
    CHECK(failing.done());
    CHECK_THROWS_AS(system.parallel_for(10, [](std::size_t i) {
        if (i == 7) {
            throw std::runtime_error("ligne 7");
        }
    }),
                    std::runtime_error);
}

TEST_CASE("jobs: a job may submit and wait for others (no deadlock with one worker)") {
    moteur::JobSystem system(1);
    std::atomic<int> sum{0};
    const moteur::JobHandle outer = system.submit([&system, &sum] {
        std::vector<moteur::JobHandle> inner;
        for (int i = 1; i <= 10; ++i) {
            inner.push_back(system.submit([&sum, i] { sum += i; }));
        }
        for (const moteur::JobHandle& handle : inner) {
            system.wait(handle);
        }
    });
    system.wait(outer);
    CHECK(sum.load() == 55);
}

TEST_CASE("jobs: a task graph runs each task after those it waits for, again and again") {
    for (const int workers : {0, 1, 4}) {
        moteur::JobSystem system(workers);
        moteur::TaskGraph graph;
        std::mutex lock;
        std::vector<std::string> order;
        const auto log = [&](const char* name) {
            return [&, name] {
                const std::scoped_lock guard(lock);
                order.emplace_back(name);
            };
        };
        // A diamond and a lone task: a before b and c, both before d.
        const auto a = graph.add("a", log("a"));
        const auto b = graph.add("b", log("b"), {a});
        const auto c = graph.add("c", log("c"), {a});
        const auto d = graph.add("d", log("d"), {b, c});
        graph.add("e", log("e"));
        REQUIRE(graph.size() == 5);
        CHECK(graph.name(d) == "d");
        for (int run = 0; run < 3; ++run) {
            order.clear();
            graph.run(system);
            REQUIRE(order.size() == 5);
            const auto at = [&](const char* name) { return std::find(order.begin(), order.end(), name) - order.begin(); };
            CHECK(at("a") < at("b"));
            CHECK(at("a") < at("c"));
            CHECK(at("b") < at("d"));
            CHECK(at("c") < at("d"));
            CHECK(graph.milliseconds(d) >= 0.0);
        }
        graph.clear();
        CHECK(graph.size() == 0);
        graph.run(system);  // nothing to do
    }
}

TEST_CASE("jobs: a task that throws stops those after it, and the exception reaches run()") {
    moteur::JobSystem system(2);
    moteur::TaskGraph graph;
    std::atomic<int> ran{0};
    const auto failing = graph.add("failing", [] { throw std::runtime_error("tâche"); });
    graph.add("after", [&ran] { ++ran; }, {failing});
    graph.add("aside", [&ran] { ran += 10; });
    CHECK_THROWS_WITH_AS(graph.run(system), "tâche", std::runtime_error);
    // The one after the failure never runs; the lone one, only if it started before the failure.
    CHECK((ran.load() == 0 || ran.load() == 10));
}

TEST_CASE("jobs: the frame's jobs go before the background's") {
    moteur::JobSystem system(1);
    // The worker held busy while both kinds are queued: then it takes the frame's first.
    std::atomic<bool> release{false};
    const moteur::JobHandle hold = system.submit([&release] {
        while (!release.load()) {
            std::this_thread::yield();
        }
    });
    std::mutex lock;
    std::vector<int> order;
    std::vector<moteur::JobHandle> handles;
    for (int i = 0; i < 3; ++i) {
        handles.push_back(system.submit([&, i] {
            const std::scoped_lock guard(lock);
            order.push_back(100 + i);
        }));
    }
    for (int i = 0; i < 3; ++i) {
        handles.push_back(system.submit([&, i] {
            const std::scoped_lock guard(lock);
            order.push_back(i);
        }, moteur::JobPriority::Frame));
    }
    release = true;
    system.wait(hold);
    for (const moteur::JobHandle& handle : handles) {
        system.wait(handle);
    }
    // Waiting from this thread may have run some itself: the worker's own order is what is
    // checked, through the first one taken after the hold.
    REQUIRE(order.size() == 6);
    CHECK(order.front() < 100);
}
