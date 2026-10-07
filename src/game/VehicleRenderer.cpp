#include "game/VehicleRenderer.h"

#include "core/StringUtil.h"

#include <cmath>
#include <format>

namespace mm2::game {
namespace {

// ltLight defaults as cars use them (vehCarModel::Init, vehSiren::Init):
// intensity 25, spot exponent 3; glows are drawn at 0.2 x sqrt(I d^2)
// metres with 0.6 x the light colour (globals every vehSiren sets).
constexpr float kLightIntensity = 25.0f;
constexpr float kSpotExponent = 3.0f;
constexpr float kGlowSize = 0.2f;
constexpr float kGlowColor = 0.6f;
// vehSiren::Update: the beams turn 2.5 pi rad/s; vehCarModel::DrawHeadlights
// sweeps the headlights at +-42.411503 rad/s while the siren is on.
constexpr float kHeadlightSweep = 42.411503f;

std::uint32_t argb(const Vec3& c) {
    auto b = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return 0xFF000000u | (b(c.x) << 16) | (b(c.y) << 8) | b(c.z);
}

// Rotates `m` so that its up row points along `normal` (Matrix34::RotateTo).
Mat34 rotateUpTo(const Mat34& m, const Vec3& from, const Vec3& normal) {
    const Vec3 axis = from.cross(normal);
    const float s = axis.mag();
    const float c = std::clamp(from.dot(normal), -1.0f, 1.0f);
    if (s < 1e-6f)
        return m;
    const Mat34 r = Mat34::rotationAxis(axis * (1.0f / s), std::atan2(s, c));
    Mat34 out = m;
    out.m0 = r.transformDir(m.m0);
    out.m1 = r.transformDir(m.m1);
    out.m2 = r.transformDir(m.m2);
    return out;
}

} // namespace

VehicleRenderer::VehicleRenderer(render::Device& device, TextureLibrary& textures, ModelLibrary& models,
                                 const asset::VehicleModel& model, int paintjob, std::string bodyPart,
                                 std::string wheelPrefix)
    : m_device(device), m_textures(textures), m_model(model), m_paintjob(paintjob), m_bodyPart(std::move(bodyPart)),
      m_wheelPrefix(std::move(wheelPrefix)) {
    m_gpu = models.add(model.baseName, model.pkg);
    setPaintjob(paintjob);
    if (!m_gpu)
        return;
    if (const GpuMesh* body = m_gpu->find(m_bodyPart, asset::Lod::High)) {
        const Vec3 half = (body->bounds.max - body->bounds.min) * 0.5f;
        m_radius = half.mag();
    }
    // A part's light colour is its first material's diffuse colour
    // (vehCarModel::GetSurfaceColor).
    auto surface = [&](std::string_view part) -> std::optional<Vec3> {
        const GpuMesh* mesh = m_gpu->find(part, asset::Lod::High);
        if (!mesh || mesh->draws.empty() || !str::iequals(mesh->part, part))
            return std::nullopt;
        const auto& mats = m_gpu->materials(m_paintjob);
        const std::uint32_t s = mesh->draws.front().shader;
        const Vec4 d = s < mats.size() ? mats[s].diffuse : Vec4{1, 1, 1, 1};
        return Vec3{d.x, d.y, d.z};
    };
    // Headlight beams: two ltLights at the headlight0/1 pivots, coloured by
    // the headlight0/1 parts, created only when the car has a headlight0 part.
    if (surface("HEADLIGHT0")) {
        for (int i = 0; i < 2; ++i) {
            const auto name = std::format("headlight{}", i);
            const auto* pivot = model.pivot(name);
            const auto color = surface(str::upper(name));
            if (pivot)
                m_headlights[static_cast<std::size_t>(i)] = Light{pivot->origin, color.value_or(Vec3{1, 1, 1})};
        }
    }
    // Siren beams: one ltLight per SRN0-3 part (vehCarModel::InitSirenLight).
    for (int i = 0; i < 4; ++i) {
        const auto name = std::format("srn{}", i);
        if (const auto color = surface(str::upper(name)))
            if (const auto* pivot = model.pivot(name))
                m_sirens.push_back({pivot->origin, *color});
    }
    // Fenders follow the front wheels at their pivot's offset from wheel 0
    // (lifted 2.5 cm; FNDR1 mirrors it).
    if (m_gpu->find("FNDR0", asset::Lod::High) && model.pivot("fndr0") && model.wheel(0))
        m_fenderOffset = model.pivot("fndr0")->origin - model.wheel(0)->position + Vec3{0, 0.025f, 0};
}

void VehicleRenderer::setPaintjob(int paintjob) {
    m_paintjob = paintjob;
    m_paint = m_gpu ? m_gpu->materials(paintjob) : std::vector<asset::PkgMaterial>{};
    // fxTexelDamage: at the high and medium LODs each body material draws a
    // copy of its clean texture ("<name>", or "<name>" for a "<name>_dmg"
    // material) into which impacts copy patches of "<name>_dmg". The low
    // LODs draw the paint job's materials as stored.
    m_live = m_paint;
    m_texelDamage.reset();
    for (const auto& mesh : m_model.pkg.meshes)
        if (str::iequals(mesh.part, m_bodyPart) && mesh.lod == asset::Lod::High) {
            static int serial = 0;
            m_texelDamage = std::make_unique<TexelDamage>(m_device, m_textures, mesh, m_live,
                                                          std::format("{}{}", m_model.baseName, ++serial));
            break;
        }
    // Materials without a damage pair: "_dmg" ones draw their clean texture.
    for (auto& m : m_live)
        if (str::iendsWith(m.texture, "_dmg") && m_textures.get(m.texture.substr(0, m.texture.size() - 4)))
            m.texture.resize(m.texture.size() - 4);
}

void VehicleRenderer::applyDamage(const Vec3& modelPoint, float radius) {
    if (m_texelDamage)
        m_texelDamage->apply(modelPoint, radius);
}

void VehicleRenderer::resetDamage() {
    if (m_texelDamage)
        m_texelDamage->reset();
    reattachAll();
}

std::optional<VehicleRenderer::Breakable> VehicleRenderer::nearestBreakable(const Vec3& modelPoint) const {
    std::optional<Breakable> best;
    float bestD2 = 100000.0f;
    auto consider = [&](const std::string& name) {
        const auto* pivot = m_model.pivot(name);
        const std::string part = str::upper(name);
        if (!pivot || !m_gpu || !m_gpu->find(part, asset::Lod::High) || m_detached.contains(part))
            return;
        const float d2 = pivot->origin.dist2(modelPoint);
        if (d2 < bestD2) {
            bestD2 = d2;
            best = Breakable{part, pivot->origin};
        }
    };
    for (const char* name : {"break0", "break1", "break2", "break3", "break01", "break12", "break23", "break03"})
        consider(name);
    consider(std::format("variant{}", m_paintjob));
    return best;
}

std::vector<VehicleRenderer::Breakable> VehicleRenderer::wreckParts(float mph, fx::Rand& rng) {
    std::vector<Breakable> out;
    if (m_wreckEjected)
        return out;
    m_wreckEjected = true;
    std::set<std::string> taken; // a pair may come up twice
    auto take = [&](const std::string& part) {
        const auto* pivot = m_model.pivot(str::lower(part));
        if (!pivot || m_detached.contains(part) || !taken.insert(part).second)
            return;
        out.push_back({part, pivot->origin});
    };
    auto pair = [&] {
        const int k = rng.irand() & 3;
        take(std::format("WHL{}", k));
        take(std::format("HUB{}", k));
    };
    if (mph > 100.0f) {
        for (int k = 0; k < 4; ++k) {
            take(std::format("WHL{}", k));
            take(std::format("HUB{}", k));
        }
        take("FNDR0");
        take("FNDR1");
        take("ENGINE");
    } else if (mph > 75.0f) {
        pair();
        pair();
        take(std::format("FNDR{}", rng.irand() & 1));
    } else if (mph > 50.0f) {
        pair();
    }
    return out;
}

void VehicleRenderer::reattachAll() {
    m_detached.clear();
    m_wreckEjected = false;
}

std::vector<std::string> VehicleRenderer::materialTextures() const {
    std::vector<std::string> out;
    for (const auto& m : m_paint)
        out.push_back(m.texture);
    return out;
}

std::optional<asset::Lod> VehicleRenderer::lodFor(const VehiclePose& pose, const Mat34& camera) const {
    // lvlInstance::IsVisible: the view depth of the car minus its radius
    // against the Object Detail thresholds; dynamic objects end at NoDraw.
    return objectLod(viewDepth(camera, pose.body.m3), m_radius, m_detail, m_detail.noDraw);
}

void VehicleRenderer::drawPart(std::string_view part, asset::Lod lod, const Mat34& transform,
                               const MeshDrawOptions& options, bool live) {
    if (!m_detached.empty() && m_detached.contains(std::string(part)))
        return;
    const GpuMesh* mesh = m_gpu ? m_gpu->find(part, lod) : nullptr;
    if (!mesh || !str::iequals(mesh->part, part))
        return;
    drawGpuMesh(m_device, m_textures, *mesh, live ? m_live : m_paint, Mat44::fromMat34(transform), options);
}

void VehicleRenderer::draw(const VehiclePose& pose, const Mat34& camera) {
    if (!m_gpu)
        return;
    const auto visible = lodFor(pose, camera);
    if (!visible)
        return;
    const asset::Lod lod = *visible;
    // vehCarModel::Draw.
    const bool live = lod == asset::Lod::High || lod == asset::Lod::Medium;
    drawPart(m_bodyPart, lod, pose.body, {}, live);
    if (lod != asset::Lod::VeryLow) {
        MeshDrawOptions decal;
        decal.blend = render::BlendMode::Alpha;
        drawPart("DECAL", lod, pose.body, decal, live);
        // Breakable parts and the paint job's variant part, at their pivots.
        for (const char* name : {"break0", "break1", "break2", "break3", "break01", "break12", "break23", "break03"})
            if (const auto* pivot = m_model.pivot(name))
                drawPart(str::upper(name), lod, Mat34::translation(pivot->origin) * pose.body, {}, live);
        const auto variant = std::format("variant{}", m_paintjob);
        if (const auto* pivot = m_model.pivot(variant))
            drawPart(str::upper(variant), lod, Mat34::translation(pivot->origin) * pose.body, {}, live);

        if (lod == asset::Lod::High) {
            // Reflections (modStatic::DrawEnvMapped): the body again with
            // refl_dc mapped from the view-space normals, added at the
            // material's power as intensity.
            if (m_reflections)
                if (const WorldTexture* env = m_textures.get("refl_dc"))
                    if (const GpuMesh* mesh = m_gpu->find(m_bodyPart, asset::Lod::High))
                        for (const auto& d : mesh->draws) {
                            const float power = d.shader < m_live.size() ? m_live[d.shader].shininess : 0.0f;
                            if (power <= 0.0f)
                                continue;
                            render::DrawCall call;
                            call.vertices = {mesh->vertices, 0};
                            call.indices = {mesh->indices, 0};
                            call.count = d.indexCount;
                            call.first = d.firstIndex;
                            call.baseVertex = d.baseVertex;
                            call.constants.world = Mat44::fromMat34(pose.body);
                            call.constants.color = {power, power, power, 1.0f};
                            call.constants.flags = render::DrawFlag::Texture1 | render::DrawFlag::EnvMap1;
                            call.textures[1] = {env->handle, env->sampler};
                            call.state.blend = render::BlendMode::Add;
                            call.state.depthWrite = false;
                            call.state.depthCompare = render::CompareOp::LessEqual;
                            call.state.cull = render::CullMode::Back;
                            m_device.draw(call);
                        }
            // Fenders: turned with the front wheels, upright with the body.
            if (m_fenderOffset && pose.hasWheelWorld) {
                for (int i = 0; i < 2; ++i) {
                    if (!pose.wheelValid[static_cast<std::size_t>(i)])
                        continue;
                    const Mat34& wheel = pose.wheelWorld[static_cast<std::size_t>(i)];
                    Mat34 m;
                    m.m0 = wheel.m0;
                    m.m1 = pose.body.m1;
                    m.m2 = m.m0.cross(m.m1);
                    Vec3 offset = *m_fenderOffset;
                    if (i == 1)
                        offset.x = -offset.x;
                    m.m3 = m.transformDir(offset) + wheel.m3;
                    drawPart(std::format("FNDR{}", i), asset::Lod::High, m, {}, live);
                }
            }
        }
        // Wheels and hubs at the simulation's wheel matrices.
        if (pose.hasWheelWorld) {
            for (std::size_t i = 0; i < 6; ++i) {
                if (!pose.wheelValid[i])
                    continue;
                drawPart(std::format("{}{}", m_wheelPrefix, i), lod, pose.wheelWorld[i], {}, live);
                if (i < 4)
                    drawPart(std::format("HUB{}", i), lod, pose.wheelWorld[i], {}, live);
            }
        } else {
            for (const auto& w : m_model.wheels) {
                const auto i = static_cast<std::size_t>(std::clamp(w.index, 0, 5));
                const Vec3 pivot = w.position + Vec3{0, pose.wheelDrop[i], 0};
                const Mat34 local = Mat34::rotationX(pose.wheelSpin[i]) * Mat34::rotationY(pose.wheelSteer[i]) *
                                    Mat34::translation(pivot);
                drawPart(std::format("{}{}", m_wheelPrefix, w.index), lod, local * pose.body, {}, live);
                if (w.index < 4)
                    drawPart(std::format("HUB{}", w.index), lod, local * pose.body, {}, live);
            }
        }
    }
    drawShadow(pose);
    drawGlows(pose, camera);
}

std::optional<Mat34> VehicleRenderer::shadowMatrix(const Mat34& body) const {
    // lvlInstance::DrawPhysics: the ground 1 m above to 1 m below the car,
    // else to 5 m below; no shadow on slopes steeper than normal.y 0.7. The
    // car's frame is turned onto the ground (upside down: its flipped up axis).
    if (!m_probe)
        return std::nullopt;
    Vec3 point, normal;
    const Vec3 p = body.m3;
    if (!m_probe(p + Vec3{0, 1, 0}, p - Vec3{0, 1, 0}, point, normal) &&
        !m_probe(p + Vec3{0, 1, 0}, p - Vec3{0, 5, 0}, point, normal))
        return std::nullopt;
    if (normal.y < 0.7f)
        return std::nullopt;
    Mat34 m;
    if (body.m1.y <= 0.0f) {
        m = rotateUpTo(body, -body.m1, normal);
        m.m1 = -m.m1;
    } else {
        m = rotateUpTo(body, body.m1, normal);
    }
    m.m3 = point;
    return m;
}

void VehicleRenderer::drawShadow(const VehiclePose& pose) {
    // vehCarModel::DrawShadow: the high LOD shadow mesh on the ground,
    // alpha blended, depth tested without writes and pulled forward (MM2
    // narrows the depth range to 0.001-0.999).
    // Without a ground probe (traffic) the shadow sits at the body.
    const auto m = m_probe ? shadowMatrix(pose.body) : std::optional<Mat34>(pose.body);
    if (!m)
        return;
    MeshDrawOptions shadow;
    shadow.lighting = false;
    shadow.depthWrite = false;
    shadow.depthBias = true;
    shadow.blend = render::BlendMode::Alpha;
    drawPart("SHADOW", asset::Lod::High, *m, shadow, false);
}

void VehicleRenderer::addLightGlow(fx::ParticleRenderer& cards, const Vec3& position, const Vec3& direction,
                                   const Vec3& color, const Mat34& camera) {
    // ltLight::ComputeIntensity: I = 25 cos^3(theta) / d^2 towards the eye,
    // 0 behind the light; ltLight::DrawGlow: a camera-facing card of half
    // size 0.2 sqrt(I d^2) (cos^1.5 theta metres, whatever the distance).
    const Vec3 toEye = camera.m3 - position;
    const float d2 = toEye.mag2();
    if (d2 <= 0.0f)
        return;
    const float c = toEye.dot(direction) / std::sqrt(d2);
    if (c < 0.0f)
        return;
    const float intensity = kLightIntensity / d2 * std::pow(c, kSpotExponent);
    fx::SparkPos card;
    card.position = position;
    card.radius = std::sqrt(d2 * intensity) * kGlowSize;
    card.color = argb(color * kGlowColor);
    cards.add(card, 1, 1, camera);
}

void VehicleRenderer::drawGlows(const VehiclePose& pose, const Mat34& camera) {
    // vehCarModel::DrawGlow: lighting off, no depth writes, added (ONE/ONE),
    // always the high LOD light parts.
    MeshDrawOptions glow;
    glow.lighting = false;
    glow.fog = false;
    glow.blend = render::BlendMode::Add;
    glow.depthWrite = false;
    if (pose.brakeLights) {
        drawPart("TLIGHT", asset::Lod::High, pose.body, glow, false);
        drawPart("BLIGHT", asset::Lod::High, pose.body, glow, false);
    }
    if (pose.headlights)
        drawPart("TLIGHT", asset::Lod::High, pose.body, glow, false);
    if (pose.reverseLights)
        drawPart("RLIGHT", asset::Lod::High, pose.body, glow, false);

    m_cards.begin();
    // Headlight beams (vehCarModel::DrawHeadlights) at night or in fog, or
    // sweeping in opposite directions while the siren is on.
    if ((pose.headlights || (pose.siren && !m_sirens.empty())) && (m_headlights[0] || m_headlights[1])) {
        for (std::size_t i = 0; i < 2; ++i) {
            if (!m_headlights[i])
                continue;
            Vec3 direction = -pose.body.m2;
            if (pose.siren && !m_sirens.empty()) {
                const float sweep = pose.sirenAngle / (2.5f * 3.1415927f) * kHeadlightSweep;
                direction = Mat34::rotationY(i == 0 ? sweep : -sweep).transformDir(direction);
            }
            addLightGlow(m_cards, pose.body.transform(m_headlights[i]->position), direction, m_headlights[i]->color,
                         camera);
        }
    }
    // Siren beams (vehSiren::Draw): world-space directions turning about Y,
    // a quarter turn apart.
    if (pose.siren)
        for (std::size_t i = 0; i < m_sirens.size(); ++i) {
            const float a = static_cast<float>(i) * 1.5707964f + pose.sirenAngle;
            const Vec3 direction = Mat34::rotationY(a).transformDir({1, 0, 0});
            addLightGlow(m_cards, pose.body.transform(m_sirens[i].position), direction, m_sirens[i].color, camera);
        }
    m_cards.flush(m_device, m_textures.get("lt_glow"), {render::BlendMode::Add, false, {1, 1, 1, 1}});
}

} // namespace mm2::game
