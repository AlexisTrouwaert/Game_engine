#include "moteur/animation.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "moteur/paths.hpp"
#include "moteur/texture_atlas.hpp"

namespace moteur {

namespace {

constexpr int kSupportedVersion = 1;

PlayMode parse_mode(const std::string& text) {
    if (text == "once") return PlayMode::Once;
    if (text == "loop") return PlayMode::Loop;
    if (text == "ping_pong") return PlayMode::PingPong;
    throw std::runtime_error("unknown mode '" + text + "' (expected once, loop or ping_pong)");
}

}  // namespace

AnimationClip::AnimationClip(std::string name, std::vector<AnimationFrame> frames, PlayMode mode,
                             std::vector<AnimationEvent> events)
    : name_(std::move(name)), frames_(std::move(frames)), mode_(mode), events_(std::move(events)) {
    if (frames_.empty()) {
        throw std::invalid_argument("animation '" + name_ + "' has no frames");
    }
    for (std::size_t i = 0; i < frames_.size(); ++i) {
        if (frames_[i].ticks < 1) {
            throw std::invalid_argument("animation '" + name_ + "': frame " + std::to_string(i) + " lasts " +
                                        std::to_string(frames_[i].ticks) + " ticks (at least 1 expected)");
        }
    }
    for (const AnimationEvent& event : events_) {
        if (event.frame < 0 || static_cast<std::size_t>(event.frame) >= frames_.size()) {
            throw std::invalid_argument("animation '" + name_ + "': event '" + event.name + "' is on frame " +
                                        std::to_string(event.frame) + ", but there are " +
                                        std::to_string(frames_.size()) + " frames");
        }
    }

    const int frame_count = static_cast<int>(frames_.size());
    const int steps = mode_ == PlayMode::PingPong && frame_count > 1 ? 2 * frame_count - 2 : frame_count;
    step_starts_.reserve(static_cast<std::size_t>(steps));
    for (int step = 0; step < steps; ++step) {
        step_starts_.push_back(cycle_ticks_);
        cycle_ticks_ += frames_[static_cast<std::size_t>(step_frame(step))].ticks;
    }
}

int AnimationClip::step_frame(int step) const {
    const int frame_count = static_cast<int>(frames_.size());
    return step < frame_count ? step : 2 * frame_count - 2 - step;
}

void AnimationPlayer::play(const AnimationClip& clip) {
    clip_ = &clip;
    restart();
}

void AnimationPlayer::restart() {
    time_ = 0;
    started_ = false;
}

void AnimationPlayer::set_speed(double multiplier) {
    if (!(multiplier >= 0.0)) {  // also rejects NaN
        throw std::invalid_argument("animation speed must be zero or positive, got " + std::to_string(multiplier));
    }
    speed_ = std::llround(multiplier * static_cast<double>(kSpeedOne));
}

void AnimationPlayer::set_time(std::int64_t time) {
    time_ = std::max<std::int64_t>(time, 0);
    if (clip_ != nullptr && clip_->mode() == PlayMode::Once) {
        time_ = std::min(time_, cycle_length());
    }
    started_ = true;  // the frame showing at this time is not "reached": its events do not fire
}

void AnimationPlayer::advance(int ticks, std::vector<const AnimationEvent*>* fired) {
    if (clip_ == nullptr) {
        return;
    }
    const std::int64_t previous = time_;
    const std::int64_t cycle = cycle_length();
    time_ += static_cast<std::int64_t>(std::max(ticks, 0)) * speed_;
    const bool once = clip_->mode() == PlayMode::Once;
    if (once) {
        time_ = std::min(time_, cycle);
    }
    const bool include_previous = !started_;
    started_ = true;
    if (fired == nullptr || clip_->events().empty()) {
        return;
    }

    // Every step that starts in (previous, time_], or [previous, time_] right after play(): the
    // intervals of successive advances touch without overlapping, so each start is seen once.
    for (std::int64_t cycle_index = previous / cycle;; ++cycle_index) {
        const std::int64_t cycle_start = cycle_index * cycle;
        if ((once && cycle_index > 0) || cycle_start > time_) {
            return;
        }
        for (int step = 0; step < clip_->step_count(); ++step) {
            const std::int64_t start = cycle_start + static_cast<std::int64_t>(clip_->step_start(step)) * kSpeedOne;
            if (start > time_) {
                return;
            }
            if (start < previous || (start == previous && !include_previous)) {
                continue;
            }
            const int frame = clip_->step_frame(step);
            for (const AnimationEvent& event : clip_->events()) {
                if (event.frame == frame) {
                    fired->push_back(&event);
                }
            }
        }
    }
}

int AnimationPlayer::step_at(std::int64_t time) const {
    const std::int64_t cycle = cycle_length();
    if (clip_->mode() == PlayMode::Once && time >= cycle) {
        return clip_->step_count() - 1;
    }
    const std::int64_t tick = (time % cycle) / kSpeedOne;
    // The last step whose start is at or before this tick.
    int step = 0;
    while (step + 1 < clip_->step_count() && clip_->step_start(step + 1) <= tick) {
        ++step;
    }
    return step;
}

int AnimationPlayer::frame_index() const {
    return clip_->step_frame(step_at(time_));
}

const std::string& AnimationPlayer::region() const {
    return clip_->frames()[static_cast<std::size_t>(frame_index())].region;
}

bool AnimationPlayer::finished() const {
    return clip_ != nullptr && clip_->mode() == PlayMode::Once && time_ >= cycle_length();
}

AnimationLibrary AnimationLibrary::parse(std::string_view json_text, const std::string& source) {
    AnimationLibrary library;
    library.source_ = source;
    try {
        const nlohmann::json doc = nlohmann::json::parse(json_text);

        const int version = doc.at("version").get<int>();
        if (version != kSupportedVersion) {
            throw std::runtime_error("format version " + std::to_string(version) + " is not supported (expected " +
                                     std::to_string(kSupportedVersion) + ")");
        }

        for (const auto& [name, clip] : doc.at("clips").items()) {
            const PlayMode mode = parse_mode(clip.value("mode", std::string("loop")));
            const int default_ticks = clip.value("frame_ticks", 1);

            std::vector<AnimationFrame> frames;
            for (const nlohmann::json& frame : clip.at("frames")) {
                if (frame.is_string()) {
                    frames.push_back({frame.get<std::string>(), default_ticks});
                } else {
                    frames.push_back({frame.at("region").get<std::string>(), frame.value("ticks", default_ticks)});
                }
            }

            std::vector<AnimationEvent> events;
            if (clip.contains("events")) {
                for (const nlohmann::json& event : clip.at("events")) {
                    events.push_back({event.at("frame").get<int>(), event.at("name").get<std::string>()});
                }
            }

            library.clips_.emplace(name, AnimationClip(name, std::move(frames), mode, std::move(events)));
        }
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error("Animations '" + source + "' are not valid: " + e.what());
    } catch (const std::exception& e) {
        throw std::runtime_error("Animations '" + source + "': " + e.what());
    }
    return library;
}

AnimationLibrary AnimationLibrary::load(const std::string& json_path) {
    std::string text;
    try {
        text = read_text_file(json_path);
    } catch (const std::runtime_error& e) {
        throw std::runtime_error("Animations '" + json_path + "': " + e.what());
    }
    return parse(text, json_path);
}

const AnimationClip& AnimationLibrary::clip(const std::string& name) const {
    const auto found = clips_.find(name);
    if (found == clips_.end()) {
        throw std::runtime_error("Animations '" + source_ + "' have no clip named '" + name + "' (there are " +
                                 std::to_string(clips_.size()) + " clips)");
    }
    return found->second;
}

std::vector<std::string> AnimationLibrary::names() const {
    std::vector<std::string> result;
    result.reserve(clips_.size());
    for (const auto& entry : clips_) {
        result.push_back(entry.first);
    }
    return result;  // std::map keeps them sorted
}

void AnimationLibrary::check_regions(const TextureAtlas& atlas) const {
    for (const auto& [name, clip] : clips_) {
        for (const AnimationFrame& frame : clip.frames()) {
            if (!atlas.contains(frame.region)) {
                throw std::runtime_error("Animations '" + source_ + "': clip '" + name + "' uses the sprite '" +
                                         frame.region + "', which is not in the atlas");
            }
        }
    }
}

}  // namespace moteur
