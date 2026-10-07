#include "game/VehicleRenderer.h"

#include "core/StringUtil.h"

#include <format>

namespace mm2::game {

VehicleRenderer::VehicleRenderer(render::Device& device, TextureLibrary& textures, ModelLibrary& models,
                                 const asset::VehicleModel& model, int paintjob, std::string bodyPart,
                                 std::string wheelPrefix)
    : m_device(device), m_textures(textures), m_model(model), m_paintjob(paintjob), m_bodyPart(std::move(bodyPart)),
      m_wheelPrefix(std::move(wheelPrefix)) {
    m_gpu = models.add(model.baseName, model.pkg);
    setPaintjob(paintjob);
}

void VehicleRenderer::setPaintjob(int paintjob) {
    m_paintjob = paintjob;
    m_materials = m_gpu ? m_gpu->materials(paintjob) : std::vector<asset::PkgMaterial>{};
    // Car bodies are split down the middle: the left half uses the clean
    // textures (vpbugyellow_sd), the right half the "_dmg" ones. Both halves
    // look clean on an undamaged car, so the "_dmg" materials are drawn with
    // the clean texture; the "_dmg" texture is the source for painting damage
    // onto a half (texel damage, TextelDamageRadius in .vehCarDamage).
    // Inferred from the geometry: no faces are shared between the halves.
    for (auto& m : m_materials)
        if (str::iendsWith(m.texture, "_dmg"))
            m.texture.resize(m.texture.size() - 4);
}

void VehicleRenderer::drawPart(std::string_view part, asset::Lod lod, const Mat34& transform,
                               const MeshDrawOptions& options) {
    const GpuMesh* mesh = m_gpu ? m_gpu->find(part, lod) : nullptr;
    if (!mesh)
        return;
    drawGpuMesh(m_device, m_textures, *mesh, m_materials, Mat44::fromMat34(transform), options);
}

void VehicleRenderer::draw(const VehiclePose& pose, const Vec3& eye) {
    if (!m_gpu)
        return;
    // LOD distances inferred (car meshes have H/M/L/VL variants).
    const float d = pose.body.m3.dist(eye);
    const asset::Lod lod = d < 25.0f ? asset::Lod::High : d < 60.0f ? asset::Lod::Medium
                         : d < 140.0f ? asset::Lod::Low : asset::Lod::VeryLow;

    // Shadow first, without depth writes so it never hides the road decals.
    MeshDrawOptions shadow;
    shadow.lighting = false;
    shadow.depthWrite = false;
    drawPart("SHADOW", lod, pose.body, shadow);

    drawPart(m_bodyPart, lod, pose.body, {});
    drawPart(m_bodyPart + "_HITCH", lod, pose.body, {});
    if (pose.hasWheelWorld) {
        for (std::size_t i = 0; i < 6; ++i)
            if (pose.wheelValid[i])
                drawPart(std::format("{}{}", m_wheelPrefix, i), lod, pose.wheelWorld[i], {});
    }
    for (const auto& w : m_model.wheels) {
        if (pose.hasWheelWorld)
            break;
        const auto i = static_cast<std::size_t>(std::clamp(w.index, 0, 5));
        const Vec3 pivot = w.position + Vec3{0, pose.wheelDrop[i], 0};
        const Mat34 local = Mat34::rotationX(pose.wheelSpin[i]) * Mat34::rotationY(pose.wheelSteer[i]) *
                            Mat34::translation(pivot);
        drawPart(std::format("{}{}", m_wheelPrefix, w.index), lod, local * pose.body, {});
    }

    MeshDrawOptions glow;
    glow.lighting = false;
    glow.blend = render::BlendMode::Additive;
    glow.depthWrite = false;
    if (pose.headlights) {
        drawPart("HLIGHT", lod, pose.body, glow);
        drawPart("TLIGHT", lod, pose.body, glow);
    }
    if (pose.brakeLights)
        drawPart("BLIGHT", lod, pose.body, glow);
    if (pose.reverseLights)
        drawPart("RLIGHT", lod, pose.body, glow);
    if (pose.sirenPhase >= 0)
        drawPart(pose.sirenPhase == 0 ? "SIREN0" : "SIREN1", lod, pose.body, glow);
}

} // namespace mm2::game
