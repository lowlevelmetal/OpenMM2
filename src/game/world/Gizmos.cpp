// The gizmo managers (gizSailboatMgr, gizBridgeMgr, gizTrainMgr,
// gizFerryMgr, gizParkedCarMgr) and their objects, ported from the code of
// midtown2.exe build 3393 (MM2Recomp; documentation only).
#include "game/world/Gizmos.h"

#include "audio/game/Ambience.h"
#include "city/RoomInfo.h"
#include "core/Log.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace mm2::game::world {
namespace {

// The default models init_gizmo_mgr's callers pass (mmGame::InitGizmos).
constexpr const char* kSailboatModel = "giz_sailboat01_f";
constexpr const char* kBridgeModel = "giz_bridge01_l";
constexpr const char* kTrainModel = "va_ug_l";
constexpr const char* kFerryModel = "giz_carferry01_f";

// gizBridge::Reposition: the leaf's frame is lowered by this much
// (a static Vector3 the game initialises to (0, -0.3, 0)).
constexpr Vec3 kBridgeOffset{0.0f, -0.3f, 0.0f};
// gizBridge::Init: the second room flagged lies this far above the leaf.
constexpr float kBridgeRoomProbe = 5.0f;
// gizParkedCarMgr: the smallest spacing between parked cars.
constexpr float kParkedCarSpacing = 5.0f;
// gizTrain::Update: the speed aiSubwayAudio::Update hears while a train
// moves (it stands at 0).
constexpr float kTrainAudioSpeed = 50.0f;

bool geometryExists(const vfs::Vfs& vfs, std::string_view model) {
    return !model.empty() && vfs.exists(std::format("geometry/{}.pkg", str::lower(model)));
}

std::vector<Vec3> pointsOf(const city::PathSetPath& path) {
    std::vector<Vec3> out;
    out.reserve(path.points.size());
    for (const auto& p : path.points)
        out.push_back(p.position);
    return out;
}

// The model of a gizmo path (gizSailboatMgr / gizTrainMgr / gizFerryMgr::
// Init): the path's name when geometry/<name>.pkg exists, else the default.
std::string pathModel(const vfs::Vfs& vfs, const city::PathSetPath& path, const char* fallback) {
    return geometryExists(vfs, path.name) ? str::lower(path.name) : std::string(fallback);
}

// The 3-argument Matrix34::Dot (a times b), in its summation order.
Mat34 dot(const Mat34& a, const Mat34& b) {
    Mat34 r;
    r.m0 = {(b.m2.x * a.m0.z + a.m0.y * b.m1.x) + a.m0.x * b.m0.x,
            (b.m0.y * a.m0.x + b.m2.y * a.m0.z) + a.m0.y * b.m1.y,
            (b.m0.z * a.m0.x + b.m2.z * a.m0.z) + a.m0.y * b.m1.z};
    r.m1 = {(a.m1.z * b.m2.x + a.m1.x * b.m0.x) + a.m1.y * b.m1.x,
            (a.m1.x * b.m0.y + a.m1.y * b.m1.y) + a.m1.z * b.m2.y,
            (a.m1.x * b.m0.z + a.m1.y * b.m1.z) + a.m1.z * b.m2.z};
    r.m2 = {(a.m2.y * b.m1.x + b.m2.x * a.m2.z) + a.m2.x * b.m0.x,
            (b.m0.y * a.m2.x + b.m1.y * a.m2.y) + b.m2.y * a.m2.z,
            (b.m0.z * a.m2.x + b.m1.z * a.m2.y) + b.m2.z * a.m2.z};
    r.m3 = {((a.m3.z * b.m2.x + a.m3.x * b.m0.x) + a.m3.y * b.m1.x) + b.m3.x,
            ((a.m3.x * b.m0.y + a.m3.y * b.m1.y) + a.m3.z * b.m2.y) + b.m3.y,
            ((a.m3.x * b.m0.z + a.m3.y * b.m1.z) + a.m3.z * b.m2.z) + b.m3.z};
    return r;
}

// gizBridgeMgr::Init's frame for a leaf hinged at `at` that opens away from
// `other`: Z from `other` to `at`, X = up x Z (not normalised), Y = Z x X.
Mat34 bridgeFrame(const Vec3& at, const Vec3& other) {
    Mat34 m;
    m.m3 = at;
    m.m2 = {m.m3.x - other.x, m.m3.y - other.y, m.m3.z - other.z};
    const float l2 = (m.m2.z * m.m2.z + m.m2.y * m.m2.y) + m.m2.x * m.m2.x;
    const float inv = l2 == 0.0f ? 0.0f : 1.0f / std::sqrt(l2);
    m.m2 = {inv * m.m2.x, inv * m.m2.y, inv * m.m2.z};
    constexpr Vec3 up{0.0f, 1.0f, 0.0f};
    const float x = up.y * m.m2.z - up.z * m.m2.y;
    const float y = up.z * m.m2.x - up.x * m.m2.z;
    m.m0 = {x, y, up.x * m.m2.y - up.y * m.m2.x};
    m.m1 = {m.m2.y * m.m0.z - y * m.m2.z, x * m.m2.z - m.m2.x * m.m0.z, y * m.m2.x - x * m.m2.y};
    return m;
}

// gizBridgeMgr::Init's model of a bridge path: a name "<type>:<model>" (or
// "<type>@<model>") names its model, a plain name is the model itself;
// otherwise (no such geometry) the default.
std::string bridgeModel(const vfs::Vfs& vfs, const std::string& name) {
    // strtok(name, ":@") then strtok(NULL, " "): the second token.
    const auto first = name.find_first_not_of(":@");
    if (first != std::string::npos) {
        const auto end = name.find_first_of(":@", first);
        if (end != std::string::npos) {
            const auto start = name.find_first_not_of(' ', end + 1);
            if (start != std::string::npos) {
                const auto stop = name.find(' ', start);
                const std::string second = name.substr(start, stop == std::string::npos ? stop : stop - start);
                if (geometryExists(vfs, second))
                    return str::lower(second);
            }
        }
    }
    // Else the name as strtok left it: cut at its first ':' or '@'.
    const std::string cut = name.substr(0, name.find_first_of(":@"));
    if (geometryExists(vfs, cut))
        return str::lower(cut);
    return {};
}

// gizBridgeMgr::Init's type of a bridge path: its first four characters
// (after strtok cut the name at ':' or '@'), compared without case.
std::optional<Gizmos::Bridge::Type> bridgeType(const std::string& name) {
    const std::string cut = name.substr(0, name.find_first_of(":@"));
    const std::string head = str::lower(cut.substr(0, 4));
    if (head == "inac")
        return Gizmos::Bridge::Type::Inactive;
    if (head == "time")
        return Gizmos::Bridge::Type::Timed;
    if (head == "prox")
        return Gizmos::Bridge::Type::Proximity;
    if (head == "open")
        return Gizmos::Bridge::Type::Open;
    return std::nullopt; // the constructor's Timed stays
}

// The draws of dgBangerInstance::Draw / gizInstance::Draw.
constexpr float kAlphaRef = 101.0f / 255.0f;
constexpr float kUnlitAlphaRef = 141.0f / 255.0f;

void drawModel(render::Device& device, ModelLibrary& models, TextureLibrary& textures, const std::string& name,
               int paint, const Mat34& matrix, asset::Lod lod, bool unlit) {
    const GpuModel* model = models.get(name);
    if (!model)
        return;
    const GpuMesh* mesh = model->find("", lod);
    if (!mesh)
        return;
    MeshDrawOptions options;
    options.alphaRef = kAlphaRef;
    if (unlit) {
        options.lighting = false;
        options.alphaRef = kUnlitAlphaRef;
    }
    const int count = static_cast<int>(model->paintjobs.size());
    drawGpuMesh(device, textures, *mesh, model->materials(count > 0 ? paint % count : 0), Mat44::fromMat34(matrix),
                options);
}

// gizBridgeMgr::Cull / gizFerryMgr::Cull: within R of the camera the high
// LOD, then medium, low and very low by R; nothing beyond 4 R.
std::optional<asset::Lod> managerLod(float distance, float r) {
    if (!(distance <= r * 4.0f))
        return std::nullopt;
    if (distance < r)
        return asset::Lod::High;
    if (distance < r + r)
        return asset::Lod::Medium;
    if (distance < r * 3.0f)
        return asset::Lod::Low;
    return asset::Lod::VeryLow;
}

float distanceTo(const Vec3& a, const Vec3& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

float modelRadius(ModelLibrary& models, const std::string& name) {
    const GpuModel* model = models.get(name);
    return model ? (model->bounds.max - model->bounds.min).mag() * 0.5f : 0.0f;
}

} // namespace

std::string gizmoPathSetPath(const vfs::Vfs& vfs, std::string_view city, std::string_view kind, GameMode mode,
                             int raceIndex) {
    const std::string name = str::lower(city);
    const std::string dir = std::format("race/{}/", name);
    if (mode != GameMode::Cruise) {
        const std::string race = bangers::racePropsName(mode, raceIndex);
        if (!race.empty()) {
            std::string path = std::format("{}{}_{}_{}.pathset", dir, name, kind, race);
            if (vfs.exists(path))
                return path;
        }
    }
    std::string path = std::format("{}{}_{}.pathset", dir, name, kind);
    return vfs.exists(path) ? path : std::string();
}

GizmoKinds GizmoKinds::forSession(GameMode mode, bool multiplayer) {
    GizmoKinds k;
    k.ferries = !multiplayer;
    k.parkedCars = !multiplayer || (mode != GameMode::Cruise && mode != GameMode::CopsAndRobbers);
    return k;
}

std::vector<bangers::PlacedProp> placeParkedCars(const city::PathSet& set, fx::Rand& random) {
    // dgPath::Enumerate at max(spacing, 5 m) (the path's trailer spacing
    // byte raised to 20 quarter metres where it is smaller), through the
    // walk the props of path sets use; every point is offered once.
    city::PathSet walk = set;
    for (auto& path : walk.paths) {
        path.name = "giz_pcar"; // any name: placePathSet needs one
        if (path.points.empty())
            continue;
        auto& trailer = path.points.back().extra;
        const auto quarters = (trailer >> 8) & 0xFFu;
        if (quarters != 0 && static_cast<float>(quarters) * 0.25f < kParkedCarSpacing)
            trailer = (trailer & ~0xFF00u) | (20u << 8);
    }
    const Mat34 turn = Mat34::rotationY(1.5707964f); // MakeRotateY(pi / 2)
    std::vector<bangers::PlacedProp> out;
    for (const auto& at : bangers::placePathSet(walk, bangers::PlacedProp::Source::PathSet)) {
        // gizParkedCarMgr_EnumeratePath.
        const int pick = random.irand() % 3;
        if (pick == 0)
            continue;
        bangers::PlacedProp p;
        p.model = std::format("giz_pcar{:02}_l", pick);
        p.transform = dot(at.transform, turn);
        p.transform.m3 = at.transform.m3;
        p.fullMatrix = true; // RequestBanger(name, 1): dgUnhitMtxBangerInstance
        p.source = bangers::PlacedProp::Source::PathSet;
        p.variant = random.irand(); // SetVariant(irand())
        out.push_back(std::move(p));
    }
    return out;
}

// --- GizmoBody -------------------------------------------------------------------------------

const phys::Bound* GizmoBody::bound(int which) const {
    return data && bounds ? bounds->boundOf(*data, which) : nullptr;
}

float GizmoBody::radius() const { return data && bounds ? bounds->boundRadius(*data) : 0.0f; }

void GizmoBody::setMatrix(const Mat34& m) {
    if (!yOnly) {
        m_matrix = m;
        return;
    }
    // dgUnhitYBangerInstance::SetMatrix keeps the position and the X row's
    // x and z; GetMatrix rebuilds (c, 0, s), (0, 1, 0), (-s, 0, c).
    const float c = m.m0.x, s = m.m0.z;
    m_matrix.m0 = {c, 0.0f, s};
    m_matrix.m1 = {0.0f, 1.0f, 0.0f};
    m_matrix.m2 = {-s, 0.0f, c};
    m_matrix.m3 = m.m3;
}

// --- Gizmos ----------------------------------------------------------------------------------

Gizmos::Gizmos(const bangers::BangerDataLibrary& data, const bangers::BangerSet& bounds)
    : m_data(data), m_bounds(bounds) {}

Gizmos::~Gizmos() { stopAudio(); }

void Gizmos::load(const vfs::Vfs& vfs, const Options& options, const phys::Level* level,
                  std::vector<std::uint16_t>* roomFlags, fx::Rand& random) {
    m_level = level;
    const GizmoKinds kinds = GizmoKinds::forSession(options.mode, options.multiplayer);
    auto pathSet = [&](std::string_view kind) -> std::optional<city::PathSet> {
        const std::string path = gizmoPathSetPath(vfs, options.city, kind, options.mode, options.raceIndex);
        if (path.empty())
            return std::nullopt; // init_gizmo_mgr: no path set, no manager
        auto bytes = vfs.readAll(path);
        std::string error;
        auto set = bytes ? city::parsePathSet(*bytes, &error) : std::nullopt;
        if (!set) {
            // dgPathSet::Load fails: the manager is still created, empty.
            log::warn("gizmos: {}: {}", path, error);
            return std::nullopt;
        }
        m_loaded.push_back(path);
        return set;
    };
    // mmGame::InitGizmos' order: sailboats, bridges, trains, ferries (the
    // parked cars are the caller's).
    if (kinds.sailboats)
        if (auto set = pathSet("sailboat"))
            loadSailboats(vfs, *set, random);
    if (kinds.bridges)
        if (auto set = pathSet("bridge"))
            loadBridges(vfs, *set, roomFlags);
    if (kinds.trains)
        if (auto set = pathSet("train"))
            loadTrains(vfs, *set);
    if (kinds.ferries)
        if (auto set = pathSet("ferry"))
            loadFerries(vfs, *set, random);
    // mmGame::InitGizmos: a network game in London opens every bridge
    // (type 3) and resets them.
    if (options.multiplayer && str::iequals(options.city, "london") && !m_bridges.empty()) {
        for (auto& b : m_bridges)
            b.type = Bridge::Type::Open;
        for (auto& b : m_bridges)
            resetBridgeState(b); // gizBridgeMgr::Reset
    }
    log::info("gizmos: {} sailboats, {} bridge leaves, {} trains, {} ferries", m_sailboats.size(), m_bridges.size(),
              m_trains.size(), m_ferries.size());
}

std::unique_ptr<GizmoBody> Gizmos::makeBody(const std::string& model, bool yOnly) const {
    auto body = std::make_unique<GizmoBody>();
    body->model = model;
    body->data = m_data.find(model);
    body->bounds = &m_bounds;
    body->yOnly = yOnly;
    // lvlInstance flags 0x13 | 0x120, then flag 1 cleared: collidable (0x10),
    // terrain-collidable (0x100), hit by the wheels (0x20), not a banger
    // that breaks loose (1).
    body->collidable = true;
    body->terrainCollidable = true;
    body->wheelCollidable = true;
    body->audioId = body->data ? body->data->colliderId : 0;
    if (!body->data)
        log::warn("gizmos: {} has no banger data", model);
    return body;
}

void Gizmos::placeBanger(GizmoBody& body, const Mat34& placement) const {
    // dgUnhitBangerInstance::Init: SetMatrix of the placement with the CG
    // offset turned by it added to the position.
    Mat34 m = placement;
    if (body.data) {
        const Vec3& cg = body.data->cg;
        m.m3.x = m.m3.x + ((placement.m0.x * cg.x + placement.m1.x * cg.y) + placement.m2.x * cg.z);
        m.m3.y = m.m3.y + ((placement.m0.y * cg.x + placement.m1.y * cg.y) + placement.m2.y * cg.z);
        m.m3.z = m.m3.z + ((placement.m0.z * cg.x + placement.m1.z * cg.y) + placement.m2.z * cg.z);
    }
    body.setMatrix(m);
}

void Gizmos::moveToRoom(GizmoBody& body, int room) {
    // lvlLevel::MoveToRoom.
    if (body.listed && body.room == room)
        return;
    if (body.listed && body.room >= 0 && static_cast<std::size_t>(body.room) < m_rooms.size())
        std::erase(m_rooms[static_cast<std::size_t>(body.room)], &body);
    body.room = room;
    body.listed = room > 0;
    if (room > 0) {
        if (m_rooms.size() <= static_cast<std::size_t>(room))
            m_rooms.resize(static_cast<std::size_t>(room) + 1);
        m_rooms[static_cast<std::size_t>(room)].push_back(&body);
    }
}

void Gizmos::updateRoom(GizmoBody& body) {
    // FindRoomId(GetPosition(), the current room), MoveToRoom when it
    // changed.
    if (!m_level)
        return;
    const int room = m_level->findRoom(body.matrix().m3, body.room);
    if (room != body.room || !body.listed)
        moveToRoom(body, room);
}

void Gizmos::instancesIn(int room, std::vector<phys::Instance*>& out) const {
    if (room <= 0 || static_cast<std::size_t>(room) >= m_rooms.size())
        return;
    for (GizmoBody* b : m_rooms[static_cast<std::size_t>(room)])
        out.push_back(b);
}

// --- Sailboats -------------------------------------------------------------------------------

void Gizmos::loadSailboats(const vfs::Vfs& vfs, const city::PathSet& set, fx::Rand& random) {
    // gizSailboatMgr::Init.
    for (const auto& path : set.paths) {
        Sailboat s;
        // gizSailboat::Init: gizInstance::Init (the geometry, the banger
        // data's CG height and a random paint job), the spline at the
        // manager's speed, its start, the room.
        s.model = pathModel(vfs, path, kSailboatModel);
        s.data = m_data.find(s.model);
        s.cgY = s.data ? s.data->cg.y : 0.0f;
        // irand() % the geometry's variants: the modulo is taken when drawn.
        s.paint = random.irand();
        s.spline.init(pointsOf(path), kSailboatSpeed);
        Mat34 m;
        s.spline.update(m.m3, m.m2, 0.0f);
        m.normalize();
        s.matrix = m;
        s.matrix.m3.y = s.cgY + s.matrix.m3.y; // gizInstance::SetMatrix
        s.room = m_level ? m_level->findRoom(s.matrix.m3, 0) : 0;
        // The path's spacing plus or minus the manager's variation.
        const float hi = path.spacing + kSailboatSpeedVariation;
        const float lo = path.spacing - kSailboatSpeedVariation;
        s.spline.setSpeed((hi - lo) * random.frand() + lo);
        m_sailboats.push_back(std::move(s));
    }
}

void Gizmos::updateSailboat(Sailboat& s, float dt) {
    // gizSailboat::Update.
    Mat34 m;
    s.spline.update(m.m3, m.m2, dt);
    m.normalize();
    s.matrix = m;
    s.matrix.m3.y = s.cgY + s.matrix.m3.y;
    if (m_level)
        s.room = m_level->findRoom(s.matrix.m3, s.room);
}

// --- Bridges ---------------------------------------------------------------------------------

void Gizmos::loadBridges(const vfs::Vfs& vfs, const city::PathSet& set, std::vector<std::uint16_t>* roomFlags) {
    // gizBridgeMgr::Init: nothing without the default model.
    if (!geometryExists(vfs, kBridgeModel)) {
        log::warn("gizmos: couldn't find {} to load in gizBridgeMgr", kBridgeModel);
        return;
    }
    auto init = [&](const std::string& model, const Mat34& placement) {
        // gizBridge::Init.
        Bridge b;
        b.body = makeBody(model, false);
        b.placement = placement;
        placeBanger(*b.body, placement);
        m_bridges.push_back(std::move(b));
        Bridge& added = m_bridges.back();
        // Reset (vtable 0, with the constructor's type) and the room.
        resetBridgeState(added);
        updateRoom(*added.body);
        // The rooms at the hinge and 5 m above it get the level room flag
        // 0x10 (no skid marks there, vehCar::UpdateTrack).
        if (m_level && roomFlags) {
            for (const Vec3& at : {placement.m3, Vec3{placement.m3.x, placement.m3.y + kBridgeRoomProbe,
                                                      placement.m3.z}}) {
                const int room = m_level->findRoom(at, 0);
                if (room >= 0 && static_cast<std::size_t>(room) < roomFlags->size())
                    (*roomFlags)[static_cast<std::size_t>(room)] |= city::LevelRoomFlag::Bridge;
            }
        }
    };
    for (const auto& path : set.paths) {
        if (path.points.size() < 2)
            continue; // OpenMM2: MM2 reads past the points (no retail path has fewer than two)
        const auto& pts = path.points;
        const bool twoLeaves = pts.size() > 2;
        std::string model = bridgeModel(vfs, path.name);
        if (model.empty())
            model = kBridgeModel;
        const auto type = bridgeType(path.name);
        // A leaf at the first point opening away from the third (the
        // second on a two-point path) ...
        init(model, bridgeFrame(pts[0].position, pts[twoLeaves ? 2 : 1].position));
        if (type)
            m_bridges.back().type = *type;
        if (twoLeaves) {
            // ... and one at the third opening away from the first; each
            // triggers the other.
            init(model, bridgeFrame(pts[2].position, pts[0].position));
            if (type)
                m_bridges.back().type = *type;
            const int second = static_cast<int>(m_bridges.size()) - 1;
            m_bridges[static_cast<std::size_t>(second)].partner = second - 1;
            m_bridges[static_cast<std::size_t>(second) - 1].partner = second;
        }
    }
}

void Gizmos::resetBridgeState(Bridge& b) {
    // gizBridge::Reset.
    b.timer = 0.0f;
    b.state = Bridge::State::Down;
    b.angle = b.type == Bridge::Type::Open ? kGoalAngle : 0.0f;
    repositionBridge(b);
    if (b.audio)
        b.audio->stop(); // Aud3DAmbientObject::Reset: out of the 3D manager
}

void Gizmos::repositionBridge(Bridge& b) {
    // gizBridge::Reposition: the leaf's centre half its length (the banger
    // data's Size.z) behind the hinge, turned about the leaf's X axis by the
    // angle, placed by the hinge's frame, then lowered by 0.3 m.
    Mat34 m;
    m.m3.z = 0.0f - (b.body->data ? b.body->data->size.z : 0.0f) * 0.5f;
    m = Mat34::mul(m, Mat34::rotationX(b.angle)); // RotateFull about (1, 0, 0)
    m = Mat34::mul(m, b.placement);
    m.m3 = {kBridgeOffset.x + m.m3.x, kBridgeOffset.y + m.m3.y, kBridgeOffset.z + m.m3.z};
    b.body->setMatrix(m);
}

bool Gizmos::triggerBridge(std::size_t i) {
    // gizBridge::Trigger: a leaf that is down starts to rise, and so does
    // its partner.
    Bridge& b = m_bridges[i];
    if (b.state != Bridge::State::Down)
        return false;
    b.state = Bridge::State::Raising;
    b.timer = 0.0f;
    if (b.partner >= 0)
        triggerBridge(static_cast<std::size_t>(b.partner));
    return true;
}

void Gizmos::updateBridge(Bridge& b, float dt) {
    // gizBridge::Update.
    updateRoom(*b.body);
    const float goal = kGoalAngle;
    bool moved = false;
    if (b.state == Bridge::State::Down && b.type == Bridge::Type::Timed) {
        b.timer = dt + b.timer;
        if (kDownInterval < b.timer) {
            b.timer = 0.0f;
            b.state = Bridge::State::Raising;
            if (b.audio)
                b.audio->activate(-1);
        }
    } else if (b.state == Bridge::State::Up) {
        b.timer = dt + b.timer;
        if (kUpInterval < b.timer) {
            b.timer = 0.0f;
            b.state = Bridge::State::Lowering;
            if (b.audio)
                b.audio->activate(-1);
        }
    } else if (b.state == Bridge::State::Lowering) {
        if (b.angle < 0.0f) {
            b.angle = kLiftSpeed * dt + b.angle;
            if (0.0f < b.angle)
                b.angle = 0.0f;
        } else if (0.0f < b.angle) {
            b.angle = b.angle - kLiftSpeed * dt;
            if (b.angle < 0.0f)
                b.angle = 0.0f;
        }
        if (b.angle == 0.0f) {
            if (b.audio)
                b.audio->deactivate(-1);
            b.timer = 0.0f;
            b.state = Bridge::State::Down;
        }
        moved = true;
    } else if (b.state == Bridge::State::Raising) {
        if (b.angle < goal) {
            b.angle = dt * kLiftSpeed + b.angle;
            if (goal < b.angle)
                b.angle = goal;
        } else if (goal < b.angle) {
            b.angle = b.angle - dt * kLiftSpeed;
            if (b.angle < goal)
                b.angle = goal;
        }
        if (goal == b.angle) {
            if (b.audio)
                b.audio->deactivate(-1);
            b.timer = 0.0f;
            b.state = Bridge::State::Up;
        }
        moved = true;
    }
    if (moved)
        repositionBridge(b);
}

// --- Trains ----------------------------------------------------------------------------------

void Gizmos::loadTrains(const vfs::Vfs& vfs, const city::PathSet& set) {
    // gizTrainMgr::Init (its ApplyTuning does nothing).
    for (const auto& path : set.paths) {
        Train t;
        const std::string model = pathModel(vfs, path, kTrainModel);
        for (auto& car : t.cars) {
            // gizTrainCar::Init: the spline at 1 m/s, its start, the body
            // there (a dgUnhitMtxBangerInstance).
            car.spline.init(pointsOf(path), 1.0f);
            Mat34 m;
            car.spline.update(m.m3, m.m2, 0.0f);
            m.normalize();
            car.body = makeBody(model, false);
            placeBanger(*car.body, m);
        }
        m_trains.push_back(std::move(t));
        resetTrain(m_trains.back()); // gizTrain::Init's Reset
    }
}

void Gizmos::resetTrain(Train& t) {
    // gizTrain::Reset: in its first station, forward, at full speed factor;
    // gizTrainCar::Reset(i): car i starts 0.44 s of travel at 40 m/s ahead.
    t.state = Train::State::InStation;
    t.timer = 0.0f;
    t.speedFactor = 1.0f;
    t.forward = true;
    if (t.audio)
        t.audio->stop();
    for (std::size_t i = 0; i < t.cars.size(); ++i) {
        TrainCar& car = t.cars[i];
        car.spline.reset();
        car.spline.setSpeed(kTrainSpeed);
        updateTrainCar(car, static_cast<float>(static_cast<int>(i)) * kCarSpacing);
    }
}

void Gizmos::updateTrainCar(TrainCar& c, float dt) {
    // gizTrainCar::Update: the spline's point, but the height straight
    // between the segment's two points and the tangent's rise to match,
    // then up by the CG height.
    Mat34 m;
    c.spline.update(m.m3, m.m2, dt);
    const float y0 = c.spline.vertex(c.spline.index()).y;
    const float rise = c.spline.vertex(c.spline.next()).y - y0;
    m.m2.y = rise;
    m.m3.y = c.spline.currentRatio() * rise + y0;
    m.normalize();
    m.m3.y = m.m3.y + (c.body->data ? c.body->data->cg.y : 0.0f);
    c.body->setMatrix(m);
    updateRoom(*c.body);
}

bool Gizmos::inStation(const Train& t) const {
    // gizTrain::InStation: going forward, the last car reaches the third
    // point from the end (gizTrainCar::IsLastStop); going back, the first
    // car is back on the first two points (IsFirstStop).
    if (t.forward) {
        const PathSpline& s = t.cars[0].spline;
        return s.vertexCount() - 3 <= s.index();
    }
    return t.cars[2].spline.index() < 2;
}

void Gizmos::updateTrain(Train& t, float dt) {
    // gizTrain::Update.
    const float step = t.forward ? dt : dt * -1.0f;
    switch (t.state) {
    case Train::State::InStation:
        t.timer = dt + t.timer;
        if (kStationWait < t.timer) {
            t.timer = 0.0f;
            t.state = Train::State::Accelerating; // (CalcTrainAccel only reads the ratio)
        }
        break;
    case Train::State::Running:
        for (auto& car : t.cars)
            updateTrainCar(car, step);
        if (inStation(t))
            t.state = Train::State::Braking;
        break;
    case Train::State::Braking:
        if (t.speedFactor < 0.0f) {
            t.speedFactor = kTrainAccel * dt + t.speedFactor;
            if (0.0f < t.speedFactor)
                t.speedFactor = 0.0f;
        } else if (0.0f < t.speedFactor) {
            t.speedFactor = t.speedFactor - kTrainAccel * dt;
            if (t.speedFactor < 0.0f)
                t.speedFactor = 0.0f;
        }
        if (t.speedFactor == 0.0f) {
            // In the station: it leaves the other way.
            t.state = Train::State::InStation;
            t.forward = !t.forward;
        } else {
            for (auto& car : t.cars)
                updateTrainCar(car, step * t.speedFactor);
        }
        break;
    case Train::State::Accelerating:
        if (t.speedFactor < 1.0f) {
            t.speedFactor = kTrainAccel * dt + t.speedFactor;
            if (1.0f < t.speedFactor)
                t.speedFactor = 1.0f;
        } else if (1.0f < t.speedFactor) {
            t.speedFactor = t.speedFactor - kTrainAccel * dt;
            if (t.speedFactor < 1.0f)
                t.speedFactor = 1.0f;
        }
        if (t.speedFactor == 1.0f) {
            t.state = Train::State::Running;
        } else {
            for (auto& car : t.cars)
                updateTrainCar(car, step * t.speedFactor);
        }
        break;
    }
}

// --- Ferries ---------------------------------------------------------------------------------

void Gizmos::loadFerries(const vfs::Vfs& vfs, const city::PathSet& set, fx::Rand& random) {
    // gizFerryMgr::Init.
    for (const auto& path : set.paths) {
        Ferry f;
        // gizFerry::Init: the spline at 1 m/s, its start, the body there (a
        // dgUnhitYBangerInstance, which keeps a rotation about Y only).
        const std::string model = pathModel(vfs, path, kFerryModel);
        f.spline.init(pointsOf(path), 1.0f);
        Mat34 m;
        f.spline.update(m.m3, m.m2, 0.0f);
        m.normalize();
        f.body = makeBody(model, true);
        placeBanger(*f.body, m);
        m_ferries.push_back(std::move(f));
    }
    // gizFerryMgr::ApplyTuning: the speed plus or minus its variation.
    for (auto& f : m_ferries) {
        const float hi = kFerrySpeed + kFerrySpeedVariation;
        const float lo = kFerrySpeed - kFerrySpeedVariation;
        f.spline.setSpeed((hi - lo) * random.frand() + lo);
    }
}

void Gizmos::updateFerry(Ferry& f, float dt) {
    // gizFerry::Update: the spline's point and tangent, up by the CG height
    // (the whole CG offset only at Init), the room.
    Mat34 m;
    f.spline.update(m.m3, m.m2, dt);
    m.normalize();
    m.m3.y = m.m3.y + (f.body->data ? f.body->data->cg.y : 0.0f);
    f.body->setMatrix(m);
    updateRoom(*f.body);
}

// --- Frame -----------------------------------------------------------------------------------

void Gizmos::loadAudio(const vfs::Vfs& vfs, audio::SoundBank& bank, audio::Mixer& mixer,
                       audio::game::Object3DManager* manager) {
    for (auto& b : m_bridges) {
        b.audio = std::make_unique<audio::game::BridgeAudio>();
        if (!b.audio->load(vfs, bank, mixer, "drawbridge", manager))
            b.audio.reset();
    }
    for (auto& t : m_trains) {
        t.audio = std::make_unique<audio::game::SubwayAudio>();
        if (!t.audio->load(vfs, bank, mixer, "subwaycar", manager))
            t.audio.reset();
    }
    for (auto& f : m_ferries) {
        f.audio = std::make_unique<audio::game::AmbientObject>();
        if (!f.audio->load(vfs, bank, mixer, "ferry", manager))
            f.audio.reset();
    }
}

void Gizmos::reset() {
    // gizSailboatMgr::Reset (gizSailboat::Reset: the spline only).
    for (auto& s : m_sailboats)
        s.spline.reset();
    // gizBridgeMgr::Reset.
    for (auto& b : m_bridges)
        resetBridgeState(b);
    // gizTrainMgr::Reset.
    for (auto& t : m_trains)
        resetTrain(t);
    // gizFerryMgr::Reset (gizFerry::Reset: the sound, the room, the spline).
    for (auto& f : m_ferries) {
        if (f.audio)
            f.audio->stop();
        updateRoom(*f.body);
        f.spline.reset();
    }
}

void Gizmos::update(float dt, const std::optional<Vec3>& trigger) {
    // The managers in the order mmGame::InitGizmos added them.
    for (auto& s : m_sailboats)
        updateSailboat(s, dt);
    // gizBridgeMgr::Update: a proximity bridge that is down rises when a
    // trigger comes within 100 m of it (gizBridgeMgr::CheckProximity).
    for (std::size_t i = 0; i < m_bridges.size(); ++i) {
        Bridge& b = m_bridges[i];
        if (b.type == Bridge::Type::Proximity && b.state == Bridge::State::Down && trigger) {
            const Vec3& p = b.body->matrix().m3;
            const Vec3& t = *trigger;
            const float d2 = (t.x - p.x) * (t.x - p.x) + (t.y - p.y) * (t.y - p.y) + (t.z - p.z) * (t.z - p.z);
            if (d2 < kProximityDist2)
                triggerBridge(i);
        }
        updateBridge(b, dt);
    }
    for (auto& t : m_trains)
        updateTrain(t, dt);
    for (auto& f : m_ferries)
        updateFerry(f, dt);
}

void Gizmos::updateAudio(const Mat34& listener, float dt, bool inTunnel) {
    // gizBridge::Update: the drawbridge sounds at the leaf, speed 0.
    for (auto& b : m_bridges)
        if (b.audio) {
            b.audio->setPosition(b.body->matrix().m3);
            b.audio->update(listener, 0.0f, dt, inTunnel);
        }
    // gizTrain::Update: the train's sounds at its middle car, at 50 while
    // it moves and 0 in a station (aiSubwayAudio::Update).
    for (auto& t : m_trains)
        if (t.audio) {
            t.audio->setPosition(t.cars[1].body->matrix().m3);
            t.audio->update(listener, t.state != Train::State::InStation ? kTrainAudioSpeed : 0.0f, dt, inTunnel);
        }
    // gizFerry::Update: the ferry sounds at the ferry, speed 0.
    for (auto& f : m_ferries)
        if (f.audio) {
            f.audio->setPosition(f.body->matrix().m3);
            f.audio->update(listener, 0.0f, dt, inTunnel);
        }
}

void Gizmos::stopAudio() {
    for (auto& b : m_bridges)
        if (b.audio)
            b.audio->stop();
    for (auto& t : m_trains)
        if (t.audio)
            t.audio->stop();
    for (auto& f : m_ferries)
        if (f.audio)
            f.audio->stop();
}

void Gizmos::draw(render::Device& device, ModelLibrary& models, TextureLibrary& textures, const Frustum& frustum,
                  const Camera& camera, const ObjectDetail& detail) const {
    const Mat34& cam = camera.transform;
    auto unlit = [](const GizmoBody& b) {
        return b.data && (b.data->billFlags & bangers::BangerData::kUnlit) != 0;
    };
    // gizBridgeMgr::Cull: by the distance from the camera to the hinge.
    for (const auto& b : m_bridges) {
        const auto lod = managerLod(distanceTo(cam.m3, b.placement.m3), kCullDistance);
        if (!lod || !frustum.intersectsSphere(b.body->matrix().m3, modelRadius(models, b.body->model)))
            continue;
        drawModel(device, models, textures, b.body->model, b.body->paint, b.body->matrix(), *lod, unlit(*b.body));
    }
    // gizFerryMgr::Cull: by the distance from the camera to the ferry.
    for (const auto& f : m_ferries) {
        const auto lod = managerLod(distanceTo(cam.m3, f.body->matrix().m3), kCullDistance);
        if (!lod || !frustum.intersectsSphere(f.body->matrix().m3, modelRadius(models, f.body->model)))
            continue;
        drawModel(device, models, textures, f.body->model, f.body->paint, f.body->matrix(), *lod, unlit(*f.body));
    }
    // The train cars and the sailboats are instances of their rooms
    // (lvlInstance::IsVisible with the dynamic objects' NoDraw limit).
    for (const auto& t : m_trains)
        for (const auto& c : t.cars) {
            const Mat34& m = c.body->matrix();
            const float r = modelRadius(models, c.body->model);
            const auto lod = objectLod(viewDepth(cam, m.m3), r, detail, detail.noDraw);
            if (lod && frustum.intersectsSphere(m.m3, r))
                drawModel(device, models, textures, c.body->model, c.body->paint, m, *lod, unlit(*c.body));
        }
    for (const auto& s : m_sailboats) {
        const float r = modelRadius(models, s.model);
        const auto lod = objectLod(viewDepth(cam, s.matrix.m3), r, detail, detail.noDraw);
        // gizInstance::Draw: lit, with its paint job.
        if (lod && frustum.intersectsSphere(s.matrix.m3, r))
            drawModel(device, models, textures, s.model, s.paint, s.matrix, *lod, false);
    }
}

std::unique_ptr<Gizmos> initGizmos(const vfs::Vfs& vfs, city::CityData& city, const RaceConfig& config,
                                   bool multiplayer, bangers::BangerSet& bangers,
                                   const bangers::BangerDataLibrary& data, const phys::Level* level,
                                   fx::Rand& random) {
    auto gizmos = std::make_unique<Gizmos>(data, bangers);
    Gizmos::Options options;
    options.city = config.city;
    options.mode = config.mode;
    options.raceIndex = config.raceIndex;
    options.multiplayer = multiplayer;
    gizmos->load(vfs, options, level, &city.levelRoomFlags, random);
    if (GizmoKinds::forSession(config.mode, multiplayer).parkedCars) {
        const std::string path = gizmoPathSetPath(vfs, config.city, "parkedcar", config.mode, config.raceIndex);
        if (!path.empty()) {
            auto bytes = vfs.readAll(path);
            std::string error;
            if (auto set = bytes ? city::parsePathSet(*bytes, &error) : std::nullopt) {
                auto cars = placeParkedCars(*set, random);
                log::info("gizmos: {} parked cars ({})", cars.size(), path);
                bangers.add(cars);
            } else {
                log::warn("gizmos: {}: {}", path, error);
            }
        }
    }
    gizmos->reset();
    return gizmos;
}

} // namespace mm2::game::world
