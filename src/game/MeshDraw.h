#pragma once

#include "asset/Pkg.h"
#include "game/ModelLibrary.h"
#include "game/TextureLibrary.h"
#include "render/Device.h"

#include <vector>

namespace mm2::game {

struct MeshDrawOptions {
    bool fog = true;
    bool lighting = true;
    // Overrides the blend of every draw (e.g. shadows: Modulate; glows: Additive).
    std::optional<render::BlendMode> blend;
    bool depthWrite = true;
    Vec4 tint{1, 1, 1, 1}; // multiplied into the material colour
    // Back-face culling with counter-clockwise front faces, as the original
    // drew (verified: car bodies are correct only this way, and models ship
    // explicit reversed copies of double-sided faces such as banners).
    bool cull = true;
};

// Draws a PKG mesh with a paint job's materials. Returns the number of draw calls.
int drawGpuMesh(render::Device& device, TextureLibrary& textures, const GpuMesh& mesh,
                const std::vector<asset::PkgMaterial>& materials, const Mat44& world, const MeshDrawOptions& options = {});

} // namespace mm2::game
