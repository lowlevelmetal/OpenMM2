#include "render/Overlay2D.h"

#include "render/Device.h"

#include <algorithm>
#include <cmath>

namespace mm2::render {

Overlay2D::Overlay2D(Device& device) : m_device(device) {}

void Overlay2D::begin(UiScaleMode mode) {
    const Extent2D out = m_device.outputExtent();
    m_layout = computeUiLayout(out, mode);
    m_device.setFrameConstants(overlayConstants(out));
    m_device.setViewport({0, 0, static_cast<float>(out.width), static_cast<float>(out.height), 0, 1});
    m_device.setScissor(nullptr);
    m_vertices.clear();
    m_indices.clear();
    m_active = true;
}

void Overlay2D::end() {
    flush();
    m_device.setScissor(nullptr);
    m_active = false;
}

void Overlay2D::setBatch(TextureHandle texture, BlendMode blend, Filter filter) {
    if (texture == m_texture && blend == m_blend && filter == m_filter && m_vertices.size() + 4 <= 65532)
        return;
    flush();
    m_texture = texture;
    m_blend = blend;
    m_filter = filter;
}

void Overlay2D::flush() {
    if (m_indices.empty())
        return;
    DrawCall call;
    call.state.vertexFormat = VertexFormat::Overlay;
    call.state.blend = m_blend;
    call.state.cull = CullMode::None;
    call.state.depthTest = false;
    call.state.depthWrite = false;
    call.vertices = m_device.uploadTransient(BufferKind::Vertex, std::span<const Vertex2D>(m_vertices));
    call.indices = m_device.uploadTransient(BufferKind::Index, std::span<const std::uint16_t>(m_indices));
    call.indexType = IndexType::U16;
    call.count = static_cast<std::uint32_t>(m_indices.size());
    call.textures[0] = {m_texture, {m_filter, AddressMode::Clamp, AddressMode::Clamp}};
    call.constants.flags = m_texture ? DrawFlag::Texture0 : 0;
    m_device.draw(call);
    m_vertices.clear();
    m_indices.clear();
}

void Overlay2D::quad(TextureHandle texture, const Vertex2D (&v)[4], BlendMode blend, Filter filter) {
    setBatch(texture, blend, filter);
    const auto base = static_cast<std::uint16_t>(m_vertices.size());
    for (const Vertex2D& src : v) {
        Vertex2D d = src;
        const Vec2 p = m_layout.toPixels({src.position[0], src.position[1]});
        d.position[0] = p.x;
        d.position[1] = p.y;
        m_vertices.push_back(d);
    }
    for (std::uint16_t i : {0, 1, 2, 0, 2, 3})
        m_indices.push_back(static_cast<std::uint16_t>(base + i));
}

void Overlay2D::image(TextureHandle texture, float x, float y, float w, float h, Vec2 uv0, Vec2 uv1,
                      std::uint32_t color, BlendMode blend, Filter filter) {
    const Vertex2D v[4] = {
        {{x, y}, {uv0.x, uv0.y}, color},
        {{x + w, y}, {uv1.x, uv0.y}, color},
        {{x + w, y + h}, {uv1.x, uv1.y}, color},
        {{x, y + h}, {uv0.x, uv1.y}, color},
    };
    quad(texture, v, blend, filter);
}

void Overlay2D::rect(float x, float y, float w, float h, std::uint32_t color, BlendMode blend) {
    image(TextureHandle{}, x, y, w, h, {0, 0}, {1, 1}, color, blend, m_filter);
}

void Overlay2D::setClip(const Vec4* r) {
    flush();
    if (!r) {
        m_device.setScissor(nullptr);
        return;
    }
    const Vec2 a = m_layout.toPixels({r->x, r->y});
    const Vec2 b = m_layout.toPixels({r->x + r->z, r->y + r->w});
    const Rect px{static_cast<std::int32_t>(std::floor(a.x)), static_cast<std::int32_t>(std::floor(a.y)),
                  static_cast<std::uint32_t>(std::max(0.0f, std::ceil(b.x - a.x))),
                  static_cast<std::uint32_t>(std::max(0.0f, std::ceil(b.y - a.y)))};
    m_device.setScissor(&px);
}

} // namespace mm2::render
