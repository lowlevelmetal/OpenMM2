#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mm2::asset {

// Decoded image: RGBA8 texels with rows ordered BOTTOM to TOP: row 0 is the
// bottom edge of the picture and corresponds to texture coordinate v = 0 in
// the game's UVs (verified: the stop sign face in sp_stop_f.pkg has v = 1 at
// its top, and .tex files store the picture's bottom row first). Upload rows
// in order and use the game's UVs unchanged; flip only for display formats
// such as PNG (encodePng does). levels[0] is the full-size image; further
// entries are the mip levels stored in the file, each half the previous size.
struct Image {
    struct Level {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::vector<std::uint8_t> rgba; // width * height * 4 bytes
    };
    std::vector<Level> levels;

    std::uint32_t width() const { return levels.empty() ? 0 : levels[0].width; }
    std::uint32_t height() const { return levels.empty() ? 0 : levels[0].height; }
    bool empty() const { return levels.empty(); }
    // True when any texel of the top level has alpha < 255.
    bool hasTranslucency() const;
};

// --- Angel .tex textures (texture/*.tex) -------------------------------------
//
// See docs/formats/tex.md. Header (14 bytes, little-endian):
//   u16 width, u16 height, u16 format, u16 mipCount, u16 reserved (always 1),
//   u32 flags
// followed by a 256-entry palette (B,G,R,A bytes, like a Windows RGBQUAD) for
// paletted formats and then the mip levels, largest first, tightly packed,
// each stored bottom row first.

enum class TexFormat : std::uint16_t {
    P8 = 1,        // 8-bit palette, opaque palette entries
    PA8 = 14,      // 8-bit palette with per-entry alpha
    P4 = 15,       // 4-bit palette (16 entries); not used by retail files
    PA4 = 16,      // 4-bit palette with alpha; not used by retail files
    RGB888 = 17,   // 24-bit, bytes R,G,B
    RGBA8888 = 18, // 32-bit, bytes R,G,B,A (note: palettes are B,G,R,A)
};

// Texture flags ("TexEnv"). Low bits match the Angel engine's
// agiTexParameters (Open1560); the high bits are MM2-specific and their exact
// meaning is not yet known. See docs/formats/tex.md for the evidence.
namespace TexFlags {
inline constexpr std::uint32_t Alpha = 0x1;  // inferred: texture is drawn with alpha blending/testing
inline constexpr std::uint32_t WrapU = 0x2;  // repeat horizontally (facades, roads, sidewalks)
inline constexpr std::uint32_t WrapV = 0x4;  // repeat vertically (roads, sidewalks)
inline constexpr std::uint32_t Unknown8000 = 0x8000;   // set on particles, roads, dashboards
inline constexpr std::uint32_t Unknown10000 = 0x10000; // set on cars, trees, skies, fences
} // namespace TexFlags

struct TexHeader {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    TexFormat format = TexFormat::P8;
    std::uint16_t mipCount = 0;
    std::uint16_t reserved = 0;
    std::uint32_t flags = 0;
};

struct Texture {
    TexHeader header;
    Image image;
    // Palette converted to R,G,B,A bytes; empty for true-colour formats.
    std::vector<std::uint8_t> palette;
};

std::optional<Texture> parseTex(std::span<const std::byte> data, std::string* error = nullptr);
// Header only (cheap; used by listings).
std::optional<TexHeader> parseTexHeader(std::span<const std::byte> data, std::string* error = nullptr);

// --- Other image files ----------------------------------------------------------

// Truevision TGA: uncompressed and RLE, true-colour (16/24/32-bit),
// colour-mapped and greyscale. Honours the origin bits (output bottom-up).
std::optional<Image> decodeTga(std::span<const std::byte> data, std::string* error = nullptr);

// JPEG, BMP and PNG through stb_image (single level).
std::optional<Image> decodeStb(std::span<const std::byte> data, std::string* error = nullptr);

// Decodes by file extension: .tex, .tga, .jpg/.jpeg, .bmp, .png.
std::optional<Image> decodeImageFile(std::string_view path, std::span<const std::byte> data,
                                     std::string* error = nullptr);

// Encodes one level as a PNG file (RGBA8), flipping it upright.
std::vector<std::byte> encodePng(const Image::Level& level);

} // namespace mm2::asset
