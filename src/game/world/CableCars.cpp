// San Francisco's cable cars: aiCableCar and aiCableCarInstance, ported
// from the code of midtown2.exe build 3393 (MM2Recomp; documentation only).
#include "game/world/CableCars.h"

#include "ai/MapView.h"
#include "ai/PathGeometry.h"
#include "ai/Traffic.h"
#include "audio/game/Ambience.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>

namespace mm2::game::world {
namespace {

// aiRailSet::ComputeXZCurve: the Hermite coefficients of x and z, summed in
// its order (ai/Traffic.cpp keeps the same for the ambient cars).
void hermite(float p0, float p1, float m0, float m1, float out[4]) {
    out[0] = ((-2.0f * p1 + m0) + m1) + 2.0f * p0;
    out[1] = ((-m1 + -2.0f * m0) + 3.0f * p1) + -3.0f * p0;
    out[2] = m0;
    out[3] = p0;
}

constexpr Vec3 kUp{0.0f, 1.0f, 0.0f};

float invMag(const Vec3& v) {
    const float m2 = (v.x * v.x + v.y * v.y) + v.z * v.z;
    return m2 == 0.0f ? 0.0f : 1.0f / std::sqrt(m2);
}

} // namespace

struct CableCars::Car {
    int index = 0;
    int startPath = 0; // +0xd0
    int startDir = 1;  // +0xd2
    // aiRailSet: the bumper and side distances from the car's origin, the
    // current curve's x and z coefficients.
    float backBumper = 0.0f, frontBumper = 0.0f, leftSide = 0.0f, rightSide = 0.0f; // +0x48 .. +0x54
    float kx[4] = {}, kz[4] = {};
    bool entered = false;  // +0x58: may enter the intersection ahead
    float roadDist = 0.0f; // +0x5c: travelled on this road
    float segDist = 0.0f;  // +0x64: travelled on the current curve
    float segLen = 1.0f;   // +0x68: the current curve's length
    int vert = 1;          // +0x72: the section ahead on the line
    int railType = 0;      // +0x7c: 0 a road, 1 an intersection
    int nextPath = -1;     // +0x80
    int path = 0;          // +0x84
    int dir = 1;           // +0x88
    int nextDir = 1;       // +0x8a
    float accel = 0.0f;     // +0x8c
    float target = 0.0f;    // +0x94: the speed it accelerates or brakes to
    float accelRate = 0.0f; // +0x9c: frand() * 2 + 1.5
    float react = kReactDistance; // +0xa4
    int sister = -1;       // +0xc8
    bool atStop = false;   // +0xcc: queued at a four-way stop
    bool active = true;    // +0xce
    int room = 0;          // +0xd4: aiMap::MapComponent's room
    int mapId = -1, mapType = -1, mapVert = -1, mapSide = 0; // +0xd6 .. +0xdc
    float speed = 0.0f;    // +0xe0
    float invLen = 1.0f;   // +0xe4
    Mat34 matrix;          // +0x14: model origin on the ground, facing -Z
    std::array<phys::ProbeCache, 3> probes; // +0x24, +0x30, +0x3c (lvlSegmentInfo)
    std::unique_ptr<Body> body;             // +0x10
    std::unique_ptr<audio::game::CableCarAudio> audio; // +0xe8
    std::unique_ptr<Obstacle> obstacle;     // its entry in the traffic's lists
    int entry = -1;
};

// aiCableCar::CurrentRoadIdx, the slot of a driver's window of three roads
// (and the vertex there) the car is in: on one of the roads, that slot (the
// section ahead, counted the window's way; on the turn at the road's end,
// vertex 0 of the next slot, or of this one against the vertex order);
// else the slot after a road whose end the car's road shares (as coded, the
// car's road's end-0 intersection, whatever way it goes), vertex 1; else
// slot 0 when the first road starts there; else none.
class CableCars::Obstacle final : public ai::Traffic::ExternalVehicle {
public:
    Obstacle(const CableCars& owner, const Car& car) : m_owner(owner), m_car(car) {}
    int currentRoadIdx(const int roads[3], const bool dirs[3], int* vert) const override {
        const auto& net = m_owner.m_ai.map().net();
        const int path = m_car.path;
        const int n = m_owner.sections(path);
        for (int i = 0; i < 3; ++i) {
            if (roads[i] != path)
                continue;
            if (n == m_car.vert) {
                *vert = 0;
                return dirs[i] ? i + 1 : i;
            }
            *vert = dirs[i] ? m_car.vert : n - m_car.vert;
            return i;
        }
        const int end0 = net.paths()[static_cast<std::size_t>(path)].intersection[0];
        for (int i = 0; i < 3; ++i) {
            if (roads[i] < 0)
                continue;
            const ai::PathInfo& w = net.paths()[static_cast<std::size_t>(roads[i])];
            if (end0 == w.intersection[dirs[i] ? 0 : 1]) {
                *vert = 1;
                return i + 1;
            }
        }
        if (roads[0] >= 0 && end0 == net.paths()[static_cast<std::size_t>(roads[0])].intersection[1]) {
            *vert = 1;
            return 0;
        }
        return -1;
    }

private:
    const CableCars& m_owner;
    const Car& m_car;
};

// aiCableCarInstance: a dgUnhitBangerInstance whose matrix is the car's
// (GetMatrix / SetMatrix / GetPosition read the car's), with flags 0x13:
// collidable and a banger, not terrain-collidable, not hit by the wheels.
class CableCars::Body final : public phys::Instance {
public:
    // dgBangerInstance::GetBound: the banger data's bound (centred on its
    // CG) placed by the car's matrix, which is at the model's origin.
    const phys::Bound* bound(int which) const override {
        return data && bounds ? bounds->boundOf(*data, which) : nullptr;
    }
    const Mat34& matrix() const override { return car->matrix; }
    float radius() const override { return data && bounds ? bounds->boundRadius(*data) : 0.0f; }
    bool isBanger() const override { return true; }
    float bangerImpulseLimit2() const override { return data ? data->impulseLimit2 : 0.0f; }
    bool bangerSphere(Vec3& centre, float& r) const override {
        if (!data || data->yRadius == 0.0f)
            return false;
        const Mat34& m = car->matrix;
        const Vec3& cg = data->cg;
        centre = {m.m3.x - (m.m0.x * cg.x + cg.y * m.m1.x + cg.z * m.m2.x),
                  m.m3.y - (m.m0.y * cg.x + cg.y * m.m1.y + cg.z * m.m2.y),
                  m.m3.z - (m.m0.z * cg.x + cg.y * m.m1.z + cg.z * m.m2.z)};
        r = data->yRadius;
        return true;
    }
    // dgUnhitBangerInstance::Impact would knock the car loose (then
    // aiCableCar::Update stops its sister and resets it once no ambient
    // car comes along its road). Its ImpulseLimit2 of 7.6e9 makes that out
    // of reach; OpenMM2 keeps it standing.

    const Car* car = nullptr;
    const bangers::BangerData* data = nullptr;
    const bangers::BangerSet* bounds = nullptr;
    bool listed = false;
};

CableCars::CableCars(ai::World& ai, const bangers::BangerDataLibrary& data, const bangers::BangerSet& bounds)
    : m_ai(ai), m_data(data), m_bounds(bounds) {}

CableCars::~CableCars() { stopAudio(); }

const Mat34& CableCars::matrix(std::size_t i) const { return m_cars[i]->matrix; }
float CableCars::speed(std::size_t i) const { return m_cars[i]->speed; }
int CableCars::path(std::size_t i) const { return m_cars[i]->path; }
int CableCars::dir(std::size_t i) const { return m_cars[i]->dir; }
int CableCars::room(std::size_t i) const { return m_cars[i]->body->room; }
int CableCars::sister(std::size_t i) const { return m_cars[i]->sister; }
int CableCars::startPath(std::size_t i) const { return m_cars[i]->startPath; }

// --- The map ---------------------------------------------------------------------------------

const city::AiPath& CableCars::source(int path) const {
    return m_ai.map().map().paths[static_cast<std::size_t>(path)];
}

const std::vector<Vec3>* CableCars::line(int path, int dir) const {
    // aiPath +0x78 / +0xdc (HasCableCarLine): the side's tram polyline,
    // after its lanes and sidewalk; direction 1 rides the second side.
    const city::AiPath& p = source(path);
    const city::AiRoadSide& side = dir == 1 ? p.right : p.left;
    if (side.numTrams == 0)
        return nullptr;
    const std::size_t k = static_cast<std::size_t>(side.numLanes) + side.numSidewalks;
    return k < side.polylines.size() ? &side.polylines[k] : nullptr;
}

int CableCars::sections(int path) const { return static_cast<int>(source(path).center.size()); }

float CableCars::centerLength(int path, int a, int b) const {
    return ai::pathCenterLength(source(path), a, b);
}

namespace {

// aiPath::SubSectionDir: the section's tangent, reversed on side -1, scaled.
Vec3 subSectionDir(const city::AiPath& p, int i, float scale, int dir) {
    const int n = static_cast<int>(p.wAxis.size());
    const Vec3 w = dir == 1 ? p.wAxis[static_cast<std::size_t>(std::clamp(i, 0, n - 1))]
                            : -p.wAxis[static_cast<std::size_t>(std::clamp(n - i - 1, 0, n - 1))];
    return {scale * w.x, scale * w.y, scale * w.z};
}

// aiPath::IntersectionEntryVector: the travel direction at the road's end.
Vec3 entryVector(const city::AiPath& p, float scale, int dir) {
    const Vec3 z = dir == 1 ? -p.zAxis.back() : p.zAxis.front();
    return {scale * z.x, scale * z.y, scale * z.z};
}

// aiPath::IntersectionExitVector: the travel direction at the road's start.
Vec3 exitVector(const city::AiPath& p, float scale, int dir) {
    const Vec3 z = dir == 1 ? -p.zAxis.front() : p.zAxis.back();
    return {scale * z.x, scale * z.y, scale * z.z};
}

} // namespace

bool CableCars::isCableCarStart(int intersection, int& path, int& dir) const {
    // aiIntersection::IsCableCarStart.
    const auto& net = m_ai.map().net();
    const ai::Intersection& node = net.intersections()[static_cast<std::size_t>(intersection)];
    int found = 0;
    for (int p : node.paths) {
        const ai::PathInfo& info = net.paths()[static_cast<std::size_t>(p)];
        // Leaving here along direction 1 (the road starts here) or -1.
        if (info.intersection[1] == intersection) {
            if (line(p, 1)) {
                path = p;
                dir = 1;
                ++found;
            }
        } else if (line(p, -1)) {
            path = p;
            dir = -1;
            ++found;
        }
    }
    return found == 1;
}

bool CableCars::nextLink(int path, int dir, int& next, int& nextDir) const {
    // aiCableCar::DetermineNextLink: at the intersection ahead, the roads
    // with a line on their second side (HasCableCarLine(1)). One: back
    // along this road. Two: the other one, the first after this road in
    // the intersection's order. Four: the second one after it (straight
    // across). Otherwise none.
    const auto& net = m_ai.map().net();
    const ai::PathInfo& info = net.paths()[static_cast<std::size_t>(path)];
    const int end = dir == 1 ? 0 : 1;
    const int node = info.intersection[end];
    if (node < 0)
        return false;
    const auto& roads = net.intersections()[static_cast<std::size_t>(node)].paths;
    const int count = static_cast<int>(roads.size());
    int lines = 0;
    for (int p : roads)
        if (line(p, 1))
            ++lines;
    if (lines == 1) {
        next = path;
        nextDir = -dir;
        return true;
    }
    if (lines != 2 && lines != 4)
        return false;
    const int wanted = lines == 2 ? 1 : 2;
    int k = info.roadIndex[end];
    int seen = 0;
    for (int tries = 0; tries < count; ++tries) {
        k = k + 1;
        if (count - 1 < k)
            k = k - count;
        if (k < 0)
            continue;
        if (line(roads[static_cast<std::size_t>(k)], 1) && ++seen == wanted) {
            next = roads[static_cast<std::size_t>(k)];
            // Direction 1 when the next road starts at this intersection.
            nextDir = net.paths()[static_cast<std::size_t>(next)].intersection[1] != node ? -1 : 1;
            return true;
        }
    }
    return false;
}

// --- Set-up ----------------------------------------------------------------------------------

void CableCars::create(fx::Rand& random) {
    // aiMap::Init: the starts, in the intersections' order, then the cars.
    const auto& net = m_ai.map().net();
    for (int i = 0; i < static_cast<int>(net.intersections().size()); ++i) {
        int path = -1, dir = 1;
        if (!isCableCarStart(i, path, dir))
            continue;
        auto c = std::make_unique<Car>();
        c->index = static_cast<int>(m_cars.size());
        // aiCableCar::Init: its start, its instance (BeginGeom of the model
        // with its SHADOW and HLIGHT parts), a random acceleration, the
        // bumper and side distances from the box of its bound.
        c->startPath = path;
        c->startDir = dir;
        c->body = std::make_unique<Body>();
        c->body->car = c.get();
        c->body->data = m_data.find(kModel);
        c->body->bounds = &m_bounds;
        c->body->collidable = true;
        c->body->terrainCollidable = false;
        c->body->wheelCollidable = false;
        c->body->audioId = c->body->data ? c->body->data->colliderId : 0;
        const float f = random.frand();
        c->accel = 0.0f;
        c->target = 0.0f;
        c->accelRate = f + f + 1.5f;
        if (const phys::Bound* b = c->body->bound(0)) {
            // lvlInstance::GetBound(3): the box around the bound.
            c->rightSide = b->boxMax.x;
            c->backBumper = b->boxMax.z;
            c->frontBumper = -b->boxMin.z;
            c->leftSide = -b->boxMin.x;
        } else {
            log::warn("cable cars: {} has no banger data", kModel);
        }
        c->obstacle = std::make_unique<Obstacle>(*this, *c);
        c->entry = m_ai.traffic().addExternal(c->obstacle.get());
        m_cars.push_back(std::move(c));
    }
    log::info("cable cars: {}", m_cars.size());
    for (auto& c : m_cars)
        determineSister(*c);
}

void CableCars::determineSister(Car& c) {
    // aiCableCar::DetermineSister: from the car's start, road by road
    // (DetermineNextLink) until the intersection ahead is a start
    // (IsCableCarStart, at most 99999 roads); the car starting on that
    // start's road. As coded, the direction followed is only ever changed
    // by IsCableCarStart's outputs (DetermineNextLink's is dropped), and the
    // road IsCableCarStart last reported is the one looked up.
    int path = c.startPath;
    int dir = c.startDir;
    int found = -1;
    const auto& net = m_ai.map().net();
    for (int tries = 99999; tries != 0; --tries) {
        const ai::PathInfo& info = net.paths()[static_cast<std::size_t>(path)];
        const int node = info.intersection[dir == 1 ? 0 : 1];
        if (node >= 0 && isCableCarStart(node, found, dir))
            break;
        int next = path, nextDir = dir;
        if (nextLink(path, dir, next, nextDir))
            path = next;
    }
    for (const auto& other : m_cars)
        if (other->startPath == found) {
            c.sister = other->index;
            return;
        }
}

void CableCars::reset(const phys::World& world) {
    // (aiMap::Reset empties the roads' and intersections' lists and stop
    // queues the cars share with the traffic: Traffic::reset.)
    for (auto& c : m_cars)
        resetCar(*c, world);
}

const CableCars::Car* CableCars::carOfEntry(int entry) const {
    for (const auto& c : m_cars)
        if (c->entry == entry)
            return c.get();
    return nullptr;
}

ai::TrackedCar CableCars::tracked(std::size_t i, int id) const {
    const Car& c = *m_cars[i];
    ai::TrackedCar t;
    t.id = id;
    t.ambient = c.entry;
    t.position = c.matrix.m3;
    t.forward = -c.matrix.m2;
    t.right = c.matrix.m0;
    t.speed = c.speed;
    t.frontBumper = c.frontBumper;
    t.backBumper = c.backBumper;
    t.leftSide = c.leftSide;
    t.rightSide = c.rightSide;
    return t;
}

void CableCars::resetCar(Car& c, const phys::World& world) {
    // aiCableCar::Reset.
    c.mapType = c.mapId = c.mapVert = -1;
    c.room = 0;
    c.active = true;
    c.atStop = false;
    c.speed = 0.0f;
    c.accel = 0.0f;
    c.target = 0.0f;
    c.react = kReactDistance;
    c.entered = false;
    c.dir = c.startDir;
    c.path = c.startPath;
    c.nextPath = c.path;
    c.nextDir = c.dir;
    nextLink(c.path, c.dir, c.nextPath, c.nextDir);
    c.vert = 1;
    c.roadDist = 0.0f;
    c.segDist = 0.0f;
    const int n = sections(c.path);
    c.segLen = c.dir == 1 ? centerLength(c.path, 0, 1) : centerLength(c.path, n - 2, n - 1);
    c.invLen = 1.0f / c.segLen;
    const auto& pts = *line(c.path, c.dir);
    const city::AiPath& p = source(c.path);
    const Vec3 t0 = exitVector(p, c.segLen, c.dir);
    const Vec3 t1 = subSectionDir(p, 1, c.segLen, c.dir);
    hermite(pts[0].x, pts[1].x, t0.x, t1.x, c.kx);
    hermite(pts[0].z, pts[1].z, t0.z, t1.z, c.kz);
    c.railType = 0;
    c.matrix.m3 = pts[0];
    solvePositionAndOrientation(c, world);
    c.room = p.rooms.empty() ? 0 : p.rooms.front();
    if (c.audio)
        c.audio->reset(); // aiCableCarAudio::Reset
    updateRoom(c, world);
}

// --- Update ----------------------------------------------------------------------------------

void CableCars::update(float dt, const ai::TrackedCar* player, const phys::World& world) {
    for (auto& c : m_cars)
        updateCar(*c, dt, player, world);
}

void CableCars::updateCar(Car& c, float dt, const ai::TrackedCar* player, const phys::World& world) {
    // aiCableCar::Update (its sounds: updateAudio). A car knocked loose
    // (instance flag 1 cleared) would stop its sister and wait for its road
    // to be clear of approaching ambient cars to reset; see Body.
    if (!c.active)
        return;
    solveVelocity(c, dt, player);
    if (c.speed != 0.0f) {
        solveRailType(c);
        solvePositionAndOrientation(c, world);
        updateObstacleMap(c);
        updateRoom(c, world);
    }
}

float CableCars::distanceToIntersection(const Car& c) const {
    // aiCableCar::DistanceToIntersection: on a road, the road's length less
    // the distance travelled and the front bumper; in an intersection, far.
    if (c.railType == 0)
        return centerLength(c.path, 0, sections(c.path) - 1) - (c.roadDist + c.frontBumper);
    return 9999.0f;
}

void CableCars::solveVelocity(Car& c, float dt, const ai::TrackedCar* player) {
    // aiCableCar::SolveVelocity.
    float obstacle = 0.0f;
    const float toIntersection = distanceToIntersection(c);
    if (checkForObstacles(c, obstacle, player)) {
        // Brake to a stop 2.5 m short of it.
        if (c.target != 0.0f) {
            c.target = 0.0f;
            c.accel = -((c.speed * c.speed) / ((obstacle - kObstacleGap) + (obstacle - kObstacleGap)));
        }
    } else if (!(toIntersection < c.react) || toIntersection <= 0.0f || c.railType == 1) {
        if (c.target < kMaxSpeed) {
            c.accel = c.accelRate;
            c.target = kMaxSpeed + 0.0001f;
        }
    } else {
        if (!c.entered && okayToEnterIntersection(c, toIntersection))
            c.entered = true;
        if (!c.entered) {
            // Brake to a stop 0.25 m short of the intersection.
            if (c.target != 0.0f) {
                c.target = 0.0f;
                const float gap = toIntersection - kStopGap;
                c.accel = -((c.speed * c.speed) / (gap + gap));
            } else if (c.speed < 0.2f && toIntersection < 0.5f) {
                c.speed = 0.0f;
            }
        } else if (c.target < kMaxSpeed) {
            c.accel = c.accelRate;
            c.target = kMaxSpeed + 0.0001f;
        }
    }
    c.speed = dt * c.accel + c.speed;
    if ((c.speed < c.target + 0.05f && c.accel < 0.0f) || (c.target < c.speed && 0.0f < c.accel)) {
        c.accel = 0.0f;
        c.speed = c.target;
    }
    const float step = dt * c.speed;
    c.segDist = step + c.segDist;
    c.roadDist = step + c.roadDist;
}

bool CableCars::checkForObstacles(Car& c, float& distance, const ai::TrackedCar* player) const {
    // aiCableCar::CheckForObstacles: on a road short of its last section,
    // the player within 30 m (XZ), then the vehicles listed on the car's
    // side at the section ahead and, within 30 m of the curve's end, at the
    // section after it, each blocking the way to that section's line point
    // (aiVehicle::IsBlockingTarget with 30 m of reach, 2 m wide) less than
    // 30 m ahead.
    if (c.railType != 0 || !(c.vert < sections(c.path) - 1))
        return false;
    const Vec3& pos = c.matrix.m3;
    const auto& pts = *line(c.path, c.dir);
    auto blocks = [&](const ai::TrackedCar& o, const Vec3& target) {
        distance = ai::blockingDistance(o, pos, target, kObstacleReach, 2.0f);
        return -1.0f < distance && distance < kObstacleReach;
    };
    if (player) {
        const float dz = pos.z - player->position.z, dx = pos.x - player->position.x;
        if (dz * dz + dx * dx < 900.0f && blocks(*player, pts[static_cast<std::size_t>(c.vert)]))
            return true;
    }
    // The obstacles of a section, in the list's order (newest first): the
    // cable cars and the ambient cars listed there.
    const auto& traffic = m_ai.traffic();
    const auto& ambient = m_ai.cars();
    auto vehicles = [&](int section, const Vec3& target) {
        for (int id : traffic.roadVehicles(c.path, c.dir, section)) {
            if (traffic.isExternal(id)) {
                const Car* o = carOfEntry(id);
                if (!o || o == &c)
                    continue;
                if (blocks(tracked(static_cast<std::size_t>(o->index), o->index), target))
                    return true;
                continue;
            }
            const auto it = std::ranges::find(ambient, id, &ai::AmbientCar::id);
            if (it == ambient.end() || !it->data)
                continue;
            // aiVehicleSpline's bumper and side distances (aiVehicleSpline::Init).
            const ai::VehicleData& d = *it->data;
            ai::TrackedCar t;
            t.id = id;
            t.position = it->transform.m3;
            t.forward = -it->transform.m2;
            t.right = it->transform.m0;
            t.backBumper = d.cg.z + d.size.z * 0.5f;
            t.frontBumper = d.size.z * 0.5f - d.cg.z;
            t.leftSide = d.size.x * 0.5f - d.cg.x;
            t.rightSide = d.cg.x + d.size.x * 0.5f;
            if (blocks(t, target))
                return true;
        }
        return false;
    };
    if (vehicles(c.vert, pts[static_cast<std::size_t>(c.vert)]))
        return true;
    if (c.segLen - c.segDist < kObstacleReach &&
        vehicles(c.vert + 1, pts[static_cast<std::size_t>(c.vert + 1)]))
        return true;
    return false;
}

bool CableCars::okayToEnterIntersection(Car& c, float distance) {
    // aiCableCar::OkayToEnterIntersection, by the control at the road's end:
    // a road told to always stop never; a four-way stop once the car stands
    // within 1.5 m (below 0.5 m/s) and its turn comes; a light when green;
    // no control always. (A road told to always go, aiPath +0x160, would
    // let it go; OpenMM2's traffic has no such road.)
    const auto& net = m_ai.map().net();
    const ai::PathInfo& info = net.paths()[static_cast<std::size_t>(c.path)];
    const int end = c.dir == 1 ? 0 : 1;
    if (m_ai.traffic().alwaysStop(c.path))
        return false;
    switch (info.rule[end]) {
    case ai::EntryRule::StopSign: {
        if (!(c.speed < 0.5f && distance < 1.5f))
            return false;
        const int node = info.intersection[end];
        if (node < 0)
            return true;
        if (!c.atStop) {
            c.atStop = true;
            m_ai.traffic().joinStopSign(node, c.entry); // aiIntersection::AddToStopSignCntl
        }
        return m_ai.traffic().stopSignTurn(node, c.entry);
    }
    case ai::EntryRule::TrafficLight: {
        // The light of the road's end (aiIntersection's light set at the
        // road's index there): the first lane of the side.
        const auto& lanes = info.lanesOf(c.dir);
        if (lanes.empty())
            return true;
        const int slot = net.lanes()[static_cast<std::size_t>(lanes.front())].lightSlot;
        return slot < 0 || m_ai.lights().state(slot) == ai::LightState::Green;
    }
    case ai::EntryRule::Uncontrolled:
        return true;
    }
    return true;
}


void CableCars::solveRailType(Car& c) {
    // aiCableCar::SolveRailType: past the end of the current curve, the next
    // one: the next section of the road, the turn through the intersection
    // to the next road (rail type 1) after the last section, or from the
    // intersection onto the next road's first section.
    if (!(c.segLen < c.segDist))
        return;
    const auto& net = m_ai.map().net();
    Vec3 p0, p1, t0, t1;
    if (c.railType == 0) {
        c.vert = c.vert + 1;
        c.segDist = c.segDist - c.segLen;
        const city::AiPath& p = source(c.path);
        const int n = sections(c.path);
        const auto& pts = *line(c.path, c.dir);
        if (c.nextPath < 0 || c.vert <= n - 1) {
            if (n - 2 < c.vert) {
                // The last section, shortened by the front bumper (where the
                // intersection's curve will start).
                const int a = c.dir == 1 ? c.vert - 1 : 0;
                const int b = c.dir == 1 ? c.vert : 1;
                c.segLen = centerLength(c.path, a, b) - c.frontBumper;
                p0 = pts[static_cast<std::size_t>(c.vert - 1)];
                const float last = centerLength(c.path, n - 2, n - 1);
                const Vec3& q1 = pts[static_cast<std::size_t>(c.vert)];
                const Vec3& q0 = pts[static_cast<std::size_t>(c.vert - 1)];
                const float f = (last - c.frontBumper) / last;
                p1 = {(q1.x - q0.x) * f + q0.x, (q1.y - q0.y) * f + q0.y, (q1.z - q0.z) * f + q0.z};
                t0 = subSectionDir(p, c.vert - 1, c.segLen, c.dir);
                t1 = entryVector(p, c.segLen, c.dir);
            } else {
                const int i = c.dir == 1 ? c.vert : n - c.vert;
                c.segLen = centerLength(c.path, i - 1, i);
                p0 = pts[static_cast<std::size_t>(c.vert - 1)];
                p1 = pts[static_cast<std::size_t>(c.vert)];
                t0 = subSectionDir(p, c.vert - 1, c.segLen, c.dir);
                t1 = subSectionDir(p, c.vert, c.segLen, c.dir);
            }
            hermite(p0.x, p1.x, t0.x, t1.x, c.kx);
            hermite(p0.z, p1.z, t0.z, t1.z, c.kz);
        } else {
            // Into the intersection: from the point short of the road's end
            // by the front bumper to the next road's first line point; the
            // curve's length is the Manhattan distance between them.
            const auto& next = *line(c.nextPath, c.nextDir);
            p1 = next[0];
            const float last = centerLength(c.path, n - 2, n - 1);
            const Vec3& q1 = pts[static_cast<std::size_t>(n - 1)];
            const Vec3& q0 = pts[static_cast<std::size_t>(n - 2)];
            const float f = (last - c.frontBumper) / last;
            p0 = {(q1.x - q0.x) * f + q0.x, (q1.y - q0.y) * f + q0.y, (q1.z - q0.z) * f + q0.z};
            c.segLen = std::abs(p0.x - p1.x) + std::abs(p0.z - p1.z);
            t0 = entryVector(p, c.segLen, c.dir);
            t1 = exitVector(source(c.nextPath), c.segLen, c.nextDir);
            hermite(p0.x, p1.x, t0.x, t1.x, c.kx);
            hermite(p0.z, p1.z, t0.z, t1.z, c.kz);
            c.railType = 1;
        }
    } else {
        // Out of the intersection onto the next road.
        c.atStop = false;
        c.entered = false;
        c.react = kReactDistance;
        const ai::PathInfo& info = net.paths()[static_cast<std::size_t>(c.path)];
        // As coded: the stop sign at the road's direction-1 end, whatever
        // way the car came.
        if (info.rule[0] == ai::EntryRule::StopSign && info.intersection[0] >= 0)
            m_ai.traffic().leaveStopSign(info.intersection[0], c.entry); // RemoveFromStopSignCntl
        c.dir = c.nextDir;
        c.path = c.nextPath;
        int next = c.nextPath, nextDir = c.nextDir;
        if (nextLink(c.path, c.dir, next, nextDir)) {
            c.nextPath = next;
            c.nextDir = nextDir;
        }
        c.vert = 1;
        c.roadDist = c.segDist - c.segLen;
        c.segDist = c.segDist - c.segLen;
        const int n = sections(c.path);
        c.segLen = c.dir == 1 ? centerLength(c.path, 0, 1) : centerLength(c.path, n - 2, n - 1);
        const auto& pts = *line(c.path, c.dir);
        const city::AiPath& p = source(c.path);
        p0 = pts[0];
        p1 = pts[1];
        t0 = exitVector(p, c.segLen, c.dir);
        t1 = subSectionDir(p, 1, c.segLen, c.dir);
        hermite(p0.x, p1.x, t0.x, t1.x, c.kx);
        hermite(p0.z, p1.z, t0.z, t1.z, c.kz);
        c.railType = 0;
    }
    c.invLen = 1.0f / c.segLen;
}

void CableCars::solvePositionAndOrientation(Car& c, const phys::World& world) {
    // aiCableCar::SolvePositionAndOrientation: the point and the direction
    // on the curve (aiRailSet::SolveXZCurve, in the ground plane) ...
    const float t = c.invLen * c.segDist;
    const Vec3 pos{((t * c.kx[0] + c.kx[1]) * t + c.kx[2]) * t + c.kx[3], 0.0f,
                   ((t * c.kz[0] + c.kz[1]) * t + c.kz[2]) * t + c.kz[3]};
    Vec3 dir{((c.kx[1] + c.kx[1]) + t * c.kx[0] * 3.0f) * t + c.kx[2], 0.0f,
             ((c.kz[1] + c.kz[1]) + t * c.kz[0] * 3.0f) * t + c.kz[2]};
    const float l2 = (dir.x * dir.x + dir.z * dir.z) + dir.y * dir.y;
    const float inv = l2 == 0.0f ? 0.0f : 1.0f / std::sqrt(l2);
    dir = {dir.x * inv, dir.y * inv, dir.z * inv};
    const Vec3 side{-dir.z, 0.0f, dir.x};
    const auto& net = m_ai.map().net();
    const bool flat = (net.paths()[static_cast<std::size_t>(c.path)].flags & 0x8) != 0 && c.nextPath >= 0 &&
                      (net.paths()[static_cast<std::size_t>(c.nextPath)].flags & 0x8) != 0;
    if (flat) {
        // ... on two flat roads, level at the height of the line's first
        // point;
        c.matrix.m0 = side;
        c.matrix.m2 = {-dir.x, -dir.y, -dir.z};
        c.matrix.m1 = kUp;
        c.matrix.m3 = {pos.x, (*line(c.path, c.dir))[0].y, pos.z};
        return;
    }
    // ... else on the ground under its front left, front right and back
    // left corners (probes from 5 m above to 5 m below its height with the
    // wheels' mask; no hit keeps its height).
    const float y = c.matrix.m3.y;
    auto probe = [&](float x, float z, int k) {
        phys::RayHit hit;
        if (world.wheelProbe({x, y + 5.0f, z}, {x, y - 5.0f, z}, hit, nullptr,
                             &c.probes[static_cast<std::size_t>(k)]))
            return hit.position.y;
        return y;
    };
    const float lx = c.leftSide * side.x, lz = c.leftSide * side.z;
    const Vec3 fl{(pos.x - lx) + c.frontBumper * dir.x, 0.0f, (pos.z - lz) + c.frontBumper * dir.z};
    const Vec3 frontLeft{fl.x, probe(fl.x, fl.z, 0), fl.z};
    const Vec3 fr{c.rightSide * side.x + pos.x + c.frontBumper * dir.x, 0.0f,
                  c.rightSide * side.z + pos.z + c.frontBumper * dir.z};
    const Vec3 frontRight{fr.x, probe(fr.x, fr.z, 1), fr.z};
    const Vec3 bl{(pos.x - lx) - c.backBumper * dir.x, 0.0f, (pos.z - lz) - c.backBumper * dir.z};
    const Vec3 backLeft{bl.x, probe(bl.x, bl.z, 2), bl.z};
    Mat34& m = c.matrix;
    m.m0 = frontRight - frontLeft;
    m.m2 = backLeft - frontLeft;
    const float f = c.frontBumper / (c.frontBumper + c.backBumper);
    const float s = c.leftSide / (c.rightSide + c.leftSide);
    const Vec3 along{f * m.m2.x, f * m.m2.y, f * m.m2.z};
    const Vec3 across{s * m.m0.x + frontLeft.x, s * m.m0.y + frontLeft.y, s * m.m0.z + frontLeft.z};
    m.m3 = along + across;
    m.m0 = m.m0 * invMag(m.m0);
    m.m2 = m.m2 * invMag(m.m2);
    m.m1 = {m.m2.y * m.m0.z - m.m0.y * m.m2.z, m.m2.z * m.m0.x - m.m2.x * m.m0.z,
            m.m0.y * m.m2.x - m.m2.y * m.m0.x};
}


void CableCars::updateObstacleMap(Car& c) {
    // aiCableCar::UpdateObstacleMap: the car's component (aiMap::
    // MapComponent from its room); on a road, the section of the line ahead
    // of it (aiPath::RoadVertice) on the side of the road it travels there,
    // re-listed when that changes; in an intersection, its list.
    int id = c.mapId, type = ai::kNoComponent;
    c.room = m_ai.map().mapComponent(c.matrix.m3, id, type, c.room);
    // (The lists are the traffic's: aiPath::AddVehicle / RemoveVehicle,
    // aiIntersection::AddVehicle / RemoveVehicle.)
    ai::Traffic& traffic = m_ai.traffic();
    auto unlinkRoad = [&] { traffic.unlistFromRoad(c.entry, c.mapId, c.mapSide, c.mapVert); };
    auto unlinkNode = [&] { traffic.unlistFromIntersection(c.entry, c.mapId); };
    if (type == ai::kRoadComponent) {
        const int side = c.path == id ? c.dir : c.nextDir;
        const int n = sections(id);
        const int v = std::clamp(ai::pathRoadVertice(source(id), c.matrix.m3, side), 1, n - 1);
        if (c.mapType == type && c.mapId == id && c.mapVert == v && c.mapSide == side)
            return;
        if (c.mapType == ai::kIntersectionComponent)
            unlinkNode();
        else if (c.mapType == ai::kRoadComponent)
            unlinkRoad();
        c.mapId = id;
        c.mapType = type;
        c.mapVert = v;
        c.mapSide = side;
        traffic.listOnRoad(c.entry, id, side, v);
        return;
    }
    if (type == ai::kIntersectionComponent) {
        if (c.mapType == ai::kIntersectionComponent)
            return;
        if (c.mapType == ai::kRoadComponent)
            unlinkRoad();
        c.mapId = id;
        c.mapType = type;
        c.mapVert = -1;
        traffic.listAtIntersection(c.entry, id);
        return;
    }
    if (type != ai::kNoComponent)
        return; // "Unknown Map Component type"
    if (c.mapType == ai::kNoComponent && c.mapId == id)
        return;
    if (c.mapType == ai::kIntersectionComponent)
        unlinkNode();
    else if (c.mapType == ai::kRoadComponent)
        unlinkRoad();
    c.mapType = type;
    c.mapId = id;
    c.mapVert = -1;
}

void CableCars::moveToRoom(Car& c, int room) {
    Body& b = *c.body;
    if (b.listed && b.room >= 0 && static_cast<std::size_t>(b.room) < m_rooms.size())
        std::erase(m_rooms[static_cast<std::size_t>(b.room)], &b);
    b.room = room;
    b.listed = room > 0;
    if (room > 0) {
        if (m_rooms.size() <= static_cast<std::size_t>(room))
            m_rooms.resize(static_cast<std::size_t>(room) + 1);
        m_rooms[static_cast<std::size_t>(room)].push_back(&b);
    }
}

void CableCars::updateRoom(Car& c, const phys::World& world) {
    // lvlLevel::MoveToRoom(FindRoomId(the car's position, its room)).
    const phys::Level* level = world.level();
    if (!level)
        return;
    const int room = level->findRoom(c.matrix.m3, c.body->room);
    if (room != c.body->room || !c.body->listed)
        moveToRoom(c, room);
}

void CableCars::instancesIn(int room, std::vector<phys::Instance*>& out) const {
    if (room <= 0 || static_cast<std::size_t>(room) >= m_rooms.size())
        return;
    for (Body* b : m_rooms[static_cast<std::size_t>(room)])
        out.push_back(b);
}

// --- Sound and drawing -----------------------------------------------------------------------

void CableCars::loadAudio(audio::SoundBank& bank, audio::Mixer& mixer,
                          audio::game::Object3DManager* manager) {
    for (auto& c : m_cars) {
        c->audio = std::make_unique<audio::game::CableCarAudio>();
        if (!c->audio->load(bank, mixer, manager, c->speed))
            c->audio.reset();
    }
}

void CableCars::updateAudio(const Mat34& listener, float dt) {
    // aiCableCar::Update: the sounds at the car, with its speed.
    for (auto& c : m_cars)
        if (c->audio)
            c->audio->update(listener, c->matrix.m3, c->speed, dt);
}

void CableCars::stopAudio() {
    for (auto& c : m_cars)
        if (c->audio)
            c->audio->stop();
}

void CableCars::draw(render::Device& device, ModelLibrary& models, TextureLibrary& textures,
                     const Frustum& frustum, const Camera& camera, const ObjectDetail& detail,
                     const RoomVisibility* rooms) const {
    // aiCableCarInstance::Draw from the car's room when the city lists it for
    // the view (cityLevel::DrawRooms' cityLevel_drawObjects): the BODY mesh
    // for the LOD lvlInstance::IsVisible picks, with the first paint job,
    // lit.
    const GpuModel* model = models.get(kModel);
    if (!model)
        return;
    const float radius = (model->bounds.max - model->bounds.min).mag() * 0.5f;
    const bool byRoom = rooms && rooms->active();
    for (const auto& c : m_cars) {
        if (byRoom && !rooms->passes(c->body->room).objects)
            continue;
        const Mat34& m = c->matrix;
        const auto lod = objectLod(viewDepth(camera.transform, m.m3), radius, detail, detail.noDraw);
        if (!lod || !frustum.intersectsSphere(m.m3, radius))
            continue;
        if (const GpuMesh* mesh = model->find("BODY", *lod)) { // BeginGeom(name, "BODY")
            MeshDrawOptions options;
            options.alphaRef = 101.0f / 255.0f;
            drawGpuMesh(device, textures, *mesh, model->materials(0), Mat44::fromMat34(m), options);
        }
    }
}

} // namespace mm2::game::world
