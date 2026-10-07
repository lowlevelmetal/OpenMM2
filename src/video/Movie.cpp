#include "video/Movie.h"

#include <format>

namespace mm2::video {

std::unique_ptr<Movie> Movie::open(std::shared_ptr<const RandomAccessFile> file, std::string* error) {
    if (!file) {
        if (error)
            *error = "no file";
        return nullptr;
    }
    auto avi = AviFile::parse(*file, error);
    if (!avi)
        return nullptr;
    const std::uint32_t handler = avi->info().videoHandler, compression = avi->info().videoCompression;
    const auto isIv50 = [](std::uint32_t f) { return fourccString(f) == "IV50" || fourccString(f) == "iv50"; };
    if (!isIv50(handler) && !isIv50(compression)) {
        if (error)
            *error = std::format("unsupported video codec '{}'", fourccString(compression));
        return nullptr;
    }
    std::unique_ptr<Movie> movie(new Movie());
    movie->m_file = std::move(file);
    movie->m_avi = std::move(*avi);
    return movie;
}

bool Movie::decodeNext(YuvFrame& out, std::string* error) {
    const auto& chunks = m_avi.videoChunks();
    if (m_next >= static_cast<int>(chunks.size()))
        return false;
    const AviChunk& c = chunks[static_cast<std::size_t>(m_next++)];
    if (c.size > 0) {
        m_chunk.resize(c.size);
        if (m_file->readExact(c.offset, m_chunk)) {
            std::string err;
            if (m_decoder.decode(m_chunk, m_last, &err) == Indeo5Decoder::Result::Error && error)
                *error = std::format("frame {}: {}", m_next - 1, err);
        } else if (error) {
            *error = std::format("frame {}: read error", m_next - 1);
        }
    }
    if (m_last.y.empty()) {
        // Nothing decoded yet: a black picture of the header's size.
        m_last.width = width();
        m_last.height = height();
        m_last.chromaWidth = (width() + 3) / 4;
        m_last.chromaHeight = (height() + 3) / 4;
        m_last.y.assign(static_cast<std::size_t>(m_last.width * m_last.height), 16);
        m_last.u.assign(static_cast<std::size_t>(m_last.chromaWidth * m_last.chromaHeight), 128);
        m_last.v = m_last.u;
    }
    out = m_last;
    return true;
}

bool Movie::decodeNextRgba(std::vector<std::uint8_t>& rgba, std::string* error) {
    YuvFrame frame;
    if (!decodeNext(frame, error))
        return false;
    yuv410ToRgba(frame, rgba);
    return true;
}

std::optional<audio::SoundBuffer> Movie::readAudio(std::string* error) const {
    return m_avi.readAudio(*m_file, error);
}

} // namespace mm2::video
