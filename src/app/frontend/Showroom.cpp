// The garage's turning 3D car: MM2's VehicleSelectBase, asDofCS,
// mmVehicleForm and the frontend camera MenuManager::Init sets up (asCamera
// under an asViewCS). See Showroom.h.
#include "app/frontend/Showroom.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "game/CamMath.h"
#include "game/CityRenderer.h"
#include "game/MeshDraw.h"
#include "render/Device.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace mm2::app::frontend {
namespace {

// mmVehicleForm::SetShape's extra parts: the chunks after the wheels whose
// name, with "_H", is one of these (at most 12, in package order).
constexpr const char* kExtraParts[] = {"break0",  "break1",  "break2",  "break3", "break01", "break12",
                                       "break23", "break03", "fndr0",   "fndr1",  "whl4",    "whl5"};

} // namespace

Showroom::Showroom(render::Device& device, const vfs::Vfs& vfs) : m_device(device), m_vfs(vfs) {
    m_textures = std::make_unique<game::TextureLibrary>(device, vfs);
    m_models = std::make_unique<game::ModelLibrary>(device, vfs);
}

Showroom::~Showroom() = default;

render::Rect Showroom::viewport640() {
    // asCamera::SetViewport(0.05, 0.115, 0.95, 0.4): gfxViewport::SetWindow
    // with the fractions of the screen truncated (ftol).
    return {static_cast<std::int32_t>(0.05f * 640.0f), static_cast<std::int32_t>(0.115f * 480.0f),
            static_cast<std::uint32_t>(0.95f * 640.0f), static_cast<std::uint32_t>(0.4f * 480.0f)};
}

Mat34 Showroom::cameraMatrix(float distance) {
    // asViewCS::Update -> UpdatePolar -> Matrix34::PolarView(distance,
    // azimuth, incline, twist), then the view's offset (+0x98) added to the
    // position. VehicleSelectBase::Update forces azimuth 0, incline 0.18 and
    // twist 0 every frame (MenuManager::Init's 0.534 incline never shows).
    Mat34 m = Mat34::identity();
    game::cam::polarView(m, distance, 0.0f, 0.18f, 0.0f);
    m.m3 += Vec3{0.0f, 0.86f, 0.0f};
    return m;
}

float Showroom::easeDistance(float distance, float goal, float dt) {
    // VehicleSelectBase::Update: the view's distance moves towards the
    // picked car's UIDist by 21 x the frame's seconds, stopping at it.
    const float stepSize = 21.0f * dt;
    if (distance < goal)
        return std::min(goal, distance + stepSize);
    return std::max(goal, distance - stepSize);
}

void Showroom::setCar(const std::string& baseName, float uiDistance) {
    m_car = str::lower(baseName);
    m_goal = uiDistance;
    auto [it, inserted] = m_cars.try_emplace(m_car);
    if (!inserted)
        return;
    // mmVehicleForm::SetShape loads geometry/<car>.pkg (MM2 builds every
    // car's form when the garage is created; OpenMM2 loads a car the first
    // time it is shown).
    std::string error;
    auto read = [this](std::string_view path) { return m_vfs.readAll(path); };
    it->second.model = asset::loadVehicleModel(m_car, read, &error);
    if (!it->second.model) {
        log::warn("showroom: cannot load {}: {}", m_car, error);
        return;
    }
    it->second.gpu = m_models->add(it->second.model->baseName, it->second.model->pkg);
}

Showroom::Car* Showroom::current() {
    const auto it = m_cars.find(m_car);
    return it == m_cars.end() || !it->second.gpu ? nullptr : &it->second;
}

void Showroom::update(float dt) {
    m_distance = easeDistance(m_distance, m_goal, dt);
    // asDofCS::Update (rotate type): the value grows by the rate (1.0 from
    // InitCarSelection and SetPick) times the frame's seconds; only the
    // shown car's node is updated.
    if (Car* car = current())
        car->angle += 1.0f * dt;
    m_textures->update(0.0);
}

std::vector<asset::PkgMaterial> Showroom::materials(const Car& car) const {
    // mmVehicleForm::SetShape: every textured shader of every paint job
    // drops a "_dmg" suffix from its texture (the clean texture is shown)
    // and gets diffuse, ambient and specular alpha 1 and emissive alpha 0.
    std::vector<asset::PkgMaterial> out = car.gpu->materials(m_paintjob);
    for (auto& m : out) {
        if (m.texture.empty())
            continue;
        const auto under = m.texture.rfind('_');
        if (under != std::string::npos && str::iequals(std::string_view(m.texture).substr(under), "_dmg"))
            m.texture.resize(under);
        m.diffuse.w = m.ambient.w = m.specular.w = 1.0f;
        m.emissive.w = 0.0f;
    }
    return out;
}

void Showroom::draw(const render::UiLayout& layout) {
    Car* car = current();
    if (!car)
        return;
    // The viewport in scene pixels: the 640x480 rectangle through the UI
    // layout, then scaled to the scene target (render scale).
    const render::Extent2D out = m_device.outputExtent();
    const render::Extent2D scene = m_device.sceneExtent();
    const float sx = out.width ? static_cast<float>(scene.width) / static_cast<float>(out.width) : 1.0f;
    const float sy = out.height ? static_cast<float>(scene.height) / static_cast<float>(out.height) : 1.0f;
    const render::Rect r = viewport640();
    const Vec2 p0 = layout.toPixels({static_cast<float>(r.x), static_cast<float>(r.y)});
    const Vec2 p1 = layout.toPixels({static_cast<float>(r.x) + static_cast<float>(r.width),
                                     static_cast<float>(r.y) + static_cast<float>(r.height)});
    const render::Viewport vp{p0.x * sx, p0.y * sy, (p1.x - p0.x) * sx, (p1.y - p0.y) * sy, 0.0f, 1.0f};
    if (vp.width < 1.0f || vp.height < 1.0f)
        return;
    const render::Rect scissor{static_cast<std::int32_t>(vp.x), static_cast<std::int32_t>(vp.y),
                               static_cast<std::uint32_t>(vp.width), static_cast<std::uint32_t>(vp.height)};
    m_device.setViewport(vp);
    m_device.setScissor(&scissor);
    render::ClearValues depth;
    depth.clearColor = false;
    depth.clearDepth = true;
    m_device.clear(depth);

    // asCamera::SetView(0.6, 3.2, 1.0, 100.0): the projection's aspect is the
    // one given, not the viewport's (608 / 192), as MM2 has it. No fog.
    // gfxRenderState::Init's Sun, the only light: white, travelling along
    // (-1, -1, -1) (MenuManager::Init); the frontend leaves the ambient at 0.
    const Mat34 camera = cameraMatrix(m_distance);
    render::FrameConstants frame;
    frame.view = Mat44::fromMat34(camera.fastInverse());
    frame.proj = Mat44::perspective(kFovY, kAspect, kNear, kFar, true);
    frame.cameraPosition = camera.m3;
    frame.fogMode = render::FogMode::None;
    frame.ambient = {0.0f, 0.0f, 0.0f};
    frame.lights[0].direction = Vec3{-1.0f, -1.0f, -1.0f}.normalized();
    frame.lights[0].color = {1.0f, 1.0f, 1.0f};
    m_device.setFrameConstants(frame);

    // mmVehicleForm::Cull at the car's node matrix (asDofCS: a rotation
    // about Y at the origin).
    const Mat34 body = Mat34::rotationY(car->angle);
    const auto mats = materials(*car);
    const auto& gpu = *car->gpu;
    game::MeshDrawOptions opts;
    opts.fog = false;
    // The frontend never runs cityLevel::DrawRooms (GREATER 100), so the
    // device default from gfxRenderState::Default holds: alpha reference 0,
    // NOTEQUAL ("alpha not 0"). Inferred: nothing in the frontend changes it.
    opts.alphaRef = 1.0f / 255.0f;
    auto drawPart = [&](std::string_view part, const Mat34& world, const game::MeshDrawOptions& o) {
        const game::GpuMesh* mesh = game::findFilledLod(gpu, part, asset::Lod::High);
        if (mesh && str::iequals(mesh->part, part))
            game::drawGpuMesh(m_device, *m_textures, *mesh, mats, Mat44::fromMat34(world), o);
    };
    // 1-2: SHADOW_H at the car's own matrix (as modelled under the car,
    // lit, depth-tested and alpha blended like the body: mmVehicleForm::Cull
    // turns blending on and leaves depth writes alone), then BODY_H.
    game::MeshDrawOptions shadow = opts;
    shadow.blend = render::BlendMode::Alpha;
    drawPart("SHADOW", body, shadow);
    drawPart("BODY", body, opts);
    // 3: the body again with refl_showroom (modShader::BeginEnvMap +
    // modStatic::DrawEnvMapped, intensity 1): each section added in the grey
    // of its material's power.
    if (const game::WorldTexture* env = m_textures->get("refl_showroom")) {
        if (const game::GpuMesh* mesh = game::findFilledLod(gpu, "BODY", asset::Lod::High)) {
            for (const auto& d : mesh->draws) {
                const float power = d.shader < mats.size() ? mats[d.shader].shininess : 0.0f;
                const int grey = static_cast<int>(power * 255.0f);
                if (grey == 0)
                    continue;
                const float level = static_cast<float>(grey & 0xFF) / 255.0f;
                render::DrawCall call;
                call.vertices = {mesh->vertices, 0};
                call.indices = {mesh->indices, 0};
                call.count = d.indexCount;
                call.first = d.firstIndex;
                call.baseVertex = d.baseVertex;
                call.constants.world = Mat44::fromMat34(body);
                call.constants.color = {level, level, level, 1.0f};
                call.constants.flags = render::DrawFlag::Texture1 | render::DrawFlag::EnvMap1;
                call.textures[1] = {env->handle, env->sampler};
                call.state.blend = render::BlendMode::Add;
                call.state.depthWrite = false;
                call.state.depthCompare = render::CompareOp::LessEqual;
                call.state.cull = render::CullMode::Back;
                m_device.draw(call);
            }
        }
    }
    // 4: the four wheels at their pivots (no spin, steer or drop).
    const auto& model = *car->model;
    for (int i = 0; i < 4; ++i)
        if (const auto* w = model.wheel(i))
            drawPart(std::format("WHL{}", i), Mat34::translation(w->position) * body, opts);
    // 5: the extra parts in package order, each at its pivot, or at the
    // origin without one. Only the high LOD (`<name>_H`) counts.
    int extras = 0;
    for (const auto& mesh : gpu.meshes) {
        if (extras == 12)
            break;
        if (mesh.lod != asset::Lod::High || mesh.draws.empty())
            continue;
        const auto it = std::ranges::find_if(kExtraParts, [&](const char* n) { return str::iequals(mesh.part, n); });
        if (it == std::end(kExtraParts))
            continue;
        ++extras;
        const auto* pivot = model.pivot(*it);
        const Mat34 world = Mat34::translation(pivot ? pivot->origin : Vec3{}) * body;
        game::drawGpuMesh(m_device, *m_textures, mesh, mats, Mat44::fromMat34(world), opts);
    }
    m_device.setScissor(nullptr);
}

} // namespace mm2::app::frontend
