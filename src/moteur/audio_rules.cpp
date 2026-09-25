#include "moteur/audio_rules.hpp"

#include <algorithm>

namespace moteur {

Placement place_sound(const Listener& listener, glm::vec3 position, const Attenuation& attenuation) {
    const glm::vec3 offset = position - listener.position;
    const float distance = glm::length(offset);
    Placement placement;
    if (distance >= attenuation.max_distance) {
        placement.gain = 0.0f;
    } else if (distance > attenuation.min_distance) {
        const float left = (attenuation.max_distance - distance) / (attenuation.max_distance - attenuation.min_distance);
        placement.gain = left * left;
    }
    const float side = attenuation.pan_distance > 0.0f ? glm::dot(offset, listener.right) / attenuation.pan_distance : 0.0f;
    placement.pan = glm::clamp(side, -1.0f, 1.0f) * attenuation.max_pan;
    return placement;
}

namespace {

// a is weaker than b: lower priority, then quieter, then older.
bool weaker(const VoiceInfo& a, const VoiceInfo& b) {
    if (a.priority != b.priority) {
        return a.priority < b.priority;
    }
    if (a.loudness != b.loudness) {
        return a.loudness < b.loudness;
    }
    return a.order < b.order;
}

// The request may take the place of `voice`: stronger priority, or the same and at least as loud.
bool beats(const VoiceInfo& request, const VoiceInfo& voice) {
    if (request.priority != voice.priority) {
        return request.priority > voice.priority;
    }
    return request.loudness >= voice.loudness;
}

}  // namespace

VoiceDecision decide_voice(std::span<const VoiceInfo> playing, const VoiceInfo& request, const VoiceLimits& limits) {
    VoiceDecision decision;
    if (request.loudness < kInaudible) {
        decision.kind = VoiceDecision::Kind::DropInaudible;
        return decision;
    }
    // The limit per sound first: stealing from the same sound frees a voice for the total too.
    std::size_t same = 0;
    std::size_t weakest_same = playing.size();
    std::size_t weakest_all = playing.size();
    for (std::size_t i = 0; i < playing.size(); ++i) {
        if (playing[i].sound == request.sound) {
            ++same;
            if (weakest_same == playing.size() || weaker(playing[i], playing[weakest_same])) {
                weakest_same = i;
            }
        }
        if (weakest_all == playing.size() || weaker(playing[i], playing[weakest_all])) {
            weakest_all = i;
        }
    }
    if (limits.max_per_sound > 0 && same >= static_cast<std::size_t>(limits.max_per_sound)) {
        if (beats(request, playing[weakest_same])) {
            decision.kind = VoiceDecision::Kind::Steal;
            decision.victim = weakest_same;
        } else {
            decision.kind = VoiceDecision::Kind::DropSoundLimit;
        }
        return decision;
    }
    if (playing.size() >= static_cast<std::size_t>(std::max(limits.max_voices, 0))) {
        if (weakest_all < playing.size() && beats(request, playing[weakest_all])) {
            decision.kind = VoiceDecision::Kind::Steal;
            decision.victim = weakest_all;
        } else {
            decision.kind = VoiceDecision::Kind::DropVoiceLimit;
        }
        return decision;
    }
    return decision;
}

std::size_t merge_requests(std::vector<VoiceInfo>& requests, std::vector<std::size_t>* kept_indices) {
    std::vector<VoiceInfo> kept;
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < requests.size(); ++i) {
        const VoiceInfo& request = requests[i];
        auto found = std::find_if(kept.begin(), kept.end(), [&](const VoiceInfo& k) { return k.sound == request.sound; });
        if (found == kept.end()) {
            kept.push_back(request);
            indices.push_back(i);
        } else if (request.priority > found->priority ||
                   (request.priority == found->priority && request.loudness > found->loudness)) {
            // The stronger one replaces it, at the place of the first (order of the frame kept).
            const auto k = static_cast<std::size_t>(found - kept.begin());
            kept[k] = request;
            indices[k] = i;
        }
    }
    const std::size_t merged = requests.size() - kept.size();
    requests = std::move(kept);
    if (kept_indices != nullptr) {
        *kept_indices = std::move(indices);
    }
    return merged;
}

}  // namespace moteur
