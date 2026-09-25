#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// The rules of the Audio (milestone 4, part 7) that need no sound device: where a sound is heard
// from, and which sounds get a voice when there are too many. Pure functions, tested on their own.
namespace moteur {

// Who hears: a point on the ground (the centre of the view) and the direction of the screen's right
// on the ground. Only left and right matter to a fixed camera: no front and back, no height.
struct Listener {
    glm::vec3 position{0.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};  // unit length
};

// How distance changes a placed sound.
struct Attenuation {
    float min_distance = 4.0f;   // metres: full volume up to here
    float max_distance = 30.0f;  // silent from here
    float pan_distance = 12.0f;  // this far to the side is fully on that side (about half the view)
    float max_pan = 0.8f;        // never entirely in one ear
};

struct Placement {
    float gain = 1.0f;  // 0 to 1
    float pan = 0.0f;   // -1 left to 1 right
};

// The gain falls from 1 at min_distance to 0 at max_distance, as the square of the distance left
// (half way: a quarter, -12 dB); the pan follows the offset along the listener's right.
Placement place_sound(const Listener& listener, glm::vec3 position, const Attenuation& attenuation);

// Below this loudness a sound is not played: nobody would hear it.
constexpr float kInaudible = 0.001f;

struct VoiceLimits {
    int max_voices = 32;    // effects, ambiences and interface together (music apart)
    int max_per_sound = 4;  // voices playing the same sound file
};

// A voice playing, or asking to play, as the rules see it.
struct VoiceInfo {
    std::uintptr_t sound = 0;  // which sound file (the limit per sound counts these)
    float loudness = 0.0f;     // volume x gain of the placement: what the listener gets
    int priority = 0;          // higher wins
    std::uint64_t order = 0;   // when it started: among equals, the oldest goes first
};

struct VoiceDecision {
    enum class Kind { Start, Steal, DropInaudible, DropSoundLimit, DropVoiceLimit };
    Kind kind = Kind::Start;
    std::size_t victim = 0;  // Steal: the index, in `playing`, of the voice to stop
};

// Whether a new voice gets to play, given the voices playing:
// - too quiet (kInaudible): dropped;
// - its sound already has max_per_sound voices: it takes the place of the weakest of them if it is
//   at least as strong (priority first, then loudness), otherwise dropped;
// - max_voices are playing: the same against the weakest of all;
// - otherwise it starts.
// The weakest: lowest priority, then quietest, then oldest.
VoiceDecision decide_voice(std::span<const VoiceInfo> playing, const VoiceInfo& request, const VoiceLimits& limits);

// Requests of one frame for the same sound file become one: the loudest, which keeps its place in
// the list (ten identical impacts in the same frame do not make ten times the noise). Returns how
// many were merged into others.
std::size_t merge_requests(std::vector<VoiceInfo>& requests, std::vector<std::size_t>* kept_indices = nullptr);

// The player's volume slider (0 to 1) as a gain: squared, which follows loudness better than a
// straight line (half the slider is -12 dB, not -6 dB).
inline float slider_gain(float slider) {
    const float v = glm::clamp(slider, 0.0f, 1.0f);
    return v * v;
}

}  // namespace moteur
