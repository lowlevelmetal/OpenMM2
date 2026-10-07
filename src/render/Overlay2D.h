#pragma once

#include "render/DisplaySettings.h"
#include "render/Projection.h"
#include "render/Types.h"

#include <vector>

namespace mm2::render {

class Device;

// Batched 2D drawing in the original game's 640x480 UI space, scaled to the
// window according to the UI scale mode (see UiLayout). Use inside an
// overlay pass:
//
//   overlay.begin(UiScaleMode::Fit);
//   overlay.image(tex, 10, 10, 128, 64);
//   overlay.rect(0, 440, 640, 40, packColor(0, 0, 0, 128));
//   overlay.end();
//
// Coordinates outside 0..640 / 0..480 are allowed; layout().left/right give
// the visible range for edge-anchored HUD elements on wide screens.
class Overlay2D {
public:
    explicit Overlay2D(Device& device);

    void begin(UiScaleMode mode);
    void end();
    const UiLayout& layout() const { return m_layout; }

    void rect(float x, float y, float w, float h, std::uint32_t color, BlendMode blend = BlendMode::Alpha);
    void image(TextureHandle texture, float x, float y, float w, float h, Vec2 uv0 = {0, 0}, Vec2 uv1 = {1, 1},
               std::uint32_t color = 0xFFFFFFFFu, BlendMode blend = BlendMode::Alpha,
               Filter filter = Filter::Bilinear);
    // Arbitrary quad; vertex positions in virtual coordinates, clockwise from top-left.
    void quad(TextureHandle texture, const Vertex2D (&vertices)[4], BlendMode blend = BlendMode::Alpha,
              Filter filter = Filter::Bilinear);

    // Clips subsequent drawing to a virtual-space rectangle (nullptr = none).
    void setClip(const Vec4* virtualRect);

private:
    void flush();
    void setBatch(TextureHandle texture, BlendMode blend, Filter filter);

    Device& m_device;
    UiLayout m_layout;
    std::vector<Vertex2D> m_vertices;
    std::vector<std::uint16_t> m_indices;
    TextureHandle m_texture;
    BlendMode m_blend = BlendMode::Alpha;
    Filter m_filter = Filter::Bilinear;
    bool m_active = false;
};

} // namespace mm2::render
