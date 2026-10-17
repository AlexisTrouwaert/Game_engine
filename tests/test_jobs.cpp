#include <doctest/doctest.h>

#include <atomic>
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
