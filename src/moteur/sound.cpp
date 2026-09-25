#include "moteur/sound.hpp"

#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace moteur {

namespace {

std::string result_text(ma_result result) {
    return ma_result_description(result);
}

}  // namespace

Sound decode_sound(std::span<const std::uint8_t> file, const std::string& name) {
    // Native channels and rate first, to know whether to mix down.
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder decoder;
    ma_result result = ma_decoder_init_memory(file.data(), file.size(), &config, &decoder);
    if (result != MA_SUCCESS) {
        throw std::runtime_error("Sound '" + name + "': cannot decode (" + result_text(result) + ")");
    }
    ma_uint32 channels = 0, sample_rate = 0;
    ma_decoder_get_data_format(&decoder, nullptr, &channels, &sample_rate, nullptr, 0);
    if (channels > 2) {
        ma_decoder_uninit(&decoder);
        config = ma_decoder_config_init(ma_format_f32, 2, 0);
        result = ma_decoder_init_memory(file.data(), file.size(), &config, &decoder);
        if (result != MA_SUCCESS) {
            throw std::runtime_error("Sound '" + name + "': cannot decode (" + result_text(result) + ")");
        }
        channels = 2;
    }
    if (channels == 0 || sample_rate == 0) {
        ma_decoder_uninit(&decoder);
        throw std::runtime_error("Sound '" + name + "': no channels or no sample rate");
    }

    // Read in chunks: the length is not always known ahead (Vorbis).
    auto samples = std::make_shared<std::vector<float>>();
    std::vector<float> chunk(4096 * channels);
    for (;;) {
        ma_uint64 read = 0;
        result = ma_decoder_read_pcm_frames(&decoder, chunk.data(), 4096, &read);
        samples->insert(samples->end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(read * channels));
        if (result != MA_SUCCESS || read < 4096) {
            break;
        }
    }
    ma_decoder_uninit(&decoder);
    if (result != MA_SUCCESS && result != MA_AT_END) {
        throw std::runtime_error("Sound '" + name + "': decoding failed (" + result_text(result) + ")");
    }
    if (samples->empty()) {
        throw std::runtime_error("Sound '" + name + "': no samples");
    }
    samples->shrink_to_fit();

    Sound sound;
    sound.samples = std::move(samples);
    sound.channels = static_cast<int>(channels);
    sound.sample_rate = static_cast<int>(sample_rate);
    sound.name = name;
    return sound;
}

Music open_music(std::vector<std::uint8_t> file, const std::string& name) {
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder decoder;
    const ma_result result = ma_decoder_init_memory(file.data(), file.size(), &config, &decoder);
    if (result != MA_SUCCESS) {
        throw std::runtime_error("Music '" + name + "': cannot decode (" + result_text(result) + ")");
    }
    ma_uint32 channels = 0, sample_rate = 0;
    ma_decoder_get_data_format(&decoder, nullptr, &channels, &sample_rate, nullptr, 0);
    ma_uint64 frames = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &frames);  // 0 for Vorbis
    ma_decoder_uninit(&decoder);
    if (channels == 0 || sample_rate == 0) {
        throw std::runtime_error("Music '" + name + "': no channels or no sample rate");
    }
    Music music;
    music.bytes = std::make_shared<const std::vector<std::uint8_t>>(std::move(file));
    music.channels = static_cast<int>(channels);
    music.sample_rate = static_cast<int>(sample_rate);
    music.seconds = static_cast<double>(frames) / sample_rate;
    music.name = name;
    return music;
}

Sound placeholder_sound() {
    constexpr int kRate = 48000;
    constexpr int kFrames = kRate / 8;  // 125 ms
    auto samples = std::make_shared<std::vector<float>>(kFrames);
    for (int i = 0; i < kFrames; ++i) {
        const float t = static_cast<float>(i) / kRate;
        const float envelope = std::min(1.0f, static_cast<float>(kFrames - i) / 600.0f);  // no click at the end
        (*samples)[static_cast<std::size_t>(i)] = 0.3f * envelope * std::sin(2.0f * 3.14159265f * 880.0f * t);
    }
    Sound sound;
    sound.samples = std::move(samples);
    sound.channels = 1;
    sound.sample_rate = kRate;
    sound.name = "(placeholder)";
    return sound;
}

std::vector<std::uint8_t> encode_wav(std::span<const float> samples, int channels, int sample_rate) {
    const auto data_bytes = static_cast<std::uint32_t>(samples.size() * 2);
    std::vector<std::uint8_t> out;
    out.reserve(44 + data_bytes);
    const auto put32 = [&out](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
        }
    };
    const auto put16 = [&out](std::uint16_t v) {
        out.push_back(static_cast<std::uint8_t>(v));
        out.push_back(static_cast<std::uint8_t>(v >> 8));
    };
    const auto tag = [&out](const char* text) { out.insert(out.end(), text, text + 4); };
    tag("RIFF");
    put32(36 + data_bytes);
    tag("WAVE");
    tag("fmt ");
    put32(16);
    put16(1);  // PCM
    put16(static_cast<std::uint16_t>(channels));
    put32(static_cast<std::uint32_t>(sample_rate));
    put32(static_cast<std::uint32_t>(sample_rate * channels * 2));
    put16(static_cast<std::uint16_t>(channels * 2));
    put16(16);
    tag("data");
    put32(data_bytes);
    for (const float sample : samples) {
        const auto value = static_cast<std::int16_t>(std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
        put16(static_cast<std::uint16_t>(value));
    }
    return out;
}

}  // namespace moteur
