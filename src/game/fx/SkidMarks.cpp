#include "game/fx/SkidMarks.h"

#include <bit>
#include <cmath>

namespace mm2::game::fx {

SkidTrack::SkidTrack(int pairs) {
    const auto n = std::bit_ceil(static_cast<std::size_t>(std::max(2, pairs)));
    m_pairs.resize(n);
    m_mask = n - 1;
}

void SkidTrack::setWidth(float width) {
    m_halfWidth = width * 0.5f;
    m_invWidth = 1.0f / width;
}

void SkidTrack::reset() {
    m_head = m_tail = 0;
    m_laying = false;
    m_pending = false;
}

// Adds a pair at the head; a full ring drops the oldest pair, and the pair
// after it too when that one starts a new strip.
void SkidTrack::push(const Pair& p) {
    const std::size_t next = (m_head + 1) & m_mask;
    if (next == m_tail) {
        m_tail = (m_tail + 1) & m_mask;
        const std::size_t after = (m_tail + 1) & m_mask;
        if (m_pairs[after].v == 0.0f)
            m_tail = after;
    }
    m_pairs[m_head] = p;
    m_head = next;
}

void SkidTrack::update(const Vec3& contact, const Vec3& axle, bool laying) {
    if (!laying) {
        m_pending = false;
        m_laying = false;
        return;
    }
    const Pair here{axle * -m_halfWidth + contact, axle * m_halfWidth + contact, 0.0f};
    if (!m_laying) {
        if (!m_pending) {
            m_pending = true;
            m_pendingPair = here;
            m_last = contact;
            return;
        }
        // Start a strip once the wheel has moved 10 cm from the held pair.
        const Vec3 d = contact - m_last;
        if (d.mag2() < 0.010000001f)
            return;
        push(m_pendingPair);
        m_pending = false;
        m_direction = d * (1.0f / d.mag());
        m_directionValid = true;
        m_laying = true;
        push({here.left, here.right, d.mag() * m_invWidth});
        return;
    }
    const Vec3 d = contact - m_last;
    const float length = d.mag();
    const float v = length * m_invWidth;
    // A wheel that has not moved has no direction: the original's NaN
    // direction fails the test, so it adds a pair (with v = 0, which starts
    // a new strip) and the next segment adds one too.
    const bool moved = length > 0.0f;
    const Vec3 direction = moved ? d * (1.0f / length) : Vec3{};
    if (moved && m_directionValid && direction.dot(m_direction) >= 0.99f && length <= 10.0f) {
        // Same direction: the newest pair follows the wheel.
        m_pairs[(m_head - 1) & m_mask] = {here.left, here.right, v};
        return;
    }
    push({here.left, here.right, v});
    m_direction = direction;
    m_directionValid = moved;
    m_last = contact;
}

void SkidRenderer::draw(render::Device& device, TextureLibrary& textures, const std::vector<const SkidTrack*>& tracks) {
    std::vector<render::Vertex3D> vertices;
    std::vector<std::uint16_t> indices;
    auto vert = [&](const Vec3& p, float u, float v) {
        render::Vertex3D rv{};
        rv.position[0] = p.x;
        rv.position[1] = p.y;
        rv.position[2] = p.z;
        rv.normal[1] = 1.0f;
        rv.color = 0xFFFFFFFFu;
        rv.uv0[0] = rv.uv1[0] = u;
        rv.uv0[1] = rv.uv1[1] = v;
        vertices.push_back(rv);
    };
    for (const SkidTrack* t : tracks) {
        t->forEachStrip([&](const std::vector<const SkidTrack::Pair*>& strip) {
            if (strip.size() < 2 || vertices.size() + strip.size() * 2 > 65535)
                return;
            // A triangle strip of (left, right) pairs: s = 0 left, 1 right; t = v.
            const auto base = static_cast<std::uint16_t>(vertices.size());
            for (const auto* p : strip) {
                vert(p->left, 0.0f, p->v);
                vert(p->right, 1.0f, p->v);
            }
            for (std::size_t k = 0; k + 1 < strip.size(); ++k) {
                const auto a = static_cast<std::uint16_t>(base + 2 * k);
                for (std::uint16_t i : {a, static_cast<std::uint16_t>(a + 1), static_cast<std::uint16_t>(a + 3), a,
                                        static_cast<std::uint16_t>(a + 3), static_cast<std::uint16_t>(a + 2)})
                    indices.push_back(i);
            }
        });
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
    // vehCar::DrawTracks leaves the default culling on: a strip faces up
    // when the wheel laid it rolling forward (the pairs run left to right
    // along the axle), so tracks laid in reverse face down and are culled.
    call.state.cull = render::CullMode::Back;
    call.state.frontFace = render::FrontFace::CounterClockwise;
    device.draw(call);
}

} // namespace mm2::game::fx
