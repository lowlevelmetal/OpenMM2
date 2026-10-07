#pragma once

// Intel Indeo Video Interactive 5 ("IV50") decoder, used by the game's intro
// movie (GAME/LOGOS.AVI). A C++ translation of FFmpeg's decoder
// (libavcodec/indeo5.c, ivi.c, ivi_dsp.c, n7.1; LGPL-2.1-or-later, distributed
// here under GPL-3.0-or-later as the LGPL permits). See docs/video.md.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace mm2::video {

// Planar YUV 4:1:0 picture (chroma subsampled 4x horizontally and vertically),
// the decoder's native output. Rows are top first.
struct YuvFrame {
    int width = 0, height = 0;
    int chromaWidth = 0, chromaHeight = 0;
    std::vector<std::uint8_t> y; // width * height
    std::vector<std::uint8_t> u; // chromaWidth * chromaHeight (Cb)
    std::vector<std::uint8_t> v; // chromaWidth * chromaHeight (Cr)
};

class Indeo5Decoder {
public:
    Indeo5Decoder();
    ~Indeo5Decoder();
    Indeo5Decoder(const Indeo5Decoder&) = delete;
    Indeo5Decoder& operator=(const Indeo5Decoder&) = delete;

    enum class Result {
        Frame,   // `out` holds a new picture
        NoFrame, // a null (repeat) frame: keep showing the previous picture
        Error,   // corrupt data; the next intra frame resynchronises
    };

    // Decodes one compressed frame (the payload of an AVI "00dc" chunk).
    Result decode(std::span<const std::byte> data, YuvFrame& out, std::string* error = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

// Converts to RGBA8 (top row first). Indeo's YVU9 output is BT.601 with
// limited (16-235) luma range; chroma is upsampled bilinearly.
void yuv410ToRgba(const YuvFrame& frame, std::vector<std::uint8_t>& rgba);

} // namespace mm2::video
