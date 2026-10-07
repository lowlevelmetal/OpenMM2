#include "game/AiRenderer.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "game/MeshDraw.h"

#include <format>

namespace mm2::game {
namespace {

constexpr float kCarDrawDistance = 300.0f;
constexpr float kPedDrawDistance = 90.0f;
constexpr float kSignalDrawDistance = 250.0f;

} // namespace

AiRenderer::AiRenderer(render::Device& device, TextureLibrary& textures, ModelLibrary& models, const vfs::Vfs& vfs)
    : m_device(device), m_textures(textures), m_models(models), m_vfs(vfs) {}

AiRenderer::CarModel* AiRenderer::carModel(const std::string& name) {
    if (auto it = m_cars.find(name); it != m_cars.end())
        return it->second.get();
    auto entry = std::make_unique<CarModel>();
    std::string error;
    auto read = [this](std::string_view path) { return m_vfs.readAll(path); };
    if (auto model = asset::loadVehicleModel(name, read, &error)) {
        entry->paintjobs = std::max<int>(1, static_cast<int>(model->pkg.paintjobs.size()));
        entry->model = std::make_unique<asset::VehicleModel>(std::move(*model));
    } else {
        log::warn("ai: traffic model {}: {}", name, error);
    }
    auto* raw = entry.get();
    m_cars[name] = std::move(entry);
    return raw;
}

const asset::PedType* AiRenderer::pedType(const std::string& name) {
    auto it = m_pedTypes.find(name);
    if (it == m_pedTypes.end()) {
        std::string error;
        auto read = [this](std::string_view path) { return m_vfs.readAll(path); };
        auto type = asset::loadPedType(name, read, &error);
        if (!type)
            log::warn("ai: pedestrian type {}: {}", name, error);
        it = m_pedTypes.emplace(name, std::move(type)).first;
    }
    return it->second ? &*it->second : nullptr;
}

void AiRenderer::drawPed(const ai::Pedestrian& ped, const asset::PedType& type, const Camera&) {
    const asset::PedAnimation* anim = type.animation(ped.animFile);
    if (!anim)
        anim = type.animation(ped.state);
    asset::posePed(type.skeleton, anim, ped.frame, m_bones);
    // pedAnimation::Load takes the root's straight-line x/z drift over the
    // clip out of every frame: the pedestrian moves by its sequence's speed,
    // so the pose stays in place.
    if (anim && anim->frameCount > 1) {
        const Vec3 drift = anim->rootTranslation(anim->frameCount - 1) - anim->rootTranslation(0);
        const float k = std::clamp(ped.frame, 0.0f, static_cast<float>(anim->frameCount - 1)) /
                        static_cast<float>(anim->frameCount - 1);
        const Vec3 shift{drift.x * k, 0.0f, drift.z * k};
        for (auto& b : m_bones)
            b.m3 -= shift;
    }
    const auto& mesh = type.mesh;
    m_skinned.resize(mesh.vertices.size());
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const auto& v = mesh.vertices[i];
        const Mat34 bone = v.bone < m_bones.size() ? m_bones[v.bone] * ped.transform : ped.transform;
        const Vec3 p = bone.transform(v.position);
        const Vec3 n = bone.transformDir(v.normal).normalized();
        auto& out = m_skinned[i];
        out = {};
        out.position[0] = p.x;
        out.position[1] = p.y;
        out.position[2] = p.z;
        out.normal[0] = n.x;
        out.normal[1] = n.y;
        out.normal[2] = n.z;
        out.color = render::packColor(v.color);
        out.uv0[0] = out.uv1[0] = v.uv.x;
        out.uv0[1] = out.uv1[1] = v.uv.y;
    }
    const render::BufferSlice vb = m_device.uploadTransient(render::BufferKind::Vertex, std::span<const render::Vertex3D>(m_skinned));
    const render::BufferSlice ib =
        m_device.uploadTransient(render::BufferKind::Index, std::span<const std::uint32_t>(mesh.indices));
    for (std::size_t m = 0; m < mesh.materials.size(); ++m) {
        const auto& mat = mesh.materials[m];
        if (!mat.indexCount)
            continue;
        Vec4 color{mat.diffuse.x, mat.diffuse.y, mat.diffuse.z, 1.0f};
        if (const auto* shader = type.shaders.get(static_cast<std::uint32_t>(ped.variant), static_cast<std::uint32_t>(m)))
            color = {shader->diffuse.x, shader->diffuse.y, shader->diffuse.z, 1.0f};
        render::DrawCall call;
        call.vertices = vb;
        call.indices = ib;
        call.indexType = render::IndexType::U32;
        call.count = mat.indexCount;
        call.first = mat.firstIndex;
        call.constants.color = color;
        call.constants.flags = render::DrawFlag::Lighting | render::DrawFlag::Fog;
        call.state.cull = render::CullMode::None;
        m_device.draw(call);
    }
}

void AiRenderer::drawSignal(const ai::Signal& signal, const Camera& camera, bool nightGlows) {
    const GpuModel* model = m_models.get(signal.model);
    if (!model)
        return;
    const float d = signal.transform.m3.dist(camera.position());
    const asset::Lod lod = d < 60 ? asset::Lod::High : d < 130 ? asset::Lod::Medium : asset::Lod::Low;
    const Mat44 world = Mat44::fromMat34(signal.transform);
    if (const GpuMesh* body = model->find("", lod))
        drawGpuMesh(m_device, m_textures, *body, model->materials(0), world);
    // aiTrafficLightInstance::DrawGlow: the light's glow together with the
    // pedestrian signal (WALK only in the walk phase), both or neither.
    const char* colour = signal.state == ai::LightState::Green  ? "GREEN"
                       : signal.state == ai::LightState::Amber  ? "YELLOW"
                                                                : "RED";
    const char* time = nightGlows ? "NIGHT" : "DAY";
    const GpuMesh* glow = model->find(std::format("{}GLOW{}", colour, time), asset::Lod::High);
    const GpuMesh* walk =
        model->find(std::format("{}_{}", signal.state == ai::LightState::Walk ? "WALK" : "NOWALK", time),
                    asset::Lod::High);
    if (glow && walk) {
        MeshDrawOptions opts;
        opts.lighting = false;
        opts.blend = render::BlendMode::Additive;
        opts.depthWrite = false;
        drawGpuMesh(m_device, m_textures, *glow, model->materials(0), world, opts);
        drawGpuMesh(m_device, m_textures, *walk, model->materials(0), world, opts);
    }
}

void AiRenderer::draw(const ai::World& world, const Camera& camera, const Frustum& frustum, TimeOfDay time,
                      const std::function<const Mat34*(int)>& physicalTransform) {
    m_stats = {};
    const Vec3 eye = camera.position();
    for (const auto& car : world.cars()) {
        const Mat34* physical = physicalTransform ? physicalTransform(car.id) : nullptr;
        const Mat34& transform = physical ? *physical : car.transform;
        if (transform.m3.dist2(eye) > sq(kCarDrawDistance) || !frustum.intersectsSphere(transform.m3, 4.0f))
            continue;
        CarModel* cm = carModel(car.model);
        if (!cm || !cm->model)
            continue;
        // aiVehicleInstance::SetColor: trunc(frand * (paint jobs - 1)), so the
        // last paint job is never chosen.
        const int paint =
            cm->paintjobs > 1 ? static_cast<int>(car.paint * static_cast<float>(cm->paintjobs - 1)) : 0;
        auto& r = cm->renderers[paint];
        if (!r)
            r = std::make_unique<VehicleRenderer>(m_device, m_textures, m_models, *cm->model, paint);
        VehiclePose pose;
        pose.body = transform;
        for (std::size_t i = 0; i < 6; ++i) {
            pose.wheelSpin[i] = -car.tireRotation; // rolling forward (-Z) spins about -X
            pose.wheelSteer[i] = i < 2 ? car.steer : 0.0f;
        }
        pose.brakeLights = car.braking;
        pose.headlights = time == TimeOfDay::Night;
        r->draw(pose, camera.transform);
        ++m_stats.cars;
    }
    for (const auto& ped : world.peds()) {
        if (ped.transform.m3.dist2(eye) > sq(kPedDrawDistance) || !frustum.intersectsSphere(ped.transform.m3, 2.0f))
            continue;
        if (const asset::PedType* type = pedType(ped.typeName)) {
            drawPed(ped, *type, camera);
            ++m_stats.peds;
        }
    }
    for (const auto& signal : world.signals()) {
        if (signal.transform.m3.dist2(eye) > sq(kSignalDrawDistance) ||
            !frustum.intersectsSphere(signal.transform.m3, 6.0f))
            continue;
        drawSignal(signal, camera, time >= TimeOfDay::Evening);
        ++m_stats.signals;
    }
}

} // namespace mm2::game
