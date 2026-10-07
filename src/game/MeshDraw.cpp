#include "game/MeshDraw.h"

namespace mm2::game {

int drawGpuMesh(render::Device& device, TextureLibrary& textures, const GpuMesh& mesh,
                const std::vector<asset::PkgMaterial>& materials, const Mat44& world, const MeshDrawOptions& options) {
    int calls = 0;
    for (const auto& d : mesh.draws) {
        const asset::PkgMaterial* mat = d.shader < materials.size() ? &materials[d.shader] : nullptr;
        const WorldTexture* tex = mat && !mat->texture.empty() ? textures.get(mat->texture) : nullptr;
        render::DrawCall call;
        call.vertices = {mesh.vertices, 0};
        call.indices = {mesh.indices, 0};
        call.indexType = render::IndexType::U16;
        call.count = d.indexCount;
        call.first = d.firstIndex;
        call.baseVertex = d.baseVertex;
        call.constants.world = world;
        const Vec4 diffuse = mat ? mat->diffuse : Vec4{1, 1, 1, 1};
        call.constants.color = {diffuse.x * options.tint.x, diffuse.y * options.tint.y, diffuse.z * options.tint.z,
                                diffuse.w * options.tint.w};
        std::uint32_t flags = render::DrawFlag::VertexColor;
        if (options.fog)
            flags |= render::DrawFlag::Fog;
        if (options.lighting)
            flags |= render::DrawFlag::Lighting;
        if (tex) {
            flags |= render::DrawFlag::Texture0;
            call.textures[0] = {tex->handle, tex->sampler};
        }
        const bool translucent = (tex && tex->translucent) || call.constants.color.w < 0.999f;
        if (translucent) {
            // Cut out fully transparent texels so they never write depth.
            flags |= render::DrawFlag::AlphaTest;
            call.constants.alphaRef = 0.02f;
            call.state.blend = render::BlendMode::Alpha;
        }
        if (options.blend)
            call.state.blend = *options.blend;
        call.state.depthWrite = options.depthWrite;
        call.constants.flags = flags;
        call.state.cull = options.cull ? render::CullMode::Back : render::CullMode::None;
        call.state.frontFace = render::FrontFace::CounterClockwise;
        device.draw(call);
        ++calls;
    }
    return calls;
}

} // namespace mm2::game
