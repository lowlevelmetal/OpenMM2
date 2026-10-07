#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mm2::ui {

// Font description as stored in the game's string table:
//   "Gill Sans MT, 12, 24, 0, 400"  ->  face, size, size2, escapement, weight
// The meaning of the two sizes is not known for certain (see
// docs/formats/strings.md); `size2` is used as the GDI cell height in pixels at
// the 640x480 reference resolution.
struct FontSpec {
    std::string face = "Arial";
    int size = 12;
    int size2 = 12;
    int escapement = 0;
    int weight = 400;

    bool bold() const { return weight >= 600 || face.find("Bold") != std::string::npos; }
    static std::optional<FontSpec> parse(std::string_view text);
};

// A TrueType/OpenType font file held in memory.
class FontFile {
public:
    static std::shared_ptr<FontFile> load(const std::filesystem::path& path);
    static std::shared_ptr<FontFile> fromMemory(std::vector<std::uint8_t> data, std::string label);
    ~FontFile();

    const std::string& label() const { return m_label; }
    const void* info() const { return m_info.get(); } // stbtt_fontinfo
    bool hasGlyph(char32_t c) const;

private:
    FontFile() = default;
    std::vector<std::uint8_t> m_data;
    std::shared_ptr<void> m_info;
    std::string m_label;
};

// Finds a font for a face name used by the game. The original Windows font is
// preferred when installed (fonts directory of the system or the user);
// otherwise the open substitute shipped in <exe>/fonts is used.
std::shared_ptr<FontFile> findFont(std::string_view face, bool bold);

// Path of a font shipped with OpenMM2 (e.g. "LiberationSans-Regular.ttf"),
// or an empty path when it is not installed.
std::filesystem::path bundledFontPath(std::string_view fileName);

struct Glyph {
    std::uint16_t x = 0, y = 0, w = 0, h = 0; // atlas rectangle
    float xoff = 0, yoff = 0;                 // from pen position to the bitmap's top-left
    float advance = 0;
};

// Glyphs of one font at one pixel size, rasterized into an 8-bit coverage atlas.
// Covers ASCII and Latin-1 (all text in the retail string tables), plus any
// extra code points requested.
class FontAtlas {
public:
    // `cellHeight` follows GDI's positive LOGFONT height: ascent + descent in pixels.
    static std::optional<FontAtlas> bake(std::shared_ptr<FontFile> font, float cellHeight,
                                         const std::vector<char32_t>& extra = {});

    int width() const { return m_width; }
    int height() const { return m_height; }
    const std::vector<std::uint8_t>& coverage() const { return m_pixels; }

    float ascent() const { return m_ascent; }
    float descent() const { return m_descent; } // negative
    float lineHeight() const { return m_lineHeight; }

    const Glyph* glyph(char32_t c) const;
    float kerning(char32_t a, char32_t b) const;

    // Width of a single line of UTF-8 text in pixels.
    float measure(std::string_view text) const;

    // Lays out one line of UTF-8 text with its baseline at (x, y) (y grows
    // downwards) and calls `emit(glyph, left, top)` for every visible glyph.
    template <class Emit>
    float layout(std::string_view text, float x, float y, Emit&& emit) const;

    // Splits text into lines no wider than `maxWidth`, honouring "\n" (the
    // string table uses a literal backslash-n as well as real newlines).
    std::vector<std::string> wrap(std::string_view text, float maxWidth) const;

private:
    std::shared_ptr<FontFile> m_font;
    float m_scale = 1.0f;
    int m_width = 0, m_height = 0;
    std::vector<std::uint8_t> m_pixels;
    std::unordered_map<char32_t, Glyph> m_glyphs;
    float m_ascent = 0, m_descent = 0, m_lineHeight = 0;
};

// Decodes the next code point from UTF-8 (invalid bytes decode as U+FFFD).
char32_t nextCodepoint(std::string_view text, std::size_t& pos);

template <class Emit>
float FontAtlas::layout(std::string_view text, float x, float y, Emit&& emit) const {
    std::size_t pos = 0;
    char32_t prev = 0;
    while (pos < text.size()) {
        const char32_t c = nextCodepoint(text, pos);
        const Glyph* g = glyph(c);
        if (!g)
            g = glyph(U'?');
        if (!g)
            continue;
        if (prev)
            x += kerning(prev, c);
        if (g->w && g->h)
            emit(*g, x + g->xoff, y + g->yoff);
        x += g->advance;
        prev = c;
    }
    return x;
}

} // namespace mm2::ui
