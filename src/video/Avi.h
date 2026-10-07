#pragma once

// Minimal AVI (RIFF) demuxer for the game's intro movie: reads the stream
// headers and lists the video and audio chunks of the "movi" list in file
// order. See docs/video.md.

#include "audio/Wav.h"
#include "core/File.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mm2::video {

struct AviChunk {
    std::uint64_t offset = 0; // payload offset in the file
    std::uint32_t size = 0;   // payload size (0 = dropped/repeat frame)
};

struct AviInfo {
    // Video stream (strh "vids" + BITMAPINFOHEADER).
    std::uint32_t videoHandler = 0;     // strh fccHandler, e.g. 'IV50'
    std::uint32_t videoCompression = 0; // biCompression
    int width = 0, height = 0;
    double fps = 0.0;
    std::uint32_t totalFrames = 0; // avih dwTotalFrames
    // Audio stream (strh "auds" + WAVEFORMATEX), if any.
    bool hasAudio = false;
    int audioFormat = 0; // 1 = PCM
    int channels = 0;
    int sampleRate = 0;
    int bitsPerSample = 0;
    int blockAlign = 0;
};

std::string fourccString(std::uint32_t fourcc);

class AviFile {
public:
    static std::optional<AviFile> parse(const RandomAccessFile& file, std::string* error = nullptr);

    const AviInfo& info() const { return m_info; }
    const std::vector<AviChunk>& videoChunks() const { return m_video; }
    const std::vector<AviChunk>& audioChunks() const { return m_audio; }
    double duration() const { return m_info.fps > 0 ? static_cast<double>(m_video.size()) / m_info.fps : 0.0; }

    // Concatenates the audio chunks into one PCM buffer (8-bit unsigned or
    // 16-bit signed PCM in the file; returned as signed 16-bit).
    std::optional<audio::SoundBuffer> readAudio(const RandomAccessFile& file, std::string* error = nullptr) const;

private:
    AviInfo m_info;
    std::vector<AviChunk> m_video;
    std::vector<AviChunk> m_audio;
};

} // namespace mm2::video
