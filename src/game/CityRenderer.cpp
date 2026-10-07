#include "game/CityRenderer.h"

#include "core/Log.h"
#include "game/MeshDraw.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace mm2::game {
namespace {

Vec3 unpackRgb(std::uint32_t argb) {
    return {static_cast<float>((argb >> 16) & 0xFF) / 255.0f, static_cast<float>((argb >> 8) & 0xFF) / 255.0f,
            static_cast<float>(argb & 0xFF) / 255.0f};
}

// Direction a light travels for an Angel heading/pitch pair (radians).
// Inferred: pitch < 0 points down; heading rotates about +Y from -Z.
Vec3 lightDirection(float heading, float pitch) {
    return Vec3{std::sin(heading) * std::cos(pitch), std::sin(pitch), -std::cos(heading) * std::cos(pitch)}
        .normalized();
}

// LOD switch distances in metres (inferred; the original's values live in
// code we cannot read and were scaled by the Object Detail option).
asset::Lod lodForDistance(float d, float scale) {
    if (d < 60.0f * scale)
        return asset::Lod::High;
    if (d < 130.0f * scale)
        return asset::Lod::Medium;
    if (d < 260.0f * scale)
        return asset::Lod::Low;
    return asset::Lod::VeryLow;
}

Aabb transformBounds(const Aabb& b, const Mat34& m) {
    Aabb out;
    for (int i = 0; i < 8; ++i) {
        const Vec3 corner{(i & 1) ? b.max.x : b.min.x, (i & 2) ? b.max.y : b.min.y, (i & 4) ? b.max.z : b.min.z};
        out.expand(m.transform(corner));
    }
    return out;
}

} // namespace

Environment makeEnvironment(const city::CityData& city, TimeOfDay time, Weather weatherIn) {
    Environment env;
    const int t = static_cast<int>(time);
    const int w = std::min(static_cast<int>(weatherIn), 3); // snow -> rain tables
    const int index = city::lightingIndex(t, w);

    auto& f = env.frame;
    if (const auto& lt = city.lighting[static_cast<std::size_t>(index)]) {
        f.lights[0] = {lightDirection(lt->keyHeading, lt->keyPitch), lt->keyColor};
        f.lights[1] = {lightDirection(lt->fill1Heading, lt->fill1Pitch), lt->fill1Color};
        f.lights[2] = {lightDirection(lt->fill2Heading, lt->fill2Pitch), lt->fill2Color};
        f.ambient = unpackRgb(lt->ambient);
    } else {
        f.lights[0] = {Vec3{0.3f, -1.0f, -0.5f}.normalized(), {1, 1, 1}};
        f.ambient = {0.4f, 0.4f, 0.4f};
    }
    if (static_cast<std::size_t>(index) < city.fog.size()) {
        const auto& fog = city.fog[static_cast<std::size_t>(index)];
        f.fogMode = render::FogMode::Linear;
        f.fogColor = {fog.r / 255.0f, fog.g / 255.0f, fog.b / 255.0f};
        f.fogStart = fog.start;
        f.fogEnd = fog.end;
        env.fogEnd = fog.end;
        env.clearColor = {f.fogColor.x, f.fogColor.y, f.fogColor.z, 1.0f};
    }
    // Sky dome paint jobs: four times of day (a = dawn, n = noon, d = dusk,
    // m = midnight in the texture names, verified by viewing them) times four
    // weathers (c clear, p partly cloudy, f fog, r rain): the same order as the
    // lighting tables.
    env.skyPaintjob = t * 4 + w;
    return env;
}

CityRenderer::CityRenderer(render::Device& device, TextureLibrary& textures, ModelLibrary& models,
                           const city::CityData& city, const std::function<bool(std::string_view)>& isDynamic)
    : m_device(device), m_textures(textures), m_models(models), m_city(city), m_locator(city.psdl) {
    const city::CityMesh mesh = city::buildCityMesh(city.psdl);
    std::vector<render::Vertex3D> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(mesh.vertexCount());
    m_rooms.resize(mesh.rooms.size());
    for (std::size_t r = 0; r < mesh.rooms.size(); ++r) {
        const auto& roomMesh = mesh.rooms[r];
        Room& room = m_rooms[r];
        room.bounds = roomMesh.bounds;
        for (const auto& batch : roomMesh.batches) {
            if (batch.indices.empty() || batch.kind == city::SurfaceKind::FacadeBound)
                continue;
            Batch b;
            b.firstIndex = static_cast<std::uint32_t>(indices.size());
            b.indexCount = static_cast<std::uint32_t>(batch.indices.size());
            b.kind = batch.kind;
            if (const auto* name = city.psdl.texture(batch.texture); name && !name->empty())
                b.textureName = name;
            const auto base = static_cast<std::uint32_t>(vertices.size());
            for (const auto& v : batch.vertices) {
                render::Vertex3D rv{};
                rv.position[0] = v.position.x;
                rv.position[1] = v.position.y;
                rv.position[2] = v.position.z;
                rv.normal[0] = v.normal.x;
                rv.normal[1] = v.normal.y;
                rv.normal[2] = v.normal.z;
                rv.color = 0xFFFFFFFFu;
                rv.uv0[0] = rv.uv1[0] = v.uv.x;
                rv.uv0[1] = rv.uv1[1] = v.uv.y;
                vertices.push_back(rv);
            }
            for (auto i : batch.indices)
                indices.push_back(base + i);
            room.batches.push_back(b);
        }
    }
    m_vertices = m_device.createBuffer(render::BufferKind::Vertex, vertices.size() * sizeof(render::Vertex3D),
                                       vertices.data());
    m_indices = m_device.createBuffer(render::BufferKind::Index, indices.size() * sizeof(std::uint32_t), indices.data());

    for (const auto& inst : city.instances) {
        if (isDynamic && isDynamic(inst.name))
            continue;
        InstanceDraw d;
        d.model = inst.name;
        d.transform = inst.transform;
        d.world = Mat44::fromMat34(inst.transform);
        const std::size_t index = m_instances.size();
        m_instances.push_back(std::move(d));
        if (inst.room < m_rooms.size())
            m_rooms[inst.room].instances.push_back(index);
    }
    m_roomMarks.assign(m_rooms.size(), 0);
    if (city.sky)
        m_sky = m_models.get(city.sky->model);
    log::info("city: {} rooms, {} vertices, {} triangles, {} instances", m_rooms.size(), vertices.size(),
              indices.size() / 3, m_instances.size());
}

CityRenderer::~CityRenderer() {
    m_device.destroyBuffer(m_vertices);
    m_device.destroyBuffer(m_indices);
}

void CityRenderer::drawMesh(const GpuMesh& mesh, const std::vector<asset::PkgMaterial>& materials, const Mat44& world,
                            bool fog, bool lighting) {
    MeshDrawOptions options;
    options.fog = fog;
    options.lighting = lighting;
    m_stats.drawCalls += drawGpuMesh(m_device, m_textures, mesh, materials, world, options);
}

void CityRenderer::drawSky(const Camera& camera, const Environment& env) {
    if (!m_sky)
        return;
    const GpuMesh* mesh = m_sky->find("", asset::Lod::High);
    if (!mesh)
        return;
    Mat34 m = Mat34::translation(camera.position());
    const auto& mats = m_sky->materials(env.skyPaintjob);
    for (const auto& d : mesh->draws) {
        const asset::PkgMaterial* mat = d.shader < mats.size() ? &mats[d.shader] : nullptr;
        const WorldTexture* tex = mat ? m_textures.get(mat->texture) : nullptr;
        render::DrawCall call;
        call.vertices = {mesh->vertices, 0};
        call.indices = {mesh->indices, 0};
        call.count = d.indexCount;
        call.first = d.firstIndex;
        call.baseVertex = d.baseVertex;
        call.constants.world = Mat44::fromMat34(m);
        call.constants.flags = render::DrawFlag::VertexColor;
        if (tex) {
            call.constants.flags |= render::DrawFlag::Texture0;
            call.textures[0] = {tex->handle, tex->sampler};
        }
        call.state.depthTest = false;
        call.state.depthWrite = false;
        call.state.cull = render::CullMode::None;
        m_device.draw(call);
        ++m_stats.drawCalls;
    }
}

void CityRenderer::resolve(InstanceDraw& inst) {
    inst.resolved = true;
    inst.gpu = m_models.get(inst.model);
    if (inst.gpu)
        inst.worldBounds = transformBounds(inst.gpu->bounds, inst.transform);
}

void CityRenderer::drawModel(const GpuModel& model, const Mat34& transform, asset::Lod lod, int depth) {
    const GpuMesh* mesh = model.find("", lod);
    if (mesh)
        drawMesh(*mesh, model.materials(0), Mat44::fromMat34(transform));
    if (depth >= 3)
        return;
    for (const auto& xref : model.xrefs) {
        if (const GpuModel* child = m_models.get(xref.name))
            drawModel(*child, xref.transform * transform, lod, depth + 1);
    }
}

void CityRenderer::drawInstance(InstanceDraw& inst, const Frustum& frustum, const Vec3& eye,
                                const DetailSettings& detail) {
    if (!inst.resolved)
        resolve(inst);
    if (!inst.gpu || !frustum.intersects(inst.worldBounds))
        return;
    const float distance = inst.worldBounds.center().dist(eye);
    drawModel(*inst.gpu, inst.transform, lodForDistance(distance, detail.lodScale), 0);
    ++m_stats.instancesDrawn;
}

void CityRenderer::drawRoom(std::size_t r, const Frustum& frustum, const Vec3& eye, const DetailSettings& detail) {
    Room& room = m_rooms[r];
    if (room.batches.empty() && room.instances.empty())
        return;
    // Instances can stick out of their room's street geometry; test them separately.
    if (frustum.intersects(room.bounds)) {
        ++m_stats.roomsDrawn;
        for (const auto& b : room.batches) {
            render::DrawCall call;
            call.vertices = {m_vertices, 0};
            call.indices = {m_indices, 0};
            call.indexType = render::IndexType::U32;
            call.count = b.indexCount;
            call.first = b.firstIndex;
            call.constants.color = {1, 1, 1, 1};
            call.constants.flags = render::DrawFlag::Fog | render::DrawFlag::Lighting;
            // Looked up per frame so day/night texture sets can switch live.
            if (const WorldTexture* tex = b.textureName ? m_textures.get(*b.textureName) : nullptr) {
                call.constants.flags |= render::DrawFlag::Texture0;
                // Street geometry tiles its textures by repeat counts (facades,
                // slivers, roads) whatever the texture's wrap flags say; with
                // clamping, repeated facades smear their edge rows.
                render::SamplerDesc sampler = tex->sampler;
                sampler.addressU = sampler.addressV = render::AddressMode::Wrap;
                call.textures[0] = {tex->handle, sampler};
                if (tex->translucent) {
                    call.constants.flags |= render::DrawFlag::AlphaTest;
                    call.constants.alphaRef = 0.5f;
                }
            }
            // Street geometry is built with counter-clockwise front faces
            // (railings are emitted double-sided).
            call.state.cull = std::getenv("OPENMM2_DEBUG_NOCULL_CITY") ? render::CullMode::None : render::CullMode::Back;
            call.state.frontFace = render::FrontFace::CounterClockwise;
            m_device.draw(call);
            ++m_stats.drawCalls;
        }
    }
    for (std::size_t i : room.instances)
        drawInstance(m_instances[i], frustum, eye, detail);
}

void CityRenderer::draw(const Camera& camera, const Frustum& frustum, const Environment& env,
                        const DetailSettings& detail) {
    m_stats = {};
    drawSky(camera, env);

    const Vec3 eye = camera.position();
    const int room = m_locator.find(eye);
    m_stats.cameraRoom = room;
    const float farSq = sq(env.fogEnd + 50.0f);

    std::ranges::fill(m_roomMarks, 0);
    // The PVS was computed for street-level viewpoints; from high above a
    // room (debug camera, big jumps) everything may be visible.
    const bool nearGround = room > 0 && eye.y - m_rooms[static_cast<std::size_t>(room)].bounds.min.y < 40.0f;
    if (detail.usePvs && nearGround && m_city.pvs && m_city.pvs->hasData(static_cast<std::size_t>(room))) {
        m_roomMarks[static_cast<std::size_t>(room)] = 1;
        for (auto r : m_city.pvs->visibleFrom(static_cast<std::size_t>(room)))
            if (r < m_roomMarks.size())
                m_roomMarks[r] = 1;
    } else {
        for (std::size_t r = 1; r < m_rooms.size(); ++r) {
            const Aabb& b = m_rooms[r].bounds;
            const Vec3 closest = vmax(b.min, vmin(eye, b.max));
            if (b.valid() && closest.dist2(eye) < farSq)
                m_roomMarks[r] = 1;
        }
    }
    for (std::size_t r = 1; r < m_rooms.size(); ++r)
        if (m_roomMarks[r])
            drawRoom(r, frustum, eye, detail);
}

} // namespace mm2::game
