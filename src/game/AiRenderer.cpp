#include "game/AiRenderer.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "game/CityRenderer.h"
#include "game/MeshDraw.h"

#include <format>

namespace mm2::game {

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

void AiRenderer::drawPed(const ai::Pedestrian& ped, const asset::PedType& type, const Camera& camera) {
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
    // aiPedestrianInstance::Draw: the posed model within 35 m of the camera,
    // the stick figure (pedAnimation::DrawSkeleton) beyond.
    if (ped.transform.m3.dist2(camera.position()) >= 1225.0f) {
        drawSkeleton(ped, type, camera);
        return;
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
        // modModel::Draw under the default culling (as for every model).
        call.state.cull = render::CullMode::Back;
        call.state.frontFace = render::FrontFace::CounterClockwise;
        m_device.draw(call);
    }
}

void AiRenderer::drawSkeleton(const ai::Pedestrian& ped, const asset::PedType& type, const Camera& camera) {
    // pedAnimation::DrawSkeleton: for each bone with a width in the .rays
    // file, its position raised by the bone's offset (which its children then
    // use), a quad to its parent's position across the camera's right axis
    // (the start and end half widths), in the colour the variant's row
    // picks from its shaders' diffuse colours; untextured, unlit, both sides.
    if (!type.rays)
        return;
    const auto& rays = *type.rays;
    const std::size_t n = std::min(rays.bones.size(), m_bones.size());
    const std::vector<int>* row =
        ped.variant >= 0 && static_cast<std::size_t>(ped.variant) < rays.variants.size()
            ? &rays.variants[static_cast<std::size_t>(ped.variant)]
            : nullptr;
    const Vec3 right = camera.transform.m0;
    std::vector<render::Vertex3D> vertices;
    std::vector<std::uint16_t> indices;
    for (std::size_t j = 0; j < n; ++j) {
        const auto& bone = rays.bones[j];
        const float w0 = bone.values.x, w1 = bone.values.y;
        if (w0 == 0.0f)
            continue;
        m_bones[j].m3.y = bone.values.z + m_bones[j].m3.y;
        const std::size_t parent = static_cast<std::size_t>(std::clamp(bone.a, 0, static_cast<int>(n) - 1));
        // The variant's shader colour: 0xFF, then trunc(diffuse x 255).
        std::uint32_t argb = 0xFFFFFFFFu;
        const auto variant = static_cast<std::uint32_t>(ped.variant);
        if (row && j < row->size())
            if (const auto* shader = type.shaders.get(variant, static_cast<std::uint32_t>((*row)[j]))) {
                auto byte = [](float v) {
                    return static_cast<std::uint32_t>(static_cast<int>(v * 255.0f)) & 0xFFu;
                };
                argb = 0xFF000000u | byte(shader->diffuse.x) << 16 | byte(shader->diffuse.y) << 8 |
                       byte(shader->diffuse.z);
            }
        const Vec3 a = ped.transform.transform(m_bones[j].m3);
        const Vec3 b = ped.transform.transform(m_bones[parent].m3);
        const Vec3 da = right * w0, db = right * w1;
        const auto base = static_cast<std::uint16_t>(vertices.size());
        for (const Vec3& p : {a - da, a + da, b - db, b + db}) {
            render::Vertex3D v{};
            v.position[0] = p.x;
            v.position[1] = p.y;
            v.position[2] = p.z;
            v.normal[1] = 1.0f;
            v.color = (argb & 0xFF00FF00u) | ((argb >> 16) & 0xFFu) | ((argb & 0xFFu) << 16);
            vertices.push_back(v);
        }
        for (std::uint16_t i : {std::uint16_t(0), std::uint16_t(1), std::uint16_t(2), std::uint16_t(2),
                                std::uint16_t(1), std::uint16_t(3)})
            indices.push_back(static_cast<std::uint16_t>(base + i));
    }
    if (indices.empty())
        return;
    render::DrawCall call;
    call.vertices =
        m_device.uploadTransient(render::BufferKind::Vertex, std::span<const render::Vertex3D>(vertices));
    call.indices =
        m_device.uploadTransient(render::BufferKind::Index, std::span<const std::uint16_t>(indices));
    call.count = static_cast<std::uint32_t>(indices.size());
    call.constants.world = Mat44::identity();
    call.constants.flags = render::DrawFlag::VertexColor | render::DrawFlag::Fog;
    call.state.cull = render::CullMode::None;
    m_device.draw(call);
}

void AiRenderer::drawSignal(const ai::Signal& signal, const Camera& camera, bool nightGlows) {
    const GpuModel* model = m_models.get(signal.model);
    if (!model)
        return;
    // lvlInstance::IsVisible with the Object Detail thresholds, and
    // aiTrafficLightInstance::Draw's first shader set.
    const auto lod =
        objectLod(viewDepth(camera.transform, signal.transform.m3), geomRadius(*model, ""), m_detail);
    if (!lod)
        return;
    const Mat44 world = Mat44::fromMat34(signal.transform);
    if (const GpuMesh* body = findFilledLod(*model, "", *lod))
        drawGpuMesh(m_device, m_textures, *body, model->materials(0), world);
    // aiTrafficLightInstance::DrawGlow: the light's glow together with the
    // pedestrian signal (WALK only in the walk phase), both or neither.
    const char* colour = signal.state == ai::LightState::Green  ? "GREEN"
                       : signal.state == ai::LightState::Amber  ? "YELLOW"
                                                                : "RED";
    // Drawn in cityLevel::DrawRooms' glow pass (rooms nearer than NoDraw):
    // unlit, no fog, added (ONE/ONE), no depth writes.
    if (signal.transform.m3.dist2(camera.position()) >= sq(m_detail.noDraw))
        return;
    const char* time = nightGlows ? "NIGHT" : "DAY";
    const GpuMesh* glow = findFilledLod(*model, std::format("{}GLOW{}", colour, time), asset::Lod::High);
    const char* walkName = signal.state == ai::LightState::Walk ? "WALK" : "NOWALK";
    const GpuMesh* walk = findFilledLod(*model, std::format("{}_{}", walkName, time), asset::Lod::High);
    if (glow && walk) {
        MeshDrawOptions opts;
        opts.lighting = false;
        opts.fog = false;
        opts.blend = render::BlendMode::Add;
        opts.depthWrite = false;
        opts.alphaRef = 1.0f / 255.0f; // the default alpha test (alpha not 0)
        drawGpuMesh(m_device, m_textures, *glow, model->materials(0), world, opts);
        drawGpuMesh(m_device, m_textures, *walk, model->materials(0), world, opts);
    }
}

void AiRenderer::draw(const ai::World& world, const Camera& camera, const Frustum& frustum, TimeOfDay time,
                      bool lights, const ObjectDetail& detail,
                      const std::function<const Mat34*(int)>& physicalTransform) {
    m_stats = {};
    m_detail = detail;
    const Vec3 eye = camera.position();
    for (const auto& car : world.cars()) {
        const Mat34* physical = physicalTransform ? physicalTransform(car.id) : nullptr;
        const Mat34& transform = physical ? *physical : car.transform;
        // The renderer's lvlInstance::IsVisible ends dynamic objects at NoDraw.
        if (!frustum.intersectsSphere(transform.m3, 4.0f))
            continue;
        CarModel* cm = carModel(car.model);
        if (!cm || !cm->model)
            continue;
        // aiVehicleInstance::SetColor: trunc(frand * (paint jobs - 1)), so the
        // last paint job is never chosen.
        const int paint =
            cm->paintjobs > 1 ? static_cast<int>(car.paint * static_cast<float>(cm->paintjobs - 1)) : 0;
        auto& r = cm->renderers[paint];
        if (!r) {
            r = std::make_unique<VehicleRenderer>(m_device, m_textures, m_models, *cm->model, paint);
            r->setTraffic(true);
        }
        r->setDetail(detail);
        VehiclePose pose;
        pose.body = transform;
        // aiVehicleInstance::Draw: the rail's tyre rotation about each axle,
        // no steering.
        for (std::size_t i = 0; i < 6; ++i)
            pose.wheelSpin[i] = -car.tireRotation; // rolling forward (-Z) spins about -X
        pose.brakeLights = car.braking;
        pose.headlights = lights;
        r->draw(pose, camera.transform);
        ++m_stats.cars;
    }
    // Pedestrians are dynamic objects too: nothing beyond NoDraw (inferred:
    // MM2 draws them through lvlInstance::IsVisible like the cars).
    for (const auto& ped : world.peds()) {
        if (ped.transform.m3.dist2(eye) > sq(detail.noDraw) ||
            !frustum.intersectsSphere(ped.transform.m3, 2.0f))
            continue;
        if (const asset::PedType* type = pedType(ped.typeName)) {
            drawPed(ped, *type, camera);
            ++m_stats.peds;
        }
    }
    for (const auto& signal : world.signals()) {
        if (!frustum.intersectsSphere(signal.transform.m3, 6.0f))
            continue;
        drawSignal(signal, camera, time >= TimeOfDay::Evening);
        ++m_stats.signals;
    }
}

} // namespace mm2::game
