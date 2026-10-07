#include "game/session/Hud.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "data/DatFile.h"
#include "render/Projection.h"

#include <cmath>
#include <format>

namespace mm2::game::session {
namespace {

// The full-size HUD art (speed.tga 194x110, digits, gauges) is twice the
// size of the *_half variants; drawn into the 640x480 UI space at half
// size, as if authored for 1280x960 (inferred).
constexpr float kArt = 0.5f;

std::optional<data::DatFile> readDat(const vfs::Vfs& vfs, const std::string& path) {
    auto bytes = vfs.readAll(path);
    if (!bytes)
        return std::nullopt;
    return data::parseDat(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
}

// Draws a mesh without depth writes (HUD geometry: arrow, map, icons), or,
// with `prop`, as a lit, fogged, back-face-culled world object.
void drawFlat(render::Device& dev, TextureLibrary& textures, const GpuMesh& mesh,
              const std::vector<asset::PkgMaterial>& mats, const Mat34& world, float alpha, bool depthTest,
              bool prop = false) {
    for (const auto& d : mesh.draws) {
        const asset::PkgMaterial* mat = d.shader < mats.size() ? &mats[d.shader] : nullptr;
        const WorldTexture* tex = mat && !mat->texture.empty() ? textures.get(mat->texture) : nullptr;
        render::DrawCall call;
        call.vertices = {mesh.vertices, 0};
        call.indices = {mesh.indices, 0};
        call.indexType = render::IndexType::U16;
        call.count = d.indexCount;
        call.first = d.firstIndex;
        call.baseVertex = d.baseVertex;
        call.constants.world = Mat44::fromMat34(world);
        Vec4 c = mat ? mat->diffuse : Vec4{1, 1, 1, 1};
        c.w *= alpha;
        call.constants.color = c;
        call.constants.flags = render::DrawFlag::VertexColor;
        if (tex) {
            call.constants.flags |= render::DrawFlag::Texture0;
            call.textures[0] = {tex->handle, tex->sampler};
        }
        if (c.w < 0.999f || (tex && tex->translucent)) {
            call.state.blend = render::BlendMode::Alpha;
            call.constants.flags |= render::DrawFlag::AlphaTest;
            call.constants.alphaRef = 0.02f;
        }
        call.state.cull = render::CullMode::None;
        call.state.depthTest = depthTest;
        call.state.depthWrite = false;
        if (prop) {
            // pt_check's banner is two coplanar quads wound opposite ways:
            // without culling they z-fight and the text reads mirrored.
            call.constants.flags |= render::DrawFlag::Lighting | render::DrawFlag::Fog;
            call.state.cull = render::CullMode::Back;
            call.state.depthWrite = true;
        }
        dev.draw(call);
    }
}

void shadowText(render::Overlay2D& ov, ui::TextRenderer& text, const ui::FontSpec& f, std::string_view s, float x,
                float y, std::uint32_t color, ui::Align align = ui::Align::Left) {
    text.draw(ov, f, s, x + 1.0f, y + 1.0f, render::packColor(0, 0, 0, 200), align);
    text.draw(ov, f, s, x, y, color, align);
}

// Splits "line one \n line two" (the string table uses a literal backslash-n).
std::vector<std::string> splitMessage(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\n' || (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n')) {
            out.emplace_back(str::trim(cur));
            cur.clear();
            if (s[i] == '\\')
                ++i;
        } else {
            cur.push_back(s[i]);
        }
    }
    out.emplace_back(str::trim(cur));
    return out;
}

} // namespace

HudMapParams loadHudMapParams(const vfs::Vfs& vfs, const std::string& city) {
    HudMapParams p;
    auto f = readDat(vfs, "tune/" + city + ".mmhudmap");
    if (!f || !f->top())
        return p;
    const data::DatNode& n = *f->top();
    n.read("Size", p.size);
    n.read("Pos", p.pos);
    if (auto z = n.getInt("ZoomIn"))
        p.zoomIn = *z != 0;
    n.read("Approach Rate", p.approachRate);
    n.read("ZoomInDist", p.zoomInDist);
    n.read("ZoomOutDist", p.zoomOutDist);
    n.read("IconScaleMin", p.iconScaleMin);
    n.read("IconScaleMax", p.iconScaleMax);
    n.read("ZoomInDistFS", p.zoomInDistFS);
    n.read("ZoomOutDistFS", p.zoomOutDistFS);
    n.read("IconScaleMinFS", p.iconScaleMinFS);
    n.read("IconScaleMaxFS", p.iconScaleMaxFS);
    n.read("Ocean Color", p.oceanColor);
    return p;
}

std::optional<DashParams> loadDashParams(const vfs::Vfs& vfs, const std::string& car) {
    auto f = readDat(vfs, "tune/" + car + "_dash.asnode");
    if (!f || !f->top())
        return std::nullopt;
    const data::DatNode& n = *f->top();
    DashParams d;
    n.read("DashPos", d.dashPos);
    n.read("RoofPos", d.roofPos);
    n.read("WheelPos", d.wheelPos);
    n.read("DmgOffset", d.dmgOffset);
    n.read("SpeedOffset", d.speedOffset);
    n.read("TachOffset", d.tachOffset);
    n.read("DmgPivotOffset", d.dmgPivotOffset);
    n.read("SpeedPivotOffset", d.speedPivotOffset);
    n.read("TachPivotOffset", d.tachPivotOffset);
    n.read("WheelPivotOffset", d.wheelPivotOffset);
    n.read("GearPivotOffset", d.gearPivotOffset);
    n.read("WheelFact", d.wheelFact);
    n.read("RPMRotMin", d.rpmRotMin);
    n.read("RPMRotMax", d.rpmRotMax);
    n.read("SpeedRotMin", d.speedRotMin);
    n.read("SpeedRotMax", d.speedRotMax);
    n.read("DamageRotMin", d.damageRotMin);
    n.read("DamageRotMax", d.damageRotMax);
    return d;
}

Hud::Hud(render::Device& device, TextureLibrary& textures, ModelLibrary& models, const vfs::Vfs& vfs,
         const Strings& strings, const std::string& city, const std::string& vehicle)
    : m_device(device), m_textures(textures), m_models(models), m_strings(strings), m_city(str::lower(city)),
      m_vehicle(str::lower(vehicle)) {
    m_map = loadHudMapParams(vfs, m_city);
    m_options.zoomedIn = m_map.zoomIn;
    m_dash = loadDashParams(vfs, m_vehicle);
}

void Hud::preload(ui::TextureCache* art) {
    for (const char* name : {"pt_check", "pt_finish", "hudarrow01", "hudarrow_blitz01", "hudarrow_cc01", "hudmap_square",
                             "hudmap_tri"}) {
        if (const GpuModel* m = m_models.get(name))
            for (const auto& pj : m->paintjobs)
                for (const auto& mat : pj)
                    m_textures.get(mat.texture);
    }
    for (const std::string& name : {"hudmap_" + m_city, m_vehicle + "_dash"}) {
        if (const GpuModel* m = m_models.get(name))
            for (const auto& pj : m->paintjobs)
                for (const auto& mat : pj)
                    m_textures.get(mat.texture);
    }
    if (art) {
        for (const char* t : {"speed.tga", "tacometer ticks_half.tga", "mph.tga", "damage.tga", "damage_lable.tga",
                              "digi_colon.tga"})
            art->get(std::string("texture/") + t);
        for (char c = '0'; c <= '9'; ++c) {
            art->get(std::format("texture/digitac_{}_half.tga", c));
            art->get(std::format("texture/digi_{}.tga", c));
            art->get(std::format("texture/digi_{}_half.tga", c));
        }
        for (const char* g : {"r", "n", "d", "p", "1", "2", "3", "4", "5", "6", "7", "8"})
            art->get(std::format("texture/digitac_gear_{}.tga", g));
    }
}

ui::FontSpec Hud::font(std::uint32_t id, const char* fallback) const {
    auto spec = ui::FontSpec::parse(m_strings.get(id, fallback));
    if (!spec)
        spec = ui::FontSpec::parse(fallback);
    // HUD fonts: the first number reads as the cell height at 640x480; the
    // second (twice as large for most entries) would fill the screen
    // (inferred).
    ui::FontSpec f = spec.value_or(ui::FontSpec{});
    f.size2 = f.size;
    return f;
}

// --- 3D: checkpoint stands, arrow, dashboard ---------------------------------

void Hud::drawWorld(const Session& session, const Camera& camera, const PlayerState& player, float steering) {
    const auto& cps = session.checkpoints();
    const bool crash = session.mode() == GameMode::CrashCourse;
    for (std::size_t i = 0; i < cps.size(); ++i) {
        if (!session.checkpointVisible(i))
            continue;
        const Checkpoint& cp = cps[i];
        // MM2 replaced MM1's ring/arrow/flag waypoint object with pt_check /
        // pt_finish: an arch of two posts and a CHECKPOINT / FINISH banner
        // (2 x 1 units). It is scaled by the waypoint radius to span the
        // gate, stands on the road and shows its banner to the approaching
        // driver (scale and placement inferred; MM1's mmWaypointInstance
        // likewise carries the radius).
        const GpuModel* model = m_models.get(cp.finish ? "pt_finish" : "pt_check");
        const GpuMesh* mesh = model ? model->find("", asset::Lod::Low) : nullptr;
        if (!mesh)
            continue;
        const int paintjob = crash ? 1 : 0; // CheckpointStand / CCStand
        const float r = cp.radius;
        Mat34 m = Mat34::rotationY(-cp.headingDeg * kDegToRad);
        m.m0 *= r;
        m.m1 *= r;
        m.m2 *= r;
        m.m3 = cp.position + Vec3{0.0f, 0.5f * r, 0.0f};
        drawFlat(m_device, m_textures, *mesh, model->materials(paintjob), m, 1.0f, true, true);
    }

    // Arrow (mmArrow::Update): 2 m above and 6.1 m ahead of the camera,
    // turned toward the target in the horizontal plane, tilted 20 degrees,
    // half transparent, drawn over everything; yellow when the target is
    // behind.
    if (auto target = session.arrowTarget(); target && !m_options.dashboard) {
        const char* name = session.mode() == GameMode::Blitz         ? "hudarrow_blitz01"
                           : session.mode() == GameMode::CrashCourse ? "hudarrow_cc01"
                                                                     : "hudarrow01";
        const GpuModel* model = m_models.get(name);
        const GpuMesh* mesh = model ? model->find("", asset::Lod::High) : nullptr;
        if (mesh) {
            const Mat34& cam = camera.transform;
            const Vec3 flat{target->x, cam.m3.y, target->z};
            Vec3 dir = cam.untransform(flat);
            dir.y = 0.0f;
            dir = dir.mag2() > 1e-6f ? dir.normalized() : Vec3{0, 0, -1};
            Mat34 local;
            local.m2 = -dir;
            local.m0 = Vec3::yAxis().cross(local.m2).normalized();
            local.m1 = local.m2.cross(local.m0);
            // Then Matrix34::Rotate(XAXIS, arotX = -20 degrees): about the
            // camera's X axis, after orienting. Far parts dip, so the arrow
            // faces the camera more (it floats above eye level).
            local = local * Mat34::rotationX(-20.0f * kDegToRad);
            local.m3 = {0.0f, 2.0f, -6.1f};
            const int paintjob = dir.z > 0.0f ? 1 : 0; // target behind the camera
            drawFlat(m_device, m_textures, *mesh, model->materials(paintjob), local * cam, 0.5f, false);
        }
    }

    if (m_options.dashboard)
        drawDash(camera, player, steering);
}

void Hud::drawDash(const Camera& camera, const PlayerState& player, float steering) {
    // mmDashView: the <car>_dash model in camera space at DashPos; needles
    // (RadialGauge) turn about Z by RotMin + value / max * (RotMax - RotMin),
    // clamped. Pivot placement inferred: the needle's end at PivotOffset.
    const GpuModel* model = m_models.get(m_vehicle + "_dash");
    if (!model || !m_dash)
        return;
    const DashParams& d = *m_dash;
    const Mat34& cam = camera.transform;
    auto draw = [&](std::string_view part, const Mat34& local) {
        if (const GpuMesh* mesh = model->find(part, asset::Lod::High))
            drawFlat(m_device, m_textures, *mesh, model->materials(0), local * cam, 1.0f, true);
    };
    const Mat34 dash = Mat34::translation(d.dashPos);
    draw("dash", dash);
    draw("roof", Mat34::translation(d.roofPos));
    auto needle = [&](std::string_view part, float value, float maxValue, float rotMin, float rotMax,
                      const Vec3& pivotOffset) {
        const GpuMesh* mesh = model->find(part, asset::Lod::High);
        if (!mesh)
            return;
        const float frac = maxValue > 0.0f ? clampf(value / maxValue, 0.0f, 1.0f) : 0.0f;
        const float rot = rotMin + frac * (rotMax - rotMin);
        const Vec3 pivot{mesh->bounds.max.x - pivotOffset.x, mesh->bounds.center().y, 0.0f};
        const Mat34 m = Mat34::translation(-pivot) * Mat34::rotationZ(-rot) * Mat34::translation(pivot) * dash;
        if (const GpuMesh* mm = mesh)
            drawFlat(m_device, m_textures, *mm, model->materials(0), m * cam, 1.0f, true);
    };
    needle("speed_needle", player.speedMph, 160.0f, d.speedRotMin, d.speedRotMax, d.speedPivotOffset);
    needle("tach_needle", player.rpm, player.maxRpm, d.rpmRotMin, d.rpmRotMax, d.tachPivotOffset);
    needle("damage_needle", player.damage01, 1.0f, d.damageRotMin, d.damageRotMax, d.dmgPivotOffset);
    if (const GpuMesh* wheel = model->find("wheel", asset::Lod::High)) {
        const Vec3 c = wheel->bounds.center() + d.wheelPivotOffset;
        const Mat34 m = Mat34::translation(-c) * Mat34::rotationZ(-steering * d.wheelFact) * Mat34::translation(c) *
                        Mat34::translation(d.wheelPos) * dash;
        drawFlat(m_device, m_textures, *wheel, model->materials(0), m * cam, 1.0f, true);
    }
}

// --- Map -----------------------------------------------------------------------

Vec4 Hud::mapRect(const render::UiLayout& l) const {
    if (m_options.fullScreenMap)
        return {l.left + 40.0f, l.top + 40.0f, (l.right - l.left) - 80.0f, (l.bottom - l.top) - 80.0f};
    const float w = l.right - l.left, h = l.bottom - l.top;
    return {l.left + m_map.pos.x * w, l.top + m_map.pos.y * h, m_map.size.x * w, m_map.size.y * h};
}

void Hud::drawMap(const Session& session, const PlayerState& player, std::span<const MapBlip> blips, float dt) {
    if (!m_options.showMap)
        return;
    const GpuModel* mapModel = m_models.get("hudmap_" + m_city);
    const GpuMesh* mapMesh = mapModel ? mapModel->find("", asset::Lod::High) : nullptr;
    if (!mapMesh)
        return;

    const render::Extent2D scene = m_device.sceneExtent();
    const render::UiLayout layout = render::computeUiLayout(scene, m_options.uiScale);
    const Vec4 r = mapRect(layout);
    const Vec2 p0 = layout.toPixels({r.x, r.y});
    const float pw = r.z * layout.scaleX, ph = r.w * layout.scaleY;
    if (pw < 2.0f || ph < 2.0f)
        return;
    m_device.setViewport({p0.x, p0.y, pw, ph, 0.0f, 1.0f});
    const render::Rect scissor{static_cast<std::int32_t>(p0.x), static_cast<std::int32_t>(p0.y),
                               static_cast<std::uint32_t>(pw), static_cast<std::uint32_t>(ph)};
    m_device.setScissor(&scissor);
    render::ClearValues clear;
    clear.color = {m_map.oceanColor.x, m_map.oceanColor.y, m_map.oceanColor.z, 1.0f};
    m_device.clear(clear);

    // Zoom eases toward the chosen distance at "Approach Rate".
    const bool fs = m_options.fullScreenMap;
    const float zIn = fs ? m_map.zoomInDistFS : m_map.zoomInDist, zOut = fs ? m_map.zoomOutDistFS : m_map.zoomOutDist;
    const float target = m_options.zoomedIn ? zIn : zOut;
    if (m_mapZoom <= 0.0f)
        m_mapZoom = target;
    m_mapZoom += (target - m_mapZoom) * std::min(1.0f, m_map.approachRate * dt);
    const float t = zOut > zIn ? clampf((m_mapZoom - zIn) / (zOut - zIn), 0.0f, 1.0f) : 0.0f;
    const float iconSize = fs ? lerp(m_map.iconScaleMinFS, m_map.iconScaleMaxFS, t)
                              : lerp(m_map.iconScaleMin, m_map.iconScaleMax, t);

    // Top-down camera over the player; up = the car's heading (rotating map)
    // or north, +Z: the race preview maps (jpg/<city>_map*.jpg) are north-up
    // and put e.g. London Blitz 1's finish (z = +150) above its start
    // (z = -295). The map mesh is in true world coordinates (verified by
    // test: the Thames' street triangles land on the map's water).
    const Vec3 at = player.transform.m3;
    Vec3 up{0, 0, 1};
    if (m_options.rotatingMap) {
        const Vec3 f = -player.transform.m2;
        const Vec3 flat{f.x, 0, f.z};
        if (flat.mag2() > 1e-6f)
            up = flat.normalized();
    }
    Mat34 camM;
    camM.m2 = Vec3::yAxis();       // back: the camera looks down -Y
    camM.m1 = up;                  // screen up
    camM.m0 = camM.m1.cross(camM.m2).normalized();
    camM.m3 = at + Vec3{0, 1000.0f, 0};
    render::FrameConstants fc;
    fc.view = Mat44::fromMat34(camM.fastInverse());
    const float halfH = m_mapZoom * 0.5f, halfW = halfH * (pw / ph);
    fc.proj = Mat44::orthographic(-halfW, halfW, -halfH, halfH, 1.0f, 3000.0f, true);
    fc.fogMode = render::FogMode::None;
    fc.ambient = {1, 1, 1};
    m_device.setFrameConstants(fc);

    drawFlat(m_device, m_textures, *mapMesh, mapModel->materials(0), Mat34::identity(), 1.0f, false);

    // Icons: squares for places, triangles for cars. IconScale is read as the
    // icon's size in metres (the square model is 14 m wide; inferred).
    const GpuModel* square = m_models.get("hudmap_square");
    const GpuModel* tri = m_models.get("hudmap_tri");
    auto place = [&](const Vec3& p, int paintjob) {
        const GpuMesh* mesh = square ? square->find("", asset::Lod::High) : nullptr;
        if (!mesh)
            return;
        const float s = iconSize / 14.0f;
        Mat34 m = Mat34::identity();
        m.m0 = {s, 0, 0};
        m.m1 = {0, s, 0};
        m.m2 = {0, 0, s};
        m.m3 = {p.x, 1.0f, p.z};
        drawFlat(m_device, m_textures, *mesh, square->materials(paintjob), m, 1.0f, false);
    };
    auto car = [&](const Mat34& tm, int paintjob) {
        const GpuMesh* mesh = tri ? tri->find("", asset::Lod::High) : nullptr;
        if (!mesh)
            return;
        const float s = iconSize / 28.0f;
        const Vec3 f = -tm.m2;
        const float yaw = std::atan2(-f.x, -f.z);
        Mat34 m = Mat34::rotationY(yaw);
        m.m0 *= s;
        m.m1 *= s;
        m.m2 *= s;
        m.m3 = {tm.m3.x, 2.0f, tm.m3.z};
        drawFlat(m_device, m_textures, *mesh, tri->materials(paintjob), m, 1.0f, false);
    };
    const auto& cps = session.checkpoints();
    for (std::size_t i = 0; i < cps.size(); ++i)
        if (session.checkpointVisible(i))
            place(cps[i].position, cps[i].finish ? 5 : 2); // FINISH_DOT / GREEN_DOT
    for (const auto& b : blips) {
        // Colours from hudmap_tri's paint jobs: red opponents, blue police,
        // grey traffic, green teammates (inferred).
        const int pj = b.kind == MapBlip::Kind::Police    ? 2
                       : b.kind == MapBlip::Kind::Ambient ? 9
                       : b.kind == MapBlip::Kind::Teammate ? 3
                                                           : 4;
        car(b.transform, pj);
    }
    car(player.transform, 5); // the player: yellow (inferred)

    const render::Extent2D out = scene;
    m_device.setViewport({0, 0, static_cast<float>(out.width), static_cast<float>(out.height), 0, 1});
    m_device.setScissor(nullptr);
}

// --- 2D overlay -------------------------------------------------------------------

void Hud::drawCluster(render::Overlay2D& ov, ui::TextureCache& art, const PlayerState& player, float x, float y) {
    const ui::UiTexture& panel = art.get("texture/speed.tga");
    ui::drawImage(ov, panel, x, y, 194 * kArt, 110 * kArt);

    // Tachometer LEDs in the top slot (10,9.5)-(153,22.5) of speed.tga,
    // lit up to the current rpm.
    const ui::UiTexture& ticks = art.get("texture/tacometer ticks_half.tga");
    const float frac = player.maxRpm > 0.0f ? clampf(player.rpm / player.maxRpm, 0.0f, 1.0f) : 0.0f;
    if (ticks && frac > 0.0f)
        ov.image(ticks.handle, x + 10 * kArt, y + 9.5f * kArt, 143 * kArt * frac, 13 * kArt,
                 {0.0f, ui::UiTexture::uvTop}, {frac, ui::UiTexture::uvBottom});

    // Speed: three green digits in the left window (10,31)-(71,74.5).
    const int speed = static_cast<int>(std::lround(m_options.metric ? player.speedMph * 1.609344f : player.speedMph));
    const std::string digits = std::format("{:>3}", std::clamp(speed, 0, 999));
    const float dw = 20 * kArt, dh = 27 * kArt;
    const float wx = x + 10 * kArt, wy = y + 31 * kArt, ww = 61 * kArt, wh = 43.5f * kArt;
    const float startX = wx + ww - 3 * dw - 1.0f, digitY = wy + (wh - dh) * 0.35f;
    for (int i = 0; i < 3; ++i) {
        if (digits[static_cast<std::size_t>(i)] == ' ')
            continue;
        const auto& tex = art.get(std::format("texture/digitac_{}_half.tga", digits[static_cast<std::size_t>(i)]));
        ui::drawImage(ov, tex, startX + static_cast<float>(i) * dw, digitY, dw, dh);
    }
    // "MPH" label (stored upside down in mph.tga, so drawn without the flip).
    if (!m_options.metric) {
        const ui::UiTexture& mph = art.get("texture/mph.tga");
        if (mph)
            ov.image(mph.handle, wx + ww - 23 * kArt - 2, wy + wh - 7 * kArt - 1.5f, 23 * kArt, 7 * kArt, {0, 0},
                     {1, 1});
    }

    // Gear in the right window (128,31)-(153,54).
    std::string gear;
    if (player.gear < 0)
        gear = "r";
    else if (player.gear == 0)
        gear = "n";
    else if (player.automatic)
        gear = "d";
    else
        gear = std::to_string(std::min(player.gear, 8));
    ui::drawImage(ov, art.get(std::format("texture/digitac_gear_{}.tga", gear)), x + 130 * kArt, y + 31 * kArt,
                  20 * kArt, 23 * kArt);
}

void Hud::drawClock(render::Overlay2D& ov, ui::TextureCache& art, float seconds, float right, float y) {
    seconds = std::max(seconds, 0.0f);
    const int total = static_cast<int>(seconds);
    const int hundredths = static_cast<int>((seconds - static_cast<float>(total)) * 100.0f);
    const std::string mmss = std::format("{}:{:02}", total / 60, total % 60);
    const float dw = 21 * kArt, dh = 32 * kArt, cw = 10 * kArt;
    const float hw = 11 * kArt, hh = 16 * kArt;
    float width = 0.0f;
    for (char c : mmss)
        width += c == ':' ? cw : dw;
    width += 2.0f + 2 * hw;
    float x = right - width;
    for (char c : mmss) {
        if (c == ':') {
            ui::drawImage(ov, art.get("texture/digi_colon.tga"), x, y, cw, dh);
            x += cw;
        } else {
            ui::drawImage(ov, art.get(std::format("texture/digi_{}.tga", c)), x, y, dw, dh);
            x += dw;
        }
    }
    x += 2.0f;
    const std::string hs = std::format("{:02}", hundredths);
    for (char c : hs) {
        ui::drawImage(ov, art.get(std::format("texture/digi_{}_half.tga", c)), x, y + dh - hh, hw, hh);
        x += hw;
    }
}

void Hud::drawOverlay(render::Overlay2D& ov, ui::TextRenderer& text, ui::TextureCache& art, const Session& session,
                      const PlayerState& player) {
    ov.begin(m_options.uiScale);
    const render::UiLayout& l = ov.layout();
    const std::uint32_t white = render::packColor(255, 255, 255);
    const std::uint32_t yellow = render::packColor(255, 230, 40);

    if (!m_options.dashboard) {
        // Instrument cluster bottom-left, damage meter above it.
        const float cx = l.left + 6.0f, cy = l.bottom - 110 * kArt - 4.0f;
        drawCluster(ov, art, player, cx, cy);
        const ui::UiTexture& label = art.get("texture/damage_lable.tga");
        ui::drawImage(ov, label, cx, cy - 16.0f, 129 * kArt, 12 * kArt);
        const ui::UiTexture& bar = art.get("texture/damage.tga");
        const float dmg = clampf(player.damage01, 0.0f, 1.0f);
        if (bar && dmg > 0.0f) {
            const float bw = 194 * kArt * dmg;
            ov.image(bar.handle, cx, cy - 9.0f, bw, 12 * kArt * 0.8f, {0.0f, ui::UiTexture::uvTop},
                     {dmg, ui::UiTexture::uvBottom});
        }
    }

    // Race clock top right: the count-down where there is one, otherwise
    // the race time (lap time in circuits).
    const GameMode mode = session.mode();
    if (mode != GameMode::Cruise) {
        const float remaining = session.timeRemaining();
        const float shown = remaining >= 0.0f ? remaining : (mode == GameMode::Circuit ? session.lapTime() : session.raceTime());
        drawClock(ov, art, session.phase() == Phase::Countdown ? (remaining >= 0.0f ? remaining : 0.0f) : shown,
                  l.right - 8.0f, l.top + 8.0f);
    }

    // Position / checkpoint / lap readouts, top left.
    if (m_options.showPosition && mode != GameMode::Cruise) {
        const ui::FontSpec labelFont = font(251, "Gill Sans MT, 16, 22, 0, 700");
        const ui::FontSpec valueFont = font(252, "Gill Sans MT, 20, 40, 0, 700");
        float y = l.top + 8.0f;
        auto line = [&](std::uint32_t id, const char* fallback, const std::string& value) {
            const std::string lab(str::trim(m_strings.get(id, fallback)));
            const float w = text.measure(ov, labelFont, lab);
            shadowText(ov, text, labelFont, lab, l.left + 10.0f, y + 3.0f, white);
            shadowText(ov, text, valueFont, value, l.left + 14.0f + w, y, yellow);
            y += 24.0f;
        };
        if (mode == GameMode::Circuit || mode == GameMode::Checkpoint)
            line(254, "Place:", std::format("{}/{}", session.position(), session.racerCount()));
        if (mode != GameMode::Circuit)
            line(255, "Check:", std::format("{}/{}", session.checkpointsCleared(), session.checkpointsTotal()));
        if (mode == GameMode::Circuit) {
            line(260, "Check:", std::format("{}/{}", session.checkpointsCleared(), session.checkpointsTotal()));
            line(261, "Lap:", std::format("{}/{}", session.lap(), session.laps()));
        }
        if (const auto* lesson = session.currentLesson(); lesson && lesson->type == LessonType::Clean) {
            const ui::FontSpec small = font(262, "Gill Sans MT, 12, 20, 0, 700");
            shadowText(ov, text, small, std::format("{}{}", m_strings.get(269, "Hit Objects:  "), session.objectHits()),
                       l.left + 10.0f, y, white);
            shadowText(ov, text, small,
                       std::format("{}{}", m_strings.get(270, "Hit Vehicles:  "), session.vehicleHits()),
                       l.left + 10.0f, y + 16.0f, white);
        }
    }

    // Messages: the upper line, then the centre line (Ready... Set... Go!).
    const ui::FontSpec small = font(562, "Arial Bold, 18, 24, 0, 400");
    const ui::FontSpec big = font(564, "Arial Bold, 24, 48, 0, 400");
    if (const HudMessage& m = session.message2(); m.timeLeft > 0.0f && !m.text.empty()) {
        float y = l.top + 96.0f;
        for (const auto& lineText : splitMessage(m.text)) {
            shadowText(ov, text, small, lineText, 320.0f, y, white, ui::Align::Center);
            y += 22.0f;
        }
    }
    if (const HudMessage& m = session.message(); m.timeLeft > 0.0f && !m.text.empty()) {
        float y = 160.0f;
        for (const auto& lineText : splitMessage(m.text)) {
            shadowText(ov, text, big, lineText, 320.0f, y, yellow, ui::Align::Center);
            y += 30.0f;
        }
    }

    // Map frame (the map itself is drawn in the scene pass).
    if (m_options.showMap) {
        const Vec4 r = mapRect(l);
        const std::uint32_t frame = render::packColor(0, 0, 0, 255);
        ov.rect(r.x - 1, r.y - 1, r.z + 2, 1, frame);
        ov.rect(r.x - 1, r.y + r.w, r.z + 2, 1, frame);
        ov.rect(r.x - 1, r.y, 1, r.w, frame);
        ov.rect(r.x + r.z, r.y, 1, r.w, frame);
    }
    ov.end();
}

} // namespace mm2::game::session
