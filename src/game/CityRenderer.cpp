#include "game/CityRenderer.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "game/MeshDraw.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace mm2::game {
namespace {

constexpr float kPi = 3.14159265f;

Vec3 unpackRgb(std::uint32_t argb) {
    return {static_cast<float>((argb >> 16) & 0xFF) / 255.0f, static_cast<float>((argb >> 8) & 0xFF) / 255.0f,
            static_cast<float>(argb & 0xFF) / 255.0f};
}

// cityLevel::SetLightDirection: the direction a light travels for a heading
// and pitch (radians).
Vec3 lightDirection(float heading, float pitch) {
    return {std::cos(heading) * std::cos(pitch), std::sin(pitch), std::sin(heading) * std::cos(pitch)};
}

// cityTimeWeatherLighting::ComputeAmbientLightLevels: lower light qualities
// use an ambient level moved towards white (by 66% at quality 1, 33% at 2).
std::uint32_t ambientForQuality(std::uint32_t argb, int quality) {
    if (quality >= 3)
        return argb;
    if (quality <= 0)
        return 0xFFFFFFFFu;
    const std::uint32_t k = quality == 1 ? 168u : 84u;
    std::uint32_t out = 0xFF000000u;
    for (int shift = 0; shift < 24; shift += 8) {
        const std::uint32_t c = (argb >> shift) & 0xFF;
        out |= (c + (((255u - c) * k) >> 8)) << shift;
    }
    return out;
}

Aabb transformBounds(const Aabb& b, const Mat34& m) {
    Aabb out;
    for (int i = 0; i < 8; ++i) {
        const Vec3 corner{(i & 1) ? b.max.x : b.min.x, (i & 2) ? b.max.y : b.min.y, (i & 4) ? b.max.z : b.min.z};
        out.expand(m.transform(corner));
    }
    return out;
}

std::uint32_t argbToRgba(std::uint32_t argb) {
    return (argb & 0xFF00FF00u) | ((argb >> 16) & 0xFFu) | ((argb & 0xFFu) << 16);
}

// A facade or sliver of a room and the wall light table entry it is drawn
// with: sdlPage16::Draw keeps the angle word of the last FacadeBound
// attribute it passed and shades the following facades and slivers with
// sdlCommon's light table entry of that index.
struct WallLight {
    Vec3 p0, p1, outward;
    std::uint8_t index = 0;
};

std::vector<WallLight> wallLights(const city::Psdl& psdl, std::size_t room) {
    std::vector<WallLight> out;
    if (room >= psdl.rooms.size())
        return out;
    int light = -1;
    for (const auto& a : psdl.rooms[room].attributes) {
        if (a.type == city::PsdlAttrType::FacadeBound) {
            light = a.facadeBoundAngle() & 63;
            continue;
        }
        if ((a.type != city::PsdlAttrType::Facade && a.type != city::PsdlAttrType::Sliver) || light < 0)
            continue;
        if (a.wallLeft() >= psdl.vertices.size() || a.wallRight() >= psdl.vertices.size())
            continue;
        WallLight w;
        w.p0 = psdl.vertices[a.wallLeft()];
        w.p1 = psdl.vertices[a.wallRight()];
        const Vec3 n = (w.p1 - w.p0).cross({0, 1, 0});
        w.outward = n.mag2() > 0.0f ? n.normalized() : Vec3{};
        w.index = static_cast<std::uint8_t>(light);
        out.push_back(w);
    }
    return out;
}

// The light table index of a wall vertex built by city::buildCityMesh: the
// wall it is a corner of (same ground position, same facing). A wall the
// lookup misses (no FacadeBound before it) takes the entry of its facing.
std::uint8_t wallLightIndex(const std::vector<WallLight>& walls, const city::CityVertex& v) {
    auto flatNear = [](const Vec3& a, const Vec3& b) { return sq(a.x - b.x) + sq(a.z - b.z) < 1e-6f; };
    for (const auto& w : walls)
        if ((flatNear(v.position, w.p0) || flatNear(v.position, w.p1)) && v.normal.dot(w.outward) > 0.99f)
            return w.index;
    const float a = std::atan2(v.normal.z, v.normal.x);
    return static_cast<std::uint8_t>(static_cast<int>(std::floor((a + kPi / 2.0f) * 32.0f / kPi)) & 63);
}

} // namespace

std::string sdlTextureName(std::string_view name) {
    // lvlSDL::LoadBinary: a name ending in '-', '0' and three more characters
    // is a movie frame ("s_thames-0009"); the texture is looked up by its base
    // name, which gfxGetTextureMovie turns into the frame sequence.
    if (name.size() > 5 && name[name.size() - 5] == '-' && name[name.size() - 4] == '0')
        return std::string(name.substr(0, name.size() - 5));
    return std::string(name);
}

const GpuMesh* findFilledLod(const GpuModel& model, std::string_view part, asset::Lod lod) {
    for (int l = static_cast<int>(lod); l <= static_cast<int>(asset::Lod::VeryLow); ++l)
        for (const auto& m : model.meshes)
            if (m.lod == static_cast<asset::Lod>(l) && str::iequals(m.part, part))
                return &m;
    // Models without LOD names.
    for (const auto& m : model.meshes)
        if (m.lod == asset::Lod::None && str::iequals(m.part, part))
            return &m;
    return nullptr;
}

float geomRadius(const GpuModel& model, std::string_view part) {
    float radius = 0.0f;
    for (const auto& m : model.meshes)
        if (str::iequals(m.part, part) && radius < m.radius)
            radius = m.radius;
    return radius;
}

Environment makeEnvironment(const city::CityData& city, TimeOfDay time, Weather weatherIn,
                            const EnvironmentOptions& options) {
    Environment env;
    const int t = static_cast<int>(time);
    const int w = std::min(static_cast<int>(weatherIn), 3); // snow -> rain tables
    const int index = city::lightingIndex(t, w);
    const int quality = std::clamp(options.lightQuality, 0, 3);

    auto& f = env.frame;
    if (const auto& lt = city.lighting[static_cast<std::size_t>(index)]) {
        // cityLevel::SetupLighting: the key light from quality 1, fill1 from
        // 2, fill2 from 3; the render state's ambient by quality.
        const std::array<std::pair<Vec3, Vec3>, 3> lights = {
            std::pair{lightDirection(lt->keyHeading, lt->keyPitch), lt->keyColor},
            std::pair{lightDirection(lt->fill1Heading, lt->fill1Pitch), lt->fill1Color},
            std::pair{lightDirection(lt->fill2Heading, lt->fill2Pitch), lt->fill2Color}};
        for (int i = 0; i < 3; ++i)
            f.lights[static_cast<std::size_t>(i)] = {lights[static_cast<std::size_t>(i)].first,
                                                     quality > i ? lights[static_cast<std::size_t>(i)].second : Vec3{}};
        f.ambient = unpackRgb(ambientForQuality(lt->ambient, quality));
    } else {
        f.lights[0] = {Vec3{0.3f, -1.0f, -0.5f}.normalized(), {1, 1, 1}};
        f.ambient = {0.4f, 0.4f, 0.4f};
    }
    // sdlCommon::UpdateLighting: the street walls' light table. A wall
    // facing angle a gets ambient + sum max(0, n . -L) x light colour.
    for (int i = 0; i < 64; ++i) {
        const float a = static_cast<float>(i) * kPi / 32.0f - kPi / 2.0f;
        const float nx = std::cos(a), nz = std::sin(a);
        std::uint32_t argb = 0xFF000000u;
        for (int c = 0; c < 3; ++c) {
            auto pick = [c](const Vec3& v) { return c == 0 ? v.x : c == 1 ? v.y : v.z; };
            float v = pick(f.ambient) * 255.0f;
            for (const auto& light : f.lights)
                v += std::max(0.0f, nx * -light.direction.x + nz * -light.direction.z) * pick(light.color) * 255.0f;
            argb |= static_cast<std::uint32_t>(std::min(v, 255.0f)) << (16 - 8 * c);
        }
        env.wallShades[static_cast<std::size_t>(i)] = argb;
    }

    env.farClip = options.farClip;
    if (static_cast<std::size_t>(index) < city.fog.size()) {
        // lvlSky::SetupFog: linear fog clamped by the far plane.
        const auto& fog = city.fog[static_cast<std::size_t>(index)];
        f.fogMode = render::FogMode::Linear;
        f.fogColor = {fog.r / 255.0f, fog.g / 255.0f, fog.b / 255.0f};
        f.fogStart = std::min(options.farClip - 30.0f, fog.start);
        f.fogEnd = std::min(options.farClip, fog.end);
        env.fogEnd = f.fogEnd;
        // The clear colour is the fog colour.
        env.clearColor = {f.fogColor.x, f.fogColor.y, f.fogColor.z, 1.0f};
    }
    // Sky dome paint jobs: four times of day (a = dawn, n = noon, d = dusk,
    // m = midnight in the texture names) times four weathers (c clear, p
    // partly cloudy, f fog, r rain): the lighting tables' order.
    env.skyPaintjob = t * 4 + w;
    return env;
}

CityRenderer::CityRenderer(render::Device& device, TextureLibrary& textures, ModelLibrary& models,
                           const city::CityData& city, const std::function<bool(std::string_view)>& isDynamic)
    : m_device(device), m_textures(textures), m_models(models), m_city(city), m_locator(city.psdl) {
    const city::CityMesh mesh = city::buildCityMesh(city.psdl);
    std::vector<std::uint32_t> indices;
    m_streetVertices.reserve(mesh.vertexCount());
    m_rooms.resize(mesh.rooms.size());
    for (std::size_t r = 0; r < mesh.rooms.size(); ++r) {
        const auto& roomMesh = mesh.rooms[r];
        Room& room = m_rooms[r];
        room.bounds = roomMesh.bounds;
        const auto walls = wallLights(city.psdl, r);
        for (const auto& batch : roomMesh.batches) {
            if (batch.indices.empty() || batch.kind == city::SurfaceKind::FacadeBound)
                continue;
            Batch b;
            b.firstIndex = static_cast<std::uint32_t>(indices.size());
            b.indexCount = static_cast<std::uint32_t>(batch.indices.size());
            b.kind = batch.kind;
            if (const auto* name = city.psdl.texture(batch.texture); name && !name->empty())
                b.textureName = sdlTextureName(*name);
            const auto base = static_cast<std::uint32_t>(m_streetVertices.size());
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
                m_streetVertices.push_back(rv);
                m_streetKinds.push_back(batch.kind);
                m_wallShade.push_back(batch.kind == city::SurfaceKind::Wall ? wallLightIndex(walls, v) : 0);
            }
            for (auto i : batch.indices)
                indices.push_back(base + i);
            room.batches.push_back(b);
        }
    }
    m_vertices = m_device.createBuffer(render::BufferKind::Vertex,
                                       m_streetVertices.size() * sizeof(render::Vertex3D), m_streetVertices.data());
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
    log::info("city: {} rooms, {} vertices, {} triangles, {} instances", m_rooms.size(), m_streetVertices.size(),
              indices.size() / 3, m_instances.size());
}

CityRenderer::~CityRenderer() {
    m_device.destroyBuffer(m_vertices);
    m_device.destroyBuffer(m_indices);
}

void CityRenderer::setEnvironment(const Environment& env) {
    // sdlPage16::Draw lights nothing: road, sidewalk, roof and ground
    // vertices take the room colour, curbs half of it, and facades and
    // slivers the light table entry of their facing, shaded by the room
    // colour. The room colours come from city/<map>.lmap, but its count
    // never matches the room count cityLevel::Load checks (London 1340 for
    // 1341, SF 1125 for 1171), so MM2 drops it and every room is white.
    // The light table index of a wall is the one the room's preceding
    // FacadeBound attribute stores (wallLights).
    for (std::size_t i = 0; i < m_streetVertices.size(); ++i) {
        auto& v = m_streetVertices[i];
        std::uint32_t argb = 0xFFFFFFFFu;
        switch (m_streetKinds[i]) {
        case city::SurfaceKind::Wall: argb = env.wallShades[m_wallShade[i] & 63u]; break;
        case city::SurfaceKind::Curb: argb = ((0xFFFFFFFFu >> 1) & 0x7F7F7Fu) | 0xFF000000u; break;
        default: break;
        }
        v.color = argbToRgba(argb);
    }
    if (!m_streetVertices.empty())
        m_device.updateBuffer(m_vertices, 0, std::as_bytes(std::span<const render::Vertex3D>(m_streetVertices)));
}

void CityRenderer::update(float dt) {
    // lvlSky::Update: the dome turns at the .sky file's rate (rad/s).
    const float speed = m_city.sky && m_city.sky->params.size() > 2 ? m_city.sky->params[2] : 0.0f;
    m_skyAngle = std::fmod(m_skyAngle + dt * speed, 2.0f * kPi);
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
    // lvlSky::DrawHat: at the camera, its height scaled and offset by the
    // .sky parameters, turned about Y; unlit, unfogged, no depth.
    const auto& p = m_city.sky->params;
    const float yOffset = p.size() > 0 ? p[0] : 0.0f, yScale = p.size() > 1 ? p[1] : 1.0f;
    const Vec3 eye = camera.position();
    const Mat34 m = Mat34::rotationY(m_skyAngle) * Mat34::translation({eye.x, eye.y * yScale + yOffset, eye.z});
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
    if (inst.gpu) {
        inst.worldBounds = transformBounds(inst.gpu->bounds, inst.transform);
        inst.radius = geomRadius(*inst.gpu, "");
    }
}

void CityRenderer::drawModel(const GpuModel& model, const Mat34& transform, asset::Lod lod, int depth) {
    if (const GpuMesh* mesh = findFilledLod(model, "", lod))
        drawMesh(*mesh, model.materials(0), Mat44::fromMat34(transform));
    if (depth >= 3)
        return;
    for (const auto& xref : model.xrefs) {
        if (const GpuModel* child = m_models.get(xref.name))
            drawModel(*child, xref.transform * transform, lod, depth + 1);
    }
}

void CityRenderer::drawInstance(InstanceDraw& inst, const Frustum& frustum, const Mat34& camera,
                                const DetailSettings& detail) {
    if (!inst.resolved)
        resolve(inst);
    if (!inst.gpu || !frustum.intersects(inst.worldBounds))
        return;
    // lvlInstance::IsVisible: the depth of the instance's origin minus its
    // radius; static instances have no NoDraw limit (only the far plane).
    const auto lod = objectLod(viewDepth(camera, inst.transform.m3), inst.radius, detail.objects);
    if (!lod)
        return;
    drawModel(*inst.gpu, inst.transform, *lod, 0);
    ++m_stats.instancesDrawn;
}

void CityRenderer::drawStreets(std::size_t r, const Frustum& frustum, bool alphaPass) {
    Room& room = m_rooms[r];
    if (room.batches.empty() || !frustum.intersects(room.bounds))
        return;
    if (!alphaPass)
        ++m_stats.roomsDrawn;
    for (const auto& b : room.batches) {
        // Looked up per frame so day/night texture sets can switch live.
        const WorldTexture* tex = b.textureName.empty() ? nullptr : m_textures.get(b.textureName);
        // vglEndBatch draws the textures whose format has no alpha first, with
        // alpha blending (and so the alpha test) off, then the others alpha
        // blended and tested (GREATER 100).
        if ((tex && tex->alphaFormat) != alphaPass)
            continue;
        render::DrawCall call;
        call.vertices = {m_vertices, 0};
        call.indices = {m_indices, 0};
        call.indexType = render::IndexType::U32;
        call.count = b.indexCount;
        call.first = b.firstIndex;
        call.constants.color = {1, 1, 1, 1};
        // Street geometry is unlit: the vertex colours carry its shading.
        call.constants.flags = render::DrawFlag::Fog | render::DrawFlag::VertexColor;
        if (tex) {
            call.constants.flags |= render::DrawFlag::Texture0;
            // The texture's own address modes (gfxRenderState::DoFlush):
            // facades and most street textures repeat both ways, the road
            // textures (flags 0x18006) clamp V.
            call.textures[0] = {tex->handle, tex->sampler};
            if (alphaPass) {
                call.constants.flags |= render::DrawFlag::AlphaTest;
                call.constants.alphaRef = 101.0f / 255.0f;
                call.state.blend = render::BlendMode::Alpha;
            }
        }
        // Street geometry is built with counter-clockwise front faces
        // (railings are emitted double-sided); sdlPage16::Draw skips the
        // walls facing away itself (sdlCommon::BACKFACE).
        call.state.cull = std::getenv("OPENMM2_DEBUG_NOCULL_CITY") ? render::CullMode::None : render::CullMode::Back;
        call.state.frontFace = render::FrontFace::CounterClockwise;
        m_device.draw(call);
        ++m_stats.drawCalls;
    }
}

void CityRenderer::draw(const Camera& camera, const Frustum& frustum, const Environment& env,
                        const DetailSettings& detail) {
    m_stats = {};
    drawSky(camera, env);

    const Vec3 eye = camera.position();
    // cityLevel::Draw: the camera's room, or the last one found while the
    // camera is outside every room.
    if (const int found = m_locator.find(eye); found > 0)
        m_lastRoom = found;
    const int room = m_lastRoom;
    m_stats.cameraRoom = room;

    std::ranges::fill(m_roomMarks, 0);
    if (detail.usePvs && room > 0 && m_city.pvs && m_city.pvs->hasData(static_cast<std::size_t>(room))) {
        m_roomMarks[static_cast<std::size_t>(room)] = 1;
        for (auto r : m_city.pvs->visibleFrom(static_cast<std::size_t>(room)))
            if (r < m_roomMarks.size())
                m_roomMarks[r] = 1;
    } else {
        // No PVS (or never inside the city, an OpenMM2 debug case): every
        // room within the far plane.
        const float farSq = sq(env.farClip + 50.0f);
        for (std::size_t r = 1; r < m_rooms.size(); ++r) {
            const Aabb& b = m_rooms[r].bounds;
            const Vec3 closest = vmax(b.min, vmin(eye, b.max));
            if (b.valid() && closest.dist2(eye) < farSq)
                m_roomMarks[r] = 1;
        }
    }
    // cityLevel::DrawRooms: the street geometry of every visible room in one
    // batch (opaque textures, then those with alpha), then the static
    // instances room by room from the last room in the list to the first.
    for (bool alphaPass : {false, true})
        for (std::size_t r = 1; r < m_rooms.size(); ++r)
            if (m_roomMarks[r])
                drawStreets(r, frustum, alphaPass);
    for (std::size_t r = m_rooms.size(); r-- > 1;)
        if (m_roomMarks[r])
            for (std::size_t i : m_rooms[r].instances)
                drawInstance(m_instances[i], frustum, camera.transform, detail);
}

} // namespace mm2::game
