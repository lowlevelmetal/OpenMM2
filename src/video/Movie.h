#pragma once

#include "video/Avi.h"
#include "video/Indeo5.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mm2::video {

// An AVI movie with Indeo 5 video, decoded frame by frame in order.
class Movie {
public:
    static std::unique_ptr<Movie> open(std::shared_ptr<const RandomAccessFile> file, std::string* error = nullptr);

    const AviInfo& info() const { return m_avi.info(); }
    int frameCount() const { return static_cast<int>(m_avi.videoChunks().size()); }
    double fps() const { return m_avi.info().fps; }
    int width() const { return m_avi.info().width; }
    int height() const { return m_avi.info().height; }
    int nextFrameIndex() const { return m_next; }

    // Decodes the next frame. Null/empty frames and decode errors repeat the
    // previous picture (as players of the era did). Returns false at the end.
    bool decodeNext(YuvFrame& out, std::string* error = nullptr);
    // Same, converted to RGBA8 (top row first).
    bool decodeNextRgba(std::vector<std::uint8_t>& rgba, std::string* error = nullptr);

    // The soundtrack as 16-bit PCM, if the movie has one.
    std::optional<audio::SoundBuffer> readAudio(std::string* error = nullptr) const;

private:
    Movie() = default;
    std::shared_ptr<const RandomAccessFile> m_file;
    AviFile m_avi;
    Indeo5Decoder m_decoder;
    YuvFrame m_last;
    std::vector<std::byte> m_chunk;
    int m_next = 0;
};

} // namespace mm2::video
