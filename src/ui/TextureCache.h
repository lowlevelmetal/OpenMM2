#pragma once

#include "render/Device.h"
#include "render/Overlay2D.h"
#include "vfs/Vfs.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace mm2::ui {

// GPU textures for 2D art loaded from the game archives by virtual path
// ("jpg/main_bk.jpg", "texture/dlg_ok.tga", "texture/foo.tex").
//
// Game images are stored bottom row first (see asset::Image), so v = 0 is
// the bottom edge; UiTexture::uvTop/uvBottom give the v coordinates of the
// picture's top and bottom edges for screen-aligned quads.
struct UiTexture {
    render::TextureHandle handle;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    static constexpr float uvTop = 1.0f;
    static constexpr float uvBottom = 0.0f;
    explicit operator bool() const { return static_cast<bool>(handle); }
};

// Draws a game image upright (flipping its bottom-up rows) at a virtual rect.
inline void drawImage(render::Overlay2D& overlay, const UiTexture& tex, float x, float y, float w, float h,
                      std::uint32_t color = 0xFFFFFFFFu, render::BlendMode blend = render::BlendMode::Alpha) {
    if (tex)
        overlay.image(tex.handle, x, y, w, h, {0.0f, UiTexture::uvTop}, {1.0f, UiTexture::uvBottom}, color, blend);
}

// Draws a game image at its native size (one texel per virtual pixel).
inline void drawImage(render::Overlay2D& overlay, const UiTexture& tex, float x, float y,
                      std::uint32_t color = 0xFFFFFFFFu) {
    drawImage(overlay, tex, x, y, static_cast<float>(tex.width), static_cast<float>(tex.height), color);
}

class TextureCache {
public:
    TextureCache(render::Device& device, const vfs::Vfs& vfs);
    ~TextureCache();
    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    // Loads (once) and returns the texture; an empty UiTexture if missing.
    const UiTexture& get(std::string_view path);
    void clear();

private:
    render::Device& m_device;
    const vfs::Vfs& m_vfs;
    std::unordered_map<std::string, UiTexture> m_textures;
};

} // namespace mm2::ui
