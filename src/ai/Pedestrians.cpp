// Pedestrians after MM2's aiPedestrian (build 3393, MM2Recomp; documentation
// only); see Pedestrians.h and docs/ai.md.
#include "ai/Pedestrians.h"

#include "core/StringUtil.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mm2::ai {
namespace {

constexpr float kFrameSeconds = 0.03333f; // pedAnimation::Load: sequence duration = frames x 0.03333

float dotXZ(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.z * b.z;
}

float distXZ2(const Vec3& a, const Vec3& b) {
    return (a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z);
}

Vec3 normalized3(const Vec3& v) {
    const float m2 = v.mag2();
    return m2 > 0.0f ? v * (1.0f / std::sqrt(m2)) : Vec3{};
}

// The pedestrian's matrix for heading h: forward (sin h, 0, cos h) is -Z.
Mat34 frameOf(float h, const Vec3& pos) {
    Mat34 m = Mat34::rotationY(h + kPi);
    m.m3 = pos;
    return m;
}

float headingOf(const Vec3& v) {
    return std::atan2(v.x, v.z);
}

void hermite(float p0, float p1, float m0, float m1, float out[4]) {
    out[0] = 2.0f * p0 - 2.0f * p1 + m0 + m1;
    out[1] = -3.0f * p0 + 3.0f * p1 - 2.0f * m0 - m1;
    out[2] = m0;
    out[3] = p0;
}

} // namespace

Pedestrians::Pedestrians(const RoadNetwork& network, std::vector<PedTypeInfo> types,
                         const PedSettings& settings, std::uint64_t seed)
    : m_net(network), m_types(std::move(types)), m_settings(settings), m_rng(seed) {
    // The sequences the AI asks for, by name (aiPedestrian::Init).
    for (const auto& t : m_types) {
        Seqs s;
        auto f = [&](std::string_view n) { return t.table.find(n); };
        s.stand = f("STAND");
        s.stand2 = f("STAND2");
        s.standWalk = f("STAND_WALK");
        s.walk = f("WALK");
        s.walkStand = f("WALK_STAND");
        s.standAntic = f("STAND_ANTIC");
        s.antic = f("ANTIC");
        s.anticWalk = f("ANTIC_WALK");
        s.walkAntic = f("WALK_ANTIC");
        s.anticLDive = f("ANTIC_LDIVE");
        s.lDiveGround = f("LDIVE_GROUNDL");
        s.groundStandL = f("GROUND_STANDL");
        s.groundStandR = f("GROUND_STANDR");
        s.anticRDive = f("ANTIC_RDIVE");
        s.rDiveGround = f("RDIVE_GROUNDR");
        s.walkRDive = f("WALK_RDIVE");
        s.walkLDive = f("WALK_LDIVE");
        s.run = f("RUN");
        s.backup = f("BACKUP");
        s.backupWalk = f("BACKUP_WALK");
        s.runWalk = f("RUN_WALK");
        m_seqs.push_back(s);
    }
    // Sidewalk geometry per road side (aiPath::SidewalkVertice: the row after
    // the lanes; the curb is the second last polyline).
    const city::AiMap* map = m_net.source();
    m_walks.resize(m_net.paths().size());
    for (std::size_t p = 0; p < m_net.paths().size() && map; ++p) {
        const city::AiPath& src = map->paths[p];
        for (int s = 0; s < 2; ++s) {
            const city::AiRoadSide& side = s == 0 ? src.left : src.right;
            Walk& w = m_walks[p][static_cast<std::size_t>(s)];
            if (side.numSidewalks == 0 || static_cast<std::size_t>(side.numLanes) >= side.polylines.size() ||
                side.polylines.size() < 3)
                continue;
            w.points = side.polylines[static_cast<std::size_t>(side.numLanes)];
            w.curb = side.polylines[side.polylines.size() - 2];
            w.cum.assign(w.points.size(), 0.0f);
            for (std::size_t i = 1; i < w.points.size(); ++i)
                w.cum[i] = w.cum[i - 1] + w.points[i].dist(w.points[i - 1]);
            const std::size_t k = static_cast<std::size_t>(side.numLanes) * 2;
            if (k + 1 < side.params.size()) {
                w.inner = side.params[k];
                w.outer = side.params[k + 1];
            }
            w.open = (side.roadType & 2) == 0 && w.points.size() >= 2;
            for (int id : m_net.paths()[p].sidewalks)
                if (m_net.sidewalks()[static_cast<std::size_t>(id)].side == s)
                    w.sidewalk = id;
        }
    }
    m_onPath.resize(m_net.paths().size());
    m_pathActive.assign(m_net.paths().size(), 0);
    // aiMap::Init: trunc(pool x density) pedestrians, types and clothing
    // drawn per pedestrian (aiPedestrian::Init).
    const int count = m_types.empty() ? 0
                                      : std::max(0, static_cast<int>(static_cast<float>(m_settings.pool) *
                                                                     m_settings.density));
    m_peds.resize(static_cast<std::size_t>(count));
    std::vector<int> allowed;
    for (const auto& name : m_settings.names)
        for (std::size_t t = 0; t < m_types.size(); ++t)
            if (str::iequals(m_types[t].name, name))
                allowed.push_back(static_cast<int>(t));
    if (allowed.empty())
        for (std::size_t t = 0; t < m_types.size(); ++t)
            allowed.push_back(static_cast<int>(t));
    for (auto& p : m_peds) {
        p.type = allowed[static_cast<std::size_t>(m_rng.frand() * static_cast<float>(allowed.size()))];
        const int variants = m_types[static_cast<std::size_t>(p.type)].variants;
        p.variant = static_cast<int>(m_rng.frand() * static_cast<float>(variants - 1));
    }
    for (int i = 0; i < count; ++i)
        m_pool.push_back(i);
}

// --- Geometry -------------------------------------------------------------------

const Pedestrians::Walk& Pedestrians::walk(int path, int side) const {
    return m_walks[static_cast<std::size_t>(path)][side == 1 ? 1u : 0u];
}

int Pedestrians::sections(int path) const {
    return static_cast<int>(walk(path, 1).points.empty() ? walk(path, -1).points.size()
                                                          : walk(path, 1).points.size());
}

Vec3 Pedestrians::sv(int path, int side, int i) const {
    const auto& pts = walk(path, side).points;
    if (pts.empty())
        return {};
    return pts[static_cast<std::size_t>(std::clamp(i, 0, static_cast<int>(pts.size()) - 1))];
}

float Pedestrians::cumAt(int path, int side, int i) const {
    const auto& cum = walk(path, side).cum;
    if (cum.empty())
        return 0.0f;
    return cum[static_cast<std::size_t>(std::clamp(i, 0, static_cast<int>(cum.size()) - 1))];
}

Vec3 Pedestrians::axisX(int path, int i) const {
    const auto& x = m_net.source()->paths[static_cast<std::size_t>(path)].xAxis;
    return x.empty() ? Vec3{} : x[static_cast<std::size_t>(std::clamp(i, 0, static_cast<int>(x.size()) - 1))];
}

Vec3 Pedestrians::axisZ(int path, int i) const {
    const auto& z = m_net.source()->paths[static_cast<std::size_t>(path)].zAxis;
    return z.empty() ? Vec3{} : z[static_cast<std::size_t>(std::clamp(i, 0, static_cast<int>(z.size()) - 1))];
}

Vec3 Pedestrians::axisW(int path, int i) const {
    const auto& w = m_net.source()->paths[static_cast<std::size_t>(path)].wAxis;
    return w.empty() ? Vec3{} : w[static_cast<std::size_t>(std::clamp(i, 0, static_cast<int>(w.size()) - 1))];
}

// aiPath::Index on the sidewalk row.
int Pedestrians::sidewalkIndex(int path, int side, float dist) const {
    const auto& cum = walk(path, side).cum;
    const int n = static_cast<int>(cum.size());
    if (n < 2)
        return 1;
    dist = clampf(dist, 0.0f, cum.back());
    for (int i = 1; i < n; ++i)
        if (dist <= cum[static_cast<std::size_t>(i)] + 1e-5f)
            return i;
    return n - 1;
}

// aiPath::GetHeading: along the sidewalk segment holding `dist`, in `dir`.
float Pedestrians::headingAt(const Ped& p, float dist, int dir) const {
    const int i = sidewalkIndex(p.path, p.side, dist);
    const Vec3 d = (sv(p.path, p.side, i) - sv(p.path, p.side, i - 1)) * static_cast<float>(dir);
    return headingOf(d);
}

// --- Animation (pedAnimationInstance) ---------------------------------------

int Pedestrians::frameCount(const Ped& p, State s) const {
    if (!s)
        return 1;
    int frames = std::max(1, s->lastFrame - s->firstFrame + 1);
    const auto& counts = m_types[static_cast<std::size_t>(p.type)].animFrames;
    if (auto it = counts.find(str::lower(s->animFile)); it != counts.end() && it->second > 0)
        frames = std::min(frames, it->second);
    return frames;
}

float Pedestrians::fwdSpeed(const Ped&, State s) const {
    if (!s)
        return 0.0f;
    const float frames = static_cast<float>(std::max(1, s->lastFrame - s->firstFrame + 1));
    return s->forwardDistance / (frames * kFrameSeconds);
}

float Pedestrians::latSpeed(const Ped&, State s) const {
    if (!s)
        return 0.0f;
    const float frames = static_cast<float>(std::max(1, s->lastFrame - s->firstFrame + 1));
    return s->sideDistance / (frames * kFrameSeconds);
}

// Start: at once, from frame 0; the sequence's own next is queued.
void Pedestrians::startSeq(Ped& p, State s) {
    if (!s)
        return;
    p.seq = s;
    p.frame = 0;
    p.queued = m_types[static_cast<std::size_t>(p.type)].table.find(s->next);
}

// --- Curves (aiPedestrian::CalcCurve, SolvePosition, SolveTargetPoint) ----

void Pedestrians::calcCurve(Ped& p, int a, int b, float lateral) {
    const int n = sections(p.path);
    lateral = clampf(lateral, -kPedMaxLateral, kPedMaxLateral);
    auto P = [&](int k) { return sv(p.path, p.side, k) - axisX(p.path, k) * lateral; };
    Vec3 p0, p1, t0, t1;
    float len = cumAt(p.path, p.side, b) - cumAt(p.path, p.side, a);
    if (b == 0 || b == n) {
        // The corner between two roads: a straight line, no lateral offset.
        const int np = sections(p.prevPath);
        const Vec3 w = p.prevDir == 1 ? sv(p.prevPath, p.prevSide, np - 1) : sv(p.prevPath, p.prevSide, 0);
        if (p.dir == 1) {
            p0 = w;
            p1 = sv(p.path, p.side, 0);
        } else {
            p0 = sv(p.path, p.side, n - 1);
            p1 = w;
        }
        t0 = t1 = p1 - p0;
        const float d = (p1 - p0).mag();
        p.invLen = p.path == p.prevPath || d == 0.0f ? 1.0f : 1.0f / d;
    } else {
        if (b == n - 1) {
            p0 = P(a);
            p1 = P(n - 1);
            t0 = axisW(p.path, a) * len;
            t1 = axisZ(p.path, n - 1) * -len;
        } else if (b == 1) {
            p0 = P(0);
            p1 = P(1);
            t0 = axisZ(p.path, 0) * -len;
            t1 = axisW(p.path, 1) * len;
        } else {
            p0 = P(a);
            p1 = P(b);
            t0 = axisW(p.path, a) * len;
            t1 = axisW(p.path, b) * len;
        }
        p.invLen = len != 0.0f ? 1.0f / len : 1.0f;
    }
    hermite(p0.x, p1.x, t0.x, t1.x, p.curve[0]);
    hermite(p0.z, p1.z, t0.z, t1.z, p.curve[1]);
}

Vec3 Pedestrians::solvePosition(const Ped& p, float t) const {
    auto f = [&](const float k[4]) { return ((k[0] * t + k[1]) * t + k[2]) * t + k[3]; };
    return {f(p.curve[0]), p.position.y, f(p.curve[1])};
}

void Pedestrians::solveTargetPoint(Ped& p, float d) {
    const int n = sections(p.path);
    float t;
    if (p.idx == 0)
        t = d * p.invLen;
    else if (p.idx < n)
        t = (d - cumAt(p.path, p.side, p.idx - 1)) * p.invLen;
    else
        t = (d - cumAt(p.path, p.side, n - 1)) * p.invLen;
    p.target = solvePosition(p, t);
    p.target.y = p.position.y;
}

// aiPedestrian::RoadDistance: the distance along the sidewalk, moving on to
// the next vertex (and its curve) once the pedestrian has passed it.
float Pedestrians::roadDistance(Ped& p) {
    const int n = sections(p.path);
    const int np = sections(p.prevPath);
    auto lateralAt = [&](int k) {
        return clampf(dotXZ(sv(p.path, p.side, k) - p.position, axisX(p.path, k)), -kPedMaxLateral,
                      kPedMaxLateral);
    };
    if (p.dir == 1) {
        const Vec3 v = sv(p.path, p.side, p.idx);
        Vec3 w, u;
        if (p.idx == 0) {
            w = p.prevDir == 1 ? sv(p.prevPath, p.prevSide, np - 1) : sv(p.prevPath, p.prevSide, 0);
            u = normalized3(w - v);
        } else {
            u = axisZ(p.path, p.idx);
        }
        const float s = dotXZ(p.position - v, u);
        if (s <= 0.0f) {
            ++p.idx;
            if (p.idx < n) {
                p.lateral = lateralAt(p.idx - 1);
                calcCurve(p, p.idx - 1, p.idx, p.lateral);
                return cumAt(p.path, p.side, p.idx - 1) - s;
            }
            p.idx = n - 1;
            p.lateral = lateralAt(p.idx);
            calcCurve(p, p.idx - 1, p.idx, p.lateral);
            return cumAt(p.path, p.side, p.idx) - s;
        }
        if (p.idx != 0)
            return cumAt(p.path, p.side, p.idx) - s;
        return dotXZ(w - sv(p.path, p.side, 0), u) - s;
    }
    if (p.idx == n) {
        // MM2 picks the other road's point the opposite way round from
        // CalcCurve here.
        const Vec3 v = sv(p.path, p.side, n - 1);
        const Vec3 w = p.prevDir == 1 ? sv(p.prevPath, p.prevSide, 0) : sv(p.prevPath, p.prevSide, np - 1);
        const Vec3 u = normalized3(w - v);
        const float s = dotXZ(p.position - v, u);
        if (s < 0.0f) {
            p.idx = n - 1;
            calcCurve(p, p.idx - 1, p.idx, p.lateral);
        }
        return cumAt(p.path, p.side, p.idx - 1) + s;
    }
    const Vec3 v = sv(p.path, p.side, p.idx - 1);
    const float s = dotXZ(p.position - v, axisZ(p.path, p.idx));
    if (s < 0.0f)
        return cumAt(p.path, p.side, p.idx - 1) - s;
    --p.idx;
    if (p.idx > 0) {
        p.lateral = lateralAt(p.idx);
        calcCurve(p, p.idx - 1, p.idx, p.lateral);
        return cumAt(p.path, p.side, p.idx) - s;
    }
    p.idx = 0;
    return -s;
}

// aiPedestrian::SetNextRoad: the next road round the intersection on the
// pedestrian's side (GetRoadToRight / GetRoadToLeft).
int Pedestrians::setNextRoad(Ped& p, int node) const {
    const auto& paths = m_net.intersections()[static_cast<std::size_t>(node)].paths;
    const int count = static_cast<int>(paths.size());
    if (count == 0)
        return p.path;
    const PathInfo& info = m_net.paths()[static_cast<std::size_t>(p.path)];
    int k = info.intersection[0] == node ? info.roadIndex[0] : info.roadIndex[1];
    if (k < 0)
        k = 0;
    const bool right = p.dir == 1 ? p.side == 1 : p.side != 1;
    k = right ? (k + 1) % count : (k - 1 + count) % count;
    return paths[static_cast<std::size_t>(k)];
}

// aiPedestrian::PickNextRdSeg: at the end of the sidewalk, round the corner,
// or (only at lit intersections with a walk phase) across the next road or
// back across this one; turn round when the way on is closed.
int Pedestrians::pickNextRoad(Ped& p) {
    const PathInfo& info = m_net.paths()[static_cast<std::size_t>(p.path)];
    const int node = info.intersection[p.dir == 1 ? 0 : 1];
    if (node < 0) {
        p.dir = -p.prevDir;
        p.heading += 3.14f;
        return p.path;
    }
    p.crossNode = node;
    int choice = 0;
    if (m_lights && m_lights->hasLights(node) && m_lights->cycleAt(node) != LightCycle::Rotate)
        choice = static_cast<int>(m_rng.next() & 0x7FFF) % 3;
    if (m_accident && m_accident(node, -1))
        choice = 0;
    p.crossChoice = choice;
    int next = p.path;
    if (choice == 2) {
        p.cross = 1;
        p.dir = -p.dir;
        p.side = -p.side;
    } else {
        next = setNextRoad(p, node);
        const PathInfo& n = m_net.paths()[static_cast<std::size_t>(next)];
        const bool atEnd = n.intersection[0] == node;
        const bool same = p.prevDir == p.prevSide;
        if (choice == 0) {
            p.dir = atEnd ? -1 : 1;
            p.side = same ? (atEnd ? -1 : 1) : (atEnd ? 1 : -1);
        } else {
            p.cross = 1;
            p.dir = atEnd ? -1 : 1;
            p.side = same ? (atEnd ? 1 : -1) : (atEnd ? -1 : 1);
        }
    }
    if (walk(next, p.side).open && m_pathActive[static_cast<std::size_t>(next)])
        return next;
    p.dir = -p.prevDir;
    p.side = p.prevSide;
    p.cross = 0;
    p.heading += 3.14f;
    return p.path;
}

void Pedestrians::solveRoadSegment(Ped& p, float dist) {
    const int n = sections(p.path);
    const bool off = p.dir == 1 ? (p.idx >= 1 && dist > cumAt(p.path, p.side, n - 1)) : (p.idx < n && dist < 0.0f);
    if (!off)
        return;
    p.prevSide = p.side;
    p.prevDir = p.dir;
    const int next = pickNextRoad(p);
    const int id = static_cast<int>(&p - m_peds.data());
    std::erase(m_onPath[static_cast<std::size_t>(p.path)], id);
    p.prevPath = p.path;
    p.path = next;
    m_onPath[static_cast<std::size_t>(next)].push_back(id);
    p.idx = p.dir == 1 ? 0 : sections(next);
    roadDistance(p);
    calcCurve(p, p.idx - 1, p.idx, p.lateral);
}

// Turns the pedestrian towards `target` by at most 0.15 rad. aiPedestrian::
// AvoidObstacle turns the other way for small angles, as coded.
void Pedestrians::steer(Ped& p, const Vec3& target, bool avoidQuirk) {
    const Mat34 m = frameOf(p.heading, p.position);
    const Vec3 d = target - p.position;
    const float a = std::atan2(dotXZ(d, m.m0), -dotXZ(d, m.m2));
    if (!avoidQuirk) {
        p.heading += clampf(-a, -kPedTurnRate, kPedTurnRate);
        return;
    }
    if (a > kPedTurnRate)
        p.heading -= kPedTurnRate;
    else if (a < -kPedTurnRate)
        p.heading += kPedTurnRate;
    else
        p.heading += a;
}

// --- Population (aiMap::AdjustPedestrians, aiPedestrian::Reset) ------------

void Pedestrians::reset(int idx, int path, int side) {
    Ped& p = m_peds[static_cast<std::size_t>(idx)];
    const Walk& w = walk(path, side);
    p.active = true;
    p.path = p.prevPath = path;
    p.wall = false;
    p.side = p.prevSide = side;
    const int n = static_cast<int>(w.points.size());
    const float dist = m_rng.frand() * (w.cum.back() - w.cum.front());
    p.lateral = ((w.outer - w.inner) * 0.5f - 0.5f) * std::sin(m_rng.frand() * 6.2831f);
    p.idx = sidewalkIndex(path, side, dist);
    p.dir = p.prevDir = m_rng.frand() < 0.5f ? 1 : -1;
    p.heading = headingAt(p, dist, p.dir);
    const float seg = w.cum[static_cast<std::size_t>(p.idx)] - w.cum[static_cast<std::size_t>(p.idx - 1)];
    const float t = seg > 0.0f ? (dist - w.cum[static_cast<std::size_t>(p.idx - 1)]) / seg : 0.0f;
    p.position = lerp(w.points[static_cast<std::size_t>(p.idx - 1)], w.points[static_cast<std::size_t>(std::min(p.idx, n - 1))], t);
    calcCurve(p, p.idx - 1, p.idx, p.lateral);
    const Vec3 xz = solvePosition(p, (dist - w.cum[static_cast<std::size_t>(p.idx - 1)]) * p.invLen);
    p.position.x = xz.x;
    p.position.z = xz.z;
    Vec3 hit;
    if (m_probe && m_probe(p.position + Vec3{0, 5, 0}, p.position - Vec3{0, 5, 0}, hit))
        p.position.y = hit.y;
    else if (w.sidewalk >= 0)
        p.position.y = m_net.sidewalks()[static_cast<std::size_t>(w.sidewalk)].centre.pointAt(dist).y;
    p.reaction = p.cross = 0;
    p.lastReaction = p.lastCross = -1;
    p.reversingAtDive = false;
    p.sideDist0 = 0.0f;
    m_onPath[static_cast<std::size_t>(path)].push_back(idx);
    startSeq(p, seqs(p).walk);
    p.frame = static_cast<int>(m_rng.frand() * static_cast<float>(frameCount(p, seqs(p).walk)));
    solveTargetPoint(p, dist + static_cast<float>(p.dir) * kPedLookAhead);
}

void Pedestrians::clearPath(int path) {
    for (int idx : m_onPath[static_cast<std::size_t>(path)]) {
        m_peds[static_cast<std::size_t>(idx)].active = false;
        m_pool.push_back(idx);
    }
    m_onPath[static_cast<std::size_t>(path)].clear();
    m_pathActive[static_cast<std::size_t>(path)] = 0;
}

void Pedestrians::adjust(int oldRoom, int newRoom) {
    const city::AiMap* map = m_net.source();
    if (!map)
        return;
    static const std::vector<std::uint16_t> kNone;
    auto list = [&](int room) -> const std::vector<std::uint16_t>& {
        if (room < 0 || static_cast<std::size_t>(room) >= map->roomPathsIn.size())
            return kNone;
        return map->roomPathsIn[static_cast<std::size_t>(room)];
    };
    std::vector<int> added;
    if (m_populateAll) {
        for (std::size_t p = 0; p < m_net.paths().size(); ++p)
            added.push_back(static_cast<int>(p));
    } else {
        const auto& from = list(oldRoom);
        const auto& to = list(newRoom);
        for (auto p : from)
            if (std::ranges::find(to, p) == to.end() && p < m_net.paths().size() && m_pathActive[p])
                clearPath(p);
        for (auto p : to)
            if (std::ranges::find(from, p) == from.end() && p < m_net.paths().size())
                added.push_back(p);
    }
    std::vector<int> fresh;
    for (int p : added) {
        if (m_pathActive[static_cast<std::size_t>(p)])
            continue;
        m_pathActive[static_cast<std::size_t>(p)] = 1;
        fresh.push_back(p);
    }
    // Round robin over the new roads, one pedestrian per open sidewalk side
    // (side -1 first) per lap, while the pool lasts; a single new road gets
    // one lap.
    int placed;
    do {
        placed = 0;
        for (int p : fresh) {
            for (int side : {-1, 1}) {
                if (m_pool.empty() || !walk(p, side).open)
                    continue;
                const int idx = m_pool.back();
                m_pool.pop_back();
                reset(idx, p, side);
                ++placed;
            }
        }
    } while (!m_pool.empty() && placed > 0 && fresh.size() > 1);
}

void Pedestrians::populateAll() {
    m_populateAll = true;
}

// --- Reactions -------------------------------------------------------------------

// aiPedestrian::DetectPlayerForwardCollision: ahead of the car (behind it
// in reverse), between a quarter of its length and 20 m, within its half
// width + 2 m.
bool Pedestrians::forwardCollision(const Ped& p, const PlayerCar& c, float& along) const {
    along = 9999.0f;
    if (!(c.speed() > 0.0f))
        return false;
    const Vec3 rel = p.position - c.transform.m3;
    const Vec3 fwd = c.reversing ? c.transform.m2 : -c.transform.m2;
    const Vec3 lat = c.reversing ? -c.transform.m0 : c.transform.m0;
    const float a = fwd.dot(rel);
    if (c.length * 0.25f < a && a < 20.0f && std::abs(lat.dot(rel)) < c.width * 0.5f + 2.0f) {
        along = a;
        return true;
    }
    return false;
}

// aiPedestrian::DetectPlayerAnticipate: the same up to 35 m, within the half
// width + 4 m.
bool Pedestrians::anticipateCollision(const Ped& p, const PlayerCar& c, float& along) const {
    along = 9999.0f;
    if (!(c.speed() > 0.0f))
        return false;
    const Vec3 rel = p.position - c.transform.m3;
    const Vec3 fwd = c.reversing ? c.transform.m2 : -c.transform.m2;
    const float a = fwd.dot(rel);
    if (c.length * 0.25f < a && a < 35.0f && std::abs((-c.transform.m0).dot(rel)) < c.width * 0.5f + 4.0f) {
        along = a;
        return true;
    }
    return false;
}

// aiPedestrian::DetectPlayerCollision: the car within 6 m ahead of the
// pedestrian, within its radius either side.
bool Pedestrians::playerCollision(const Ped& p, const PlayerCar& c, float& ahead) const {
    if (!c.valid)
        return false;
    const Mat34 m = frameOf(p.heading, p.position);
    const Vec3 d = c.transform.m3 - p.position;
    ahead = dotXZ(d, -m.m2);
    const float side = dotXZ(d, m.m0);
    const float r = c.radius;
    return -r < ahead && ahead < 6.0f && -r < side && side < r;
}

bool Pedestrians::wallProbe(Ped& p) {
    p.wall = false;
    if (!m_probe)
        return false;
    const Vec3 x = axisX(p.path, std::clamp(p.idx, 0, sections(p.path) - 1));
    const float s = p.side == 1 ? 1.0f : -1.0f;
    const Vec3 base = p.position + Vec3{0, 1, 0};
    Vec3 hit;
    if (m_probe(base + x * (2.0f * s), base - x * (10.0f * s), hit)) {
        p.wall = true;
        p.wallHit = hit;
    }
    return p.wall;
}

// Backed against the wall it ran to: 0.2 m out from it, facing the road.
void Pedestrians::backupAt(Ped& p) {
    const Vec3 x = axisX(p.path, std::clamp(p.idx, 0, sections(p.path) - 1));
    const float s = p.side == 1 ? 1.0f : -1.0f;
    p.position.x = p.wallHit.x + x.x * 0.2f * s;
    p.position.z = p.wallHit.z + x.z * 0.2f * s;
    p.heading = headingOf(x * s);
    startSeq(p, seqs(p).backup);
}

// aiPedestrian::AvoidObstacle: step round an obstacle on the walkway, or turn
// back when it blocks the walkway's whole width.
void Pedestrians::avoidObstacle(Ped& p, const Vec3& obstacle, float radius) {
    const int n = sections(p.path);
    int i = p.idx == n ? n - 1 : p.idx;
    Vec3 d = p.dir == 1 ? -axisX(p.path, i) : axisX(p.path, i);
    const float pedLat = dotXZ(p.position - sv(p.path, p.side, i), d);
    const float obsLat = dotXZ(obstacle - sv(p.path, p.side, i), d);
    float r = radius;
    if (obsLat + radius > 1.5f && obsLat - radius < -1.5f) {
        if (0 < p.idx && p.idx < n) {
            p.dir = -p.dir;
            p.heading = headingAt(p, roadDistance(p), p.dir);
        } else {
            const int path = p.path, side = p.side, dir = p.dir;
            p.path = p.prevPath;
            p.dir = -p.prevDir;
            p.side = p.prevSide;
            p.prevPath = path;
            p.prevSide = side;
            p.prevDir = dir;
            p.heading += 3.14f;
            p.idx = p.dir == 1 ? 0 : sections(p.path);
            i = p.idx == sections(p.path) ? sections(p.path) - 1 : p.idx;
            d = p.dir == 1 ? -axisX(p.path, i) : axisX(p.path, i);
            roadDistance(p);
            calcCurve(p, p.idx - 1, p.idx, p.lateral);
        }
    } else if (pedLat > obsLat) {
        r = obsLat + radius <= 1.5f ? radius : -radius;
    } else {
        r = obsLat - radius >= -1.5f ? -radius : radius;
    }
    steer(p, obstacle + d * r, true);
}

void Pedestrians::wander(Ped& p, const PlayerCar& c) {
    const Seqs& s = seqs(p);
    if (p.reaction != p.lastReaction || p.cross != p.lastCross) {
        if (p.seq == s.antic || p.seq == s.walkAntic) {
            queueSeq(p, s.anticWalk);
        } else if (p.seq == s.run) {
            queueSeq(p, s.runWalk);
        } else if (p.seq == s.backup) {
            p.scream = true; // PlayAvoidanceReaction
            queueSeq(p, s.backupWalk);
        }
        p.wall = false;
        p.lastReaction = p.reaction;
        p.lastCross = p.cross;
    }
    const float dist = roadDistance(p);
    float ahead = 9999.0f;
    bool player = false;
    if (c.valid && distXZ2(c.transform.m3, p.position) < 36.0f)
        player = playerCollision(p, c, ahead);
    if (player && ahead < 9999.0f) {
        // aiPedestrian::AvoidPlayer: round the car at its radius + 1 m.
        avoidObstacle(p, c.transform.m3, c.radius + 1.0f);
        if (p.seq == s.stand)
            queueSeq(p, s.standWalk);
        calcCurve(p, p.idx - 1, p.idx, p.lateral);
        return;
    }
    if (p.seq == s.stand)
        queueSeq(p, s.standWalk);
    solveTargetPoint(p, dist + static_cast<float>(p.dir) * kPedLookAhead);
    steer(p, p.target);
    solveRoadSegment(p, dist);
}

void Pedestrians::anticipate(Ped& p, const PlayerCar& c) {
    const Seqs& s = seqs(p);
    const int n = sections(p.path);
    const bool entering = p.reaction != p.lastReaction || p.cross != p.lastCross;
    p.lastReaction = p.reaction;
    p.lastCross = p.cross;
    auto queueBrace = [&] {
        if (p.seq == s.walk || p.seq == s.standWalk)
            queueSeq(p, s.walkAntic);
        else if (p.seq == s.stand || p.seq == s.walkStand)
            queueSeq(p, s.standAntic);
    };
    auto faceCar = [&] { p.heading = std::atan2(c.transform.m3.x - p.position.x, c.transform.m3.z - p.position.z); };
    if (p.idx == 0 || p.idx == n) {
        if (entering)
            queueBrace();
        faceCar();
        return;
    }
    wallProbe(p);
    if (entering) {
        const Vec3 x = axisX(p.path, p.idx);
        const float sgn = p.side == 1 ? 1.0f : -1.0f;
        if (p.wall) {
            if (std::sqrt(distXZ2(p.position, p.wallHit)) >= 1.25f) {
                p.heading = headingOf(x * -sgn); // to the buildings
                startSeq(p, s.run);
            } else {
                backupAt(p);
            }
        } else if (p.seq == s.walk) {
            if (m_rng.frand() >= 0.5f) {
                // Run along the road the way the car is going.
                startSeq(p, s.run);
                const auto& centre = m_net.source()->paths[static_cast<std::size_t>(p.path)].center;
                const Vec3 travel = c.reversing ? c.transform.m2 : -c.transform.m2;
                const int i = std::clamp(p.idx, 1, static_cast<int>(centre.size()) - 1);
                const int newDir =
                    (centre[static_cast<std::size_t>(i)] - centre[static_cast<std::size_t>(i - 1)]).dot(travel) > 0.0f ? 1 : -1;
                if (newDir != p.dir) {
                    p.dir = newDir;
                    const float dist = roadDistance(p);
                    p.heading = headingAt(p, dist, p.dir);
                    if (p.dir == 1)
                        p.heading += 3.14f; // as coded: it faces back; Wander turns it round
                }
            } else {
                startSeq(p, s.walkAntic);
            }
        } else {
            queueBrace();
        }
    }
    if (p.wall) {
        if (p.seq == s.run && p.position.dist(p.wallHit) < 1.25f)
            backupAt(p);
        return;
    }
    if (p.seq == s.run) {
        wander(p, c);
        return;
    }
    faceCar();
}

void Pedestrians::avoid(Ped& p, const PlayerCar& c, float& latScale) {
    const Seqs& s = seqs(p);
    const int n = sections(p.path);
    const bool entering = p.reaction != p.lastReaction || p.cross != p.lastCross;
    p.lastReaction = p.reaction;
    p.lastCross = p.cross;
    const bool onSegment = p.idx != 0 && p.idx != n;
    if (onSegment)
        wallProbe(p);
    else
        p.wall = false;
    auto axes = [&](Vec3& a, Vec3& b) {
        a = p.reversingAtDive ? -c.transform.m0 : c.transform.m0;
        b = p.reversingAtDive ? -c.transform.m2 : c.transform.m2;
    };
    if (entering) {
        if (!p.wall) {
            // Dive, facing against the car's way, to the side it steers away
            // from or, going straight, away from its centre line.
            p.reversingAtDive = c.reversing;
            Vec3 a, b;
            axes(a, b);
            p.heading = std::atan2(b.x, b.z);
            p.sideDist0 = a.dot(p.position - c.transform.m3);
            bool right;
            if (c.steering > 0.85f)
                right = true;
            else if (c.steering < -0.85f)
                right = false;
            else
                right = !(p.sideDist0 > 0.0f);
            const bool walking = p.seq == s.walk;
            startSeq(p, right ? (walking ? s.walkRDive : s.anticRDive) : (walking ? s.walkLDive : s.anticLDive));
            p.scream = true;
        } else {
            const Vec3 x = axisX(p.path, p.idx);
            if (p.position.dist(p.wallHit) >= 1.25f) {
                p.heading = headingOf(-x); // as coded, whatever the side
                p.scream = true;
                startSeq(p, s.run);
            } else {
                p.heading = headingOf(x);
                p.position.x = p.wallHit.x + x.x * 0.2f;
                p.position.z = p.wallHit.z + x.z * 0.2f;
                startSeq(p, s.backup);
            }
        }
    }
    if (onSegment) {
        if (p.seq == s.run) {
            if (p.wall && p.position.dist(p.wallHit) < 1.25f)
                backupAt(p);
            return;
        }
        if (p.seq == s.backup)
            return;
    }
    // Keep up with the dive: faster sideways when behind schedule.
    Vec3 a, b;
    axes(a, b);
    const float now = a.dot(p.position - c.transform.m3);
    const float v = latSpeed(p, p.seq) * kFrameSeconds;
    const float f = static_cast<float>(p.frame);
    if (p.seq == s.lDiveGround || p.seq == s.rDiveGround) {
        if (std::abs(now) < std::abs(f * v + p.sideDist0 + 2.54f))
            latScale = 5.0f;
    } else if (f > 12.0f && std::abs(now) < std::abs(f * v + p.sideDist0)) {
        latScale = 3.0f;
    }
    p.heading = std::atan2(b.x, b.z);
}

// --- Crossing the street ------------------------------------------------------

// A point on a curb at the end touching the intersection, 2.5 m into it.
Vec3 Pedestrians::curbPoint(int path, int side, bool atEnd) const {
    const auto& curb = walk(path, side).curb;
    if (curb.empty())
        return {};
    if (atEnd)
        return curb.back() - axisZ(path, static_cast<int>(curb.size()) - 1) * 2.5f;
    return curb.front() + axisZ(path, 0) * 2.5f;
}

void Pedestrians::crossTargets(const Ped& p, Vec3& nearSide, Vec3& farSide) const {
    int road, side;
    bool atEnd;
    if (p.crossChoice == 2) {
        road = p.prevPath;
        side = p.prevSide;
        atEnd = p.prevDir == 1;
    } else {
        road = p.path;
        const bool nodeAtEnd = m_net.paths()[static_cast<std::size_t>(road)].intersection[0] == p.crossNode;
        if (p.prevDir == p.prevSide) {
            side = nodeAtEnd ? -1 : 1;
            atEnd = nodeAtEnd;
        } else {
            side = !nodeAtEnd ? -1 : 1;
            atEnd = nodeAtEnd;
        }
    }
    nearSide = curbPoint(road, side, atEnd);
    farSide = curbPoint(road, -side, atEnd);
}

// aiPedestrian::Accident: a car out of normal driving in the intersection
// being crossed or near the ends of this road.
bool Pedestrians::accident(const Ped& p) const {
    return m_accident && m_accident(p.crossNode, p.path);
}

void Pedestrians::abortCrossing(Ped& p) {
    p.dir = -p.prevDir;
    p.heading += 3.14f;
    p.side = p.prevSide;
    p.cross = 0;
    const int id = static_cast<int>(&p - m_peds.data());
    std::erase(m_onPath[static_cast<std::size_t>(p.path)], id);
    p.path = p.prevPath;
    m_onPath[static_cast<std::size_t>(p.path)].push_back(id);
    p.idx = p.dir == 1 ? 0 : sections(p.path);
    queueSeq(p, seqs(p).walk);
}

void Pedestrians::preCross(Ped& p, const PlayerCar& c) {
    Vec3 nearSide, farSide;
    crossTargets(p, nearSide, farSide);
    if (p.cross != p.lastCross || p.reaction != p.lastReaction) {
        p.lastCross = p.cross;
        p.lastReaction = p.reaction;
        p.target = nearSide;
        queueSeq(p, seqs(p).walk);
    }
    steer(p, p.target);
    float ahead;
    if (c.speed() < 1.0f && playerCollision(p, c, ahead))
        abortCrossing(p);
    else if (distXZ2(p.position, p.target) < 1.0f)
        p.cross = 2;
}

void Pedestrians::waitCross(Ped& p, const PlayerCar& c) {
    Vec3 nearSide, farSide;
    crossTargets(p, nearSide, farSide);
    if (p.cross != p.lastCross || p.reaction != p.lastReaction) {
        p.lastCross = p.cross;
        p.lastReaction = p.reaction;
        p.target = farSide;
        queueSeq(p, m_rng.frand() < 0.5f ? seqs(p).stand : seqs(p).stand2);
    }
    steer(p, p.target);
    float ahead;
    if (accident(p) || (c.speed() < 1.0f && playerCollision(p, c, ahead))) {
        abortCrossing(p);
        return;
    }
    if (distXZ2(p.position, p.target) < 1.0f) {
        p.cross = 1;
        return;
    }
    // Across once the lights' pedestrian phase shows WALK.
    if (m_lights && m_lights->walkPhaseAt(p.crossNode) && m_lights->firstLightAt(p.crossNode) == LightState::Walk)
        p.cross = 3;
}

void Pedestrians::crossStreet(Ped& p, const PlayerCar& c) {
    if (p.cross != p.lastCross || p.reaction != p.lastReaction) {
        p.lastCross = p.cross;
        p.lastReaction = p.reaction;
        queueSeq(p, seqs(p).walk);
    }
    steer(p, p.target);
    if (m_lights && m_lights->firstLightAt(p.crossNode) == LightState::WalkEnd)
        queueSeq(p, seqs(p).run);
    float ahead;
    if (c.speed() < 1.0f && playerCollision(p, c, ahead))
        abortCrossing(p);
    else if (distXZ2(p.position, p.target) < 1.5f)
        p.cross = 0;
}

// --- Update -------------------------------------------------------------------

void Pedestrians::update(int idx, float dt, const PlayerCar& c) {
    Ped& p = m_peds[static_cast<std::size_t>(idx)];
    const Seqs& s = seqs(p);
    p.scream = false;
    const float d2 = c.valid ? distXZ2(p.position, c.transform.m3) : 1e9f;
    const float speed = c.valid ? c.speed() : 0.0f;
    float latScale = 1.0f;
    if (p.seq == s.backup) {
        // Out of the corner once the car has gone by.
        const Vec3 m = normalized3(c.velocity);
        if ((p.position - c.transform.m3).dot(m) < 0.0f && sq(2.0f * c.radius) < d2) {
            p.reaction = 0;
            p.lateral = 1.5f * std::sin(m_rng.frand() * 6.2831f);
            calcCurve(p, p.idx - 1, p.idx, p.lateral);
        }
    } else if ((p.seq == s.run && p.reaction == static_cast<int>(p.wall)) || p.seq == s.anticLDive ||
               p.seq == s.anticRDive || p.seq == s.walkLDive || p.seq == s.walkRDive || p.seq == s.lDiveGround ||
               p.seq == s.rDiveGround || p.seq == s.groundStandL || p.seq == s.groundStandR) {
        // Busy: keep the reaction.
    } else {
        float along;
        p.reaction = 0;
        if (d2 < sq(kPedAwareRadius)) {
            const float inv = speed > 0.001f ? 1.0f / speed : 0.0f;
            if (forwardCollision(p, c, along)) {
                const float ttc = (along - 2.0f) * inv;
                if (speed >= 1.0f && ttc < 0.75f)
                    p.reaction = 2;
                else if (speed >= 1.0f && ttc < 2.3f)
                    p.reaction = 1;
            } else if (anticipateCollision(p, c, along)) {
                const float ttc = (along - 2.0f) * inv;
                if (speed >= 1.0f && ttc < 2.3f)
                    p.reaction = 1;
            }
        }
    }
    switch (p.reaction) {
    case 1:
        anticipate(p, c);
        break;
    case 2:
        avoid(p, c, latScale);
        break;
    default:
        switch (p.cross) {
        case 1:
            preCross(p, c);
            break;
        case 2:
            waitCross(p, c);
            break;
        case 3:
            crossStreet(p, c);
            break;
        default:
            wander(p, c);
            break;
        }
        break;
    }
    // Move by the current sequence's speeds, free of the path.
    const Mat34 m = frameOf(p.heading, p.position);
    p.position -= m.m2 * (dt * fwdSpeed(p, p.seq)) + m.m0 * (dt * latScale * latSpeed(p, p.seq));
    // Ground: a probe 2 m up and down, taken when within 0.5 m (MM2 keeps
    // flat roads' first curb height beyond 50 m of the player).
    const PathInfo& info = m_net.paths()[static_cast<std::size_t>(p.path)];
    if ((info.flags & 0x8) && d2 > 2500.0f && !walk(p.path, 1).curb.empty()) {
        p.position.y = walk(p.path, 1).curb.front().y;
    } else if (m_probe) {
        Vec3 hit;
        if (m_probe(p.position + Vec3{0, 2, 0}, p.position - Vec3{0, 2, 0}, hit) &&
            std::abs(hit.y - p.position.y) < 0.5f)
            p.position.y = hit.y;
    } else if (const int w = walk(p.path, p.side).sidewalk; w >= 0) {
        const auto& line = m_net.sidewalks()[static_cast<std::size_t>(w)].centre;
        p.position.y = line.pointAt(line.project(p.position)).y;
    }
    p.target.y = p.position.y;
    // Animation: one frame per 1/30 s; at the end the queued sequence starts.
    p.frame += static_cast<int>(dt * kPedAnimFps + 0.5f);
    if (p.frame >= frameCount(p, p.seq))
        startSeq(p, p.queued ? p.queued : p.seq);
}

void Pedestrians::step(float dt, const PlayerCar& player, int room) {
    if (!m_started) {
        m_started = true;
        m_room = room;
        adjust(0, room);
        m_populateAll = false;
    } else if (room != 0 && room != m_room) {
        adjust(m_room, room);
        m_room = room;
    }
    std::vector<int> order;
    for (std::size_t path = 0; path < m_onPath.size(); ++path)
        if (m_pathActive[path])
            for (int idx : m_onPath[path])
                order.push_back(idx);
    for (int idx : order)
        if (m_peds[static_cast<std::size_t>(idx)].active && m_peds[static_cast<std::size_t>(idx)].path >= 0)
            update(idx, dt, player);
    publish();
}

void Pedestrians::publish() {
    m_public.clear();
    for (std::size_t i = 0; i < m_peds.size(); ++i) {
        const Ped& p = m_peds[i];
        if (!p.active || !p.seq)
            continue;
        Pedestrian out;
        out.id = static_cast<int>(i);
        out.type = p.type;
        out.typeName = m_types[static_cast<std::size_t>(p.type)].name;
        out.variant = p.variant;
        out.sidewalk = walk(p.path, p.side).sidewalk;
        out.transform = frameOf(p.heading, p.position);
        out.state = p.seq->name;
        out.animFile = p.seq->animFile;
        out.frame = static_cast<float>(p.frame);
        out.scream = p.scream;
        out.crossing = p.cross != 0;
        m_public.push_back(std::move(out));
    }
}

std::size_t Pedestrians::activeCount() const {
    return static_cast<std::size_t>(std::ranges::count_if(m_peds, [](const Ped& p) { return p.active; }));
}

float Pedestrians::distanceFromSidewalk(int pedId) const {
    if (pedId < 0 || static_cast<std::size_t>(pedId) >= m_peds.size())
        return 0.0f;
    const Ped& p = m_peds[static_cast<std::size_t>(pedId)];
    const int w = p.active ? walk(p.path, p.side).sidewalk : -1;
    if (w < 0)
        return 0.0f;
    float d = 0.0f;
    m_net.sidewalks()[static_cast<std::size_t>(w)].centre.project(p.position, &d);
    return d;
}

} // namespace mm2::ai
