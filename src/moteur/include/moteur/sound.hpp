#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace moteur {

// A short sound (an effect), decoded once into memory: 32-bit float samples, interleaved.
//
// The samples are shared: a voice playing the sound keeps them alive, so a hot reload (which
// replaces the Sound in place) never frees what the audio thread is reading.
struct Sound {
    std::shared_ptr<const std::vector<float>> samples;
    int channels = 0;     // 1 or 2
    int sample_rate = 0;  // Hz
    std::string name;     // for messages

    std::size_t frames() const { return samples && channels > 0 ? samples->size() / static_cast<std::size_t>(channels) : 0; }
    double seconds() const { return sample_rate > 0 ? static_cast<double>(frames()) / sample_rate : 0.0; }
    std::size_t bytes() const { return samples ? samples->size() * sizeof(float) : 0; }
};

// A long sound (music, ambience), kept compressed in memory and decoded while it plays. The bytes
// are shared for the same reason as a Sound's samples.
struct Music {
    std::shared_ptr<const std::vector<std::uint8_t>> bytes;
    int channels = 0;
    int sample_rate = 0;
    double seconds = 0.0;  // 0 if the format does not tell without decoding it all (Vorbis)
    std::string name;

    std::size_t memory() const { return bytes ? bytes->size() : 0; }
};

// Decodes a WAV, FLAC, MP3 or OGG Vorbis file (its bytes) into a Sound; more than two channels are
// mixed down to two. Throws std::runtime_error naming `name` if the data cannot be decoded.
Sound decode_sound(std::span<const std::uint8_t> file, const std::string& name);

// Checks that the file can be decoded (and reads its format) without decoding it: a Music.
Music open_music(std::vector<std::uint8_t> file, const std::string& name);

// A short beep, which stands in for a sound that failed to load: heard, rather than silent.
Sound placeholder_sound();

// A WAV file (16-bit PCM) from float samples: tests and generated sounds.
std::vector<std::uint8_t> encode_wav(std::span<const float> samples, int channels, int sample_rate);

}  // namespace moteur
