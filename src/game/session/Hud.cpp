#include "game/session/Hud.h"

#include "asset/Mtx.h"
#include "core/StringUtil.h"
#include "data/DatFile.h"
#include "game/Catalog.h"
#include "render/Projection.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace mm2::game::session {
namespace {

std::optional<data::DatFile> readDat(const vfs::Vfs& vfs, const std::string& path) {
    auto bytes = vfs.readAll(path);
    if (!bytes)
        return std::nullopt;
    return data::parseDat(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
}

// Draws a mesh without depth writes (HUD geometry: arrow, map, icons,
// dashboard), or, with `prop`, as an unlit, fogged, back-face-culled world
// object (the checkpoint stands, drawn pre-lit by mmCheckpointInstance).
void drawFlat(render::Device& dev, TextureLibrary& textures, const GpuMesh& mesh,
              const std::vector<asset::PkgMaterial>& mats, const Mat34& world, bool depthTest,
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
        const Vec4 c = mat ? mat->diffuse : Vec4{1, 1, 1, 1};
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
            call.constants.flags |= render::DrawFlag::Fog;
            call.state.cull = render::CullMode::Back;
            call.state.depthWrite = true;
        }
        dev.draw(call);
    }
}

// 0xAARRGGBB as the shader's colour.
Vec4 argbColor(std::uint32_t argb) {
    auto ch = [&](int shift) { return static_cast<float>((argb >> shift) & 0xFFu) / 255.0f; };
    return {ch(16), ch(8), ch(0), ch(24)};
}

// mmTextNode::RenderText with DT_WORDBREAK: greedy word wrap within `width`,
// also breaking at "\n" (the string table spells some as a literal
// backslash-n).
std::vector<std::string> wrapText(render::Overlay2D& ov, ui::TextRenderer& text, const ui::FontSpec& f,
                                  std::string_view s, float width) {
    std::vector<std::string> paragraphs(1);
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\n' || (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n')) {
            paragraphs.emplace_back();
            if (s[i] == '\\')
                ++i;
        } else {
            paragraphs.back().push_back(s[i]);
        }
    }
    std::vector<std::string> lines;
    for (const auto& p : paragraphs) {
        std::string line;
        for (const std::string_view word : str::split(str::trim(p), ' ')) {
            if (word.empty())
                continue;
            const std::string candidate = line.empty() ? std::string(word) : line + " " + std::string(word);
            if (!line.empty() && text.measure(ov, f, candidate) > width) {
                lines.push_back(line);
                line = word;
            } else {
                line = candidate;
            }
        }
        lines.push_back(line);
    }
    return lines;
}

// mmHudMap::DrawWaypoints: hudmap_square paint jobs.
enum MapDot : int { kDotGreen = 2, kDotGrey = 3, kDotYellow = 4, kDotFinish = 5 };

// mmTextNode colours (COLORREF from the Vector4 passed to SetFGColor).
constexpr std::uint32_t kLabelColor = render::packColor(127, 255, 127); // (0.5, 1, 0.5)
constexpr std::uint32_t kNumberColor = render::packColor(255, 255, 255);
constexpr std::uint32_t kMessageColor = render::packColor(255, 255, 0); // (1, 1, 0)
constexpr std::uint32_t kShadowColor = render::packColor(15, 15, 15);   // 0x0f0f0f

} // namespace

// --- Pure logic ------------------------------------------------------------------------

namespace hud {

std::string clockText(float seconds) {
    // mmHUD::Update: negative -> 0, then +0.005 and split into the whole and
    // fractional seconds; the digits are minutes % 100, seconds % 60 and
    // hundredths, drawn as MM:SS:HH.
    const double t = static_cast<double>(std::max(seconds, 0.0f)) + 0.005;
    double whole = 0.0;
    const double frac = std::modf(t, &whole);
    const int hundredths = static_cast<int>(frac * 100.0);
    const int total = static_cast<int>(whole);
    const int minutes = (total / 60) % 100;
    return std::format("{:02}:{:02}:{:02}", minutes, total % 60, hundredths % 100);
}

std::string lapTimeText(float seconds) {
    if (!(seconds > 0.0f))
        return "  ---  ";
    const double t = static_cast<double>(seconds) + 0.005;
    double whole = 0.0;
    const double frac = std::modf(t, &whole);
    const int hundredths = static_cast<int>(frac * 100.0);
    const int total = static_cast<int>(whole);
    return std::format("{}:{:02}:{:02}", total / 60, total - total / 60 * 60, hundredths);
}

std::array<char, 3> speedDigits(float speed) {
    const int v = static_cast<int>(speed); // __ftol: truncation
    const int hundreds = v / 100, tens = v % 100 / 10, units = v % 10;
    std::array<char, 3> out{' ', ' ', static_cast<char>('0' + std::abs(units))};
    if (hundreds > 0 && hundreds < 10)
        out[0] = static_cast<char>('0' + hundreds);
    if (tens != 0 || hundreds != 0)
        out[1] = static_cast<char>('0' + std::abs(tens));
    return out;
}

std::string gearArt(int gear, bool automatic) {
    // vehTransmission counts 0 reverse, 1 neutral, 2 first...; neutral uses
    // the "p" bitmap and every forward gear of an automatic "d".
    if (gear < 0)
        return "r";
    if (gear == 0)
        return "p";
    if (automatic)
        return "d";
    return std::to_string(std::min(gear, 8));
}

int linearGaugeLength(float value, float maxValue, int length) {
    if (!(maxValue > 0.0f))
        return 0;
    // Clamped here: the original would copy past the bitmap's edge.
    return std::clamp(static_cast<int>(value / maxValue * static_cast<float>(length)), 0, length);
}

int slidingGaugeOffset(float value, float maxValue, int bitmapLength, int window) {
    if (!(maxValue > 0.0f))
        return 0;
    const int range = bitmapLength - window;
    return std::clamp(static_cast<int>(value / maxValue * static_cast<float>(range)), 0, std::max(range, 0));
}

float gaugeAngle(float value, float floorValue, float maxValue, float rotMin, float rotMax) {
    const float v = floorValue <= value ? value : floorValue;
    const float a = maxValue != 0.0f ? (rotMax - rotMin) * (v / maxValue) + rotMin : rotMin;
    return clampf(a, std::min(rotMin, rotMax), std::max(rotMin, rotMax));
}

ArrowPose arrowPose(const Mat34& camera, const Vec3& target) {
    // The target at the camera's height, in camera space, normalised (not
    // flattened: a pitched camera tilts the arrow).
    Vec3 dir = camera.untransform({target.x, camera.m3.y, target.z});
    const float len2 = dir.mag2();
    dir = len2 != 0.0f ? dir * (1.0f / std::sqrt(len2)) : Vec3{};
    ArrowPose pose;
    Mat34& m = pose.local;
    m.m2 = -dir;
    m.m0 = {m.m2.z, 0.0f, -m.m2.x}; // Y x m2, left unnormalised as in the original
    m.m1 = m.m2.cross(m.m0);
    m.m3 = {};
    // Matrix34::Rotate(XAXIS, -20 degrees) post-multiplies the 3x3 part.
    m = m * Mat34::rotationX(-20.0f * kDegToRad);
    m.m3 = {0.0f, 2.5f, -6.1f};
    if (m.m2.z < 0.0f)
        pose.behind = true;
    else if (m.m2.z > 0.0f)
        pose.behind = false;
    return pose;
}

Mat34 standMatrix(const Checkpoint& cp) {
    // mmWaypointObject: the stand is rotated by -heading about Y and raised
    // by half its 7.5 m height; mmCheckpointInstance::Draw scales the model
    // (2 x 1 x 0.17 units) by (radius, 7.5, radius) first.
    constexpr float kHeight = 7.5f;
    Mat34 m = Mat34::rotationY(-cp.headingDeg * kDegToRad);
    m.m0 *= cp.radius;
    m.m1 *= kHeight;
    m.m2 *= cp.radius;
    m.m3 = cp.position + Vec3{0.0f, kHeight * 0.5f, 0.0f};
    return m;
}

Mat34 mapCamera(const Mat34& car, float height, bool rotating) {
    // The car's backward axis flattened and normalised (x, z); the camera is
    // RotX(-90 degrees) followed by the rotation that turns -Z onto the car's
    // heading. Without rotation (x, z) = (0, 1): -Z is up, +X right.
    float x = 0.0f, z = 1.0f;
    if (rotating) {
        const float len2 = car.m2.x * car.m2.x + car.m2.z * car.m2.z;
        if (len2 > 0.0f) { // the original degenerates for a vertical car
            const float inv = 1.0f / std::sqrt(len2);
            x = car.m2.x * inv;
            z = car.m2.z * inv;
        }
    }
    Mat34 cam;
    cam.m0 = {z, 0.0f, -x};
    cam.m1 = {-x, 0.0f, -z};
    cam.m2 = {0.0f, 1.0f, 0.0f};
    cam.m3 = {car.m3.x, height, car.m3.z};
    return cam;
}

Mat34 mapIconMatrix(const Mat34& car, float iconScale) {
    // Up forced to +Y, then Matrix34::Normalize (m0 = m1 x m2, m1 = m2 x m0,
    // all normalised), raised 15 m and scaled.
    Mat34 m = car;
    m.m1 = Vec3::yAxis();
    m.m0 = m.m1.cross(m.m2).normalized();
    m.m1 = m.m2.cross(m.m0).normalized();
    m.m2 = m.m2.normalized();
    m.m3.y += 15.0f;
    m.m0 *= iconScale;
    m.m1 *= iconScale;
    m.m2 *= iconScale;
    return m;
}

Vec4 mapRect(const render::UiLayout& l, const HudMapParams& map, const HudOptions& options,
             bool rightHandDrive) {
    // mmHudMap::SetMapMode, in fractions of the whole output.
    const float w = l.right - l.left, h = l.bottom - l.top;
    switch (options.mapMode) {
    case MapMode::Off: return {};
    case MapMode::Split: return {l.left, l.top + h * 0.5f, w, h * 0.5f};
    case MapMode::FullScreen: return {l.left, l.top, w, h};
    case MapMode::Small: break;
    }
    // Small: Pos/Size less 10 pixels; right-hand-drive cars move it to the
    // left edge in the dashboard view.
    const float x = rightHandDrive && options.dashboard ? l.left : l.left + map.pos.x * w;
    const float inset = 10.0f * options.pixelSize;
    return {x, l.top + map.pos.y * h, map.size.x * w - inset, map.size.y * h - inset};
}

float approach(float current, float target, float rate, float dt) {
    if (target > current)
        return std::min(current + rate * dt, target);
    if (target < current)
        return std::max(current - rate * dt, target);
    return current;
}

bool arrowShown(GameMode mode, const LessonEvent* lesson) {
    // mmHUD::Init: no arrow in cruise and circuit races; mmSingleStunt::InitHUD
    // turns it off for the follow, destroy and map lessons.
    switch (mode) {
    case GameMode::Cruise:
    case GameMode::Circuit: return false;
    case GameMode::CrashCourse:
        return !lesson || (lesson->type != LessonType::Follow && lesson->type != LessonType::Destroy &&
                           lesson->type != LessonType::Map);
    default: return true;
    }
}

bool clockShown(GameMode mode, const LessonEvent* lesson) {
    switch (mode) {
    case GameMode::Cruise:
    case GameMode::CopsAndRobbers: return false;
    case GameMode::CrashCourse:
        if (!lesson)
            return true;
        if (lesson->type == LessonType::Follow)
            return false;
        if (lesson->type == LessonType::MinimumSpeed)
            return lesson->timeLimit != 0.0f;
        return true;
    default: return true;
    }
}

bool checkReadoutShown(GameMode mode, const LessonEvent* lesson) {
    if (mode == GameMode::Cruise || mode == GameMode::CopsAndRobbers)
        return false;
    // mmSingleStunt::InitHUD hides the mmWPHUD for follow and destroy lessons.
    return !(mode == GameMode::CrashCourse && lesson &&
             (lesson->type == LessonType::Follow || lesson->type == LessonType::Destroy));
}

MapMode nextMapMode(MapMode mode, MapMode beforeFullScreen) {
    if (mode == MapMode::FullScreen)
        return beforeFullScreen;
    return static_cast<MapMode>((static_cast<int>(mode) + 1) % 3);
}

std::uint32_t mapIconColor(MapIcon icon) {
    // mmHudMap's IconType colour table (0xAARRGGBB).
    static constexpr std::array<std::uint32_t, 10> kTable{0xFF000000u, 0xFFFF0000u, 0xFF0000EFu, 0xFF00EF00u,
                                                          0xFFEF0000u, 0xFFFFFF00u, 0xFFFF5A00u, 0xFFB400FFu,
                                                          0xFF00FFFFu, 0xFFFF0390u};
    return kTable[static_cast<std::size_t>(icon) % kTable.size()];
}

} // namespace hud

// --- Data -----------------------------------------------------------------------------

HudMapParams loadHudMapParams(const vfs::Vfs& vfs, const std::string& city) {
    HudMapParams p;
    if (auto f = readDat(vfs, "tune/" + city + ".mmhudmap"); f && f->top()) {
        const data::DatNode& n = *f->top();
        n.read("Size", p.size);
        n.read("Pos", p.pos);
        if (auto z = n.getInt("ZoomIn"))
            p.zoomIn = *z != 0;
        // mmHudMap::FileIO also names "Approach Rate" and "Ocean Color", but
        // datParser field names are one token: the file's values are never
        // read and the constructor's (1.2, and the colour mmHudMap::Init
        // sets below) stay.
        n.read("ZoomInDist", p.zoomInDist);
        n.read("ZoomOutDist", p.zoomOutDist);
        n.read("IconScaleMin", p.iconScaleMin);
        n.read("IconScaleMax", p.iconScaleMax);
        n.read("ZoomInDistFS", p.zoomInDistFS);
        n.read("ZoomOutDistFS", p.zoomOutDistFS);
        n.read("IconScaleMinFS", p.iconScaleMinFS);
        n.read("IconScaleMaxFS", p.iconScaleMaxFS);
    }
    // mmHudMap::Init sets the ocean colour: London's map sits on beige,
    // every other city on blue.
    p.oceanColor = str::lower(city) == "london" ? Vec3{0.92f, 0.84f, 0.778f} : Vec3{0.084f, 0.68f, 0.92f};
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
    // mmDashView::LoadPivotInfo: GetPivot reads geometry/<car>_dash_<part>.mtx;
    // the needles and the wheel turn about the centre of their box.
    auto centre = [&](const char* part) {
        if (auto bytes = vfs.readAll(std::format("geometry/{}_dash_{}.mtx", car, part)))
            if (auto mtx = asset::parseMtx(*bytes))
                return mtx->center;
        return Vec3{};
    };
    d.dmgPivot = centre("damage_needle");
    d.speedPivot = centre("speed_needle");
    d.tachPivot = centre("tach_needle");
    d.wheelPivot = centre("wheel");
    return d;
}

// --- Hud ------------------------------------------------------------------------------

Hud::Hud(render::Device& device, TextureLibrary& textures, ModelLibrary& models, const vfs::Vfs& vfs,
         const Strings& strings, const std::string& city, const std::string& vehicle)
    : m_device(device), m_textures(textures), m_models(models), m_strings(strings), m_city(str::lower(city)),
      m_vehicle(str::lower(vehicle)) {
    m_map = loadHudMapParams(vfs, m_city);
    m_options.zoomedIn = m_map.zoomIn;
    m_dash = loadDashParams(vfs, m_vehicle);
    if (auto bytes = vfs.readAll("tune/" + m_vehicle + ".info")) {
        const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        if (auto info = parseVehicleInfo(text))
            m_rightHandDrive = (info->flags & VehicleInfo::kFlagBritish) != 0;
    }
}

void Hud::preload(ui::TextureCache* art) {
    const std::string models[] = {"pt_check", "pt_finish", "hudarrow01", "hudmap_square", "hudmap_" + m_city,
                                  m_vehicle + "_dash"};
    for (const std::string& name : models) {
        if (const GpuModel* m = m_models.get(name))
            for (const auto& pj : m->paintjobs)
                for (const auto& mat : pj)
                    m_textures.get(mat.texture);
    }
    if (art) {
        for (const char* t : {"speed_ticks.tga", "damage.tga", "damage_lable.tga"})
            art->get(std::string("texture/") + t);
        art->getColorKeyed("texture/digi_colon.tga");
        for (char c = '0'; c <= '9'; ++c) {
            art->get(std::format("texture/digitac_{}.tga", c));
            art->getColorKeyed(std::format("texture/digi_{}.tga", c));
        }
        for (const char* g : {"r", "p", "d", "1", "2", "3", "4", "5", "6", "7", "8"})
            art->get(std::format("texture/digitac_gear_{}.tga", g));
    }
}

ui::FontSpec Hud::font(std::uint32_t id, const char* fallback) const {
    auto spec = ui::FontSpec::parse(m_strings.get(id, fallback));
    if (!spec)
        spec = ui::FontSpec::parse(fallback);
    // mmText::CreateLocFont: the second size is the GDI cell height at
    // widths of 640 pixels and more (the first one below).
    ui::FontSpec f = spec.value_or(ui::FontSpec{});
    f.size2 = std::max(1, static_cast<int>(std::lround(static_cast<float>(f.size2) * m_options.pixelSize)));
    return f;
}

void Hud::drawTriangle(const Vec3& a, const Vec3& b, const Vec3& c, std::uint32_t argb) {
    const Vec3 pts[3] = {a, b, c};
    render::Vertex3D v[3]{};
    for (int i = 0; i < 3; ++i) {
        v[i].position[0] = pts[i].x;
        v[i].position[1] = pts[i].y;
        v[i].position[2] = pts[i].z;
        v[i].normal[1] = 1.0f;
        v[i].color = 0xFFFFFFFFu;
    }
    render::DrawCall call;
    call.vertices =
        m_device.uploadTransient(render::BufferKind::Vertex, std::span<const render::Vertex3D>(v));
    call.count = 3;
    call.constants.world = Mat44::identity();
    call.constants.color = argbColor(argb);
    call.constants.flags = render::DrawFlag::VertexColor;
    call.state.blend = render::BlendMode::Alpha;
    call.state.cull = render::CullMode::None;
    call.state.depthTest = false; // rglEnableDisable(0, false)
    call.state.depthWrite = false;
    m_device.draw(call);
}

// --- 3D: checkpoint stands, icons, arrow, dashboard ---------------------------------------

void Hud::drawWorld(const Session& session, const Camera& camera, const PlayerState& player, float steering,
                    std::span<const MapBlip> blips) {
    drawStands(session);
    drawIcons(session, camera, blips);
    if (m_options.visible)
        drawArrow(session, camera);
    // The dash view is a child of mmHUD's container node: mmHUD::Disable
    // hides it with the rest.
    if (m_options.dashboard && m_options.visible)
        drawDash(camera, player, steering);
}

void Hud::drawStands(const Session& session) {
    const auto& cps = session.checkpoints();
    // mmSingleStunt::InitNewEvent gives every crash course stand variant 1 (CCStand).
    const int paintjob = session.mode() == GameMode::CrashCourse ? 1 : 0;
    for (std::size_t i = 0; i < cps.size(); ++i) {
        if (!session.checkpointVisible(i))
            continue;
        const GpuModel* model = m_models.get(cps[i].finish ? "pt_finish" : "pt_check");
        // The L mesh (arch and banner); the VL one is the banner alone for
        // far LODs, which are not selected here.
        const GpuMesh* mesh = model ? model->find("", asset::Lod::Low) : nullptr;
        if (mesh)
            drawFlat(m_device, m_textures, *mesh, model->materials(paintjob), hud::standMatrix(cps[i]), true,
                     true);
    }
}

void Hud::drawIcons(const Session& session, const Camera& camera, std::span<const MapBlip> blips) {
    if (!m_options.opponentIcons)
        return;
    // mmIcons::Cull: a triangle card facing the camera, pointing down: the
    // corners (0, 4), (s/2, 2s + 4), (-s/2, 2s + 4) for an icon size s,
    // drawn over everything. Opponents have size 2: 2 m wide and 4 m tall
    // with the tip 4 m above the car.
    const Mat34& cam = camera.transform;
    auto card = [&](const Vec3& p, float size, std::uint32_t argb) {
        const Vec3 top = p + cam.m1 * (2.0f * size + 4.0f);
        drawTriangle(p + cam.m1 * 4.0f, top + cam.m0 * (0.5f * size), top - cam.m0 * (0.5f * size), argb);
    };
    if (session.mode() == GameMode::Blitz) {
        // mmSingleBlitz::InitHUD / Update: cyan cards 5 m above the
        // checkpoints still to clear, sized 1.9 near the camera up to 4.1
        // at 300 m and beyond.
        const auto& cps = session.checkpoints();
        for (std::size_t i = 1; i < cps.size(); ++i) {
            if (!session.checkpointVisible(i))
                continue;
            const Vec3 p = cps[i].position + Vec3{0.0f, 5.0f, 0.0f};
            const float d = std::min(cam.m3.dist(p), 300.0f);
            card(p, d / 300.0f * (4.1f - 1.9f) + 1.9f, 0xFF00FFFFu);
        }
        return;
    }
    // Opponents (registered for every single-player mode): violet.
    for (const auto& b : blips)
        if (b.kind == MapBlip::Kind::Opponent)
            card(b.transform.m3, 2.0f, 0xFFB400FFu);
}

void Hud::drawArrow(const Session& session, const Camera& camera) {
    // mmArrow: hudarrow01 in every mode, 2.5 m above and 6.1 m ahead of the
    // camera, turned toward the target, tilted 20 degrees, unlit, opaque and
    // drawn without depth test; paint job 1 (yellow) while the target is
    // behind.
    // mmWaypoints::Update turns the arrow off once the waypoints are done
    // (Session::arrowTarget); a lost race leaves it pointing.
    if (!hud::arrowShown(session.mode(), session.currentLesson()))
        return;
    const auto target = session.arrowTarget();
    if (!target)
        return;
    const GpuModel* model = m_models.get("hudarrow01");
    const GpuMesh* mesh = model ? model->find("", asset::Lod::High) : nullptr;
    if (!mesh)
        return;
    const hud::ArrowPose pose = hud::arrowPose(camera.transform, *target);
    if (pose.behind)
        m_arrowPaint = *pose.behind ? 1 : 0;
    drawFlat(m_device, m_textures, *mesh, model->materials(m_arrowPaint), pose.local * camera.transform,
             false);
}

void Hud::drawDash(const Camera& camera, const PlayerState& player, float steering) {
    // mmDashView::Cull: the parts follow the camera (DashPos / RoofPos),
    // unlit, without depth test, painted in this order.
    const GpuModel* model = m_models.get(m_vehicle + "_dash");
    if (!model || !m_dash)
        return;
    const DashParams& d = *m_dash;
    const Mat34& cam = camera.transform;
    const Mat34 dash = Mat34::translation(d.dashPos) * cam;
    auto draw = [&](std::string_view part, const Mat34& world, int paintjob = 0) {
        if (const GpuMesh* mesh = model->find(part, asset::Lod::High))
            drawFlat(m_device, m_textures, *mesh, model->materials(paintjob), world, false);
    };
    draw("dash", dash);
    draw("roof", Mat34::translation(d.roofPos) * cam);

    // Gear indicator: in dash space, moved by GearPivotOffset; its paint job
    // is the transmission gear (0 R, 1 N, 2 first, ...), automatic or not.
    Mat34 gear = dash;
    gear.m3 = dash.transform(d.gearPivotOffset);
    const int lastPaint = std::max(0, static_cast<int>(model->paintjobs.size()) - 1);
    const int gearPaint = std::clamp(player.gear + 1, 0, lastPaint);
    draw("gear_indicator", gear, gearPaint);

    // RadialGauge::Cull: rotate about pivot (box centre + PivotOffset), then
    // move by Offset in dash space.
    auto needle = [&](std::string_view part, float angle, const Vec3& pivot, const Vec3& offset) {
        const Mat34 m = Mat34::translation(-pivot) * Mat34::rotationZ(-angle) * Mat34::translation(pivot) *
                        Mat34::translation(offset) * dash;
        draw(part, m);
    };
    // mmDashView::Init: speed against 160 (display units), rpm against a
    // fixed 8000 with the needle resting at 800, damage against its maximum.
    needle("speed_needle", hud::gaugeAngle(player.speedMph, 0.0f, 160.0f, d.speedRotMin, d.speedRotMax),
           d.speedPivot + d.speedPivotOffset, d.speedOffset);
    needle("tach_needle", hud::gaugeAngle(player.rpm, 800.0f, 8000.0f, d.rpmRotMin, d.rpmRotMax),
           d.tachPivot + d.tachPivotOffset, d.tachOffset);
    needle("damage_needle", hud::gaugeAngle(player.damage01, 0.0f, 1.0f, d.damageRotMin, d.damageRotMax),
           d.dmgPivot + d.dmgPivotOffset, d.dmgOffset);
    draw("dash_extra", dash);

    // Steering wheel: turns by steering x WheelFact about its pivot, placed at WheelPos.
    const Vec3 p = d.wheelPivot + d.wheelPivotOffset;
    draw("wheel", Mat34::translation(-p) * Mat34::rotationZ(-steering * d.wheelFact) * Mat34::translation(p) *
                      Mat34::translation(d.wheelPos) * dash);
}

// --- Map -----------------------------------------------------------------------------------

Vec4 Hud::mapRect(const render::UiLayout& l) const {
    return hud::mapRect(l, m_map, m_options, m_rightHandDrive);
}

void Hud::cycleMap() { m_options.mapMode = hud::nextMapMode(m_options.mapMode, m_mapModeBeforeFull); }

void Hud::toggleFullScreenMap() {
    if (m_options.mapMode == MapMode::FullScreen) {
        m_options.mapMode = m_mapModeBeforeFull;
    } else {
        m_mapModeBeforeFull = m_options.mapMode;
        m_options.mapMode = MapMode::FullScreen;
    }
}

void Hud::toggleMapZoom() {
    if (m_options.mapMode != MapMode::Off)
        m_options.zoomedIn = !m_options.zoomedIn;
}

void Hud::toggleMapRotation() {
    if (m_options.mapMode != MapMode::Off)
        m_options.rotatingMap = !m_options.rotatingMap;
}

void Hud::toggleCluster() {
    if (m_options.cluster)
        m_options.cluster = false;
    else if (!m_options.dashboard)
        m_options.cluster = true;
}

void Hud::drawMap(const Session& session, const PlayerState& player, std::span<const MapBlip> blips,
                  float dt) {
    if (m_options.mapMode == MapMode::Off)
        return;
    const GpuModel* mapModel = m_models.get("hudmap_" + m_city);
    const GpuMesh* mapMesh = mapModel ? mapModel->find("", asset::Lod::High) : nullptr;
    if (!mapMesh)
        return; // "Missing hud asset 'hudmap_%s.pkg', disabling hudmap"

    const render::Extent2D scene = m_device.sceneExtent();
    const render::UiLayout layout = render::computeUiLayout(scene, m_options.uiScale);
    const Vec4 r = mapRect(layout);
    const Vec2 p0 = layout.toPixels({r.x, r.y});
    const float pw = r.z * layout.scaleX, ph = r.w * layout.scaleY;
    if (pw < 2.0f || ph < 2.0f || r.z <= 0.0f || r.w <= 0.0f)
        return;

    // Zoom (camera height) and icon size: snapped when the mode changes,
    // then approaching the Map Zoom setting linearly (mmHudMap::Cull).
    const bool fs = m_options.mapMode == MapMode::FullScreen;
    const float zIn = fs ? m_map.zoomInDistFS : m_map.zoomInDist;
    const float zOut = fs ? m_map.zoomOutDistFS : m_map.zoomOutDist;
    const float iMin = fs ? m_map.iconScaleMinFS : m_map.iconScaleMin;
    const float iMax = fs ? m_map.iconScaleMaxFS : m_map.iconScaleMax;
    const float zoomTarget = m_options.zoomedIn ? zIn : zOut;
    const float iconTarget = m_options.zoomedIn ? iMin : iMax;
    if (m_mapModeApplied != m_options.mapMode) {
        m_mapModeApplied = m_options.mapMode;
        m_mapZoom = zoomTarget;
        m_mapIconScale = iconTarget;
    }
    m_mapZoom = hud::approach(m_mapZoom, zoomTarget, (zOut - zIn) * m_map.approachRate, dt);
    m_mapIconScale = hud::approach(m_mapIconScale, iconTarget, (iMax - iMin) * m_map.approachRate, dt);

    m_device.setViewport({p0.x, p0.y, pw, ph, 0.0f, 1.0f});
    const render::Rect scissor{static_cast<std::int32_t>(p0.x), static_cast<std::int32_t>(p0.y),
                               static_cast<std::uint32_t>(pw), static_cast<std::uint32_t>(ph)};
    m_device.setScissor(&scissor);
    render::ClearValues clear;
    clear.color = {m_map.oceanColor.x, m_map.oceanColor.y, m_map.oceanColor.z, 1.0f};
    m_device.clear(clear);

    // Perspective camera, 60 degrees vertical field of view, near 10, far
    // 1600, aspect 1.25 (2.5 split) whatever the viewport's shape at 4:3. On
    // other aspect ratios the horizontal field grows with the viewport.
    const float aspect43 = [&] {
        const render::UiLayout ref = render::computeUiLayout({640, 480}, render::UiScaleMode::Stretch);
        const Vec4 r43 = mapRect(ref);
        return r43.w > 0.0f ? r43.z / r43.w : 1.0f;
    }();
    const float mm2Aspect = m_options.mapMode == MapMode::Split ? 2.5f : 1.25f;
    const float aspect = mm2Aspect * ((r.z / r.w) / aspect43);
    render::FrameConstants fc;
    const Mat34 camM = hud::mapCamera(player.transform, m_mapZoom, m_options.rotatingMap);
    fc.view = Mat44::fromMat34(camM.fastInverse());
    fc.proj = Mat44::perspective(60.0f * kDegToRad, aspect, 10.0f, 1600.0f, true);
    fc.cameraPosition = camM.m3;
    fc.fogMode = render::FogMode::None;
    fc.ambient = {1, 1, 1};
    m_device.setFrameConstants(fc);

    drawFlat(m_device, m_textures, *mapMesh, mapModel->materials(0), Mat34::identity(), false);

    // mmHudMap::DrawWaypoints: squares (hudmap_square scaled by icon / 7.51)
    // 10 m above the waypoint; green to clear, yellow for the current goal,
    // grey cleared (circuits), the finish dot for the finish line.
    const GpuModel* square = m_models.get("hudmap_square");
    const GpuMesh* squareMesh = square ? square->find("", asset::Lod::High) : nullptr;
    auto dot = [&](const Vec3& p, int paintjob) {
        if (!squareMesh)
            return;
        const float s = m_mapIconScale / 7.51f;
        Mat34 m;
        m.m0 = {s, 0, 0};
        m.m1 = {0, s, 0};
        m.m2 = {0, 0, s};
        m.m3 = {p.x, std::max(p.y, 0.0f) + 10.0f, p.z};
        drawFlat(m_device, m_textures, *squareMesh, square->materials(paintjob), m, false);
    };
    const auto& cps = session.checkpoints();
    const int current = session.targetCheckpoint();
    for (std::size_t i = 0; i < cps.size(); ++i) {
        if (session.mode() == GameMode::Circuit) {
            if (i == 0)
                dot(cps[i].position, kDotFinish);
            else if (session.checkpointCleared(i))
                dot(cps[i].position, kDotGrey);
            else
                dot(cps[i].position, static_cast<int>(i) == current ? kDotYellow : kDotGreen);
            continue;
        }
        if (!session.checkpointVisible(i))
            continue;
        if (cps[i].finish && session.mode() == GameMode::Checkpoint)
            dot(cps[i].position, kDotFinish);
        else
            dot(cps[i].position, static_cast<int>(i) == current ? kDotYellow : kDotGreen);
    }

    // Car arrows: flat triangles (DrawColoredTri), police, then opponents,
    // then the player over a black outline 1.3 times its size.
    auto car = [&](const Mat34& tm, hud::MapIcon icon, float scale) {
        const Mat34 m = hud::mapIconMatrix(tm, scale);
        drawTriangle(m.transform({0.0f, 0.0f, -1.0f}), m.transform({-0.7f, 0.0f, 1.0f}),
                     m.transform({0.7f, 0.0f, 1.0f}), hud::mapIconColor(icon));
    };
    for (const auto& b : blips)
        if (b.kind == MapBlip::Kind::Police)
            car(b.transform, hud::MapIcon::Police, m_mapIconScale);
    for (const auto& b : blips) {
        if (b.kind == MapBlip::Kind::Opponent)
            car(b.transform, hud::MapIcon::Opponent, m_mapIconScale);
        else if (b.kind == MapBlip::Kind::Teammate)
            car(b.transform, hud::MapIcon::Teammate, m_mapIconScale);
    }
    car(player.transform, hud::MapIcon::Outline, m_mapIconScale * 1.3f);
    car(player.transform, hud::MapIcon::Player, m_mapIconScale);

    m_device.setViewport({0, 0, static_cast<float>(scene.width), static_cast<float>(scene.height), 0, 1});
    m_device.setScissor(nullptr);
}

// --- 2D overlay ------------------------------------------------------------------------------

void Hud::drawCluster(render::Overlay2D& ov, ui::TextureCache& art, const PlayerState& player, float x,
                      float y) {
    // mmExternalView: origin at the left edge, 100 pixels above the bottom.
    // Painted in this order: damage meter, gear, tachometer, speed.
    const ui::UiTexture& ticks = art.get("texture/speed_ticks.tga");
    const ui::UiTexture& damage = art.get("texture/damage.tga");
    const ui::UiTexture& label = art.get("texture/damage_lable.tga");

    // mmSlidingGauge at (8, 88): a window as wide as speed_ticks slides
    // across the 500-pixel colour bar with the damage; the label on top.
    if (damage && ticks) {
        const int window = static_cast<int>(ticks.width);
        const int off = hud::slidingGaugeOffset(clampf(player.damage01, 0.0f, 1.0f), 1.0f,
                                                static_cast<int>(damage.width), window);
        const float dw = static_cast<float>(damage.width);
        ov.image(damage.handle, x + px(8), y + px(88), px(static_cast<float>(window)),
                 px(static_cast<float>(damage.height)), {static_cast<float>(off) / dw, ui::UiTexture::uvTop},
                 {static_cast<float>(off + window) / dw, ui::UiTexture::uvBottom});
    }
    ui::drawImage(ov, label, x + px(8), y + px(88), px(static_cast<float>(label.width)),
                  px(static_cast<float>(label.height)));

    // mmGearIndicator at (16, 46).
    const std::string gearName = hud::gearArt(player.gear, player.automatic);
    const ui::UiTexture& gear = art.get(std::format("texture/digitac_gear_{}.tga", gearName));
    ui::drawImage(ov, gear, x + px(16), y + px(46), px(static_cast<float>(gear.width)),
                  px(static_cast<float>(gear.height)));

    // mmLinearGauge at (8, 41): speed_ticks lit from the left up to rpm / MaxRPM.
    if (ticks) {
        const int lit = hud::linearGaugeLength(player.rpm, player.maxRpm, static_cast<int>(ticks.width));
        if (lit > 0) {
            const float frac = static_cast<float>(lit) / static_cast<float>(ticks.width);
            ov.image(ticks.handle, x + px(8), y + px(41), px(static_cast<float>(lit)),
                     px(static_cast<float>(ticks.height)), {0.0f, ui::UiTexture::uvTop},
                     {frac, ui::UiTexture::uvBottom});
        }
    }

    // mmSpeedIndicator at (19, -14): digitac digits at x, x + w + 1, x + 2w + 1.
    const float speed = m_options.metric ? player.speedMph * 1.609344f : player.speedMph;
    const auto digits = hud::speedDigits(std::max(speed, 0.0f));
    const ui::UiTexture& zero = art.get("texture/digitac_0.tga");
    const float w = static_cast<float>(zero.width);
    const float offsets[3] = {0.0f, w + 1.0f, 2.0f * w + 1.0f};
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (digits[i] == ' ')
            continue;
        const ui::UiTexture& tex = art.get(std::format("texture/digitac_{}.tga", digits[i]));
        ui::drawImage(ov, tex, x + px(19.0f + offsets[i]), y + px(-14.0f), px(static_cast<float>(tex.width)),
                      px(static_cast<float>(tex.height)));
    }
}

void Hud::drawClock(render::Overlay2D& ov, ui::TextureCache& art, float seconds, float centerX, float y) {
    // mmHUD::Cull: eight colour-keyed bitmaps MM:SS:HH at the top of the
    // screen, starting three digits and a colon left of the centre.
    const std::string s = hud::clockText(seconds);
    const ui::UiTexture& colon = art.getColorKeyed("texture/digi_colon.tga");
    const ui::UiTexture& zero = art.getColorKeyed("texture/digi_0.tga");
    float x = centerX - px(3.0f * static_cast<float>(zero.width) + static_cast<float>(colon.width));
    for (char c : s) {
        const ui::UiTexture& tex =
            c == ':' ? colon : art.getColorKeyed(std::format("texture/digi_{}.tga", c));
        ui::drawImage(ov, tex, x, y, px(static_cast<float>(tex.width)), px(static_cast<float>(tex.height)));
        x += px(static_cast<float>(tex.width));
    }
}

void Hud::trackLapTimes(const Session& session) {
    // mmCircuitHUD::SetLapTime fills one row per completed lap.
    if (session.mode() != GameMode::Circuit)
        return;
    if (session.phase() == Phase::Countdown) {
        m_lapTimes.clear();
        m_lastLapSeen = 0.0f;
    }
    const float last = session.lastLapTime();
    if (last > 0.0f && last != m_lastLapSeen) {
        m_lastLapSeen = last;
        m_lapTimes.push_back(last);
    }
}

void Hud::drawReadouts(render::Overlay2D& ov, ui::TextRenderer& text, const Session& session) {
    // mmWPHUD / mmCircuitHUD / mmCollideHUD: light green labels at the left
    // edge, white numbers right after them, rows at 3.5 %, 8.5 % and 13.5 %
    // of the screen height. No shadow.
    const render::UiLayout& l = ov.layout();
    const float h = l.bottom - l.top;
    const GameMode mode = session.mode();
    const bool circuit = mode == GameMode::Circuit;
    const ui::FontSpec labelFont = circuit ? font(258, "Gill Sans MT, 16, 22, 0, 700")
                                           : font(253, "Gill Sans MT, 16, 22, 0, 700");
    const ui::FontSpec numberFont = circuit ? font(256, "Gill Sans MT, 16, 22, 0, 700")
                                            : font(251, "Gill Sans MT, 16, 22, 0, 700");
    auto row = [&](float yFrac, const std::string& label, const std::string& value) {
        const float y = l.top + yFrac * h;
        const float w = text.draw(ov, labelFont, label, l.left, y, kLabelColor);
        text.draw(ov, numberFont, value, l.left + w, y, kNumberColor);
    };
    const LessonEvent* lesson = session.currentLesson();
    if (circuit) {
        row(0.035f, m_strings.get(259, "Place:  "),
            std::format("{}/{}", session.position(), session.racerCount()));
        row(0.085f, m_strings.get(260, "Check:  "),
            std::format("{}/{}", session.checkpointsCleared(), session.checkpointsTotal()));
        row(0.135f, m_strings.get(261, "Lap:  "), std::format("{}/{}", session.lap(), session.laps()));
        // Lap times: "1." ... at 18.5 %, 5 % apart, the time after the
        // width of "10.  ". mmWaypoints::Update sets the row of the lap
        // being driven every frame (mmHUD::SetLapTime(lap, 0, false)) until
        // the race is finished: it runs live; completed laps keep their time.
        const float timeX = l.left + text.measure(ov, labelFont, "10.  ");
        const bool running = session.phase() == Phase::Countdown || session.phase() == Phase::Racing;
        const std::size_t rows = m_lapTimes.size() + (running ? 1u : 0u);
        for (std::size_t i = 0; i < rows && static_cast<int>(i) < session.laps(); ++i) {
            const float y = l.top + (static_cast<float>(i + 1) * 0.05f + 0.135f) * h;
            const float t = i < m_lapTimes.size() ? m_lapTimes[i] : session.lapTime();
            text.draw(ov, labelFont, std::format("{}.", i + 1), l.left, y, kLabelColor);
            text.draw(ov, numberFont, hud::lapTimeText(t), timeX, y, kNumberColor);
        }
        return;
    }
    if (!hud::checkReadoutShown(mode, lesson))
        return;
    float y = 0.035f;
    if (mode == GameMode::Checkpoint) { // mmSingleRace::InitHUD shows the place
        row(y, m_strings.get(254, "Place:  "),
            std::format("{}/{}", session.position(), session.racerCount()));
        y = 0.085f;
    }
    row(y, m_strings.get(255, "Check:  "),
        std::format("{}/{}", session.checkpointsCleared(), session.checkpointsTotal()));
    if (lesson && lesson->type == LessonType::Clean) {
        // mmCollideHUD::Init creates "Hit Vehicles:" at 19 % as well, but
        // never adds it to the HUD: only the object count shows.
        row(0.14f, m_strings.get(269, "Hit Objects:  "), std::format("{}", session.objectHits()));
    }
}

void Hud::drawMessage(render::Overlay2D& ov, ui::TextRenderer& text, const HudMessage& m, bool second) {
    // mmHUD::Update clears the text when the time runs out; until then it shows.
    if (m.text.empty())
        return;
    // mmHUD: a full-width text node at 80 % of the screen height, 15 % tall
    // (SetMessage mode 0), or at 20 % for the upper messages (mode 1); yellow
    // Gill Sans (string 60), centred and word-wrapped, with a dark shadow
    // offset by a 18th of the line height. Lines past the node are cut off.
    const render::UiLayout& l = ov.layout();
    const float w = l.right - l.left, h = l.bottom - l.top;
    const ui::FontSpec f = font(60, "Gill Sans MT, 20, 36, 0, 400");
    // mmHUD::Update places the nodes: the message at 0.8 (mode 0) or 0.2
    // (mode 1), 0.15 tall; the second line at 0.875 or 0.2 + 0.15, 0.075
    // tall (mmHUD::mmHUD).
    const float y0 = second ? (m.top ? 0.2f + 0.15f : 0.875f) : (m.top ? 0.2f : 0.8f);
    const float top = l.top + y0 * h;
    const float boxBottom = top + (second ? 0.075f : 0.15f) * h;
    const float lineHeight = static_cast<float>(f.size2); // DrawText steps by the cell height
    // RenderText: shadow offset = text height / 9, halved for word-wrapped
    // nodes, at least one pixel.
    const int cellPixels = static_cast<int>(std::lround(static_cast<float>(f.size2) / m_options.pixelSize));
    const float shadow = px(static_cast<float>(std::max(1, cellPixels / 9 / 2)));
    float y = top;
    for (const auto& line : wrapText(ov, text, f, m.text, w)) {
        if (y + lineHeight > boxBottom + 0.5f)
            break;
        const float cx = l.left + w * 0.5f;
        text.draw(ov, f, line, cx + shadow, y + shadow, kShadowColor, ui::Align::Center);
        text.draw(ov, f, line, cx, y, kMessageColor, ui::Align::Center);
        y += lineHeight;
    }
}

void Hud::drawCheckpointLabels(render::Overlay2D& ov, ui::TextRenderer& text, const Session& session) {
    // mmSingleBlitz::InitHUD registers every checkpoint's number ("%d") with
    // mmIcons as a label in font string 48, cyan; mmIcons::Cull draws each
    // label whose point (2 m above the icon) is in front of the camera,
    // centred over it with its bottom there, nearest first.
    if (!m_viewProjValid)
        return;
    const render::UiLayout& l = ov.layout();
    const render::Extent2D out = m_device.outputExtent(); // the scene fills the output
    const ui::FontSpec f = font(48, "Gill Sans MT, 10, 16, 0, 400");
    struct Label {
        Vec2 at;
        float depth;
        std::size_t index;
    };
    std::vector<Label> labels;
    const auto& cps = session.checkpoints();
    for (std::size_t i = 1; i < cps.size(); ++i) {
        const Vec3 p = cps[i].position + Vec3{0.0f, 5.0f + 2.0f, 0.0f};
        const Vec4 c = m_viewProj.transform(Vec4{p.x, p.y, p.z, 1.0f});
        if (!(c.w > 0.0f))
            continue;
        const float inv = 1.0f / c.w;
        const Vec2 pixel{(c.x * inv * 0.5f + 0.5f) * static_cast<float>(out.width),
                         (0.5f - c.y * inv * 0.5f) * static_cast<float>(out.height)};
        labels.push_back({l.toVirtual(pixel), c.z * inv, i});
    }
    std::ranges::sort(labels, {}, &Label::depth);
    const float lineHeight = static_cast<float>(f.size2);
    for (const auto& lb : labels)
        text.draw(ov, f, std::format("{}", lb.index), lb.at.x, lb.at.y - lineHeight, render::packColor(0, 255, 255),
                  ui::Align::Center);
}

void Hud::drawOverlay(render::Overlay2D& ov, ui::TextRenderer& text, ui::TextureCache& art,
                      const Session& session, const PlayerState& player) {
    ov.begin(m_options.uiScale);
    const render::UiLayout& l = ov.layout();
    const GameMode mode = session.mode();
    trackLapTimes(session);

    // The instrument cluster (mmExternalView) is replaced by the dashboard.
    if (m_options.visible && m_options.cluster && !m_options.dashboard)
        drawCluster(ov, art, player, l.left, l.bottom - px(100.0f));

    // Race clock, top centre (mmHUD::Cull): the count-down in Blitz and the
    // crash course, otherwise the race time. Stays with the HUD toggled off.
    if (hud::clockShown(mode, session.currentLesson())) {
        const float remaining = session.timeRemaining();
        const bool countDown = mode == GameMode::Blitz || mode == GameMode::CrashCourse;
        const float shown = countDown ? std::max(remaining, 0.0f) : session.raceTime();
        drawClock(ov, art, shown, (l.left + l.right) * 0.5f, l.top);
    }

    if (m_options.visible)
        drawReadouts(ov, text, session);
    if (m_options.opponentIcons && mode == GameMode::Blitz)
        drawCheckpointLabels(ov, text, session);
    // mmHUD::mmHUD puts the message nodes under the container mmHUD::Disable
    // hides in single player, and directly under the HUD in multiplayer.
    if (m_options.visible || session.multiplayer()) {
        drawMessage(ov, text, session.message());
        // SetMessage2: the line under the message.
        drawMessage(ov, text, session.message2(), true);
    }
    ov.end();
}

} // namespace mm2::game::session
