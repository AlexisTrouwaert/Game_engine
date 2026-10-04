#include "moteur/profiler.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace moteur {

Profiler* Profiler::current_ = nullptr;

Profiler::Profiler() : main_thread_(std::this_thread::get_id()) {
    frame_.zones.reserve(256);
}

Profiler::~Profiler() {
    if (current_ == this) {
        current_ = nullptr;
    }
}

double Profiler::milliseconds(std::uint64_t counter_ticks) {
    return static_cast<double>(counter_ticks) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
}

void Profiler::begin_frame() {
    frame_.zones.clear();
    stack_.clear();
    frame_.index = next_index_++;
    frame_.start = SDL_GetPerformanceCounter();
    in_frame_ = enabled_;  // off: no zone is measured this frame (its cost, see milestone 7 part 10)
}

void Profiler::begin_zone(const char* name) {
    if (!in_frame_ || std::this_thread::get_id() != main_thread_) {
        return;
    }
    Zone zone{name, SDL_GetPerformanceCounter(), 0, stack_.empty() ? kNoParent : stack_.back(),
              static_cast<std::uint16_t>(stack_.size())};
    stack_.push_back(static_cast<std::uint32_t>(frame_.zones.size()));
    frame_.zones.push_back(zone);
}

void Profiler::end_zone() {
    if (!in_frame_ || std::this_thread::get_id() != main_thread_ || stack_.empty()) {
        return;
    }
    frame_.zones[stack_.back()].end = SDL_GetPerformanceCounter();
    stack_.pop_back();
}

void Profiler::end_frame() {
    if (!in_frame_) {
        return;
    }
    frame_.end = SDL_GetPerformanceCounter();
    while (!stack_.empty()) {  // a zone left open closes with the frame
        frame_.zones[stack_.back()].end = frame_.end;
        stack_.pop_back();
    }
    in_frame_ = false;
    MOTEUR_PROFILE_FRAME();
    // Whole-run totals: the time of each zone in this frame (a zone run twice adds up).
    per_frame_.clear();
    for (const Zone& zone : frame_.zones) {
        auto found = std::find_if(per_frame_.begin(), per_frame_.end(), [&zone](const auto& e) { return e.first == zone.name; });
        if (found == per_frame_.end()) {
            per_frame_.push_back({zone.name, {0.0, 0}});
            found = per_frame_.end() - 1;
        }
        found->second.first += milliseconds(zone.end - zone.start);
        ++found->second.second;
    }
    for (const auto& [name, value] : per_frame_) {
        Totals& totals = totals_[name];
        totals.ms.add(value.first);
        totals.calls += value.second;
        totals.frames += 1.0;
    }
    if (capturing_) {
        capture_.push_back(frame_);
    }
    if (!frozen_) {
        history_.push_back(frame_);
        while (history_.size() > kHistory) {
            history_.pop_front();
        }
    }
}

std::vector<Profiler::Summary> Profiler::summary() const {
    std::unordered_map<std::string, Totals> stats;
    for (const Frame& frame : history_) {
        std::unordered_map<std::string, std::pair<double, int>> per_name;
        for (const Zone& zone : frame.zones) {
            auto& [ms, calls] = per_name[zone.name];
            ms += milliseconds(zone.end - zone.start);
            ++calls;
        }
        for (const auto& [name, value] : per_name) {
            Totals& totals = stats[name];
            totals.ms.add(value.first);
            totals.calls += value.second;
            totals.frames += 1.0;
        }
    }
    std::vector<Summary> result;
    for (const auto& [name, totals] : stats) {
        result.push_back({name, totals.ms.mean(), totals.ms.percentile(99.0), totals.ms.max(), totals.calls / totals.frames});
    }
    std::sort(result.begin(), result.end(), [](const Summary& a, const Summary& b) {
        return a.mean_ms != b.mean_ms ? a.mean_ms > b.mean_ms : a.name < b.name;
    });
    return result;
}

std::vector<Profiler::Summary> Profiler::totals() const {
    std::vector<Summary> result;
    for (const auto& [name, totals] : totals_) {
        const auto same = std::find_if(result.begin(), result.end(), [name](const Summary& s) { return s.name == name; });
        if (same != result.end()) {
            continue;  // the same name at another place: shown once (the first)
        }
        result.push_back({name, totals.ms.mean(), totals.ms.percentile(99.0), totals.ms.max(), totals.calls / totals.frames});
    }
    std::sort(result.begin(), result.end(), [](const Summary& a, const Summary& b) {
        return a.mean_ms != b.mean_ms ? a.mean_ms > b.mean_ms : a.name < b.name;
    });
    return result;
}

void Profiler::start_capture() {
    capture_.clear();
    capturing_ = true;
}

std::string Profiler::chrome_trace(const std::vector<Frame>& frames) {
    nlohmann::json events = nlohmann::json::array();
    if (frames.empty()) {
        return nlohmann::json{{"traceEvents", events}, {"displayTimeUnit", "ms"}}.dump();
    }
    const std::uint64_t origin = frames.front().start;
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    const auto microseconds = [origin, frequency](std::uint64_t counter) {
        return static_cast<double>(counter - origin) * 1e6 / frequency;
    };
    for (const Frame& frame : frames) {
        events.push_back({{"name", "image " + std::to_string(frame.index)}, {"ph", "X"}, {"pid", 1}, {"tid", 1},
                          {"ts", microseconds(frame.start)}, {"dur", microseconds(frame.end) - microseconds(frame.start)}});
        for (const Zone& zone : frame.zones) {
            events.push_back({{"name", zone.name}, {"ph", "X"}, {"pid", 1}, {"tid", 1}, {"ts", microseconds(zone.start)},
                              {"dur", microseconds(zone.end) - microseconds(zone.start)}});
        }
    }
    return nlohmann::json{{"traceEvents", events}, {"displayTimeUnit", "ms"}}.dump();
}

bool Profiler::stop_capture(const std::string& path) {
    capturing_ = false;
    const std::string text = chrome_trace(capture_);
    capture_.clear();
    std::error_code error;
    const std::filesystem::path file(std::u8string(path.begin(), path.end()));
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), error);
    }
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
    return static_cast<bool>(out);
}

}  // namespace moteur
