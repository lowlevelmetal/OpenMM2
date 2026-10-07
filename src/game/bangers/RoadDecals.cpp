#include "game/bangers/RoadDecals.h"

#include "core/StringUtil.h"

#include <cmath>

namespace mm2::game::bangers {

void RoadDecals::load(const city::PathSet& set) {
    m_decals.clear();
    for (const auto& path : set.paths) {
        if (path.points.size() <= 2)
            continue;
        Decal d;
        d.texture = str::lower(path.name);
        for (const auto& p : path.points)
            d.points.push_back(p.position + Vec3{0.0f, 0.01f, 0.0f});
        // dgRoadDecalInstance: v in widths of the first pair along the strip.
        const Vec3 p0 = path.points[0].position, p1 = path.points[1].position;
        const float width = p0.dist(p1);
        for (std::size_t k = 0; k < path.points.size() / 2; ++k)
            d.v.push_back(width > 0.0f ? std::floor(path.points[2 * k].position.dist(p0) / width + 0.5f) : 0.0f);
        m_decals.push_back(std::move(d));
    }
}

void RoadDecals::draw(render::Device& device, TextureLibrary& textures) const {
    for (const auto& d : m_decals) {
        std::vector<render::Vertex3D> vertices;
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
        for (std::size_t i = 0; i < d.points.size(); ++i)
            vert(d.points[i], static_cast<float>(i & 1), d.v[std::min(i / 2, d.v.size() - 1)]);
        // Triangle strip as a list.
        std::vector<std::uint16_t> indices;
        for (std::size_t i = 0; i + 2 < vertices.size(); ++i) {
            const auto a = static_cast<std::uint16_t>(i), b = static_cast<std::uint16_t>(i + 1),
                       c = static_cast<std::uint16_t>(i + 2);
            if (i & 1)
                indices.insert(indices.end(), {b, a, c});
            else
                indices.insert(indices.end(), {a, b, c});
        }
        if (indices.empty())
            continue;
        render::DrawCall call;
        call.vertices = device.uploadTransient(render::BufferKind::Vertex, std::span<const render::Vertex3D>(vertices));
        call.indices = device.uploadTransient(render::BufferKind::Index, std::span<const std::uint16_t>(indices));
        call.count = static_cast<std::uint32_t>(indices.size());
        call.constants.world = Mat44::identity();
        call.constants.flags = render::DrawFlag::VertexColor | render::DrawFlag::Fog | render::DrawFlag::AlphaTest;
        call.constants.alphaRef = 101.0f / 255.0f;
        if (const WorldTexture* tex = textures.get(d.texture)) {
            call.constants.flags |= render::DrawFlag::Texture0;
            call.textures[0] = {tex->handle, tex->sampler};
        }
        call.state.blend = render::BlendMode::Alpha;
        call.state.depthWrite = false;
        call.state.depthBias = true;
        call.depthBias = 2.0f;
        call.depthBiasSlope = 1.0f;
        call.state.cull = render::CullMode::None;
        device.draw(call);
    }
}

} // namespace mm2::game::bangers
