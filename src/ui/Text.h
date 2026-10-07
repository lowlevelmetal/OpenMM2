#pragma once

#include "render/Overlay2D.h"
#include "ui/Font.h"

#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace mm2::ui {

enum class Align { Left, Center, Right };

// Draws text through an Overlay2D in the 640x480 virtual space. Glyphs are
// rasterized at the output's real pixel size (virtual size x UI scale), so
// text stays sharp at any resolution; atlases are rebuilt when the scale
// changes.
class TextRenderer {
public:
    explicit TextRenderer(render::Device& device);
    ~TextRenderer();
    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    // Draws `text` with its baseline-top box at (x, y) in virtual units;
    // `size` is the cell height in virtual pixels (the FontSpec height).
    // Returns the advance width in virtual units.
    float draw(render::Overlay2D& overlay, const FontSpec& font, std::string_view text, float x, float y,
               std::uint32_t color, Align align = Align::Left);
    // Width in virtual units at the overlay's current scale.
    float measure(const render::Overlay2D& overlay, const FontSpec& font, std::string_view text);
    // Line height in virtual units.
    float lineHeight(const render::Overlay2D& overlay, const FontSpec& font);

    // Draws wrapped text inside a box; returns the height used.
    float drawWrapped(render::Overlay2D& overlay, const FontSpec& font, std::string_view text, float x, float y,
                      float width, std::uint32_t color, Align align = Align::Left);

private:
    struct Entry {
        std::unique_ptr<FontAtlas> atlas;
        render::TextureHandle texture;
        float pixelScale = 1.0f; // atlas pixels per virtual unit
    };
    Entry* entry(const render::Overlay2D& overlay, const FontSpec& font);

    render::Device& m_device;
    std::map<std::string, Entry> m_entries;
};

} // namespace mm2::ui
