#include <doctest/doctest.h>

#include <stdexcept>

#include "moteur/frame_stats.hpp"

TEST_CASE("FrameStats reports zero when empty") {
    moteur::FrameStats stats;
    CHECK(stats.count() == 0);
    CHECK(stats.mean() == 0.0);
    CHECK(stats.max() == 0.0);
    CHECK(stats.percentile(99) == 0.0);
}

TEST_CASE("FrameStats computes mean and maximum") {
    moteur::FrameStats stats;
    for (int i = 1; i <= 100; ++i) {
        stats.add(i);
    }
    CHECK(stats.count() == 100);
    CHECK(stats.mean() == doctest::Approx(50.5));
    CHECK(stats.max() == 100.0);
}

TEST_CASE("FrameStats percentiles use the nearest rank") {
    moteur::FrameStats stats;
    for (int i = 1; i <= 100; ++i) {
        stats.add(i);
    }
    CHECK(stats.percentile(50) == 50.0);
    CHECK(stats.percentile(99) == 99.0);
    CHECK(stats.percentile(100) == 100.0);
    CHECK(stats.percentile(0) == 1.0);
}

TEST_CASE("FrameStats percentiles do not depend on insertion order") {
    moteur::FrameStats stats;
    for (const double v : {5.0, 1.0, 4.0, 2.0, 3.0}) {
        stats.add(v);
    }
    CHECK(stats.percentile(50) == 3.0);
    CHECK(stats.percentile(100) == 5.0);
}

TEST_CASE("FrameStats catches a rare spike that the mean hides") {
    moteur::FrameStats stats;
    for (int i = 0; i < 99; ++i) {
        stats.add(1.0);
    }
    stats.add(50.0);
    CHECK(stats.mean() < 2.0);
    CHECK(stats.max() == 50.0);
    CHECK(stats.percentile(99) == 1.0);
    CHECK(stats.percentile(100) == 50.0);
}

TEST_CASE("FrameStats only keeps the latest samples once full") {
    moteur::FrameStats stats(3);
    for (int i = 1; i <= 5; ++i) {
        stats.add(i);
    }
    CHECK(stats.count() == 3);
    CHECK(stats.mean() == doctest::Approx(4.0));  // 3, 4, 5
    CHECK(stats.max() == 5.0);
    CHECK(stats.percentile(0) == 3.0);
}

TEST_CASE("FrameStats can be cleared and reused") {
    moteur::FrameStats stats(2);
    stats.add(10.0);
    stats.add(20.0);
    stats.add(30.0);
    stats.clear();
    CHECK(stats.count() == 0);
    stats.add(7.0);
    CHECK(stats.mean() == 7.0);
}

TEST_CASE("FrameStats rejects a zero capacity") {
    CHECK_THROWS_AS(moteur::FrameStats(0), std::invalid_argument);
}
