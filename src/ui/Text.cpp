#include "ui/Text.h"

#include "core/Log.h"
#include "render/Device.h"

#include <cmath>
#include <format>
#include <vector>

namespace mm2::ui {

TextRenderer::TextRenderer(render::Device& device) : m_device(device) {}

TextRenderer::~TextRenderer() {
    for (auto& [key, e] : m_entries)
        if (e.texture)
            m_device.destroyTexture(e.texture);
}

TextRenderer::Entry* TextRenderer::entry(const render::Overlay2D& overlay, const FontSpec& font) {
    const float scale = overlay.layout().scaleY;
    // Quantize the pixel size so small window resizes reuse atlases.
    const float pixels = std::max(4.0f, std::round(static_cast<float>(font.size2) * scale));
    const std::string key = std::format("{}|{}|{}", font.face, font.bold() ? 'b' : 'r', pixels);
    if (auto it = m_entries.find(key); it != m_entries.end())
        return it->second.atlas ? &it->second : nullptr;

    Entry e;
    if (auto file = findFont(font.face, font.bold())) {
        if (auto atlas = FontAtlas::bake(file, pixels)) {
            e.atlas = std::make_unique<FontAtlas>(std::move(*atlas));
            // Coverage -> white RGBA with coverage in alpha.
            std::vector<std::uint8_t> rgba(e.atlas->coverage().size() * 4);
            for (std::size_t i = 0; i < e.atlas->coverage().size(); ++i) {
                rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
                rgba[i * 4 + 3] = e.atlas->coverage()[i];
            }
            render::TextureDesc desc;
            desc.width = static_cast<std::uint32_t>(e.atlas->width());
            desc.height = static_cast<std::uint32_t>(e.atlas->height());
            desc.debugName = "font " + key;
            const render::TextureData data{rgba.data(), 0};
            e.texture = m_device.createTexture(desc, std::span(&data, 1));
            e.pixelScale = pixels / static_cast<float>(font.size2);
        }
    }
    if (!e.atlas)
        log::warn("ui: no font for '{}'", font.face);
    auto& stored = m_entries[key] = std::move(e);
    return stored.atlas ? &stored : nullptr;
}

// mmTextNode::GetTextDimensions (for this and lineHeight).
float TextRenderer::measure(const render::Overlay2D& overlay, const FontSpec& font, std::string_view text) {
    Entry* e = entry(overlay, font);
    return e ? e->atlas->measure(text) / e->pixelScale : 0.0f;
}

float TextRenderer::lineHeight(const render::Overlay2D& overlay, const FontSpec& font) {
    Entry* e = entry(overlay, font);
    return e ? e->atlas->lineHeight() / e->pixelScale : static_cast<float>(font.size2);
}

// Stands in for mmTextNode::RenderText and mmTextNode::Cull (GDI text drawn
// into a bitmap and copied to the screen); the widgets draw the text effects.
float TextRenderer::draw(render::Overlay2D& overlay, const FontSpec& font, std::string_view text, float x, float y,
                         std::uint32_t color, Align align) {
    Entry* e = entry(overlay, font);
    if (!e)
        return 0.0f;
    const float inv = 1.0f / e->pixelScale;
    const float width = e->atlas->measure(text) * inv;
    if (align == Align::Center)
        x -= width * 0.5f;
    else if (align == Align::Right)
        x -= width;
    // Snap the pen to whole output pixels to keep glyphs crisp.
    const auto& l = overlay.layout();
    const float px = std::round(l.offsetX + x * l.scaleX);
    const float py = std::round(l.offsetY + y * l.scaleY + e->atlas->ascent());
    const float aw = static_cast<float>(e->atlas->width()), ah = static_cast<float>(e->atlas->height());
    e->atlas->layout(text, px, py, [&](const Glyph& g, float gx, float gy) {
        const float vx = (gx - l.offsetX) / l.scaleX, vy = (gy - l.offsetY) / l.scaleY;
        overlay.image(e->texture, vx, vy, g.w / l.scaleX, g.h / l.scaleY, {g.x / aw, g.y / ah},
                      {(g.x + g.w) / aw, (g.y + g.h) / ah}, color, render::BlendMode::Alpha, render::Filter::Point);
    });
    return width;
}

// mmTextNode::RenderText with DT_WORDBREAK (text effect 0x20).
float TextRenderer::drawWrapped(render::Overlay2D& overlay, const FontSpec& font, std::string_view text, float x,
                                float y, float width, std::uint32_t color, Align align) {
    Entry* e = entry(overlay, font);
    if (!e)
        return 0.0f;
    const auto lines = e->atlas->wrap(text, width * e->pixelScale);
    const float lh = e->atlas->lineHeight() / e->pixelScale;
    float cy = y;
    for (const auto& line : lines) {
        const float lx = align == Align::Center ? x + width * 0.5f : (align == Align::Right ? x + width : x);
        draw(overlay, font, line, lx, cy, color, align);
        cy += lh;
    }
    return cy - y;
}

} // namespace mm2::ui
