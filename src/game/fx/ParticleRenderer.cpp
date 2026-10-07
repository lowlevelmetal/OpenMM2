#include "game/fx/ParticleRenderer.h"

#include <cmath>

namespace mm2::game::fx {
namespace {

// agiMeshSet::DefaultQuad: x, y, u, v.
constexpr float kQuad[4][4] = {{-1, -1, 0, 0}, {1, -1, 1, 0}, {1, 1, 1, 1}, {-1, 1, 0, 1}};

std::uint32_t argbToRgba(std::uint32_t argb) {
    return (argb & 0xFF00FF00u) | ((argb >> 16) & 0xFFu) | ((argb & 0xFFu) << 16);
}

} // namespace

ParticleRenderer::ParticleRenderer() {
    // agiMeshCardInfo::Init: angle = r * 2pi / count, x' = c x - s y, y' = s x + c y.
    const float step = 6.2831855f / static_cast<float>(kRotations);
    for (int r = 0; r < kRotations; ++r) {
        const float a = static_cast<float>(r) * step;
        const float c = std::cos(a), s = std::sin(a);
        for (int i = 0; i < 4; ++i)
            m_rot[static_cast<std::size_t>(r)][static_cast<std::size_t>(i)] = {c * kQuad[i][0] - s * kQuad[i][1],
                                                                               s * kQuad[i][0] + c * kQuad[i][1]};
    }
}

void ParticleRenderer::begin() {
    m_vertices.clear();
    m_indices.clear();
}

void ParticleRenderer::add(const SparkPos& p, int fw, int fh, const Mat34& cam) {
    if (m_vertices.size() + 4 > 65535)
        return;
    const auto& rot = m_rot[static_cast<std::size_t>((p.rotation >> 2) & (kRotations - 1))];
    const int frame = std::max(0, static_cast<int>(p.frame));
    const float col = static_cast<float>(frame % fw), row = static_cast<float>((frame / fw) % fh);
    const std::uint32_t color = argbToRgba(p.color);
    const auto base = static_cast<std::uint16_t>(m_vertices.size());
    for (int i = 0; i < 4; ++i) {
        const Vec2 o = rot[static_cast<std::size_t>(i)] * p.radius;
        const Vec3 w = p.position + cam.m0 * o.x + cam.m1 * o.y;
        render::Vertex3D v{};
        v.position[0] = w.x;
        v.position[1] = w.y;
        v.position[2] = w.z;
        v.normal[0] = cam.m2.x;
        v.normal[1] = cam.m2.y;
        v.normal[2] = cam.m2.z;
        v.color = color;
        v.uv0[0] = v.uv1[0] = (kQuad[i][2] + col) / static_cast<float>(fw);
        v.uv0[1] = v.uv1[1] = (kQuad[i][3] + row) / static_cast<float>(fh);
        m_vertices.push_back(v);
    }
    for (std::uint16_t k : {0, 1, 2, 0, 2, 3})
        m_indices.push_back(static_cast<std::uint16_t>(base + k));
}

void ParticleRenderer::flush(render::Device& device, const WorldTexture* texture, const CardStyle& style) {
    if (m_indices.empty())
        return;
    render::DrawCall call;
    call.vertices = device.uploadTransient(render::BufferKind::Vertex, std::span<const render::Vertex3D>(m_vertices));
    call.indices = device.uploadTransient(render::BufferKind::Index, std::span<const std::uint16_t>(m_indices));
    call.indexType = render::IndexType::U16;
    call.count = static_cast<std::uint32_t>(m_indices.size());
    call.constants.world = Mat44::identity();
    call.constants.color = style.tint;
    call.constants.flags = render::DrawFlag::VertexColor | render::DrawFlag::AlphaTest;
    call.constants.alphaRef = 1.0f / 255.0f;
    if (style.fog)
        call.constants.flags |= render::DrawFlag::Fog;
    if (texture) {
        call.constants.flags |= render::DrawFlag::Texture0;
        render::SamplerDesc sampler = texture->sampler;
        sampler.addressU = sampler.addressV = render::AddressMode::Clamp;
        call.textures[0] = {texture->handle, sampler};
    }
    call.state.blend = style.blend;
    call.state.depthWrite = false;
    call.state.cull = render::CullMode::None;
    device.draw(call);
    m_cardsDrawn += static_cast<int>(m_indices.size() / 6);
    begin();
}

void ParticleRenderer::draw(render::Device& device, const Mat34& cameraBasis, const ParticleSystem& system,
                            const WorldTexture* texture, const CardStyle& style) {
    begin();
    for (const auto& p : system.positions())
        add(p, system.framesWide(), system.framesHigh(), cameraBasis);
    flush(device, texture, style);
}

} // namespace mm2::game::fx
