#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <vector>

// The reverberation of the audio buses (milestone 9 bis, part 7), written for the engine: the
// classic Schroeder-Moorer design of Jezar's "Freeverb" (public domain): eight parallel comb
// filters with a low-pass in their feedback, then four all-pass filters in series, per channel,
// the right channel's delays a little longer (stereo width). Plain code, no miniaudio: the node
// (audio.cpp) calls process() on the audio thread; the settings are atomics set from the main one.
namespace moteur::audio {

class Reverb {
public:
    /// For `channels` interleaved channels (1 or 2 used; more get the left's) at `sample_rate`.
    Reverb(int channels, int sample_rate);

    /// 0 to 1: how long it rings, how fast its highs die, how much of it is heard. Any thread.
    void set(float room, float damping, float wet);
    /// `frames` interleaved frames of `in` into `out` (may be the same buffer): dry plus wet.
    void process(const float* in, float* out, std::size_t frames);

private:
    struct Comb {
        std::vector<float> buffer;
        std::size_t index = 0;
        float store = 0.0f;
        float process(float input, float feedback, float damp);
    };
    struct Allpass {
        std::vector<float> buffer;
        std::size_t index = 0;
        float process(float input);
    };
    struct Channel {
        std::array<Comb, 8> combs;
        std::array<Allpass, 4> allpasses;
    };

    int channels_;
    std::array<Channel, 2> lines_;
    std::atomic<float> room_{0.5f};
    std::atomic<float> damping_{0.5f};
    std::atomic<float> wet_{0.0f};
    float wet_now_ = 0.0f;  ///< audio thread: glides to wet_ (no click when a zone is entered)
};

}  // namespace moteur::audio
