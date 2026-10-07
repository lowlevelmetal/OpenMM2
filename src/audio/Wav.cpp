#include "audio/Wav.h"

#include "core/File.h"

#include <algorithm>
#include <cstring>
#include <format>

namespace mm2::audio {
namespace {

void setError(std::string* error, std::string msg) {
    if (error)
        *error = std::move(msg);
}

std::int16_t floatToS16(float f) {
    f = std::clamp(f, -1.0f, 1.0f);
    return static_cast<std::int16_t>(f * 32767.0f);
}

} // namespace

std::optional<SoundBuffer> decodeWav(std::span<const std::byte> data, std::string* error) {
    if (data.size() < 12 || std::memcmp(data.data(), "RIFF", 4) != 0 || std::memcmp(data.data() + 8, "WAVE", 4) != 0) {
        setError(error, "not a RIFF/WAVE file");
        return std::nullopt;
    }
    std::uint16_t format = 0, channels = 0, bits = 0, blockAlign = 0;
    std::uint32_t rate = 0;
    bool haveFmt = false;
    std::span<const std::byte> pcm;
    bool havePcm = false;

    std::size_t pos = 12;
    while (pos + 8 <= data.size()) {
        const std::byte* hdr = data.data() + pos;
        const auto size = loadLE<std::uint32_t>(hdr + 4);
        const std::size_t body = pos + 8;
        // Tolerate a final chunk whose declared size runs past the end of file.
        const std::size_t avail = std::min<std::size_t>(size, data.size() - body);
        if (std::memcmp(hdr, "fmt ", 4) == 0 && avail >= 16) {
            format = loadLE<std::uint16_t>(data.data() + body);
            channels = loadLE<std::uint16_t>(data.data() + body + 2);
            rate = loadLE<std::uint32_t>(data.data() + body + 4);
            blockAlign = loadLE<std::uint16_t>(data.data() + body + 12);
            bits = loadLE<std::uint16_t>(data.data() + body + 14);
            if (format == 0xFFFE && avail >= 26) // WAVE_FORMAT_EXTENSIBLE: subformat GUID's first word
                format = loadLE<std::uint16_t>(data.data() + body + 24);
            haveFmt = true;
        } else if (std::memcmp(hdr, "data", 4) == 0) {
            pcm = data.subspan(body, avail);
            havePcm = true;
        }
        pos = body + size + (size & 1);
    }
    if (!haveFmt || !havePcm) {
        setError(error, "missing fmt or data chunk");
        return std::nullopt;
    }
    if (channels < 1 || channels > 2 || rate == 0 || rate > 384000) {
        setError(error, std::format("unsupported layout: {} channels at {} Hz", channels, rate));
        return std::nullopt;
    }
    const bool isFloat = format == 3;
    if (!(format == 1 || isFloat) || (isFloat && bits != 32) ||
        (!isFloat && bits != 8 && bits != 16 && bits != 24 && bits != 32)) {
        setError(error, std::format("unsupported encoding: format {} with {} bits", format, bits));
        return std::nullopt;
    }
    const std::size_t bytesPerSample = bits / 8;
    if (blockAlign != bytesPerSample * channels) {
        setError(error, "inconsistent block alignment");
        return std::nullopt;
    }

    SoundBuffer out;
    out.sampleRate = static_cast<int>(rate);
    out.channels = channels;
    const std::size_t count = pcm.size() / bytesPerSample;
    out.samples.resize(count - count % channels);
    for (std::size_t i = 0; i < out.samples.size(); ++i) {
        const std::byte* p = pcm.data() + i * bytesPerSample;
        std::int16_t s = 0;
        if (isFloat)
            s = floatToS16(loadLE<float>(p));
        else if (bits == 8)
            s = static_cast<std::int16_t>((std::to_integer<int>(p[0]) - 128) << 8);
        else if (bits == 16)
            s = loadLE<std::int16_t>(p);
        else if (bits == 24)
            s = static_cast<std::int16_t>(loadLE<std::int16_t>(p + 1));
        else
            s = static_cast<std::int16_t>(loadLE<std::int32_t>(p) >> 16);
        out.samples[i] = s;
    }
    return out;
}

} // namespace mm2::audio
