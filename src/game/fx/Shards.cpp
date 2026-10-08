#include "game/fx/Shards.h"

#include <cmath>

namespace mm2::game::fx {

void Shards::reset() {
    for (auto& s : m_shards)
        s.age = 3.4e38f;
    m_next = 0;
}

int Shards::live() const {
    int n = 0;
    for (const auto& s : m_shards)
        n += s.age < kLifetime;
    return n;
}

void Shards::emit(const Vec3& position, float impact, float speed, const Mat34& body) {
    if (!(500.0f < impact) || !(5.0f < speed))
        return;
    int n = static_cast<int>(impact / 300.0f);
    if (n >= 2)
        n = 2;
    for (; n > 0; --n)
        emitOne(position, speed, body);
}

// fxShardManager::EmitShard + fxShard::AddShard.
void Shards::emitOne(const Vec3& position, float speed, const Mat34& body) {
    const float side = (m_rand.frand() * 2.0f - 1.0f) * 0.3f * speed;
    const float up = (m_rand.frand() + 1.0f) * 0.5f * 0.3f * speed;
    const float back = (m_rand.frand() * 0.9f + 0.1f) * 0.2f * speed;
    const float ax = m_rand.frand() - 0.5f;
    const float ay = m_rand.frand();
    const float az = m_rand.frand() - 0.5f;
    const float len2 = ax * ax + ay * ay + az * az;
    const float inv = len2 == 0.0f ? 0.0f : 1.0f / std::sqrt(len2);
    const float spin = (10.8f * 5.0f - 10.8f) * m_rand.frand() + 10.8f;
    Shard& s = m_shards[static_cast<std::size_t>(m_next)];
    m_next = (m_next + 1) & (kCount - 1);
    s.age = 0.0f;
    s.frame.m3 = position;
    s.velocity = body.m0 * side + body.m1 * up + body.m2 * back;
    s.axis = {inv * az, inv * ay, inv * ax};
    s.spin = spin;
    s.u = m_rand.frand();
    s.v = m_rand.frand();
}

void Shards::update(float dt) {
    for (auto& s : m_shards) {
        if (!(s.age < kLifetime))
            continue;
        s.velocity.y -= dt * 20.0f;
        if (s.axis.mag2() > 0.0f) {
            const Mat34 r = Mat34::rotationAxis(s.axis, dt * s.spin);
            s.frame.m0 = r.transformDir(s.frame.m0);
            s.frame.m1 = r.transformDir(s.frame.m1);
            s.frame.m2 = r.transformDir(s.frame.m2);
        }
        s.frame.m3 += s.velocity * dt;
        s.age += dt;
    }
}

std::size_t Shards::materialFor(std::size_t shard, std::size_t materials) {
    // fxShardManager::Draw steps through the shaders but starts again at 0
    // after count / materials of them (integer division; never when that is
    // 0, i.e. with more than 16 materials).
    if (materials == 0)
        return 0;
    const std::size_t cycle = static_cast<std::size_t>(kCount) / materials;
    return cycle == 0 ? shard : shard % cycle;
}

void Shards::draw(render::Device& device, TextureLibrary& textures, const std::vector<std::string>& materials) const {
    // fxShard::Draw -> draw_textured_tri: (0, 0, 0.1) at (u, v + 0.3), (0, 0, 0) at (u, v),
    // (0.1, 0, 0) at (u + 0.3, v); white, both sides.
    for (std::size_t i = 0; i < m_shards.size(); ++i) {
        const Shard& s = m_shards[i];
        if (!(s.age < kLifetime))
            continue;
        // With fewer than 4 materials the cycle runs past the paint job (into
        // the next one's shaders in MM2); such shards draw untextured here.
        const std::size_t m = materialFor(i, materials.size());
        const WorldTexture* tex = m < materials.size() ? textures.get(materials[m]) : nullptr;
        const Vec3 corners[3] = {{0, 0, 0.1f}, {0, 0, 0}, {0.1f, 0, 0}};
        const float uv[3][2] = {{s.u, s.v + 0.3f}, {s.u, s.v}, {s.u + 0.3f, s.v}};
        render::Vertex3D vertices[3]{};
        for (int k = 0; k < 3; ++k) {
            const Vec3 p = s.frame.transform(corners[k]);
            vertices[k].position[0] = p.x;
            vertices[k].position[1] = p.y;
            vertices[k].position[2] = p.z;
            vertices[k].normal[1] = 1.0f;
            vertices[k].color = 0xFFFFFFFFu;
            vertices[k].uv0[0] = vertices[k].uv1[0] = uv[k][0];
            vertices[k].uv0[1] = vertices[k].uv1[1] = uv[k][1];
        }
        render::DrawCall call;
        call.vertices = device.uploadTransient(render::BufferKind::Vertex, std::span<const render::Vertex3D>(vertices));
        call.count = 3;
        call.constants.world = Mat44::identity();
        // Drawn from lvlLevel's late callbacks (cityLevel::DrawRooms): unlit,
        // no fog, alpha blended with the default alpha test (alpha not 0),
        // no depth writes; fxShardManager::Draw turns culling off. The
        // texture keeps its own address modes (car paint clamps).
        call.constants.flags = render::DrawFlag::VertexColor | render::DrawFlag::AlphaTest;
        call.constants.alphaRef = 1.0f / 255.0f;
        if (tex) {
            call.constants.flags |= render::DrawFlag::Texture0;
            call.textures[0] = {tex->handle, tex->sampler};
        }
        call.state.blend = render::BlendMode::Alpha;
        call.state.depthWrite = false;
        call.state.cull = render::CullMode::None;
        device.draw(call);
    }
}

} // namespace mm2::game::fx
