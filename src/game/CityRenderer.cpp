#include "game/CityRenderer.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "game/CityLevel.h"
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

// cityLevel::SetupLighting's ambient: the table's own at quality 3, white at
// 0, and at 1 and 2 the level cityTimeWeatherLighting::
// ComputeAmbientLightLevels moved towards white (by 66% at quality 1, 33% at
// 2) from `before`, the ambient the table held before the .ltNN file loaded
// (LoadCityTimeWeatherLighting computes the levels first).
std::uint32_t ambientForQuality(std::uint32_t loaded, std::uint32_t before, int quality) {
    if (quality >= 3)
        return loaded;
    if (quality <= 0)
        return 0xFFFFFFFFu;
    const std::uint32_t argb = before;
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
        f.ambient = unpackRgb(
            ambientForQuality(lt->ambient, city.ambientBeforeLoad[static_cast<std::size_t>(index)], quality));
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
    : m_device(device), m_textures(textures), m_models(models), m_city(city), m_locator(city.psdl, city.info.mapName) {
    // sdlPage16::Draw's primitives for every room and level of detail; the
    // tunnels (not ported) from the CityMesh reconstruction, at every level.
    std::unordered_map<std::string, std::uint32_t> slots;
    m_slotNames.emplace_back(); // 0: untextured
    auto slotOf = [&](int texture) -> std::uint32_t {
        const auto* name = city.psdl.texture(texture);
        if (!name || name->empty())
            return 0;
        const std::string key = sdlTextureName(*name);
        auto [it, added] = slots.try_emplace(key, static_cast<std::uint32_t>(m_slotNames.size()));
        if (added)
            m_slotNames.push_back(key);
        return it->second;
    };
    auto addVertex = [&](const Vec3& p, const Vec2& uv, city::SdlShade shade, std::uint8_t light) {
        render::Vertex3D rv{};
        rv.position[0] = p.x;
        rv.position[1] = p.y;
        rv.position[2] = p.z;
        rv.normal[1] = 1.0f;
        rv.color = 0xFFFFFFFFu;
        rv.uv0[0] = rv.uv1[0] = uv.x;
        rv.uv0[1] = rv.uv1[1] = uv.y;
        m_streetVertices.push_back(rv);
        m_vertexShade.push_back(shade);
        m_vertexLight.push_back(light);
    };
    m_rooms.resize(city.psdl.rooms.size());
    for (std::size_t r = 1; r < m_rooms.size(); ++r) {
        Room& room = m_rooms[r];
        city::sdlRoomBoundSphere(city.psdl, r, room.centre, room.radius);
        const city::SdlRoomDraw sdl = city::buildSdlRoomDraw(city.psdl, r);
        const auto base = static_cast<std::uint32_t>(m_streetVertices.size());
        // A vertex belongs to one primitive: take its shading from it.
        std::vector<city::SdlShade> shade(sdl.vertices.size(), city::SdlShade::Room);
        std::vector<std::uint8_t> light(sdl.vertices.size(), 0);
        for (const auto& lod : sdl.lods)
            for (const auto& prim : lod)
                for (std::uint32_t i = 0; i < prim.indexCount; ++i) {
                    const auto v = sdl.indices[prim.firstIndex + i];
                    shade[v] = prim.shade;
                    light[v] = prim.light;
                }
        for (std::size_t i = 0; i < sdl.vertices.size(); ++i)
            addVertex(sdl.vertices[i].position, sdl.vertices[i].uv, shade[i], light[i]);
        const auto indexBase = static_cast<std::uint32_t>(m_streetIndices.size());
        for (const auto i : sdl.indices)
            m_streetIndices.push_back(base + i);
        for (std::size_t l = 0; l < 4; ++l) {
            for (const auto& prim : sdl.lods[l]) {
                Prim p;
                p.first = indexBase + prim.firstIndex;
                p.count = prim.indexCount;
                p.slot = slotOf(prim.texture);
                p.wall = prim.wall;
                p.wall0 = prim.wall0;
                p.wall1 = prim.wall1;
                p.belowCamera = prim.belowCamera;
                p.height = prim.height;
                room.lods[l].push_back(p);
            }
        }
        for (const auto& batch : city::buildRoomMesh(city.psdl, r).batches) {
            if (batch.kind != city::SurfaceKind::Tunnel || batch.indices.empty())
                continue;
            const auto tunnelBase = static_cast<std::uint32_t>(m_streetVertices.size());
            for (const auto& v : batch.vertices)
                addVertex(v.position, v.uv, city::SdlShade::Room, 0);
            Prim p;
            p.first = static_cast<std::uint32_t>(m_streetIndices.size());
            p.count = static_cast<std::uint32_t>(batch.indices.size());
            p.slot = slotOf(batch.texture);
            for (const auto i : batch.indices)
                m_streetIndices.push_back(tunnelBase + i);
            for (auto& lod : room.lods)
                lod.push_back(p);
        }
    }
    m_buckets.resize(m_slotNames.size());
    m_vertices = m_device.createBuffer(render::BufferKind::Vertex,
                                       m_streetVertices.size() * sizeof(render::Vertex3D), m_streetVertices.data());

    for (const auto& inst : city.instances) {
        if (isDynamic && isDynamic(inst.name))
            continue;
        InstanceDraw d;
        d.model = inst.name;
        d.transform = inst.transform;
        d.world = Mat44::fromMat34(inst.transform);
        const std::size_t index = m_instances.size();
        // lvlLevel::LoadInstances: a collidable object that is not terrain
        // local goes through lvlMultiRoomInstance::Create, which leaves it in
        // room 0 and puts a stand-in in every neighbour of its room that its
        // sphere (position, the model's radius) reaches across the
        // perimeter; reaching none, it is never drawn.
        constexpr std::uint16_t kInstTerrainLocal = 0x100, kInstBanger = 0x200, kInstCollidable = 0x2000;
        if ((inst.flags & kInstCollidable) && !(inst.flags & (kInstTerrainLocal | kInstBanger))) {
            resolve(d);
            d.multiRoom = true;
            int rooms[32];
            const int n = cityTouchedNeighbors(city.psdl, rooms, 32, inst.room, inst.transform.m3, d.radius);
            m_instances.push_back(std::move(d));
            for (int k = 0; k < n; ++k)
                if (rooms[k] > 0 && static_cast<std::size_t>(rooms[k]) < m_rooms.size())
                    m_rooms[static_cast<std::size_t>(rooms[k])].instances.push_back(index);
            continue;
        }
        m_instances.push_back(std::move(d));
        if (inst.room < m_rooms.size())
            m_rooms[inst.room].instances.push_back(index);
    }
    m_roomMarks.assign(m_rooms.size(), 0);
    if (city.sky)
        m_sky = m_models.get(city.sky->model);
    log::info("city: {} rooms, {} vertices, {} triangles, {} instances", m_rooms.size(), m_streetVertices.size(),
              m_streetIndices.size() / 3, m_instances.size());
}

CityRenderer::~CityRenderer() {
    m_device.destroyBuffer(m_vertices);
}

void CityRenderer::setEnvironment(const Environment& env) {
    // sdlPage16::Draw lights nothing: it colours each primitive with the
    // room colour, half of it (curb faces and caps), or for facades and
    // slivers the light table entry of the room's last FacadeBound shaded by
    // it (GetShadedColor). The room colours come from city/<map>.lmap, but
    // its count never matches the room count cityLevel::Load checks (London
    // 1340 for 1341, SF 1125 for 1171), so MM2 drops it and every room is
    // white: GetShadedColor of white is the light itself.
    constexpr std::uint32_t kRoomColor = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < m_streetVertices.size(); ++i) {
        std::uint32_t argb = kRoomColor;
        switch (m_vertexShade[i]) {
        case city::SdlShade::Wall: argb = env.wallShades[m_vertexLight[i] & 63u]; break;
        case city::SdlShade::HalfRoom: argb = ((kRoomColor >> 1) & 0x7F7F7Fu) | 0xFF000000u; break;
        case city::SdlShade::Room: break;
        }
        m_streetVertices[i].color = argbToRgba(argb);
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
    // A model's PKG xrefs are not drawn with it: lvlLevel::LoadInstances
    // places them as unhit bangers (BangerSet draws them; one without banger
    // data is not placed at all).
    static_cast<void>(depth);
}

void CityRenderer::drawInstance(InstanceDraw& inst, const Frustum& frustum, const Mat34& camera,
                                const DetailSettings& detail) {
    if (inst.multiRoom) {
        // lvlMultiRoomInstance::Draw: once per cityLevel::DrawRooms.
        if (inst.drawnFrame == m_frame)
            return;
        inst.drawnFrame = m_frame;
    }
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

void CityRenderer::gatherStreets(const Room& room, int lod, const Vec3& eye) {
    // sdlPage16::Draw: road fans, crosswalks and roofs are drawn only when
    // they are not above the camera (cityLevel's iso height: the camera's,
    // MM2 adding the view matrix's third row times the near distance), and
    // facades and slivers only from in front (sdlCommon::BACKFACE).
    for (const Prim& p : room.lods[static_cast<std::size_t>(lod)]) {
        if (p.belowCamera && eye.y < p.height)
            continue;
        if (p.wall && city::sdlBackface(eye, p.wall0, p.wall1))
            continue;
        auto& bucket = m_buckets[p.slot];
        const auto first = m_streetIndices.begin() + p.first;
        bucket.insert(bucket.end(), first, first + p.count);
    }
}

void CityRenderer::drawStreets(bool alphaPass) {
    for (std::size_t slot = 0; slot < m_buckets.size(); ++slot) {
        const auto& bucket = m_buckets[slot];
        if (bucket.empty())
            continue;
        // Looked up per frame so day/night texture sets can switch live.
        const WorldTexture* tex = slot == 0 ? nullptr : m_textures.get(m_slotNames[slot]);
        // vglEndBatch draws the textures whose format has no alpha first, with
        // alpha blending (and so the alpha test) off, then the others alpha
        // blended and tested (GREATER 100).
        if ((tex && tex->alphaFormat) != alphaPass)
            continue;
        render::DrawCall call;
        call.vertices = {m_vertices, 0};
        call.indices =
            m_device.uploadTransient(render::BufferKind::Index, std::span<const std::uint32_t>(bucket));
        call.indexType = render::IndexType::U32;
        call.count = static_cast<std::uint32_t>(bucket.size());
        call.first = 0;
        call.constants.color = {1, 1, 1, 1};
        // Street geometry is unlit: the vertex colours carry its shading.
        call.constants.flags = render::DrawFlag::Fog | render::DrawFlag::VertexColor;
        if (tex) {
            call.constants.flags |= render::DrawFlag::Texture0;
            // The texture's own address modes (gfxRenderState::DoFlush).
            call.textures[0] = {tex->handle, tex->sampler};
            if (alphaPass) {
                call.constants.flags |= render::DrawFlag::AlphaTest;
                call.constants.alphaRef = 101.0f / 255.0f;
                call.state.blend = render::BlendMode::Alpha;
            }
        }
        // The render state's default culling (rglOpenPipe, cityLevel::Load:
        // clockwise faces culled, OpenMM2's counter-clockwise front faces as
        // for models); sdlPage16::Draw's strips and fans face up and out.
        call.state.cull = render::CullMode::Back;
        call.state.frontFace = render::FrontFace::CounterClockwise;
        m_device.draw(call);
        ++m_stats.drawCalls;
    }
}

void CityRenderer::draw(const Camera& camera, const Frustum& frustum, const Environment& env,
                        const DetailSettings& detail) {
    m_stats = {};
    ++m_frame;
    drawSky(camera, env);

    const Vec3 eye = camera.position();
    // cityLevel::Draw: the camera's room (cityLevel::FindRoomId from the
    // last one), or the last one found while the camera is outside every
    // room.
    if (const int found = m_locator.find(eye, m_lastRoom); found > 0)
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
        // room whose sphere is in view (MM2 floods out from the camera's
        // room through the neighbours instead).
        std::fill(m_roomMarks.begin() + 1, m_roomMarks.end(), std::uint8_t{1});
    }
    // cityLevel::Draw lists the camera's room and the rooms whose spheres
    // are in view, each with its sphere's depth minus its radius (the
    // camera's room minus its radius). cityLevel::DrawRooms draws their
    // street geometry in one batch at the level of detail that distance
    // picks (opaque textures, then those with alpha), then the static
    // instances room by room from the last room in the list to the first.
    for (auto& bucket : m_buckets)
        bucket.clear();
    for (std::size_t r = 1; r < m_rooms.size(); ++r) {
        if (!m_roomMarks[r])
            continue;
        const Room& rm = m_rooms[r];
        float distance = -rm.radius;
        if (static_cast<int>(r) != room) {
            if (!frustum.intersectsSphere(rm.centre, rm.radius)) {
                m_roomMarks[r] = 0;
                continue;
            }
            distance = viewDepth(camera.transform, rm.centre) - rm.radius;
        }
        gatherStreets(rm, city::sdlRoomLod(distance), eye);
        ++m_stats.roomsDrawn;
    }
    for (bool alphaPass : {false, true})
        drawStreets(alphaPass);
    for (std::size_t r = m_rooms.size(); r-- > 1;)
        if (m_roomMarks[r])
            for (std::size_t i : m_rooms[r].instances)
                drawInstance(m_instances[i], frustum, camera.transform, detail);
}

} // namespace mm2::game
