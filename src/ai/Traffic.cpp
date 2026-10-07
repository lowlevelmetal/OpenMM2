// Ambient traffic after MM2's aiVehicleAmbient and its goals (build 3393,
// MM2Recomp, documentation only); see Traffic.h and docs/ai.md.
#include "ai/Traffic.h"

#include "core/StringUtil.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mm2::ai {
namespace {

constexpr float kNoDistance = 9999.0f;

Vec2 xz(const Vec3& v) {
    return {v.x, v.z};
}

float sign(float v) {
    return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f);
}

// aiRailSet::ComputeXZCurve: Hermite coefficients, summed in MM2's order.
void hermite(float p0, float p1, float m0, float m1, float out[4]) {
    out[0] = ((-2.0f * p1 + m0) + m1) + 2.0f * p0;
    out[1] = ((-m1 + -2.0f * m0) + 3.0f * p1) + -3.0f * p0;
    out[2] = m0;
    out[3] = p0;
}

float cubic(const float k[4], float t) {
    return ((t * k[0] + k[1]) * t + k[2]) * t + k[3];
}

float cubicSlope(const float k[4], float t) {
    return ((t * k[0]) * 3.0f + (k[1] + k[1])) * t + k[2];
}

// Hermite point on (p0, p1, m0, m1) at t, XZ only (aiRailSet::CalcXZPosition;
// t below 0 is clamped).
Vec3 hermitePoint(const Vec3& p0, const Vec3& p1, const Vec3& m0, const Vec3& m1, float t,
                  Vec3* direction = nullptr) {
    float kx[4], kz[4];
    hermite(p0.x, p1.x, m0.x, m1.x, kx);
    hermite(p0.z, p1.z, m0.z, m1.z, kz);
    t = std::max(t, 0.0f);
    if (direction)
        *direction = {cubicSlope(kx, t), 0.0f, cubicSlope(kz, t)};
    return {cubic(kx, t), lerp(p0.y, p1.y, clampf(t, 0.0f, 1.0f)), cubic(kz, t)};
}

float manhattanXZ(const Vec3& a, const Vec3& b) {
    return std::abs(a.z - b.z) + std::abs(a.x - b.x);
}

Mat34 frameFromRows(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& pos) {
    Mat34 m;
    m.m0 = a;
    m.m1 = b;
    m.m2 = c;
    m.m3 = pos;
    return m;
}

} // namespace

PlayerCar PlayerCar::at(const Vec3& pos, const Vec3& vel) {
    PlayerCar p;
    Vec3 f{vel.x, 0.0f, vel.z};
    const float yaw = f.mag2() > 0.01f ? std::atan2(-f.x, -f.z) : 0.0f;
    p.transform = Mat34::rotationY(yaw + kPi); // forward (-Z) along the velocity
    p.transform.m3 = pos;
    p.velocity = vel;
    return p;
}

// --- Construction ----------------------------------------------------------

Traffic::Traffic(const RoadNetwork& network, TrafficLights& lights, std::vector<VehicleData> types,
                 const TrafficSettings& settings, std::uint64_t seed)
    : m_net(network), m_lights(lights), m_types(std::move(types)), m_settings(settings), m_rng(seed) {
    if (m_types.empty()) {
        VehicleData d;
        d.model = "va_sedans_s";
        m_types.push_back(d);
    }
    // aiMap::Init: AIMAP +0x3c = clamp(density, 0, 1) * 0.2; no vehicles at
    // all when the density is 0.
    const float density = clampf(m_settings.density, 0.0f, 1.0f);
    m_density = density * kAmbientDensityScale;
    const int count = density == 0.0f ? 0 : std::max(0, m_settings.poolSize);
    m_cars.resize(static_cast<std::size_t>(count));
    // Constructors of the vehicle array: aiRailSet (lane randomness), then
    // aiVehicleSpline (reaction ticks), for every car.
    for (auto& c : m_cars) {
        c.laneRandomness = std::sin(m_rng.frand() * 6.2831f) * 0.5f;
        c.totReactTicks = 8 - static_cast<int>(m_rng.frand() * -17.0f);
    }
    // Then per car: its type, aiVehicleAmbient::Init (aiVehicleInstance::
    // SetColor, aiGoalRandomDrive ctor).
    float exceedCounter = 0.0f;
    for (auto& c : m_cars) {
        c.type = pickType();
        c.paint = m_rng.frand();
        c.exceedLimit = exceedCounter + exceedCounter;
        exceedCounter -= 1.0f;
        if (exceedCounter < 0.0f)
            exceedCounter = 4.0f;
        const float f = m_rng.frand(); // one draw for both
        c.accelFactor = f * 3.0f + 5.0f;
        c.separation = f * 2.5f + 0.5f;
        // aiVehicleSpline::Init: bumper and side distances from the box bound
        // (aiVehicleManager::AddVehicleDataEntry: centred at CG, Size extents).
        const VehicleData& d = m_types[static_cast<std::size_t>(c.type)];
        c.backBumper = d.cg.z + d.size.z * 0.5f;
        c.frontBumper = d.size.z * 0.5f - d.cg.z;
        c.leftSide = d.size.x * 0.5f - d.cg.x;
        c.rightSide = d.cg.x + d.size.x * 0.5f;
    }
    // aiMap::Reset: every car into the free pool, the last one on top.
    for (int i = 0; i < count; ++i)
        m_pool.push_back(i);
    m_queues.resize(m_net.lanes().size());
    m_pathActive.assign(m_net.paths().size(), 0);
    m_stopWaiting.resize(m_net.intersections().size());
    m_stopAllowed.resize(m_net.intersections().size());
}

int Traffic::pickType() {
    // aiMap::Init: the first type whose cumulative probability exceeds frand.
    const float r = m_rng.frand();
    for (const auto& t : m_settings.types) {
        if (r < t.cumulative) {
            for (std::size_t i = 0; i < m_types.size(); ++i)
                if (str::iequals(m_types[i].model, t.model))
                    return static_cast<int>(i);
            break;
        }
    }
    return static_cast<int>(m_types.size()) - 1;
}

// --- Lane geometry (aiPath helpers) ----------------------------------------

const Lane* Traffic::laneOf(int path, int dir, int lane) const {
    const int id = m_net.lane(path, dir, lane);
    return id >= 0 ? &m_net.lanes()[static_cast<std::size_t>(id)] : nullptr;
}

float Traffic::laneLength(int path, int dir, int lane) const {
    const Lane* l = laneOf(path, dir, lane);
    return l ? l->line.length : 0.0f;
}

Vec3 Traffic::xAxisAt(int path, int dir, int i) const {
    const auto& x = m_net.paths()[static_cast<std::size_t>(path)].xAxis;
    if (x.empty())
        return {};
    const int n = static_cast<int>(x.size());
    const int k = dir == 1 ? i : n - 1 - i;
    return x[static_cast<std::size_t>(std::clamp(k, 0, n - 1))];
}

// Lane vertex i in travel order, moved sideways by the car's lane randomness.
Vec3 Traffic::lanePoint(const Car& c, int path, int dir, int lane, int i) const {
    const Lane* l = laneOf(path, dir, lane);
    if (!l || l->line.points.empty())
        return {};
    const auto& pts = l->line.points;
    const Vec3 p = pts[static_cast<std::size_t>(std::clamp(i, 0, static_cast<int>(pts.size()) - 1))];
    return p + xAxisAt(path, dir, i) * c.laneRandomness;
}

// aiPath::SubSectionDir: wAxis (the segment direction), unnormalised, scaled.
Vec3 Traffic::subSectionDir(int path, int dir, int i, float scale) const {
    const auto& w = m_net.source()->paths[static_cast<std::size_t>(path)].wAxis;
    const int n = static_cast<int>(w.size());
    if (n == 0)
        return {};
    if (dir == 1)
        return w[static_cast<std::size_t>(std::clamp(i, 0, n - 1))] * scale;
    return w[static_cast<std::size_t>(std::clamp(n - 1 - i, 0, n - 1))] * -scale;
}

// aiPath::IntersectionEntryVector: the travel direction at the road's end.
Vec3 Traffic::entryVector(int path, int dir, float scale) const {
    const auto& z = m_net.source()->paths[static_cast<std::size_t>(path)].zAxis;
    if (z.empty())
        return {};
    return dir == 1 ? z.back() * -scale : z.front() * scale;
}

// aiPath::IntersectionExitVector: the travel direction at the road's start.
Vec3 Traffic::exitVector(int path, int dir, float scale) const {
    const auto& z = m_net.source()->paths[static_cast<std::size_t>(path)].zAxis;
    if (z.empty())
        return {};
    return dir == 1 ? z.front() * -scale : z.back() * scale;
}

// aiPath::IntersectionEntryPt: the point `d` short of the lane's end. MM2
// measures the last segment on lane 0 whatever the lane.
Vec3 Traffic::entryPoint(int path, int dir, int lane, float d) const {
    const Lane* l = laneOf(path, dir, lane);
    const Lane* l0 = laneOf(path, dir, 0);
    if (!l || l->line.points.size() < 2)
        return l && !l->line.points.empty() ? l->line.points.back() : Vec3{};
    const auto& pts = l->line.points;
    const std::size_t n = pts.size();
    const auto& cum0 = (l0 ? l0 : l)->line.distances;
    const float len = cum0[n - 1] - cum0[n - 2];
    const float t = len > 0.0f ? (len - d) / len : 0.0f;
    return (pts[n - 1] - pts[n - 2]) * t + pts[n - 2];
}

// aiPath::SubSectionPt: the point `d` back from vertex i.
Vec3 Traffic::subSectionPoint(int path, int dir, int lane, int i, float d) const {
    const Lane* l = laneOf(path, dir, lane);
    if (!l || l->line.points.size() < 2)
        return {};
    const int n = static_cast<int>(l->line.points.size());
    i = std::clamp(i, 1, n - 1);
    const Vec3& p1 = l->line.points[static_cast<std::size_t>(i)];
    const Vec3& p0 = l->line.points[static_cast<std::size_t>(i - 1)];
    const float seg = l->line.distances[static_cast<std::size_t>(i)] - l->line.distances[static_cast<std::size_t>(i - 1)];
    const float t = seg > 0.0f ? 1.0f - d / seg : 0.0f;
    return (p1 - p0) * t + p0;
}

// aiPath::Index: the end vertex of the segment holding `dist`.
int Traffic::index(int path, int dir, int lane, float dist) const {
    const Lane* l = laneOf(path, dir, lane);
    if (!l || l->line.points.size() < 2)
        return 1;
    const auto& cum = l->line.distances;
    const int n = static_cast<int>(cum.size());
    dist = clampf(dist, 0.0f, cum.back());
    for (int i = 1; i < n; ++i)
        if (dist <= cum[static_cast<std::size_t>(i)] + 1e-5f)
            return i;
    return n - 1;
}

float Traffic::subSectionDist(int path, int dir, int lane, float dist) const {
    const Lane* l = laneOf(path, dir, lane);
    if (!l || l->line.points.size() < 2)
        return -1.0f;
    const int i = index(path, dir, lane, dist);
    return clampf(dist, 0.0f, l->line.length) - l->line.distances[static_cast<std::size_t>(i - 1)];
}

// Length of the turn from the car's lane end to its next lane: the
// Manhattan XZ distance between the two points (aiGoalRandomDrive).
float Traffic::turnLength(const Car& c) const {
    if (c.nextPath < 0)
        return 0.0f;
    const int n = static_cast<int>(laneOf(c.path, c.dir, c.lane) ? laneOf(c.path, c.dir, c.lane)->line.points.size() : 1);
    const Vec3 p0 = entryPoint(c.path, c.dir, c.lane, c.frontBumper) + xAxisAt(c.path, c.dir, n - 1) * c.laneRandomness;
    const Vec3 p1 = lanePoint(c, c.nextPath, c.nextDir, c.nextLane, 0);
    return manhattanXZ(p0, p1);
}

void Traffic::setCurve(Car& c, const Vec3& p0, const Vec3& p1, const Vec3& m0, const Vec3& m1) {
    hermite(p0.x, p1.x, m0.x, m1.x, c.curve[0]);
    hermite(p0.z, p1.z, m0.z, m1.z, c.curve[1]);
    c.turnY0 = p0.y;
    c.turnY1 = p1.y;
}

// aiRailSet::SolveXZCurve on the car's stored curve; the height runs
// linearly between the curve's end points.
Vec3 Traffic::curvePoint(const Car& c, float t, Vec3* direction) const {
    if (direction)
        *direction = {cubicSlope(c.curve[0], t), 0.0f, cubicSlope(c.curve[1], t)};
    return {cubic(c.curve[0], t), lerp(c.turnY0, c.turnY1, clampf(t, 0.0f, 1.0f)), cubic(c.curve[1], t)};
}

// aiRailSet::CalcRailPosition: the point of the car's rail `dist` along its
// lane, through the turn and onto the next road. The height is the road
// centre's at that distance (aiPath::CenterPosition), as AdjustAmbients reads it.
Vec3 Traffic::railPosition(const Car& c, float dist) const {
    const Lane* lane = laneOf(c.path, c.dir, c.lane);
    if (!lane || lane->line.points.size() < 2)
        return c.transform.m3;
    const auto& cum = lane->line.distances;
    const int S = static_cast<int>(cum.size());
    const float fb = c.frontBumper;
    const float L = lane->line.length;
    Vec3 out;
    if (dist < L - fb || c.nextPath < 0) {
        dist = std::min(dist, L - fb);
        const int i = index(c.path, c.dir, c.lane, dist);
        float segLen = cum[static_cast<std::size_t>(i)] - cum[static_cast<std::size_t>(i - 1)];
        Vec3 p0 = lanePoint(c, c.path, c.dir, c.lane, i - 1), p1, m0, m1;
        if (i > S - 2) {
            segLen -= fb;
            p1 = entryPoint(c.path, c.dir, c.lane, fb) + xAxisAt(c.path, c.dir, S - 2) * c.laneRandomness;
            m0 = subSectionDir(c.path, c.dir, i - 1, segLen);
            m1 = entryVector(c.path, c.dir, segLen);
        } else if (i == 1) {
            p1 = lanePoint(c, c.path, c.dir, c.lane, 1);
            m0 = exitVector(c.path, c.dir, segLen);
            m1 = subSectionDir(c.path, c.dir, 1, segLen);
        } else {
            p1 = lanePoint(c, c.path, c.dir, c.lane, i);
            m0 = subSectionDir(c.path, c.dir, i - 1, segLen);
            m1 = subSectionDir(c.path, c.dir, i, segLen);
        }
        const float t = segLen > 0.0f ? (dist - cum[static_cast<std::size_t>(i - 1)]) / segLen : 0.0f;
        out = hermitePoint(p0, p1, m0, m1, t);
    } else {
        const float over = dist - (L - fb);
        const Vec3 p1 = lanePoint(c, c.nextPath, c.nextDir, c.nextLane, 0);
        const Vec3 p0 =
            entryPoint(c.path, c.dir, c.lane, fb) + xAxisAt(c.path, c.dir, S - 1) * c.laneRandomness;
        const float turnLen = manhattanXZ(p0, p1);
        if (over < turnLen) {
            // MM2 takes the exit vector with the current road's direction here.
            out = hermitePoint(p0, p1, entryVector(c.path, c.dir, turnLen),
                               exitVector(c.nextPath, c.dir, turnLen), over / turnLen);
        } else {
            const Lane* next = laneOf(c.nextPath, c.nextDir, c.nextLane);
            if (!next || next->line.points.size() < 2)
                return p1;
            const float d2 = over - turnLen;
            const int j = index(c.nextPath, c.nextDir, c.nextLane, d2);
            const auto& cn = next->line.distances;
            const float segLen = cn[static_cast<std::size_t>(j)] - cn[static_cast<std::size_t>(j - 1)];
            const Vec3 q0 = lanePoint(c, c.nextPath, c.nextDir, c.nextLane, j - 1);
            const Vec3 q1 = lanePoint(c, c.nextPath, c.nextDir, c.nextLane, j);
            const Vec3 m0 = j == 1 ? exitVector(c.nextPath, c.nextDir, segLen)
                                   : subSectionDir(c.nextPath, c.nextDir, j - 1, segLen);
            const Vec3 m1 = subSectionDir(c.nextPath, c.nextDir, j, segLen);
            out = hermitePoint(q0, q1, m0, m1,
                               segLen > 0.0f ? (d2 - cn[static_cast<std::size_t>(j - 1)]) / segLen : 0.0f);
        }
    }
    // aiPath::CenterPosition: the centre line's height at `dist`.
    const city::AiPath& src = m_net.source()->paths[static_cast<std::size_t>(c.path)];
    if (src.center.size() >= 2 && src.centerLengths.size() + 1 >= src.center.size()) {
        const float total = src.centerLengths.back();
        const float d = clampf(dist, 0.0f, total);
        for (std::size_t i = 1; i < src.center.size(); ++i) {
            const float a = i == 1 ? 0.0f : src.centerLengths[i - 2];
            const float b = src.centerLengths[i - 1];
            if (d <= b || i + 1 == src.center.size()) {
                const float t = b > a ? (d - a) / (b - a) : 0.0f;
                out.y = lerp(src.center[i - 1].y, src.center[i].y, clampf(t, 0.0f, 1.0f));
                break;
            }
        }
    }
    return out;
}

// --- Lane queues (aiPath ambient vehicle lists) -----------------------------

std::vector<int>& Traffic::queue(int path, int dir, int lane) {
    static std::vector<int> none;
    const int id = m_net.lane(path, dir, lane);
    if (id < 0) {
        none.clear();
        return none;
    }
    return m_queues[static_cast<std::size_t>(id)];
}

const std::vector<int>* Traffic::queueOf(int path, int dir, int lane) const {
    const int id = m_net.lane(path, dir, lane);
    return id >= 0 ? &m_queues[static_cast<std::size_t>(id)] : nullptr;
}

// aiPath::PushAmbVehicle: append at the tail (the rear).
void Traffic::pushVehicle(int car, int path, int dir, int lane) {
    auto& q = queue(path, dir, lane);
    if (std::ranges::find(q, car) == q.end())
        q.push_back(car);
}

// aiPath::PopAmbVehicle: remove the head (anything else is removed in place).
void Traffic::popVehicle(int car, int path, int dir, int lane) {
    removeVehicle(car, path, dir, lane);
}

// aiPath::AddAmbVehicle: sorted by distance, ties behind the cars already there.
void Traffic::addVehicle(int car, int path, int dir, int lane, float dist) {
    auto& q = queue(path, dir, lane);
    if (std::ranges::find(q, car) != q.end())
        return;
    auto it = q.begin();
    while (it != q.end() && !(m_cars[static_cast<std::size_t>(*it)].roadDist < dist))
        ++it;
    q.insert(it, car);
}

void Traffic::removeVehicle(int car, int path, int dir, int lane) {
    auto& q = queue(path, dir, lane);
    if (auto it = std::ranges::find(q, car); it != q.end())
        q.erase(it);
}

// The vehicle ahead of `car` in its list for `lane` of its road (vehicle
// +0x90[lane]).
int Traffic::ahead(int car, int lane) const {
    const Car& c = m_cars[static_cast<std::size_t>(car)];
    const auto* q = queueOf(c.path, c.dir, lane);
    if (!q)
        return -1;
    auto it = std::ranges::find(*q, car);
    if (it == q->end() || it == q->begin())
        return -1;
    return *(it - 1);
}

// aiVehicleSpline::ResetReactTicks: the car and everything behind it in its
// lane that is standing still waits its reaction time again.
void Traffic::resetReactTicks(int car) {
    const Car& c = m_cars[static_cast<std::size_t>(car)];
    const auto* q = queueOf(c.path, c.dir, c.lane);
    if (!q)
        return;
    auto it = std::ranges::find(*q, car);
    for (; it != q->end(); ++it) {
        Car& o = m_cars[static_cast<std::size_t>(*it)];
        if (o.speed < 0.01f)
            o.curReactTicks = 0;
    }
}

// --- Population (aiMap::AdjustAmbients, aiPath::ClearAmbients) --------------

void Traffic::activate(int path) {
    m_pathActive[static_cast<std::size_t>(path)] = 1;
    m_activePaths.insert(m_activePaths.begin(), path);
}

void Traffic::returnToPool(int car) {
    Car& c = m_cars[static_cast<std::size_t>(car)];
    // Out of every list it may be on (aiMap::AddAmbient).
    if (c.path >= 0) {
        const auto& info = m_net.paths()[static_cast<std::size_t>(c.path)];
        for (std::size_t l = 0; l < info.lanesOf(c.dir).size(); ++l)
            removeVehicle(car, c.path, c.dir, static_cast<int>(l));
        const int node = arrivalIntersection(m_net, c.path, c.dir);
        if (node >= 0) {
            std::erase(m_stopWaiting[static_cast<std::size_t>(node)], car);
            std::erase(m_stopAllowed[static_cast<std::size_t>(node)], car);
        }
    }
    c.active = false;
    c.physical = false;
    c.speed = 0.0f;
    c.signal = TurnSignal::None;
    m_pool.push_back(car);
}

void Traffic::clearPath(int path) {
    // ClearAmbients: lanes of direction +1 first, then -1; a car returns to
    // the pool from the list of the lane it is drawn on.
    for (int dir : {1, -1}) {
        const auto& lanes = m_net.paths()[static_cast<std::size_t>(path)].lanesOf(dir);
        for (std::size_t l = 0; l < lanes.size(); ++l) {
            auto cars = m_queues[static_cast<std::size_t>(lanes[l])];
            for (int car : cars) {
                removeVehicle(car, path, dir, static_cast<int>(l));
                if (m_cars[static_cast<std::size_t>(car)].drawLane == static_cast<int>(l))
                    returnToPool(car);
            }
        }
    }
    m_pathActive[static_cast<std::size_t>(path)] = 0;
    std::erase(m_activePaths, path);
}

// One car from the pool onto (path, dir, lane) at `dist`. Returns false when
// no next road can be chosen (the lane is given up); a spot too close to an
// opponent is skipped without using the car.
bool Traffic::placeCar(int slot, int path, int dir, int lane, float dist) {
    Car& c = m_cars[static_cast<std::size_t>(slot)];
    c.path = path;
    c.dir = dir;
    c.lane = c.drawLane = lane;
    c.roadDist = dist;
    if (!chooseNext(c))
        return false;
    const Vec3 at = railPosition(c, dist);
    for (const Vec3& o : m_opponents)
        if (o.dist2(at) < sq(kAmbientOpponentClearance))
            return true;
    m_pool.pop_back();
    // aiVehicleAmbient::Reset.
    c.active = true;
    c.physical = false;
    c.goal = AmbientGoal::RandomDrive;
    c.goalTicks = 0;
    c.speed = 0.0f;
    c.accel = 0.0f;
    c.target = 0.0f;
    c.laneChangeOk = false;
    c.atStopSign = false;
    c.enterInt = false;
    c.curReactTicks = c.totReactTicks;
    c.signal = TurnSignal::None;
    c.horn = false;
    c.fitted = false;
    c.transform = Mat34::identity();
    c.transform.m3 = at;
    addVehicle(slot, path, dir, lane, dist);
    return true;
}

void Traffic::adjustAmbients(int oldRoom, int newRoom) {
    const city::AiMap* map = m_net.source();
    if (!map)
        return;
    static const std::vector<std::uint16_t> kNone;
    auto roomList = [&](int room) -> const std::vector<std::uint16_t>& {
        if (room < 0 || static_cast<std::size_t>(room) >= map->roomPathsNear.size())
            return kNone;
        return map->roomPathsNear[static_cast<std::size_t>(room)];
    };
    std::vector<int> added, removed;
    if (m_populateAll) {
        for (std::size_t p = 0; p < m_net.paths().size(); ++p)
            added.push_back(static_cast<int>(p));
    } else {
        const auto& from = roomList(oldRoom);
        const auto& to = roomList(newRoom);
        for (auto p : to)
            if (std::ranges::find(from, p) == from.end() && p < m_net.paths().size())
                added.push_back(p);
        for (auto p : from)
            if (std::ranges::find(to, p) == to.end() && p < m_net.paths().size())
                removed.push_back(p);
    }
    for (int p : removed)
        if (m_pathActive[static_cast<std::size_t>(p)])
            clearPath(p);

    // The total usable lane length of the new roads (centre length - 5 m per
    // lane, roads with an [Exceptions] entry aside) sets the spacing: about
    // density / 8 cars per metre spread evenly over all of them.
    auto open = [&](const PathInfo& info, int dir) { return (info.flagsOf(dir) & 1) == 0; };
    float total = 0.0f;
    for (int p : added) {
        const PathInfo& info = m_net.paths()[static_cast<std::size_t>(p)];
        if (info.hasException)
            continue;
        for (int dir : {1, -1})
            if (open(info, dir))
                total = (info.centreLength - 5.0f) * static_cast<float>(info.lanesOf(dir).size()) + total;
    }
    const int k = 1 - static_cast<int>(total * m_density * -0.125f);
    const float spacing = total / static_cast<float>(k);

    float carry = 0.0f, leftover = 0.0f;
    for (int p : added) {
        if (m_pathActive[static_cast<std::size_t>(p)])
            continue; // already populated
        activate(p);
        const PathInfo& info = m_net.paths()[static_cast<std::size_t>(p)];
        for (int dir : {-1, 1}) {
            if (!open(info, dir))
                continue;
            const int lanes = static_cast<int>(info.lanesOf(dir).size());
            if (info.hasException) {
                // aiMap::NumCars on the first lane of direction +1, then each
                // lane gets that many cars at (i + 1/2) spacings, jittered.
                const int n = static_cast<int>(laneLength(p, 1, 0) * info.density / kAmbientCarSpacing);
                for (int l = 0; l < lanes; ++l) {
                    const float space = laneLength(p, dir, l) / static_cast<float>(n + 1);
                    for (int i = 0; i < n && !m_pool.empty(); ++i) {
                        const float jitter = std::sin(m_rng.frand() * 6.28f);
                        const float dist = (static_cast<float>(i + 1) * space - space * 0.5f) +
                                           (space - 10.0f) * jitter * 0.5f;
                        if (!placeCar(m_pool.back(), p, dir, l, dist))
                            break;
                    }
                }
                continue;
            }
            const float usable = info.centreLength - 5.0f;
            for (int l = 0; l < lanes; ++l) {
                carry = usable + carry;
                float s = 0.0f;
                while (carry > spacing + 0.001f) {
                    carry = carry - spacing;
                    s = (spacing - leftover) + s;
                    leftover = 0.0f;
                    if (m_pool.empty())
                        break;
                    const Car& next = m_cars[static_cast<std::size_t>(m_pool.back())];
                    const float dist = clampf(s, 0.0f, std::max(0.0f, usable - next.frontBumper));
                    if (!placeCar(m_pool.back(), p, dir, l, dist))
                        break;
                }
                leftover = carry;
            }
        }
    }
}

void Traffic::populateAll() {
    m_populateAll = true;
}

int Traffic::poolFree() const {
    return static_cast<int>(m_pool.size());
}

// --- aiGoalRandomDrive ------------------------------------------------------

bool Traffic::chooseNext(Car& c) {
    RailLink next;
    if (chooseNextLaneLink(m_net, {c.path, c.dir, c.drawLane}, m_rng, next)) {
        c.nextPath = next.path;
        c.nextDir = next.dir;
        c.nextLane = next.lane;
        return true;
    }
    c.nextPath = -1;
    return false;
}

// aiGoalRandomDrive::SpeedLimit: the road's limit plus the car's excess;
// freeways add 5 m/s per lane counted from the rightmost.
float Traffic::speedLimit(const Car& c) const {
    const PathInfo& info = m_net.paths()[static_cast<std::size_t>(c.path)];
    if (info.freeway()) {
        const int lanes = static_cast<int>(info.lanesOf(c.dir).size());
        return static_cast<float>((lanes - c.lane) - 1) * 5.0f + c.exceedLimit + info.speedLimit;
    }
    return info.speedLimit + c.exceedLimit;
}

// aiVehicleSpline::DistanceToIntersection: from the front bumper to the
// lane's end.
float Traffic::distanceToIntersection(const Car& c) const {
    const float laneLen = laneLength(c.path, c.dir, c.lane);
    switch (c.rail) {
    case Rail::Lane:
        return laneLen - (c.roadDist + c.frontBumper);
    case Rail::LaneChange:
        return ((laneLen - c.frontBumper) + (c.segLen - c.segDist)) - c.laneChangeDist;
    default:
        return kNoDistance;
    }
}

// aiVehicleSpline::DistanceToVehicle: centre to centre along the rails.
float Traffic::distanceToVehicle(const Car& a, const Car& b) const {
    if (a.path != b.path)
        return (a.segLen - a.segDist) + b.roadDist;
    auto euclid = [&] { return a.transform.m3.dist(b.transform.m3); };
    if (b.rail == Rail::Regain)
        return euclid();
    switch (a.rail) {
    case Rail::Lane:
        if (b.rail == Rail::LaneChange) {
            const float bl = laneLength(b.path, b.dir, b.drawLane);
            const float q = bl - a.frontBumper;
            if (a.drawLane == b.drawLane || bl <= 0.0f)
                return (q * 0.25f + b.segDist) - a.roadDist;
            return ((q * 0.25f + b.segDist) * laneLength(a.path, a.dir, a.drawLane)) / bl - a.roadDist;
        }
        return b.roadDist - a.roadDist;
    case Rail::Turn:
        if (b.rail == Rail::Lane)
            return b.roadDist - a.roadDist;
        if (b.rail == Rail::Turn)
            return m_net.paths()[static_cast<std::size_t>(a.path)].intersection[0] ==
                           m_net.paths()[static_cast<std::size_t>(b.path)].intersection[0]
                       ? b.segDist - a.segDist
                       : kNoDistance;
        return kNoDistance;
    case Rail::LaneChange:
        if (b.rail == Rail::LaneChange) {
            const Lane* la = laneOf(a.path, a.dir, a.drawLane);
            const Lane* lb = laneOf(b.path, b.dir, b.drawLane);
            if (!la || !lb)
                return kNoDistance;
            const int j = index(a.path, a.dir, b.drawLane, b.roadDist);
            return (la->line.distances[static_cast<std::size_t>(j)] - a.roadDist) -
                   (lb->line.distances[static_cast<std::size_t>(j)] - b.roadDist);
        }
        return b.roadDist - (a.laneChangeDist - (a.segLen - a.segDist));
    default:
        return euclid();
    }
}

// aiGoalRandomDrive::Reset: puts the car on the curve for its lane distance.
void Traffic::resetRandomDrive(Car& c) {
    c.atStopSign = false;
    c.enterInt = false;
    c.signal = TurnSignal::None;
    const Lane* lane = laneOf(c.path, c.dir, c.lane);
    if (!lane || lane->line.points.size() < 2)
        return;
    float laneEnd = lane->line.length - c.frontBumper;
    c.reactDist = kIntersectionReactDist;
    c.laneChangeOk = c.roadDist <= laneEnd * 0.25f;
    float turnLen = 0.0f;
    if (c.nextPath >= 0) {
        turnLen = turnLength(c);
        if (turnLen + laneEnd < c.roadDist) {
            // Beyond the turn: onto the next road.
            const int idx = static_cast<int>(&c - m_cars.data());
            c.roadDist -= turnLen + laneEnd;
            popVehicle(idx, c.path, c.dir, c.drawLane);
            pushVehicle(idx, c.nextPath, c.nextDir, c.nextLane);
            c.path = c.nextPath;
            c.dir = c.nextDir;
            c.lane = c.drawLane = c.nextLane;
            chooseNext(c);
            lane = laneOf(c.path, c.dir, c.lane);
            if (!lane || lane->line.points.size() < 2)
                return;
            laneEnd = lane->line.length - c.frontBumper;
            turnLen = c.nextPath >= 0 ? turnLength(c) : 0.0f;
        }
    }
    const int S = static_cast<int>(lane->line.points.size());
    const auto& cum = lane->line.distances;
    if (laneEnd <= c.roadDist && c.nextPath >= 0) {
        // In the turn.
        c.segDist = c.roadDist - laneEnd;
        c.segLen = std::max(turnLen, 1e-3f);
        const Vec3 p0 =
            entryPoint(c.path, c.dir, c.lane, c.frontBumper) + xAxisAt(c.path, c.dir, S - 1) * c.laneRandomness;
        const Vec3 p1 = lanePoint(c, c.nextPath, c.nextDir, c.nextLane, 0);
        setCurve(c, p0, p1, entryVector(c.path, c.dir, c.segLen), exitVector(c.nextPath, c.nextDir, c.segLen));
        c.rail = Rail::Turn;
        c.section = S;
        const int node = arrivalIntersection(m_net, c.path, c.dir);
        c.transform = Mat34::identity();
        c.transform.m3 = node >= 0 ? m_net.intersections()[static_cast<std::size_t>(node)].centre : p0;
    } else {
        c.roadDist = std::min(c.roadDist, laneEnd);
        const int i = index(c.path, c.dir, c.lane, c.roadDist);
        c.section = i;
        c.segDist = subSectionDist(c.path, c.dir, c.lane, c.roadDist);
        float segLen = cum[static_cast<std::size_t>(i)] - cum[static_cast<std::size_t>(i - 1)];
        Vec3 p0 = lanePoint(c, c.path, c.dir, c.lane, i - 1), p1, m0, m1;
        if (i < S - 1) {
            p1 = lanePoint(c, c.path, c.dir, c.lane, i);
            m0 = i == 1 ? exitVector(c.path, c.dir, segLen) : subSectionDir(c.path, c.dir, i - 1, segLen);
            m1 = subSectionDir(c.path, c.dir, i, segLen);
        } else {
            segLen -= c.frontBumper;
            p1 = subSectionPoint(c.path, c.dir, c.lane, i, c.frontBumper) + xAxisAt(c.path, c.dir, i) * c.laneRandomness;
            m0 = subSectionDir(c.path, c.dir, i - 1, segLen);
            m1 = entryVector(c.path, c.dir, segLen);
        }
        c.segLen = std::max(segLen, 1e-3f);
        setCurve(c, p0, p1, m0, m1);
        c.rail = Rail::Lane;
        // The vertices of the drawn lane, interpolated.
        const Lane* drawn = laneOf(c.path, c.dir, c.drawLane);
        const Lane* src = drawn ? drawn : lane;
        const float t = clampf(c.segDist / c.segLen, 0.0f, 1.0f);
        c.transform = Mat34::identity();
        c.transform.m3 = lerp(src->line.points[static_cast<std::size_t>(i - 1)],
                              src->line.points[static_cast<std::size_t>(std::min(i, S - 1))], t);
    }
    c.curReactTicks = c.totReactTicks;
}

bool Traffic::stopSignOkayToGo(int node, int car) {
    // aiIntersection::StopSignOkayToGo: when nobody has permission, the
    // first car to have stopped gets it, together with the first waiting
    // car from the same road.
    auto& allowed = m_stopAllowed[static_cast<std::size_t>(node)];
    auto& waiting = m_stopWaiting[static_cast<std::size_t>(node)];
    if (allowed.empty() && !waiting.empty()) {
        const int first = waiting.front();
        waiting.erase(waiting.begin());
        allowed.push_back(first);
        const int road = m_cars[static_cast<std::size_t>(first)].path;
        for (auto it = waiting.begin(); it != waiting.end(); ++it) {
            if (m_cars[static_cast<std::size_t>(*it)].path == road) {
                allowed.push_back(*it);
                waiting.erase(it);
                break;
            }
        }
    }
    return std::ranges::find(allowed, car) != allowed.end();
}

void Traffic::removeFromStopSign(int node, int car) {
    std::erase(m_stopAllowed[static_cast<std::size_t>(node)], car);
}

// aiGoalRandomDrive::OkayToEnterIntersection.
bool Traffic::okayToEnter(int idx, float dist) {
    Car& c = m_cars[static_cast<std::size_t>(idx)];
    const PathInfo& info = m_net.paths()[static_cast<std::size_t>(c.path)];
    const int end = c.dir == 1 ? 0 : 1;
    switch (info.rule[end]) {
    case EntryRule::Uncontrolled:
        resetReactTicks(idx);
        return true;
    case EntryRule::TrafficLight: {
        const Lane* lane = laneOf(c.path, c.dir, c.lane);
        return !lane || lane->lightSlot < 0 || m_lights.state(lane->lightSlot) == LightState::Green;
    }
    case EntryRule::StopSign:
        break;
    }
    if (c.speed >= 0.5f || dist >= 1.5f)
        return false;
    const int node = info.intersection[end];
    if (node < 0)
        return true;
    if (!c.atStopSign) {
        c.atStopSign = true;
        m_stopWaiting[static_cast<std::size_t>(node)].push_back(idx);
    }
    return stopSignOkayToGo(node, idx);
}

// aiGoalRandomDrive::UpcomingAccident: a car out of its normal driving
// (hit, regaining its lane, avoiding the player, parked) in the intersection
// ahead or on the next road. MM2 finds them through its obstacle map; here a
// car counts where its rail registers it (inferred equivalent).
bool Traffic::upcomingAccident(const Car& c) const {
    const int node = arrivalIntersection(m_net, c.path, c.dir);
    for (const Car& o : m_cars) {
        if (!o.active || o.goal == AmbientGoal::RandomDrive)
            continue;
        if (o.rail == Rail::Turn) {
            if (arrivalIntersection(m_net, o.path, o.dir) == node)
                return true;
        } else if (o.path == c.nextPath) {
            return true;
        }
    }
    return false;
}

// aiPath::RoadCapacity: the car, and every car ahead of it on its road bound
// for the same next road, must fit before the last car of the next lane.
bool Traffic::roadCapacity(int idx) const {
    const Car& c = m_cars[static_cast<std::size_t>(idx)];
    const auto* q = queueOf(c.nextPath, c.nextDir, c.nextLane);
    if (!q || q->empty())
        return true;
    const Car& tail = m_cars[static_cast<std::size_t>(q->back())];
    auto totLength = [](const Car& o) { return (o.separation + o.frontBumper) + o.backBumper; };
    float acc = tail.backBumper;
    int o = ahead(idx, c.lane);
    while (o >= 0) {
        const Car& oc = m_cars[static_cast<std::size_t>(o)];
        if (oc.path != c.path || !(c.roadDist < oc.roadDist))
            break;
        if (oc.nextPath == c.nextPath)
            acc += totLength(oc);
        o = ahead(o, oc.lane);
    }
    return totLength(c) + acc < tail.roadDist;
}

// aiGoalRandomDrive::AnyVehiclesComingThisWay: a car committed to the same
// intersection from another road whose way (its lane end to its next lane's
// start) crosses this car's line. As MM2 codes it, the other roads' lanes and
// vertices are read for this car's own directions.
bool Traffic::anyVehiclesComingThisWay(const Car& c) const {
    const PathInfo& info = m_net.paths()[static_cast<std::size_t>(c.path)];
    if (info.freeway() || (c.nextPath >= 0 && m_net.paths()[static_cast<std::size_t>(c.nextPath)].freeway()))
        return false;
    const int node = arrivalIntersection(m_net, c.path, c.dir);
    if (node < 0)
        return false;
    const Vec3& pos = c.transform.m3;
    const Vec3& right = c.transform.m0;
    for (int other : m_net.intersections()[static_cast<std::size_t>(node)].paths) {
        if (other == c.path)
            continue;
        const auto& lanes = m_net.paths()[static_cast<std::size_t>(other)].lanesOf(c.dir);
        for (std::size_t l = 0; l < lanes.size(); ++l) {
            const auto& q = m_queues[static_cast<std::size_t>(lanes[l])];
            if (q.empty())
                continue;
            const Car& h = m_cars[static_cast<std::size_t>(q.front())];
            if (!h.enterInt || h.nextPath < 0)
                continue;
            const Lane* from = laneOf(h.path, c.dir, h.drawLane);
            const Lane* to = laneOf(h.nextPath, c.nextDir, h.nextLane);
            if (!from || !to || from->line.points.empty() || to->line.points.empty())
                continue;
            const float a = (from->line.points.back() - pos).dot(right);
            const float b = (to->line.points.front() - pos).dot(right);
            if (sign(a) != sign(b))
                return true;
        }
    }
    return false;
}

// aiGoalRandomDrive::AvoidCollision: car following.
void Traffic::avoidCollision(Car& c, const Car& lead, float d) {
    const float u = clampf(lead.speed - 2.5f, 0.0f, 999.0f);
    const float leadAccel = lead.accel;
    const float gap = lead.backBumper + c.frontBumper;
    if (d < gap) {
        if (lead.rail != Rail::LaneChange) {
            c.speed = 0.0f;
            c.accel = 0.0f;
            c.target = 0.0f;
        } else {
            c.accel = c.speed * c.speed * -0.16666667f;
            c.target = 0.0f;
        }
        return;
    }
    const float limit = speedLimit(c);
    if (gap + c.separation <= d) {
        if (u <= c.speed) {
            const float room = c.separation + lead.backBumper + c.frontBumper;
            const bool sameKind = (c.rail == Rail::LaneChange && lead.rail == Rail::LaneChange) ||
                                  (c.rail == Rail::Turn && lead.rail == Rail::Turn);
            const float span = sameKind ? (d - room) - 2.0f : d - room;
            // (MM2 divides by zero when the gap is exactly met; kept finite.)
            c.accel = (u * u - c.speed * c.speed) / (span != 0.0f ? span + span : 1e-6f);
        } else {
            c.accel = c.accelFactor;
        }
        c.target = limit < u ? limit : u;
        return;
    }
    if (u <= c.speed) {
        c.speed = c.speed * 0.75f;
        if (leadAccel <= 0.0f) {
            c.accel = 0.0f;
            c.target = 0.0f;
            return;
        }
        c.accel = c.accelFactor < leadAccel ? c.accelFactor : leadAccel;
    } else {
        c.accel = c.accelFactor;
    }
    c.target = u <= limit ? u : limit;
}

// aiGoalRandomDrive::SolveVelocity.
void Traffic::solveVelocity(int idx, float dt) {
    Car& c = m_cars[static_cast<std::size_t>(idx)];
    float leadDist = 99999.0f;
    const float dist = distanceToIntersection(c);
    const float v = c.speed;
    const bool reacted = c.totReactTicks < c.curReactTicks;
    const float limit = speedLimit(c);
    auto cruise = [&] {
        c.target = limit + 0.0001f;
        c.accel = c.accelFactor;
    };

    if (c.reactDist <= dist || c.rail == Rail::Turn) {
        const int lead = ahead(idx, c.lane);
        if (lead >= 0) {
            leadDist = distanceToVehicle(c, m_cars[static_cast<std::size_t>(lead)]);
            if (20.0f <= leadDist) {
                if (c.target < limit || c.accel < c.accelFactor)
                    cruise();
            } else if (reacted) {
                avoidCollision(c, m_cars[static_cast<std::size_t>(lead)], leadDist);
            }
        } else if (c.rail == Rail::Turn) {
            // The last car of the lane being turned into, or a car of this
            // road turning into the same lane ahead of this one.
            int best = -1;
            if (const auto* q = queueOf(c.nextPath, c.nextDir, c.nextLane); q && !q->empty() && q->back() != idx) {
                best = q->back();
                leadDist = distanceToVehicle(c, m_cars[static_cast<std::size_t>(best)]);
            }
            const auto& lanes = m_net.paths()[static_cast<std::size_t>(c.path)].lanesOf(c.dir);
            for (int laneId : lanes) {
                const auto& q = m_queues[static_cast<std::size_t>(laneId)];
                if (q.empty() || q.front() == idx)
                    continue;
                const Car& o = m_cars[static_cast<std::size_t>(q.front())];
                if (o.rail != Rail::Turn || o.nextPath != c.nextPath || o.nextLane != c.nextLane)
                    continue;
                const float d = distanceToVehicle(c, o);
                if (d < leadDist) {
                    leadDist = d;
                    best = q.front();
                }
            }
            if (0.0f < leadDist && leadDist < 20.0f) {
                if (reacted && best >= 0)
                    avoidCollision(c, m_cars[static_cast<std::size_t>(best)], leadDist);
            } else if (!(limit <= c.target && c.accelFactor <= c.accel)) {
                cruise();
            }
        } else if (c.target < limit) {
            cruise();
        }
    } else {
        // Within reach of the intersection.
        if (!c.enterInt && okayToEnter(idx, dist)) {
            bool committed = false;
            if (!upcomingAccident(c) && c.nextPath >= 0 && roadCapacity(idx)) {
                committed = true;
                if (!anyVehiclesComingThisWay(c)) {
                    c.enterInt = true;
                    if (c.speed < 0.01f && dist < 0.5f)
                        c.curReactTicks = c.totReactTicks - 3;
                }
            }
            if (!committed)
                chooseNext(c);
        }
        if (!c.enterInt) {
            int lead = ahead(idx, c.lane);
            if (lead >= 0 && m_cars[static_cast<std::size_t>(lead)].goal == AmbientGoal::RegainRail)
                lead = ahead(lead, c.lane);
            const Car* lc = lead >= 0 ? &m_cars[static_cast<std::size_t>(lead)] : nullptr;
            if (lc && lc->rail != Rail::Turn && lc->path == c.path) {
                leadDist = distanceToVehicle(c, *lc);
                if (20.0f <= leadDist) {
                    if (c.target < limit || c.accel < c.accelFactor)
                        cruise();
                } else if (reacted) {
                    avoidCollision(c, *lc, leadDist);
                }
            } else if (!reacted || 2.0f < v || dist <= 1.0f) {
                // Stop at the line.
                if (c.target == 0.0f) {
                    if (c.speed < 0.2f && dist < 0.5f)
                        c.speed = 0.0f;
                } else {
                    const float room = (dist - 0.25f) + (dist - 0.25f);
                    c.accel = -((v * v) / (room != 0.0f ? room : 1e-6f));
                    c.target = 0.0f;
                }
            } else {
                // Stopped short of the line: creep up to it.
                c.speed = 2.0f;
                c.reactDist = kRestartReactDist;
                cruise();
            }
        } else {
            const int lead = ahead(idx, c.lane);
            if (lead < 0) {
                if (c.target < limit && reacted)
                    cruise();
            } else {
                const Car& lc = m_cars[static_cast<std::size_t>(lead)];
                leadDist = distanceToVehicle(c, lc);
                if (20.0f <= leadDist) {
                    if (reacted) {
                        if (limit <= c.target) {
                            if (0.0f < c.target && c.accel == 0.0f)
                                c.accel = c.accelFactor;
                        } else {
                            cruise();
                        }
                    }
                } else if (lc.target != c.target && reacted) {
                    avoidCollision(c, lc, leadDist);
                }
            }
        }
        // Indicators (aiRailSet::SolveTurnType).
        if (c.nextPath >= 0) {
            const TurnType turn = solveTurnType(m_net, {c.path, c.dir, c.drawLane}, c.nextPath, c.nextDir);
            if (turn == TurnType::Right)
                c.signal = TurnSignal::Right;
            else if (turn == TurnType::Left)
                c.signal = TurnSignal::Left;
        }
    }

    // Integration.
    c.speed = dt * c.accel + c.speed;
    if ((c.speed < c.target + 0.05f && c.accel < 0.0f) || (c.target < c.speed && 0.0f < c.accel)) {
        c.speed = c.target;
        c.accel = 0.0f;
    }
    const int lead = ahead(idx, c.lane);
    if (lead >= 0 && m_cars[static_cast<std::size_t>(lead)].speed < 0.01f && c.speed < 0.01f && leadDist < 20.0f)
        c.curReactTicks = 0;
    c.segDist = dt * c.speed + c.segDist;
    c.roadDist = dt * c.speed + c.roadDist;
}

// aiGoalRandomDrive::SolveRailType: the next segment, the turn through the
// intersection, and the end of a turn or lane change. Returns false when the
// car left the populated roads.
bool Traffic::solveRailType(int idx) {
    Car& c = m_cars[static_cast<std::size_t>(idx)];
    if (c.segDist <= c.segLen)
        return true;
    const Lane* lane = laneOf(c.path, c.dir, c.lane);
    if (!lane || lane->line.points.size() < 2) {
        returnToPool(idx);
        return false;
    }
    int S = static_cast<int>(lane->line.points.size());
    auto beginTurn = [&](float backFromEnd) {
        const Vec3 p0 =
            entryPoint(c.path, c.dir, c.lane, backFromEnd) + xAxisAt(c.path, c.dir, S - 1) * c.laneRandomness;
        const Vec3 p1 = lanePoint(c, c.nextPath, c.nextDir, c.nextLane, 0);
        c.segLen = std::max(manhattanXZ(p0, p1), 1e-3f);
        setCurve(c, p0, p1, entryVector(c.path, c.dir, c.segLen), exitVector(c.nextPath, c.nextDir, c.segLen));
        c.rail = Rail::Turn;
    };
    switch (c.rail) {
    case Rail::Lane: {
        c.section += 1;
        c.segDist -= c.segLen;
        const auto& cum = lane->line.distances;
        if (c.nextPath < 0 || c.section <= S - 1) {
            const int i = std::min(c.section, S - 1);
            float segLen = cum[static_cast<std::size_t>(i)] - cum[static_cast<std::size_t>(i - 1)];
            if (c.section > S - 2) {
                segLen -= c.frontBumper;
                if (segLen <= 0.0f && c.nextPath >= 0) {
                    beginTurn(c.frontBumper + segLen);
                    break;
                }
                const Vec3 p0 = lanePoint(c, c.path, c.dir, c.lane, i - 1);
                const Vec3 p1 = entryPoint(c.path, c.dir, c.lane, c.frontBumper) +
                                xAxisAt(c.path, c.dir, S - 2) * c.laneRandomness;
                c.segLen = std::max(segLen, 1e-3f);
                setCurve(c, p0, p1, subSectionDir(c.path, c.dir, i - 1, c.segLen),
                         entryVector(c.path, c.dir, c.segLen));
            } else {
                c.segLen = std::max(segLen, 1e-3f);
                setCurve(c, lanePoint(c, c.path, c.dir, c.lane, i - 1), lanePoint(c, c.path, c.dir, c.lane, i),
                         subSectionDir(c.path, c.dir, i - 1, c.segLen), subSectionDir(c.path, c.dir, i, c.segLen));
            }
        } else {
            beginTurn(c.frontBumper);
        }
        break;
    }
    case Rail::Turn: {
        // Through the intersection.
        c.atStopSign = false;
        c.enterInt = false;
        c.laneChangeOk = true;
        c.reactDist = kIntersectionReactDist;
        const PathInfo& info = m_net.paths()[static_cast<std::size_t>(c.path)];
        const int end = c.dir == 1 ? 0 : 1;
        if (info.rule[end] == EntryRule::StopSign && info.intersection[end] >= 0)
            removeFromStopSign(info.intersection[end], idx);
        popVehicle(idx, c.path, c.dir, c.drawLane);
        if (c.nextPath < 0 || !m_pathActive[static_cast<std::size_t>(c.nextPath)]) {
            // The next road is not populated: one-way roads give the car
            // back (MM2 tests the first side's flag whatever the direction),
            // others turn it round onto the opposite side of its road.
            if (info.sideFlags[0] & 1) {
                returnToPool(idx);
                return false;
            }
            c.dir = -c.dir;
            const int count = static_cast<int>(info.lanesOf(c.dir).size());
            if (count == 0) {
                returnToPool(idx);
                return false;
            }
            if (count <= c.drawLane)
                c.lane = c.drawLane = count - 1;
            pushVehicle(idx, c.path, c.dir, c.drawLane);
        } else {
            pushVehicle(idx, c.nextPath, c.nextDir, c.nextLane);
            c.lane = c.drawLane = c.nextLane;
            c.dir = c.nextDir;
            c.path = c.nextPath;
        }
        lane = laneOf(c.path, c.dir, c.lane);
        if (!lane || lane->line.points.size() < 2) {
            returnToPool(idx);
            return false;
        }
        S = static_cast<int>(lane->line.points.size());
        chooseNext(c);
        c.section = 1;
        c.roadDist = c.segDist - c.segLen;
        c.segDist = c.roadDist;
        const auto& cum = lane->line.distances;
        float segLen = cum[1] - cum[0];
        Vec3 p1, m1;
        if (S == 2) {
            segLen -= c.frontBumper;
            p1 = subSectionPoint(c.path, c.dir, c.lane, 1, c.frontBumper) + xAxisAt(c.path, c.dir, 1) * c.laneRandomness;
            m1 = entryVector(c.path, c.dir, segLen);
        } else {
            p1 = lanePoint(c, c.path, c.dir, c.lane, 1);
            m1 = subSectionDir(c.path, c.dir, 1, segLen);
        }
        c.segLen = std::max(segLen, 1e-3f);
        setCurve(c, lanePoint(c, c.path, c.dir, c.lane, 0), p1, exitVector(c.path, c.dir, c.segLen), m1);
        c.rail = Rail::Lane;
        c.signal = TurnSignal::None;
        break;
    }
    case Rail::LaneChange: {
        c.roadDist = (c.segDist - c.segLen) + c.laneChangeDist;
        const int i = index(c.path, c.dir, c.lane, c.roadDist);
        c.section = i;
        c.segDist = subSectionDist(c.path, c.dir, c.lane, c.roadDist);
        const auto& cum = lane->line.distances;
        float segLen = cum[static_cast<std::size_t>(i)] - cum[static_cast<std::size_t>(i - 1)];
        const Vec3 p0 = lanePoint(c, c.path, c.dir, c.lane, i - 1);
        if (i < S - 1) {
            c.segLen = std::max(segLen, 1e-3f);
            setCurve(c, p0, lanePoint(c, c.path, c.dir, c.lane, i), subSectionDir(c.path, c.dir, i - 1, c.segLen),
                     subSectionDir(c.path, c.dir, i, c.segLen));
        } else {
            segLen -= c.frontBumper;
            c.segLen = std::max(segLen, 1e-3f);
            setCurve(c, p0,
                     subSectionPoint(c.path, c.dir, c.lane, i, c.frontBumper) +
                         xAxisAt(c.path, c.dir, i) * c.laneRandomness,
                     subSectionDir(c.path, c.dir, i - 1, c.segLen), entryVector(c.path, c.dir, c.segLen));
        }
        removeVehicle(idx, c.path, c.dir, c.drawLane);
        c.drawLane = c.lane;
        chooseNext(c);
        c.rail = Rail::Lane;
        c.signal = TurnSignal::None;
        break;
    }
    case Rail::Regain:
        break;
    }
    return true;
}

// aiGoalRandomDrive::SolveLane: on roads over 60 m, move to the neighbouring
// lane with fewer cars beyond the first quarter.
void Traffic::solveLane(Car& c) {
    const PathInfo& info = m_net.paths()[static_cast<std::size_t>(c.path)];
    if (!(60.0f < info.centreLength))
        return;
    const float q = laneLength(c.path, c.dir, c.drawLane) * 0.25f;
    auto count = [&](int lane) {
        int n = 0;
        if (const auto* list = queueOf(c.path, c.dir, lane))
            for (int o : *list) {
                if (!(q < m_cars[static_cast<std::size_t>(o)].roadDist))
                    break;
                ++n;
            }
        return n;
    };
    int n = count(c.drawLane);
    if (0 < c.drawLane) {
        const int m = count(c.drawLane - 1);
        if (m < n) {
            c.lane = c.drawLane - 1;
            n = m;
        }
    }
    const int lanes = static_cast<int>(info.lanesOf(c.dir).size());
    if (c.drawLane < lanes - 1 && count(c.drawLane + 1) < n)
        c.lane = c.drawLane + 1;
}

// aiGoalRandomDrive::ChangeLanes: a curve from the car's position onto the
// new lane, ending a quarter of the lane plus 30 m along it.
void Traffic::changeLanes(int idx) {
    Car& c = m_cars[static_cast<std::size_t>(idx)];
    if (c.lane == c.drawLane)
        return;
    const Lane* oldLane = laneOf(c.path, c.dir, c.drawLane);
    const Lane* newLane = laneOf(c.path, c.dir, c.lane);
    if (!oldLane || !newLane || oldLane->line.points.size() < 2) {
        c.lane = c.drawLane;
        return;
    }
    const int oldL = c.drawLane, newL = c.lane;
    const auto& co = oldLane->line.distances;
    const auto& cn = newLane->line.distances;
    const int S = static_cast<int>(co.size());
    // The car's distance carried over to the new lane, for its list.
    int i = index(c.path, c.dir, oldL, c.roadDist);
    const float remaining = co[static_cast<std::size_t>(i)] - c.roadDist;
    addVehicle(idx, c.path, c.dir, newL, cn[static_cast<std::size_t>(i)] - remaining);
    // Start: the car's place on its current curve.
    const float laneEnd = oldLane->line.length - c.frontBumper;
    Vec3 startDir;
    const Vec3 start = curvePoint(c, c.segDist / c.segLen, &startDir);
    // End: a quarter of the lane plus 30 m, carried over to the new lane.
    const float target = laneEnd * 0.25f + 30.0f;
    i = index(c.path, c.dir, oldL, target);
    const float back = co[static_cast<std::size_t>(i)] - target;
    c.laneChangeDist = cn[static_cast<std::size_t>(i)] - back;
    const int k = index(c.path, c.dir, newL, c.laneChangeDist);
    float segLen;
    Vec3 q0, q1, m0, m1;
    if (k == S - 1) {
        // MM2 adds the back bumper to this segment's length.
        segLen = (cn[static_cast<std::size_t>(k)] - cn[static_cast<std::size_t>(k - 1)]) + c.backBumper;
        q0 = newLane->line.points[static_cast<std::size_t>(k - 1)];
        q1 = subSectionPoint(c.path, c.dir, newL, k, c.frontBumper);
        m0 = subSectionDir(c.path, c.dir, k - 1, segLen);
        m1 = entryVector(c.path, c.dir, segLen);
    } else {
        segLen = cn[static_cast<std::size_t>(k)] - cn[static_cast<std::size_t>(k - 1)];
        q0 = newLane->line.points[static_cast<std::size_t>(k - 1)];
        q1 = newLane->line.points[static_cast<std::size_t>(k)];
        m0 = subSectionDir(c.path, c.dir, k - 1, segLen);
        m1 = subSectionDir(c.path, c.dir, k, segLen);
    }
    q0 += xAxisAt(c.path, c.dir, k - 1) * c.laneRandomness;
    q1 += xAxisAt(c.path, c.dir, k) * c.laneRandomness;
    Vec3 endDir;
    const float tEnd = segLen > 0.0f ? subSectionDist(c.path, c.dir, newL, c.laneChangeDist) / segLen : 0.0f;
    const Vec3 end = hermitePoint(q0, q1, m0, m1, tEnd, &endDir);
    c.segDist = 0.0f;
    c.segLen = std::max(c.laneChangeDist - c.roadDist, 1e-3f);
    auto scaled = [&](Vec3 d) {
        const float m2 = d.mag2();
        return m2 == 0.0f ? Vec3{} : d * (c.segLen / std::sqrt(m2));
    };
    Vec3 endPoint = end;
    endPoint.y = lerp(q0.y, q1.y, clampf(tEnd, 0.0f, 1.0f));
    setCurve(c, start, endPoint, scaled(startDir), scaled(endDir));
    c.signal = oldL < newL ? TurnSignal::Right : TurnSignal::Left;
    c.rail = Rail::LaneChange;
}

// The car's matrix (the solver aiGoalRandomDrive calls after moving it).
void Traffic::solvePose(Car& c, const PlayerCar& player) {
    const float t = c.segLen > 0.0f ? c.segDist / c.segLen : 0.0f;
    Vec3 F;
    Vec3 P = curvePoint(c, t, &F);
    const float f2 = F.mag2();
    F = f2 == 0.0f ? Vec3{0, 0, -1} : F * (1.0f / std::sqrt(f2));
    const Vec3 R{-F.z, 0.0f, F.x};
    const PathInfo& info = m_net.paths()[static_cast<std::size_t>(c.path)];
    const city::AiPath& src = m_net.source()->paths[static_cast<std::size_t>(c.path)];
    const bool flat = info.flags & 0x8;
    const bool nextFlat = c.nextPath >= 0 && (m_net.paths()[static_cast<std::size_t>(c.nextPath)].flags & 0x8);
    // Flat roads (path flag 0x8): upright, at the road's first centre height.
    if ((c.nextPath < 0 && flat) || (c.goal == AmbientGoal::RandomDrive && flat && nextFlat)) {
        c.transform = frameFromRows(R, Vec3::yAxis(), -F, {P.x, src.center.empty() ? P.y : src.center[0].y, P.z});
        c.fitted = false;
        return;
    }
    const bool playerNear =
        player.valid && Vec2{player.transform.m3.x - P.x, player.transform.m3.z - P.z}.mag2() < 10000.0f;
    const int S = static_cast<int>(src.center.size());
    if (!playerNear && c.goal != AmbientGoal::RegainRail && c.rail == Rail::Lane && c.section < S - 1 &&
        !src.xAxis.empty()) {
        // The road section's own frame; the height along the drawn lane.
        const int k = c.dir == 1 ? c.section : S - c.section;
        const auto u = static_cast<std::size_t>(std::clamp(k, 0, S - 1));
        const Vec3 a = c.dir == 1 ? -src.xAxis[u] : src.xAxis[u];
        const Vec3 c2 = c.dir == 1 ? src.zAxis[u] : -src.zAxis[u];
        float h = P.y;
        if (const Lane* drawn = laneOf(c.path, c.dir, c.drawLane); drawn && c.section < static_cast<int>(drawn->line.points.size())) {
            const Vec3& v0 = drawn->line.points[static_cast<std::size_t>(c.section - 1)];
            const Vec3& v1 = drawn->line.points[static_cast<std::size_t>(c.section)];
            h = (v1.y - v0.y) * t + v0.y;
        }
        c.transform = frameFromRows(a, src.yAxis[u], c2, {P.x, h, P.z});
        c.fitted = false;
        return;
    }
    // Three corners fitted to the ground: front left, front right, back left.
    const float y = c.transform.m3.y;
    Vec3 fl = P - R * c.leftSide + F * c.frontBumper;
    Vec3 fr = P + R * c.rightSide + F * c.frontBumper;
    Vec3 bl = P - R * c.leftSide - F * c.backBumper;
    for (Vec3* corner : {&fl, &fr, &bl}) {
        corner->y = m_probe ? y : P.y;
        if (!m_probe)
            continue;
        for (int k = 1; k <= 3; ++k) {
            Vec3 hit;
            const float r = 2.5f * static_cast<float>(k);
            if (m_probe({corner->x, corner->y + r, corner->z}, {corner->x, corner->y - r, corner->z}, hit)) {
                corner->y = hit.y;
                break;
            }
        }
    }
    Vec3 a = fr - fl;
    Vec3 b2 = bl - fl;
    const float u = c.frontBumper / (c.backBumper + c.frontBumper);
    const float v = c.leftSide / (c.rightSide + c.leftSide);
    const Vec3 pos = fl + a * v + b2 * u;
    a = a.normalized();
    b2 = b2.normalized();
    const Vec3 up{b2.y * a.z - b2.z * a.y, a.x * b2.z - b2.x * a.z, b2.x * a.y - a.x * b2.y};
    c.transform = frameFromRows(a, up, b2, pos);
    c.fitted = true;
}

// aiGoalRandomDrive::Update.
void Traffic::updateRandomDrive(int idx, float dt, const PlayerCar& player) {
    Car& c = m_cars[static_cast<std::size_t>(idx)];
    if (c.goalTicks == 0)
        resetRandomDrive(c);
    solveVelocity(idx, dt);
    if (m_settings.laneChanges && c.laneChangeOk && c.rail == Rail::Lane &&
        laneLength(c.path, c.dir, c.drawLane) * 0.25f < c.roadDist) {
        solveLane(c);
        changeLanes(idx);
        c.laneChangeOk = false;
    }
    if (c.speed <= 0.0f) {
        // A stopped car is only refitted once a player comes within 100 m.
        if (!c.fitted && player.valid &&
            Vec2{player.transform.m3.x - c.transform.m3.x, player.transform.m3.z - c.transform.m3.z}.mag2() <
                10000.0f) {
            c.fitted = true;
            solvePose(c, player);
        }
    } else if (solveRailType(idx)) {
        solvePose(c, player);
        if (player.valid &&
            Vec2{c.transform.m3.x - player.transform.m3.x, c.transform.m3.z - player.transform.m3.z}.mag2() <
                sq(kPlayerZoneDistance) &&
            detectPlayerCollision(c, player) && !ambientBlockingPlayer(idx, player)) {
            c.goal = AmbientGoal::AvoidPlayer;
            c.goalTicks = 0;
            return;
        }
    } else {
        return; // back in the pool
    }
    ++c.curReactTicks;
    ++c.goalTicks;
}

// --- Player reactions ---------------------------------------------------------

// aiVehicleSpline::DetectPlayerCollision: the player's centre within the
// car's width along the next three 10 m pieces of its rail.
bool Traffic::detectPlayerCollision(const Car& c, const PlayerCar& p) const {
    Vec3 s = c.transform.m3;
    for (int i = 1; i <= 3; ++i) {
        const Vec3 q = railPosition(c, c.roadDist + static_cast<float>(i) * 10.0f);
        Vec2 n = xz(q - s);
        const float m2 = n.mag2();
        n = m2 == 0.0f ? Vec2{} : n * (1.0f / std::sqrt(m2));
        const Vec2 pp = xz(p.transform.m3 - s);
        const float along = pp.y * n.y + pp.x * n.x;
        const float lat = n.x * pp.y + -n.y * pp.x;
        if (along < 11.0f && c.frontBumper < along && -c.leftSide < lat && lat < c.rightSide)
            return true;
        s = q;
    }
    return false;
}

// aiVehicleSpline::DetectPlayerZoneCollision: the player within the next
// 25 m of rail, at least 3 m either side.
bool Traffic::detectPlayerZoneCollision(const Car& c, const PlayerCar& p) const {
    const Vec3& C = c.transform.m3;
    const Vec3 Z = c.transform.m2, X = c.transform.m0;
    const Vec3 q = railPosition(c, 25.0f + c.roadDist);
    const float zoneLen = Z.dot(C - q);
    const float railLat = (-X).dot(C - q);
    const float lo = std::min(railLat, -3.0f), hi = std::max(railLat, 3.0f);
    const float pz = Z.dot(C - p.transform.m3);
    const float px = (-X).dot(C - p.transform.m3);
    return pz < zoneLen && c.frontBumper < pz && lo < px && px < hi;
}

// aiVehicleSpline::IsThePlayerInFrontOfMe.
bool Traffic::playerInFront(const Car& c, const PlayerCar& p) const {
    const Vec3 d = c.transform.m3 - p.transform.m3;
    return c.transform.m2.dot(d) > c.frontBumper && d.mag2() < 625.0f;
}

// aiVehicleSpline::IsAmbientBlockingPlayer: the car ahead in the lane is
// nearer than the player.
bool Traffic::ambientBlockingPlayer(int idx, const PlayerCar& p) const {
    const Car& c = m_cars[static_cast<std::size_t>(idx)];
    const int lead = ahead(idx, c.lane);
    if (lead < 0)
        return false;
    const Vec3& C = c.transform.m3;
    const Vec3& Z = c.transform.m2;
    return Z.dot(C - m_cars[static_cast<std::size_t>(lead)].transform.m3) < Z.dot(C - p.transform.m3);
}

// Fits an off-rail car to the ground (the function aiGoalAvoidPlayer calls):
// three corners probed 5 m up and down from the current matrix.
void Traffic::fitOffRail(Car& c) {
    if (!m_probe)
        return;
    Mat34& m = c.transform;
    const Vec3 R = m.m0, F = -m.m2;
    Vec3 corners[3] = {m.m3 - R * c.leftSide + F * c.frontBumper, m.m3 + R * c.rightSide + F * c.frontBumper,
                       m.m3 - R * c.leftSide - F * c.backBumper};
    for (Vec3& p : corners) {
        Vec3 hit;
        if (m_probe(p + Vec3{0, 5, 0}, p - Vec3{0, 5, 0}, hit))
            p.y = hit.y;
        else
            p.y = m.m3.y;
    }
    Vec3 a = corners[1] - corners[0];
    Vec3 b = corners[2] - corners[0];
    const float u = c.frontBumper / (c.backBumper + c.frontBumper);
    const float v = c.leftSide / (c.rightSide + c.leftSide);
    m.m3 = corners[0] + a * v + b * u;
    m.m0 = a.normalized();
    m.m2 = b.normalized();
    m.m1 = m.m2.cross(m.m0);
}

// aiGoalAvoidPlayer: off its rail, swerving round the player.
void Traffic::updateAvoidPlayer(int idx, float dt, const PlayerCar& p) {
    Car& c = m_cars[static_cast<std::size_t>(idx)];
    const PathInfo& info = m_net.paths()[static_cast<std::size_t>(c.path)];
    const float cap = info.speedLimit + c.exceedLimit;
    const float R = p.radius;
    auto brakeFor = [&] {
        const float d = c.transform.m3.dist(p.transform.m3);
        const float room = d - ((R + c.separation) + c.frontBumper);
        c.accel = -((c.speed * c.speed) / (room != 0.0f ? room + room : 1e-6f));
    };
    if (c.goalTicks == 0) {
        // Reset: a chance to honk (the horn audio decides), the speed law,
        // the pass offset and whether the player is dead ahead.
        c.horn = true;
        if (cap <= c.speed)
            c.accel = c.accelFactor;
        else
            brakeFor();
        c.passOffset = (R + c.rightSide) + 2.5f;
        c.heading = std::atan2(c.transform.m2.x, c.transform.m2.z);
        const float lat = c.transform.m0.dot(p.transform.m3 - c.transform.m3);
        c.centred = -0.4f <= lat && lat <= 0.4f;
    }
    ++c.goalTicks;
    // aiGoalAvoidPlayer::AvoidPlayer.
    {
        const Vec3 d = p.transform.m3 - c.transform.m3;
        const float lat = c.transform.m0.dot(d);
        const float fwd = (-c.transform.m2).dot(d);
        const float f = m_rng.frand();
        const float off = c.passOffset;
        float beta;
        if ((info.flags & 0x1) && c.drawLane == 0) {
            if (f < 0.5f) {
                c.speed -= 0.5f;
                beta = 0.4f;
            } else {
                beta = std::atan2(lat + off, fwd);
            }
        } else if (c.centred) {
            c.accel -= 10.0f;
            beta = lat < 0.0f ? std::atan2(lat + off, fwd) : std::atan2(lat - off, fwd);
        } else if (std::abs(lat) <= 0.4f) {
            beta = 0.4f;
        } else if (lat > 0.4f) {
            beta = (p.horn || f < 0.7f) ? std::atan2(lat - off, fwd) : std::atan2(lat + off, fwd);
        } else {
            beta = (p.horn || f < 0.7f) ? std::atan2(lat + off, fwd) : std::atan2(lat - off, fwd);
        }
        float ratio = cap > 0.0f ? c.speed / cap : 0.0f;
        if (c.centred && c.speed > 5.0f)
            ratio = 1.0f;
        beta = clampf(beta, -0.02f, 0.02f);
        // Per update, as MM2 (not scaled by the time step).
        c.heading = beta * 1.5f * ratio + c.heading;
        const Vec3 pos = c.transform.m3;
        c.transform = Mat34::rotationY(c.heading);
        c.transform.m3 = pos;
        c.speed = dt * c.accel + c.speed;
        if (c.speed < 0.25f)
            c.speed = 0.0f;
        if (cap < c.speed)
            c.speed = cap;
        const float step = dt * c.speed;
        c.roadDist += step;
        c.transform.m3 -= c.transform.m2 * step;
        fitOffRail(c);
    }
    if (c.speed < cap)
        brakeFor();
    // The lane distance from the car's free position (aiMap::DetermineRoadPosInfo
    // on its road; inferred: the projection onto its lane).
    if (const Lane* lane = laneOf(c.path, c.dir, c.lane))
        c.roadDist = lane->line.project(c.transform.m3);
    if (!detectPlayerZoneCollision(c, p) && !playerInFront(c, p)) {
        c.goal = AmbientGoal::RegainRail;
        c.goalTicks = 0;
    }
}

// aiGoalRegainRail::Reset: a curve of up to 30 m from where the car is back
// onto its lane. MM2 first maps the car onto the road or intersection under
// it (aiMap::MapComponent); here it keeps its road and lane (inferred).
void Traffic::resetRegainRail(int idx) {
    Car& c = m_cars[static_cast<std::size_t>(idx)];
    const Vec3 pos = c.transform.m3;
    if (pos.dist2(c.regainStart) <= 1.0f) {
        ++c.regainAttempts;
    } else {
        c.regainAttempts = 1;
        c.regainStart = pos;
    }
    const Lane* lane = laneOf(c.path, c.dir, c.lane);
    const PathInfo& info = m_net.paths()[static_cast<std::size_t>(c.path)];
    float off = 0.0f;
    if (!lane || (info.flagsOf(c.dir) & 1)) {
        c.goal = AmbientGoal::Parked;
        return;
    }
    const float d = lane->line.project(pos, &off);
    if (off > info.halfWidth + 5.0f) {
        c.goal = AmbientGoal::Parked; // off the road
        return;
    }
    // Back into the lane list at the end of the curve.
    for (std::size_t l = 0; l < info.lanesOf(c.dir).size(); ++l)
        removeVehicle(idx, c.path, c.dir, static_cast<int>(l));
    c.drawLane = c.lane;
    chooseNext(c);
    c.regainLength = kRegainDistance;
    c.regainBase = d;
    c.roadDist = d + c.regainLength;
    addVehicle(idx, c.path, c.dir, c.lane, c.roadDist);
    // The curve: from the car along its heading to its rail point 30 m on.
    const Vec3 end = railPosition(c, c.roadDist);
    const Vec3 ahead = railPosition(c, c.roadDist + 1.0f);
    Vec3 endDir = ahead - end;
    endDir.y = 0.0f;
    endDir = endDir.mag2() > 1e-8f ? endDir.normalized() : -c.transform.m2;
    const Vec3 fwd = -c.transform.m2;
    setCurve(c, pos, end, fwd * 30.0f, endDir * c.regainLength);
    c.turnY1 = end.y;
    c.segDist = 0.0f;
    c.segLen = c.regainLength;
    c.rail = Rail::Regain;
    c.enterInt = false;
    c.target = info.speedLimit + c.exceedLimit;
    c.roadDist = c.regainBase;
}

// aiGoalRegainRail::Update.
void Traffic::updateRegainRail(int idx, float dt, const PlayerCar& p) {
    Car& c = m_cars[static_cast<std::size_t>(idx)];
    if (c.goalTicks == 0) {
        resetRegainRail(idx);
        if (c.goal != AmbientGoal::RegainRail)
            return;
    }
    ++c.goalTicks;
    if (p.valid && detectPlayerCollision(c, p)) {
        c.goal = AmbientGoal::AvoidPlayer;
        c.goalTicks = 0;
        return;
    }
    if (c.speed > c.target)
        c.speed = c.target;
    else if (c.speed < c.target)
        c.speed = dt * c.accelFactor + c.speed;
    c.segDist = c.speed * dt + c.segDist;
    c.roadDist = c.segDist + c.regainBase;
    solvePose(c, p);
    if (c.segDist > c.regainLength) {
        c.goal = AmbientGoal::RandomDrive;
        c.goalTicks = 0;
        c.signal = TurnSignal::None; // hazards off
    }
}

// --- Physics hand-over ---------------------------------------------------------

void Traffic::impact(int carId, const Vec3& impulse) {
    if (carId < 0 || static_cast<std::size_t>(carId) >= m_cars.size())
        return;
    Car& c = m_cars[static_cast<std::size_t>(carId)];
    if (!c.active || c.physical)
        return;
    // aiVehicleAmbient::Impact(1).
    c.speed = 0.0f;
    c.target = m_net.paths()[static_cast<std::size_t>(c.path)].speedLimit + c.exceedLimit + 0.0001f;
    c.goal = AmbientGoal::Collision;
    c.goalTicks = 0;
    c.physical = true;
    publish();
    if (m_onImpact)
        for (auto& a : m_public)
            if (a.id == carId)
                m_onImpact(a, impulse);
}

void Traffic::detach(int carId, const Mat34& transform, bool upright) {
    if (carId < 0 || static_cast<std::size_t>(carId) >= m_cars.size())
        return;
    Car& c = m_cars[static_cast<std::size_t>(carId)];
    if (!c.active)
        return;
    c.physical = false;
    c.transform = transform;
    // aiVehicleActive::Detach: upright on the ground and never wrecked ->
    // aiVehicleAmbient::Impact(0); otherwise a wreck for good.
    if (upright && !c.wreck) {
        c.speed = 0.0f;
        c.target = m_net.paths()[static_cast<std::size_t>(c.path)].speedLimit + c.exceedLimit + 0.0001f;
        c.goal = c.regainAttempts == 3 ? AmbientGoal::Parked : AmbientGoal::RegainRail;
        c.goalTicks = 0;
    } else {
        c.wreck = true;
    }
}

void Traffic::setPhysicalTransform(int carId, const Mat34& transform) {
    if (carId >= 0 && static_cast<std::size_t>(carId) < m_cars.size())
        m_cars[static_cast<std::size_t>(carId)].transform = transform;
}

void Traffic::release(int carId) {
    if (carId >= 0 && static_cast<std::size_t>(carId) < m_cars.size() && m_cars[static_cast<std::size_t>(carId)].active)
        returnToPool(carId);
}

// --- Update ------------------------------------------------------------------

void Traffic::step(float dt, const Vec3& pos, const Vec3& vel, int playerRoom) {
    step(dt, PlayerCar::at(pos, vel), playerRoom);
}

void Traffic::step(float dt, const PlayerCar& player, int playerRoom) {
    m_player = player;
    // Population: aiMap::Reset, then whenever the player enters a new room.
    if (!m_started) {
        m_started = true;
        m_room = playerRoom;
        adjustAmbients(0, playerRoom);
        m_populateAll = false;
    } else if (playerRoom != 0 && playerRoom != m_room) {
        adjustAmbients(m_room, playerRoom);
        m_room = playerRoom;
    }
    // Lights that turned green restart their queues (aiTrafficLightSet::
    // Update -> aiPath::ResetVehicleReactTicks).
    for (const GreenEvent& g : m_lights.newGreens()) {
        if (g.path < 0)
            continue;
        const PathInfo& info = m_net.paths()[static_cast<std::size_t>(g.path)];
        const int dir = info.intersection[1] != g.intersection ? 1 : -1;
        for (int laneId : info.lanesOf(dir)) {
            const auto& q = m_queues[static_cast<std::size_t>(laneId)];
            if (!q.empty())
                resetReactTicks(q.front());
        }
    }
    // aiPath::UpdateAmbients for each populated road, each car once.
    std::vector<int> order;
    std::vector<std::uint8_t> seen(m_cars.size(), 0);
    for (int p : m_activePaths) {
        for (int dir : {-1, 1}) {
            const auto& lanes = m_net.paths()[static_cast<std::size_t>(p)].lanesOf(dir);
            for (std::size_t l = 0; l < lanes.size(); ++l) {
                for (int car : m_queues[static_cast<std::size_t>(lanes[l])]) {
                    const Car& c = m_cars[static_cast<std::size_t>(car)];
                    if (c.lane == static_cast<int>(l) && !seen[static_cast<std::size_t>(car)]) {
                        seen[static_cast<std::size_t>(car)] = 1;
                        order.push_back(car);
                    }
                }
            }
        }
    }
    for (auto& c : m_cars)
        c.horn = false;
    for (int idx : order) {
        Car& c = m_cars[static_cast<std::size_t>(idx)];
        if (!c.active)
            continue;
        switch (c.goal) {
        case AmbientGoal::RandomDrive:
            updateRandomDrive(idx, dt, player);
            break;
        case AmbientGoal::Collision:
            if (c.goalTicks == 0)
                c.signal = TurnSignal::Hazard; // aiGoalCollision::Reset
            ++c.goalTicks;
            break;
        case AmbientGoal::RegainRail:
            updateRegainRail(idx, dt, player);
            break;
        case AmbientGoal::AvoidPlayer:
            updateAvoidPlayer(idx, dt, player);
            break;
        case AmbientGoal::Parked:
            break;
        }
        if (!c.active)
            continue;
        // aiVehicleSpline::Update: the tyres turn with the distance driven.
        c.tireRotation += dt * c.speed;
        if (c.tireRotation > kTireRotationWrap)
            c.tireRotation -= kTireRotationWrap;
    }
    publish();
}

void Traffic::publish() {
    m_public.clear();
    for (std::size_t i = 0; i < m_cars.size(); ++i) {
        const Car& c = m_cars[i];
        if (!c.active)
            continue;
        AmbientCar a;
        a.id = static_cast<int>(i);
        a.data = &m_types[static_cast<std::size_t>(c.type)];
        a.model = a.data->model;
        a.paint = c.paint;
        a.transform = c.transform;
        a.speed = c.speed;
        a.velocity = -c.transform.m2 * c.speed;
        a.tireRotation = c.tireRotation;
        a.braking = c.accel < -0.5f || (c.speed < 0.5f && c.target == 0.0f);
        a.signal = c.signal;
        a.horn = c.horn;
        a.goal = c.goal;
        a.physical = c.physical;
        a.wreck = c.wreck;
        m_public.push_back(std::move(a));
    }
}

std::size_t Traffic::activeCount() const {
    return static_cast<std::size_t>(std::ranges::count_if(m_cars, [](const Car& c) { return c.active; }));
}

Traffic::DebugCar Traffic::debug(int carId) const {
    DebugCar d;
    if (carId < 0 || static_cast<std::size_t>(carId) >= m_cars.size())
        return d;
    const Car& c = m_cars[static_cast<std::size_t>(carId)];
    if (!c.active)
        return d;
    d.lane = m_net.lane(c.path, c.dir, c.lane);
    d.nextLane = c.nextPath >= 0 ? m_net.lane(c.nextPath, c.nextDir, c.nextLane) : -1;
    d.s = c.roadDist;
    d.turning = c.rail == Rail::Turn;
    d.changingLane = c.rail == Rail::LaneChange;
    d.entered = c.enterInt;
    d.accel = c.accel;
    d.targetVelocity = c.target;
    d.reactTicks = c.curReactTicks;
    d.totReactTicks = c.totReactTicks;
    d.goal = c.goal;
    d.lead = ahead(carId, c.lane);
    if (d.lead >= 0)
        d.leadDistance = distanceToVehicle(c, m_cars[static_cast<std::size_t>(d.lead)]);
    return d;
}

} // namespace mm2::ai
