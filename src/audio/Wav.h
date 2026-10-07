#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mm2::audio {

// Decoded PCM sound, interleaved signed 16-bit.
struct SoundBuffer {
    int sampleRate = 0;
    int channels = 0;
    std::vector<std::int16_t> samples; // frames * channels

    std::size_t frames() const { return channels ? samples.size() / static_cast<std::size_t>(channels) : 0; }
    double seconds() const { return sampleRate ? static_cast<double>(frames()) / sampleRate : 0.0; }
};

// Decodes a RIFF/WAVE file. Supports PCM 8/16/24/32-bit integer and 32-bit
// float with 1 or 2 channels (the retail files are all 16-bit mono PCM at
// 11025, 22050 or 48000 Hz).
std::optional<SoundBuffer> decodeWav(std::span<const std::byte> data, std::string* error = nullptr);

} // namespace mm2::audio
