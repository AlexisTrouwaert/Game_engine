#include "moteur/audio_reverb.hpp"

#include <algorithm>
#include <cmath>

namespace moteur::audio {

namespace {

// Freeverb's tuning, in samples at 44.1 kHz (scaled to the mixing rate).
constexpr int kCombTuning[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr int kAllpassTuning[4] = {556, 441, 341, 225};
constexpr int kStereoSpread = 23;
constexpr float kFixedGain = 0.015f;
constexpr float kScaleRoom = 0.28f;
constexpr float kOffsetRoom = 0.7f;
constexpr float kScaleDamp = 0.4f;
constexpr float kAllpassFeedback = 0.5f;
constexpr float kScaleWet = 3.0f;

std::size_t scaled(int samples, int sample_rate) {
    return static_cast<std::size_t>(std::max(1L, std::lround(static_cast<double>(samples) * sample_rate / 44100.0)));
}

}  // namespace

float Reverb::Comb::process(float input, float feedback, float damp) {
    const float output = buffer[index];
    store = output * (1.0f - damp) + store * damp;
    buffer[index] = input + store * feedback;
    index = index + 1 < buffer.size() ? index + 1 : 0;
    return output;
}

float Reverb::Allpass::process(float input) {
    const float buffered = buffer[index];
    buffer[index] = input + buffered * kAllpassFeedback;
    index = index + 1 < buffer.size() ? index + 1 : 0;
    return buffered - input;
}

Reverb::Reverb(int channels, int sample_rate) : channels_(std::max(1, channels)) {
    for (int c = 0; c < 2; ++c) {
        const int spread = c == 0 ? 0 : kStereoSpread;
        for (int i = 0; i < 8; ++i) {
            lines_[static_cast<std::size_t>(c)].combs[static_cast<std::size_t>(i)].buffer.assign(scaled(kCombTuning[i] + spread, sample_rate), 0.0f);
        }
        for (int i = 0; i < 4; ++i) {
            lines_[static_cast<std::size_t>(c)].allpasses[static_cast<std::size_t>(i)].buffer.assign(scaled(kAllpassTuning[i] + spread, sample_rate), 0.0f);
        }
    }
}

void Reverb::set(float room, float damping, float wet) {
    room_.store(std::clamp(room, 0.0f, 1.0f), std::memory_order_relaxed);
    damping_.store(std::clamp(damping, 0.0f, 1.0f), std::memory_order_relaxed);
    wet_.store(std::clamp(wet, 0.0f, 1.0f), std::memory_order_relaxed);
}

void Reverb::process(const float* in, float* out, std::size_t frames) {
    const float feedback = room_.load(std::memory_order_relaxed) * kScaleRoom + kOffsetRoom;
    const float damp = damping_.load(std::memory_order_relaxed) * kScaleDamp;
    const float wet_target = wet_.load(std::memory_order_relaxed);
    const float glide = 1.0f / 2048.0f;  // about 40 ms to a new wet level
    for (std::size_t f = 0; f < frames; ++f) {
        wet_now_ += (wet_target - wet_now_) * glide;
        const float* frame_in = in + f * static_cast<std::size_t>(channels_);
        float* frame_out = out + f * static_cast<std::size_t>(channels_);
        const float left = frame_in[0];
        const float right = channels_ > 1 ? frame_in[1] : left;
        const float input = (left + right) * kFixedGain;
        float wet[2] = {0.0f, 0.0f};
        for (int c = 0; c < 2; ++c) {
            Channel& line = lines_[static_cast<std::size_t>(c)];
            float sum = 0.0f;
            for (Comb& comb : line.combs) {
                sum += comb.process(input, feedback, damp);
            }
            for (Allpass& allpass : line.allpasses) {
                sum = allpass.process(sum);
            }
            wet[c] = sum;
        }
        const float gain = wet_now_ * kScaleWet;
        frame_out[0] = left + wet[0] * gain;
        if (channels_ > 1) {
            frame_out[1] = right + wet[1] * gain;
            for (int c = 2; c < channels_; ++c) {
                frame_out[c] = frame_in[c];
            }
        }
    }
}

}  // namespace moteur::audio
