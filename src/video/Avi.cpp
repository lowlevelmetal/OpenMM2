#include "video/Avi.h"

#include <array>
#include <cstring>
#include <format>
#include <functional>

namespace mm2::video {
namespace {

constexpr std::uint32_t fourcc(const char (&s)[5]) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(s[0])) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(s[1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(s[2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(s[3])) << 24);
}

void setError(std::string* error, std::string msg) {
    if (error)
        *error = std::move(msg);
}

bool readU32(const RandomAccessFile& f, std::uint64_t off, std::uint32_t& out) {
    std::array<std::byte, 4> b{};
    if (!f.readExact(off, b))
        return false;
    out = loadLE<std::uint32_t>(b.data());
    return true;
}

} // namespace

std::string fourccString(std::uint32_t v) {
    std::string s(4, ' ');
    for (int i = 0; i < 4; ++i) {
        const char c = static_cast<char>((v >> (8 * i)) & 0xFF);
        s[static_cast<std::size_t>(i)] = (c >= 32 && c < 127) ? c : '?';
    }
    return s;
}

std::optional<AviFile> AviFile::parse(const RandomAccessFile& file, std::string* error) {
    std::uint32_t riff = 0, riffSize = 0, form = 0;
    if (!readU32(file, 0, riff) || !readU32(file, 4, riffSize) || !readU32(file, 8, form) || riff != fourcc("RIFF") ||
        form != fourcc("AVI ")) {
        setError(error, "not an AVI file");
        return std::nullopt;
    }
    AviFile avi;
    const std::uint64_t end = std::min<std::uint64_t>(file.size(), 8ull + riffSize);

    // Stream type of each strl in order (index = stream number).
    std::vector<std::uint32_t> streamTypes;
    std::uint32_t pendingType = 0;
    bool sawMovi = false;

    std::function<bool(std::uint64_t, std::uint64_t, bool)> walk = [&](std::uint64_t off, std::uint64_t stop,
                                                                       bool inMovi) -> bool {
        while (off + 8 <= stop) {
            std::uint32_t id = 0, size = 0;
            if (!readU32(file, off, id) || !readU32(file, off + 4, size))
                return false;
            const std::uint64_t body = off + 8;
            const std::uint64_t bodyEnd = std::min<std::uint64_t>(body + size, stop);
            if (id == fourcc("LIST")) {
                std::uint32_t type = 0;
                if (!readU32(file, body, type))
                    return false;
                if (type == fourcc("movi"))
                    sawMovi = true;
                if (!walk(body + 4, bodyEnd, inMovi || type == fourcc("movi")))
                    return false;
            } else if (inMovi) {
                // "##dc"/"##db" video, "##wb" audio; ## = stream number.
                const char c0 = static_cast<char>(id & 0xFF), c1 = static_cast<char>((id >> 8) & 0xFF);
                if (c0 >= '0' && c0 <= '9' && c1 >= '0' && c1 <= '9') {
                    const std::size_t stream = static_cast<std::size_t>((c0 - '0') * 10 + (c1 - '0'));
                    const std::uint32_t kind = id >> 16;
                    const std::uint32_t type = stream < streamTypes.size() ? streamTypes[stream] : 0;
                    const AviChunk chunk{body, static_cast<std::uint32_t>(bodyEnd - body)};
                    if (type == fourcc("vids") && (kind == ('d' | ('c' << 8)) || kind == ('d' | ('b' << 8))))
                        avi.m_video.push_back(chunk);
                    else if (type == fourcc("auds") && kind == ('w' | ('b' << 8)))
                        avi.m_audio.push_back(chunk);
                }
            } else if (id == fourcc("avih") && size >= 40) {
                std::array<std::byte, 40> h{};
                if (!file.readExact(body, h))
                    return false;
                const auto usPerFrame = loadLE<std::uint32_t>(h.data());
                avi.m_info.totalFrames = loadLE<std::uint32_t>(h.data() + 16);
                avi.m_info.width = static_cast<int>(loadLE<std::uint32_t>(h.data() + 32));
                avi.m_info.height = static_cast<int>(loadLE<std::uint32_t>(h.data() + 36));
                if (usPerFrame)
                    avi.m_info.fps = 1e6 / usPerFrame;
            } else if (id == fourcc("strh") && size >= 32) {
                std::array<std::byte, 32> h{};
                if (!file.readExact(body, h))
                    return false;
                pendingType = loadLE<std::uint32_t>(h.data());
                streamTypes.push_back(pendingType);
                if (pendingType == fourcc("vids")) {
                    avi.m_info.videoHandler = loadLE<std::uint32_t>(h.data() + 4);
                    const auto scale = loadLE<std::uint32_t>(h.data() + 20);
                    const auto rate = loadLE<std::uint32_t>(h.data() + 24);
                    if (scale && rate)
                        avi.m_info.fps = static_cast<double>(rate) / scale;
                }
            } else if (id == fourcc("strf")) {
                if (pendingType == fourcc("vids") && size >= 20) {
                    std::array<std::byte, 20> h{};
                    if (!file.readExact(body, h))
                        return false;
                    avi.m_info.width = static_cast<int>(loadLE<std::int32_t>(h.data() + 4));
                    avi.m_info.height = std::abs(static_cast<int>(loadLE<std::int32_t>(h.data() + 8)));
                    avi.m_info.videoCompression = loadLE<std::uint32_t>(h.data() + 16);
                } else if (pendingType == fourcc("auds") && size >= 16) {
                    std::array<std::byte, 16> h{};
                    if (!file.readExact(body, h))
                        return false;
                    avi.m_info.hasAudio = true;
                    avi.m_info.audioFormat = loadLE<std::uint16_t>(h.data());
                    avi.m_info.channels = loadLE<std::uint16_t>(h.data() + 2);
                    avi.m_info.sampleRate = static_cast<int>(loadLE<std::uint32_t>(h.data() + 4));
                    avi.m_info.blockAlign = loadLE<std::uint16_t>(h.data() + 12);
                    avi.m_info.bitsPerSample = loadLE<std::uint16_t>(h.data() + 14);
                }
            }
            off = body + size + (size & 1);
        }
        return true;
    };
    if (!walk(12, end, false)) {
        setError(error, "truncated AVI file");
        return std::nullopt;
    }
    if (!sawMovi || avi.m_video.empty()) {
        setError(error, "AVI has no video frames");
        return std::nullopt;
    }
    return avi;
}

std::optional<audio::SoundBuffer> AviFile::readAudio(const RandomAccessFile& file, std::string* error) const {
    const AviInfo& in = m_info;
    if (!in.hasAudio || m_audio.empty()) {
        setError(error, "no audio stream");
        return std::nullopt;
    }
    if (in.audioFormat != 1 || (in.bitsPerSample != 8 && in.bitsPerSample != 16) || in.channels < 1 ||
        in.channels > 2 || in.sampleRate <= 0) {
        setError(error, std::format("unsupported audio: format {} {} bits {} channels", in.audioFormat,
                                    in.bitsPerSample, in.channels));
        return std::nullopt;
    }
    audio::SoundBuffer out;
    out.sampleRate = in.sampleRate;
    out.channels = in.channels;
    std::vector<std::byte> buf;
    for (const auto& c : m_audio) {
        buf.resize(c.size);
        if (!file.readExact(c.offset, buf)) {
            setError(error, "truncated audio chunk");
            return std::nullopt;
        }
        if (in.bitsPerSample == 8) {
            for (std::byte b : buf)
                out.samples.push_back(static_cast<std::int16_t>((std::to_integer<int>(b) - 128) << 8));
        } else {
            for (std::size_t i = 0; i + 1 < buf.size(); i += 2)
                out.samples.push_back(loadLE<std::int16_t>(buf.data() + i));
        }
    }
    out.samples.resize(out.samples.size() - out.samples.size() % static_cast<std::size_t>(in.channels));
    return out;
}

} // namespace mm2::video
