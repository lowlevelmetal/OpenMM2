#include "game/fx/SkidMarks.h"

#include <cmath>

namespace mm2::game::fx {

SkidTrail::SkidTrail(int maxSkids) : m_quads(static_cast<std::size_t>(std::max(1, maxSkids))) {}

void SkidTrail::reset() {
    for (auto& q : m_quads)
        q.used = false;
    m_next = 0;
    m_timeSinceTrack = 0.0f;
    m_notSkidding = true;
    m_distance = 0.0f;
}

bool SkidTrail::update(float dt, const SkidInput& in) {
    // mmSkidManager::Update
    if ((in.carSpeed > kSpeedThreshold || in.wheelSpeed > kSpeedThreshold) &&
        (std::abs(in.latSlip) > in.slipThreshold || std::abs(in.longSlip) > in.slipThreshold) && in.onGround &&
        in.shouldSkid && in.groundNormal.y > 0.1f) {
        m_timeSinceTrack += dt;
        if (m_timeSinceTrack > kTrackInterval) {
            if (in.allowTrack)
                layTrack(in);
            m_timeSinceTrack = 0.0f;
        }
        return true;
    }
    m_notSkidding = true;
    return false;
}

void SkidTrail::layTrack(const SkidInput& in) {
    // mmSkidManager::LayTrack: the track's edges are +-half the tyre width
    // along the contact frame's axle direction.
    const Vec3 half = in.contact.m0.normalized() * (in.width * 0.5f);
    const Vec3 left = in.contact.m3 - half;
    const Vec3 right = in.contact.m3 + half;
    if (m_notSkidding) {
        // First sample of a new track: just remember the edge.
        m_notSkidding = false;
    } else {
        Quad& q = m_quads[static_cast<std::size_t>(m_next)];
        q.p[0] = m_prevLeft;
        q.p[1] = m_prevRight;
        q.p[2] = left;
        q.p[3] = right;
        q.light = in.lightMarks;
        const float step = ((left + right) * 0.5f).dist((m_prevLeft + m_prevRight) * 0.5f);
        q.v0 = m_distance;
        m_distance += step / std::max(in.width, 0.05f);
        q.v1 = m_distance;
        q.used = true;
        m_next = (m_next + 1) % static_cast<int>(m_quads.size());
    }
    m_prevLeft = left;
    m_prevRight = right;
}

void SkidRenderer::draw(render::Device& device, TextureLibrary& textures, const std::vector<const SkidTrail*>& trails) {
    std::vector<render::Vertex3D> vertices;
    std::vector<std::uint16_t> indices;
    auto vert = [&](const Vec3& p, float u, float v, std::uint32_t color) {
        render::Vertex3D rv{};
        rv.position[0] = p.x;
        rv.position[1] = p.y + 0.01f; // lift off the road to avoid z-fighting
        rv.position[2] = p.z;
        rv.normal[1] = 1.0f;
        rv.color = color;
        rv.uv0[0] = rv.uv1[0] = u;
        rv.uv0[1] = rv.uv1[1] = v;
        vertices.push_back(rv);
    };
    for (const SkidTrail* t : trails) {
        for (const auto& q : t->quads()) {
            if (!q.used || vertices.size() + 4 > 65535)
                continue;
            // Dark marks normally, pale ones in snow (MM1 variant 1); colours inferred.
            const std::uint32_t color = q.light ? 0xC0E0E0E0u : 0xC0303030u;
            const auto base = static_cast<std::uint16_t>(vertices.size());
            vert(q.p[0], 0.0f, q.v0, color);
            vert(q.p[1], 1.0f, q.v0, color);
            vert(q.p[2], 0.0f, q.v1, color);
            vert(q.p[3], 1.0f, q.v1, color);
            for (std::uint16_t k : {0, 1, 3, 0, 3, 2})
                indices.push_back(static_cast<std::uint16_t>(base + k));
        }
    }
    if (indices.empty())
        return;
    render::DrawCall call;
    call.vertices = device.uploadTransient(render::BufferKind::Vertex, std::span<const render::Vertex3D>(vertices));
    call.indices = device.uploadTransient(render::BufferKind::Index, std::span<const std::uint16_t>(indices));
    call.count = static_cast<std::uint32_t>(indices.size());
    call.constants.world = Mat44::identity();
    call.constants.flags = render::DrawFlag::VertexColor | render::DrawFlag::Fog;
    if (const WorldTexture* tex = textures.get("tire_track")) {
        call.constants.flags |= render::DrawFlag::Texture0;
        render::SamplerDesc s = tex->sampler;
        s.addressU = render::AddressMode::Clamp;
        s.addressV = render::AddressMode::Wrap;
        call.textures[0] = {tex->handle, s};
    }
    call.state.blend = render::BlendMode::Alpha;
    call.state.depthWrite = false;
    call.state.depthBias = true;
    call.depthBias = 2.0f; // positive pulls towards the viewer (render::DrawCall)
    call.depthBiasSlope = 1.0f;
    call.state.cull = render::CullMode::None;
    device.draw(call);
}

} // namespace mm2::game::fx
