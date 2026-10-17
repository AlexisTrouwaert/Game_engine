#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace moteur {

/// A short sound (an effect), decoded once into memory: 32-bit float samples, interleaved.
///
/// The samples are shared: a voice playing the sound keeps them alive, so a hot reload (which
/// replaces the Sound in place) never frees what the audio thread is reading.
struct Sound {
    /// The decoded samples, `channels` floats per frame (-1 to 1), at the file's own rate (the
    /// mixer resamples). Null in a default-constructed Sound.
    std::shared_ptr<const std::vector<float>> samples;
    int channels = 0;     ///< 1 or 2
    int sample_rate = 0;  ///< Hz
    std::string name;     ///< for messages

    /// The length in frames (one sample per channel); 0 without samples or channels.
    std::size_t frames() const { return samples && channels > 0 ? samples->size() / static_cast<std::size_t>(channels) : 0; }
    /// The length in seconds; 0 without a sample rate.
    double seconds() const { return sample_rate > 0 ? static_cast<double>(frames()) / sample_rate : 0.0; }
    /// The memory the samples take, in bytes (the asset cache's budget counts this).
    std::size_t bytes() const { return samples ? samples->size() * sizeof(float) : 0; }
};

/// A long sound (music, ambience), kept compressed in memory and decoded while it plays. The bytes
/// are shared for the same reason as a Sound's samples.
struct Music {
    /// The whole file, as read from disk (still compressed). Null in a default-constructed Music.
    std::shared_ptr<const std::vector<std::uint8_t>> bytes;
    int channels = 0;      ///< the file's channel count
    int sample_rate = 0;   ///< Hz, the file's own
    double seconds = 0.0;  ///< 0 if the format does not tell without decoding it all (Vorbis)
    std::string name;      ///< for messages

    /// The memory the file takes, in bytes.
    std::size_t memory() const { return bytes ? bytes->size() : 0; }
};

/// Decodes a WAV, FLAC, MP3 or OGG Vorbis file (its bytes) into a Sound; more than two channels are
/// mixed down to two. Throws std::runtime_error naming `name` if the data cannot be decoded, or
/// holds no samples. Allocates the whole decoded sound: meant for short effects.
Sound decode_sound(std::span<const std::uint8_t> file, const std::string& name);

/// Checks that the file can be decoded and reads its format without decoding it, then keeps the
/// bytes (moved in) as a Music. Throws std::runtime_error naming `name` if the format is not
/// recognised or has no channels or sample rate.
Music open_music(std::vector<std::uint8_t> file, const std::string& name);

/// A short beep (125 ms, 880 Hz, mono, 48000 Hz), which stands in for a sound that failed to load:
/// heard, rather than silent.
Sound placeholder_sound();

/// A WAV file (16-bit PCM) from interleaved float samples (clamped to -1..1): tests and generated
/// sounds. The returned bytes can be given to decode_sound().
std::vector<std::uint8_t> encode_wav(std::span<const float> samples, int channels, int sample_rate);

}  // namespace moteur
