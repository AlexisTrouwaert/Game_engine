#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include <algorithm>

#include "moteur/profiler.hpp"

// Milestone 7, part 4: the profiler's zones, history, statistics and Chrome Trace export.

namespace {

void work() {
    MOTEUR_PROFILE("travail");
    volatile int sum = 0;
    for (int i = 0; i < 1000; ++i) {
        sum = sum + i;
    }
}

}  // namespace

TEST_CASE("Profiler records nested zones per frame, and keeps a history") {
    moteur::Profiler profiler;
    moteur::Profiler::set_current(&profiler);
    for (int frame = 0; frame < 3; ++frame) {
        profiler.begin_frame();
        {
            MOTEUR_PROFILE("logique");
            work();
            work();
        }
        {
            MOTEUR_PROFILE("rendu");
        }
        profiler.end_frame();
    }
    REQUIRE(profiler.history().size() == 3);
    const moteur::Profiler::Frame& last = profiler.history().back();
    CHECK(last.index == 2);
    REQUIRE(last.zones.size() == 4);
    CHECK(std::string(last.zones[0].name) == "logique");
    CHECK(last.zones[0].depth == 0);
    CHECK(last.zones[0].parent == moteur::Profiler::kNoParent);
    CHECK(std::string(last.zones[1].name) == "travail");
    CHECK(last.zones[1].depth == 1);
    CHECK(last.zones[1].parent == 0);
    CHECK(last.zones[3].depth == 0);
    for (const moteur::Profiler::Zone& zone : last.zones) {
        CHECK(zone.end >= zone.start);
        CHECK(zone.start >= last.start);
        CHECK(zone.end <= last.end);
    }
    const auto summary = profiler.summary();
    const auto travail = std::find_if(summary.begin(), summary.end(), [](const auto& s) { return s.name == "travail"; });
    REQUIRE(travail != summary.end());
    CHECK(travail->calls == doctest::Approx(2.0));  // twice per frame, added up
    CHECK(profiler.totals().size() == 3);

    // Frozen: the history stops, totals go on. Disabled: nothing recorded.
    profiler.set_frozen(true);
    profiler.begin_frame();
    work();
    profiler.end_frame();
    CHECK(profiler.history().size() == 3);
    profiler.set_frozen(false);
    profiler.set_enabled(false);
    const std::size_t frames = profiler.history().size();
    const std::size_t names = profiler.totals().size();
    profiler.begin_frame();
    work();
    profiler.begin_zone("posée à la main");  // the zones of the Application too
    profiler.end_zone();
    profiler.end_frame();
    CHECK(profiler.history().size() == frames);
    CHECK(profiler.totals().size() == names);
    moteur::Profiler::set_current(nullptr);
    work();  // no profiler: nothing happens
}

TEST_CASE("A capture is written as Chrome Trace events") {
    moteur::Profiler profiler;
    moteur::Profiler::set_current(&profiler);
    profiler.start_capture();
    for (int frame = 0; frame < 2; ++frame) {
        profiler.begin_frame();
        work();
        profiler.end_frame();
    }
    CHECK(profiler.capturing());
    std::vector<moteur::Profiler::Frame> frames(profiler.history().begin(), profiler.history().end());
    const nlohmann::json trace = nlohmann::json::parse(moteur::Profiler::chrome_trace(frames));
    REQUIRE(trace.contains("traceEvents"));
    CHECK(trace["traceEvents"].size() == 4);  // two frames, a zone each
    CHECK(trace["traceEvents"][1]["name"] == "travail");
    CHECK(trace["traceEvents"][1]["ph"] == "X");
    CHECK(trace["traceEvents"][1]["dur"].get<double>() >= 0.0);
    moteur::Profiler::set_current(nullptr);
}
