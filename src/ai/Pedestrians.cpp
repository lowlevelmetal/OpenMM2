// Pedestrians after MM2's aiPedestrian (build 3393, MM2Recomp; documentation
// only); see Pedestrians.h and docs/ai.md.
// Also: aiPedestrian::GetRoadToLeft (setNextRoad).
#include "ai/Pedestrians.h"

#include "core/StringUtil.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace mm2::ai {
namespace {

constexpr float kFrameSeconds = 0.03333f; // pedAnimation::Load: sequence duration = frames x 0.03333
constexpr float kHalfTurn = 3.14f;        // aiPedestrian turns round by adding 3.14
constexpr float kFarAway = 1.0e9f;        // aiPedestrian::Update: no player yet
constexpr float kNoHit = 9999.0f;         // the Detect* functions' "nothing ahead"

// v scaled by 1 / sqrt(m2), where the caller sums m2 in MM2's order; zero
// stays zero.
Vec3 scaledUnit(const Vec3& v, float m2) {
    const float k = m2 == 0.0f ? 0.0f : 1.0f / std::sqrt(m2);
    return v * k;
}

// The matrix aiPedestrian builds from its heading h (Reset, end of Update):
// m0 = (-cos h, 0, sin h), m1 = up, m2 = (-sin h, 0, -cos h), so it faces
// (sin h, 0, cos h).
Mat34 frameOf(float h, const Vec3& pos) {
    const float c = std::cos(h), s = std::sin(h);
    Mat34 m;
    m.m0 = {-c, 0.0f, s};
    m.m1 = {0.0f, 1.0f, 0.0f};
    m.m2 = {-s, 0.0f, -c};
    m.m3 = pos;
    return m;
}

// aiPedestrian::ComputeCurve: the Hermite basis applied to (p0, p1, t0, t1),
// summed in MM2's order.
void hermite(float p0, float p1, float m0, float m1, float out[4]) {
    out[0] = ((2.0f * p0 + m1 * 1.0f) + m0 * 1.0f) + p1 * -2.0f;
    out[1] = ((-3.0f * p0 + 3.0f * p1) + -2.0f * m0) + -1.0f * m1;
    out[2] = m0;
    out[3] = p0;
}

// The heading change of aiPedestrian's steering (Wander, PreCrossStreet,
// WaitCrossStreet, CrossStreet): towards the target by at most 0.15 rad.
float turnTowards(float heading, float angle) {
    const float a = -angle;
    if (kPedTurnRate < a)
        return kPedTurnRate + heading;
    if (-kPedTurnRate <= a)
        return a + heading;
    return heading - kPedTurnRate;
}

} // namespace

Pedestrians::Pedestrians(const RoadNetwork& network, std::vector<PedTypeInfo> types,
                         const PedSettings& settings, std::uint64_t seed)
    : m_net(network), m_types(std::move(types)), m_settings(settings), m_seed(seed), m_rng(seed) {
    // The sequences the AI asks for, by name (aiPedestrian::Init). LDIVE and
    // RDIVE are looked up too but no retail table has them.
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
    // Sidewalk geometry per road side (aiPath::SidewalkVertice: the vertex row
    // after the lanes; the curb is the second last polyline). On a road that
    // aiPath::ReverseDirection reverses (drive on the left, two-way), each
    // side's lane rows become the other side's, reversed in point and lane
    // order, the sidewalk row stays, and every row's lengths are recomputed
    // as 3D distances; elsewhere the lengths are the file's.
    const city::AiMap* map = m_net.source();
    m_walks.resize(m_net.paths().size());
    for (std::size_t p = 0; p < m_net.paths().size() && map; ++p) {
        const city::AiPath& src = map->paths[p];
        const bool reversed = m_net.driveOnLeft() && src.left.numLanes != 0;
        for (int s = 0; s < 2; ++s) {
            const city::AiRoadSide& side = s == 0 ? src.left : src.right;
            const city::AiRoadSide& other = s == 0 ? src.right : src.left;
            Walk& w = m_walks[p][static_cast<std::size_t>(s)];
            const std::size_t lanes = side.numLanes;
            if (side.numSidewalks == 0 || lanes >= side.polylines.size() || side.polylines.size() < 3)
                continue;
            // The vertex rows MM2 holds: lanes, then the sidewalk.
            std::vector<std::vector<Vec3>> rows(lanes + 1);
            for (std::size_t r = 0; r <= lanes; ++r)
                rows[r] = side.polylines[r];
            // Inferred: ReverseDirection only behaves on roads whose sides have
            // as many lanes (every retail one); others keep their own rows.
            const bool swap = reversed && other.numLanes == side.numLanes;
            if (swap)
                for (std::size_t r = 0; r < lanes; ++r)
                    rows[r].assign(other.polylines[lanes - 1 - r].rbegin(), other.polylines[lanes - 1 - r].rend());
            w.rowCum.resize(lanes + 1);
            for (std::size_t r = 0; r <= lanes; ++r) {
                auto& cum = w.rowCum[r];
                cum.assign(rows[r].size(), 0.0f);
                // (Recomputed too when the file has no lengths for the row,
                // e.g. a synthetic map; the retail lengths match to 1e-4 m.)
                if (reversed || r >= side.laneLengths.size() || side.laneLengths[r].size() + 1 < cum.size()) {
                    for (std::size_t i = 1; i < rows[r].size(); ++i) {
                        const Vec3 d = rows[r][i] - rows[r][i - 1];
                        cum[i] = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z) + cum[i - 1];
                    }
                } else {
                    for (std::size_t i = 1; i < cum.size(); ++i)
                        cum[i] = side.laneLengths[r][i - 1];
                }
            }
            w.points = rows[lanes];
            w.cum = w.rowCum[lanes];
            w.curb = side.polylines[side.polylines.size() - 2];
            const std::size_t k = lanes * 2;
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
    m_pathHead.assign(m_net.paths().size(), -1);
    m_pathActive.assign(m_net.paths().size(), 0);
    m_activeNext.assign(m_net.paths().size(), -1);
    // aiMap::Init: trunc(pool x density) pedestrians, type drawn per
    // pedestrian, then aiPedestrian::Init draws its clothing variant.
    const int count = m_types.empty() ? 0
                                      : std::max(0, static_cast<int>(static_cast<float>(m_settings.pool) *
                                                                     m_settings.density));
    m_peds.resize(static_cast<std::size_t>(count));
    std::vector<int> allowed;
    for (const auto& name : m_settings.names)
        for (std::size_t t = 0; t < m_types.size(); ++t)
            if (str::iequals(m_types[t].name, name))
                allowed.push_back(static_cast<int>(t));
    // OpenMM2: no listed type loads, use every type (MM2 would leave the
    // pedestrians uninitialised).
    if (allowed.empty())
        for (std::size_t t = 0; t < m_types.size(); ++t)
            allowed.push_back(static_cast<int>(t));
    for (auto& p : m_peds) {
        p.type = allowed[static_cast<std::size_t>(m_rng.frand() * static_cast<float>(allowed.size()))];
        const int variants = m_types[static_cast<std::size_t>(p.type)].variants;
        p.variant = static_cast<int>(m_rng.frand() * static_cast<float>(variants - 1));
    }
    // aiMap::Reset: every pedestrian into the pool in index order, so the
    // last one is taken first.
    for (int i = 0; i < count; ++i)
        poolAdd(i);
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

// aiPath::SidewalkSubSectionLength: negative indices count as 0.
float Pedestrians::subLength(int path, int side, int a, int b) const {
    return cumAt(path, side, std::max(b, 0)) - cumAt(path, side, std::max(a, 0));
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

// aiPath::Index on the sidewalk row: the first vertex whose cumulative length
// reaches `dist` (within 1e-5), `dist` clamped to the row.
int Pedestrians::sidewalkIndex(int path, int side, float dist) const {
    const auto& cum = walk(path, side).cum;
    const int n = static_cast<int>(cum.size());
    if (n < 2)
        return 1;
    const float total = cum[static_cast<std::size_t>(n - 1)] - cum[0];
    if (0.0f <= dist) {
        if (total < dist)
            dist = total;
    } else {
        dist = 0.0f;
    }
    for (int i = 1; i < n; ++i)
        if (dist <= cum[static_cast<std::size_t>(i)] + 1e-05f)
            return i;
    return n - 1;
}

// aiPath::GetHeading(dist, row, dir): the segment is found on the cumulative
// lengths of vertex row `row` of side `dir` (not necessarily the sidewalk:
// Anticipate and AvoidObstacle pass row 0, the first lane), and the heading
// is that of side `dir`'s sidewalk segment there, in the direction `dir`.
float Pedestrians::getHeading(int path, float dist, int row, int dir) const {
    const Walk& w = walk(path, dir);
    if (w.rowCum.empty() || w.points.size() < 2)
        return 0.0f;
    // MM2 reads past the side's rows when `row` is beyond them (a one-way
    // road's empty side); OpenMM2 uses the sidewalk row then.
    const auto& cum = w.rowCum[static_cast<std::size_t>(std::min<std::size_t>(row, w.rowCum.size() - 1))];
    const int n = static_cast<int>(std::min(cum.size(), w.points.size()));
    if (n < 2)
        return 0.0f;
    const float total = cum[static_cast<std::size_t>(n - 1)] - cum[0];
    if (0.0f <= dist) {
        if (total < dist)
            dist = total;
    } else {
        dist = 0.0f;
    }
    for (int i = 1; i < n; ++i) {
        if (dist <= cum[static_cast<std::size_t>(i)]) {
            const Vec3& a = w.points[static_cast<std::size_t>(i - 1)];
            const Vec3& b = w.points[static_cast<std::size_t>(i)];
            return dir == 1 ? std::atan2(b.x - a.x, b.z - a.z) : std::atan2(a.x - b.x, a.z - b.z);
        }
    }
    return 0.0f;
}

// The intersection a crossing pedestrian is at: where its previous road
// ended (MM2 recomputes it from the previous road and direction each time).
int Pedestrians::crossedNode(const Ped& p) const {
    if (p.prevPath < 0)
        return -1;
    return m_net.paths()[static_cast<std::size_t>(p.prevPath)].intersection[p.prevDir == 1 ? 0 : 1];
}

// --- Animation (pedAnimationInstance) ---------------------------------------

// pedAnimation::Load: a sequence lasts last - first + 1 frames, at most as many
// as its .anim has; it plays from frame 0 of the .anim.
int Pedestrians::frameCount(const Ped& p, State s) const {
    if (!s)
        return 1;
    int frames = std::max(1, s->lastFrame - s->firstFrame + 1);
    const auto& counts = m_types[static_cast<std::size_t>(p.type)].animFrames;
    if (auto it = counts.find(str::lower(s->animFile)); it != counts.end() && it->second > 0)
        frames = std::min(frames, it->second);
    return frames;
}

// pedAnimation::Load: the CSV distances over the unclamped duration.
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

// pedAnimationInstance::Start: at once, from frame 0; the sequence's own next
// is queued.
void Pedestrians::startSeq(Ped& p, State s) {
    if (!s)
        return;
    p.seq = s;
    p.frame = 0;
    p.queued = m_types[static_cast<std::size_t>(p.type)].table.find(s->next);
}

// --- Lists (aiPath, aiMap) ------------------------------------------------------

// aiPath::AddPedestrian: at the head of the road's list.
void Pedestrians::pathAdd(int path, int idx) {
    m_peds[static_cast<std::size_t>(idx)].next = m_pathHead[static_cast<std::size_t>(path)];
    m_pathHead[static_cast<std::size_t>(path)] = idx;
}

// aiPath::RemovePedestrian.
void Pedestrians::pathRemove(int path, int idx) {
    int* link = &m_pathHead[static_cast<std::size_t>(path)];
    while (*link >= 0 && *link != idx)
        link = &m_peds[static_cast<std::size_t>(*link)].next;
    if (*link == idx)
        *link = m_peds[static_cast<std::size_t>(idx)].next;
}

// aiMap::AddPedestrian: at the head of the pool.
void Pedestrians::poolAdd(int idx) {
    Ped& p = m_peds[static_cast<std::size_t>(idx)];
    p.active = false;
    p.next = m_poolHead;
    m_poolHead = idx;
}

// aiMap::RemovePedestrian.
void Pedestrians::poolRemove(int idx) {
    int* link = &m_poolHead;
    while (*link >= 0 && *link != idx)
        link = &m_peds[static_cast<std::size_t>(*link)].next;
    if (*link == idx)
        *link = m_peds[static_cast<std::size_t>(idx)].next;
}

// Off its road and onto `path` (RemovePedestrian, then AddPedestrian).
void Pedestrians::moveToPath(Ped& p, int path) {
    const int id = static_cast<int>(&p - m_peds.data());
    pathRemove(p.path, id);
    p.path = path;
    pathAdd(path, id);
}

// --- Curves (aiPedestrian::CalcCurve, SolvePosition, SolveTargetPoint) ----

void Pedestrians::calcCurve(Ped& p, int a, int b, float lateral) {
    const int n = sections(p.path);
    lateral = clampf(lateral, -kPedMaxLateral, kPedMaxLateral);
    auto P = [&](int k) { return axisX(p.path, k) * -lateral + sv(p.path, p.side, k); };
    Vec3 p0, p1, t0, t1;
    if (b == n - 1) {
        const float len = subLength(p.path, p.side, a, b);
        p.invLen = 1.0f / len;
        p0 = P(a);
        p1 = P(n - 1);
        t0 = axisW(p.path, a) * len;          // aiPath::SubSectionDir
        t1 = (-axisZ(p.path, n - 1)) * len;   // aiPath::IntersectionEntryVector
    } else if (b == 1) {
        const float len = subLength(p.path, p.side, a, 1);
        p.invLen = 1.0f / len;
        p0 = P(0);
        p1 = P(1);
        t0 = (-axisZ(p.path, 0)) * len; // aiPath::IntersectionExitVector
        t1 = axisW(p.path, 1) * len;
    } else if (b != 0 && b != n) {
        const float len = subLength(p.path, p.side, a, b);
        p.invLen = 1.0f / len;
        p0 = P(a);
        p1 = P(b);
        t0 = axisW(p.path, a) * len;
        t1 = axisW(p.path, b) * len;
    } else {
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
        float d = std::sqrt(t0.x * t0.x + t0.y * t0.y + t0.z * t0.z);
        if (p.prevPath == p.path)
            d = 1.0f;
        // OpenMM2 guard: MM2 divides by a zero length too.
        p.invLen = d != 0.0f ? 1.0f / d : 1.0f;
    }
    hermite(p0.x, p1.x, t0.x, t1.x, p.curve[0]);
    hermite(p0.z, p1.z, t0.z, t1.z, p.curve[1]);
}

Vec3 Pedestrians::solvePosition(const Ped& p, float t) const {
    auto f = [&](const float k[4]) { return ((t * k[0] + k[1]) * t + k[2]) * t + k[3]; };
    return {f(p.curve[0]), p.position.y, f(p.curve[1])};
}

void Pedestrians::solveTargetPoint(Ped& p, float d) {
    const int n = sections(p.path);
    float t;
    if (p.idx == 0)
        t = d * p.invLen;
    else if (p.idx != n)
        t = (d - subLength(p.path, p.side, 0, p.idx - 1)) * p.invLen;
    else
        t = (d - subLength(p.path, p.side, 0, n - 1)) * p.invLen;
    p.target = solvePosition(p, t);
    p.target.y = p.position.y;
}

// aiPedestrian::RoadDistance: the distance along the sidewalk, moving on to
// the next vertex (and its curve, at the pedestrian's offset there) once the
// pedestrian has passed it.
float Pedestrians::roadDistance(Ped& p) {
    const int n = sections(p.path);
    const int np = sections(p.prevPath);
    auto lateralAt = [&](int k) {
        const Vec3 d = sv(p.path, p.side, k) - p.position;
        const Vec3 x = axisX(p.path, k);
        return clampf(d.x * x.x + d.y * x.y + d.z * x.z, -kPedMaxLateral, kPedMaxLateral);
    };
    if (p.dir == 1) {
        const Vec3 v = sv(p.path, p.side, p.idx);
        Vec3 w, u;
        if (p.idx == 0) {
            w = p.prevDir == 1 ? sv(p.prevPath, p.prevSide, np - 1) : sv(p.prevPath, p.prevSide, 0);
            const Vec3 e = w - v;
            u = scaledUnit(e, e.x * e.x + e.z * e.z + e.y * e.y);
        } else {
            u = axisZ(p.path, p.idx);
        }
        const float s = (p.position.x - v.x) * u.x + (p.position.z - v.z) * u.z;
        if (s <= 0.0f) {
            ++p.idx;
            if (p.idx < n) {
                p.lateral = lateralAt(p.idx - 1);
                calcCurve(p, p.idx - 1, p.idx, p.lateral);
                return subLength(p.path, p.side, 0, p.idx - 1) - s;
            }
            p.idx = n - 1;
            p.lateral = lateralAt(p.idx);
            calcCurve(p, p.idx - 1, p.idx, p.lateral);
            return subLength(p.path, p.side, 0, p.idx) - s;
        }
        if (p.idx != 0)
            return subLength(p.path, p.side, 0, p.idx) - s;
        const Vec3 v0 = sv(p.path, p.side, 0);
        return ((w.x - v0.x) * u.x + (w.z - v0.z) * u.z) - s;
    }
    if (p.idx == n) {
        // MM2 picks the other road's point the opposite way round from
        // CalcCurve here.
        const Vec3 v = sv(p.path, p.side, n - 1);
        const Vec3 w = p.prevDir == 1 ? sv(p.prevPath, p.prevSide, 0) : sv(p.prevPath, p.prevSide, np - 1);
        const Vec3 e = w - v;
        const Vec3 u = scaledUnit(e, e.y * e.y + e.x * e.x + e.z * e.z);
        const float s = (p.position.x - v.x) * u.x + (p.position.z - v.z) * u.z;
        if (s < 0.0f) {
            --p.idx;
            calcCurve(p, p.idx - 1, p.idx, p.lateral);
        }
        return subLength(p.path, p.side, 0, p.idx - 1) + s;
    }
    const Vec3 v = sv(p.path, p.side, p.idx - 1);
    const Vec3 z = axisZ(p.path, p.idx);
    const float s = (p.position.x - v.x) * z.x + (p.position.z - v.z) * z.z;
    if (s < 0.0f)
        return subLength(p.path, p.side, 0, p.idx - 1) - s;
    --p.idx;
    if (p.idx > 0) {
        p.lateral = lateralAt(p.idx);
        calcCurve(p, p.idx - 1, p.idx, p.lateral);
        return subLength(p.path, p.side, 0, p.idx) - s;
    }
    p.idx = 0;
    return -s;
}

// aiPedestrian::SetNextRoad: the next road round the intersection on the
// pedestrian's side (GetRoadToRight / GetRoadToLeft), counted from this
// road's place in the list of the end it walks towards.
int Pedestrians::setNextRoad(const Ped& p, int node) const {
    const auto& paths = m_net.intersections()[static_cast<std::size_t>(node)].paths;
    const int count = static_cast<int>(paths.size());
    if (count == 0)
        return p.path;
    const PathInfo& info = m_net.paths()[static_cast<std::size_t>(p.path)];
    int k = info.roadIndex[p.dir == 1 ? 0 : 1];
    if (k < 0)
        k = 0;
    // GetRoadToRight/Left step over the shortcut roads of <city>_sup.bai.
    const bool right = p.dir == 1 ? p.side == 1 : p.side != 1;
    for (int tries = 0; tries < count; ++tries) {
        k = right ? (k + 1 >= count ? k + 1 - count : k + 1) : (k - 1 < 0 ? k - 1 + count : k - 1);
        const int next = paths[static_cast<std::size_t>(k)];
        if (next < 0 || static_cast<std::size_t>(next) >= m_net.paths().size() ||
            !m_net.paths()[static_cast<std::size_t>(next)].shortcut)
            return next;
    }
    return p.path;
}

// aiPedestrian::PickNextRdSeg: at the end of the sidewalk, round the corner,
// or (only at lit intersections with a walk phase) across the next road or
// back across this one; turn round when the way on is closed or unpopulated.
int Pedestrians::pickNextRoad(Ped& p) {
    const PathInfo& info = m_net.paths()[static_cast<std::size_t>(p.path)];
    const int node = info.intersection[p.dir == 1 ? 0 : 1];
    if (node < 0) {
        // OpenMM2 guard: every retail road end has an intersection.
        p.dir = -p.prevDir;
        p.side = p.prevSide;
        p.cross = 0;
        p.heading += kHalfTurn;
        return p.path;
    }
    int choice = 0;
    if (m_lights && m_lights->hasLights(node) && m_lights->cycleAt(node) != LightCycle::Rotate)
        choice = m_rng.irand() % 3;
    // aiPedestrian::UpcomingAccident: a car out of normal driving there.
    if (m_accident && m_accident(node, -1, 0))
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
    p.heading += kHalfTurn;
    return p.path;
}

void Pedestrians::solveRoadSegment(Ped& p, float dist) {
    const int n = sections(p.path);
    const bool off =
        p.dir == 1 ? (p.idx >= 1 && dist > subLength(p.path, p.side, 0, n - 1)) : (p.idx < n && dist < 0.0f);
    if (!off)
        return;
    p.prevSide = p.side;
    p.prevDir = p.dir;
    const int next = pickNextRoad(p);
    const int from = p.path;
    moveToPath(p, next);
    p.prevPath = from;
    p.idx = p.dir == 1 ? 0 : sections(next);
    p.dist = roadDistance(p);
    calcCurve(p, p.idx - 1, p.idx, p.lateral);
}

// Wander, PreCrossStreet, WaitCrossStreet and CrossStreet turn towards their
// target by at most 0.15 rad, measured in the matrix of the last update.
void Pedestrians::steer(Ped& p, const Vec3& target) {
    const Mat34 m = frameOf(p.frameHeading, p.position);
    const float dz = target.z - p.position.z, dx = target.x - p.position.x;
    const float angle = std::atan2(dz * m.m0.z + dx * m.m0.x, -(dz * m.m2.z + dx * m.m2.x));
    p.heading = turnTowards(p.heading, angle);
}

// --- Population (aiMap::AdjustPedestrians, aiPedestrian::Reset) ------------

void Pedestrians::reset(int idx, int path, int side) {
    Ped& p = m_peds[static_cast<std::size_t>(idx)];
    const Walk& w = walk(path, side);
    p.placed = true; // AudCreatureContainer::Reset
    p.active = true;
    p.lost = false;
    p.path = p.prevPath = path;
    p.wall = false;
    p.side = p.prevSide = side;
    const int lanes = static_cast<int>(w.rowCum.size()) - 1;
    const float dist = m_rng.frand() * subLength(path, side, 0, sections(path) - 1);
    p.dist = dist;
    p.lateral = ((w.outer - w.inner) * 0.5f - 0.5f) * std::sin(m_rng.frand() * 6.2831f);
    p.idx = sidewalkIndex(path, side, dist);
    p.dir = p.prevDir = m_rng.frand() < 0.5f ? 1 : -1;
    // (MM2 turns the old heading round here for direction +1; it is
    // overwritten at once.)
    p.heading = getHeading(path, dist, lanes, p.dir);
    const float t = (dist - subLength(path, side, 0, p.idx - 1)) / subLength(path, side, p.idx - 1, p.idx);
    const Vec3 a = sv(path, side, p.idx - 1), b = sv(path, side, p.idx);
    p.position = {(b.x - a.x) * t + a.x, (b.y - a.y) * t + a.y, (b.z - a.z) * t + a.z};
    calcCurve(p, p.idx - 1, p.idx, p.lateral);
    const float before = subLength(path, side, 0, p.idx - 1);
    p.invLen = 1.0f / subLength(path, side, p.idx - 1, p.idx);
    const Vec3 xz = solvePosition(p, (dist - before) * p.invLen);
    p.position.x = xz.x;
    p.position.z = xz.z;
    // The ground 5 m up and down, in the road's first room.
    Vec3 hit;
    if (m_probe && m_probe(p.position + Vec3{0, 5, 0}, p.position - Vec3{0, 5, 0}, hit))
        p.position.y = hit.y;
    else if (!m_probe && w.sidewalk >= 0)
        p.position.y = m_net.sidewalks()[static_cast<std::size_t>(w.sidewalk)].centre.pointAt(dist).y;
    p.reaction = p.cross = 0;
    p.lastReaction = p.lastCross = -1;
    pathAdd(path, idx);
    p.frameHeading = p.heading;
    startSeq(p, seqs(p).walk);
    p.frame = static_cast<int>(m_rng.frand() * static_cast<float>(frameCount(p, seqs(p).walk)));
    solveTargetPoint(p, static_cast<float>(p.dir) * kPedLookAhead + dist);
    p.reversingAtDive = false;
    p.sideDist0 = 0.0f;
}

// aiMap::ClearPeds: the road's pedestrians back to the pool, its head first.
void Pedestrians::clearPeds(int path) {
    int idx = m_pathHead[static_cast<std::size_t>(path)];
    while (idx >= 0) {
        const int next = m_peds[static_cast<std::size_t>(idx)].next;
        pathRemove(path, idx);
        poolAdd(idx);
        idx = next;
    }
}

// aiMap::AdjustPedestrians for the player leaving the room with road list
// `from` for the room with `to` (the .bai's second per-room lists).
void Pedestrians::adjust(const std::vector<std::uint16_t>& from, const std::vector<std::uint16_t>& to) {
    const std::size_t pathCount = m_net.paths().size();
    // Roads that drop out return their pedestrians and leave the populated
    // list (aiPath::RemPedPlayer: the only player left).
    for (auto p : from) {
        if (std::ranges::find(to, p) != to.end() || p >= pathCount || !m_pathActive[p])
            continue;
        clearPeds(p);
        int* link = &m_activeHead;
        while (*link >= 0 && *link != p)
            link = &m_activeNext[static_cast<std::size_t>(*link)];
        if (*link == p)
            *link = m_activeNext[p];
        m_activeNext[p] = -1;
        m_pathActive[p] = 0;
    }
    // New roads join the head of the populated list (aiPath::AddPedPlayer).
    std::vector<int> fresh;
    for (auto p : to) {
        if (std::ranges::find(from, p) != from.end() || p >= pathCount || m_pathActive[p])
            continue;
        m_pathActive[p] = 1;
        m_activeNext[p] = m_activeHead;
        m_activeHead = p;
        fresh.push_back(p);
    }
    // Dealt round the new roads from the pool's head, one pedestrian per open
    // side (side -1 first) per road, until the pool runs dry or the turn
    // comes back to the road where one was last placed.
    const int count = static_cast<int>(fresh.size());
    int ped = m_poolHead;
    int k = 0, last = 0;
    if (ped < 0)
        return;
    while (count != 0) {
        const int path = fresh[static_cast<std::size_t>(k)];
        int cur = ped;
        if (walk(path, -1).open) {
            cur = m_peds[static_cast<std::size_t>(ped)].next;
            poolRemove(ped);
            reset(ped, path, -1);
            last = k;
        }
        ped = cur;
        if (ped >= 0 && walk(path, 1).open) {
            const int next = m_peds[static_cast<std::size_t>(ped)].next;
            poolRemove(ped);
            reset(ped, path, 1);
            last = k;
            ped = next;
        }
        if (++k == count)
            k = 0;
        if (last == k || ped < 0)
            return;
    }
}

void Pedestrians::populateAll() {
    m_populateAll = true;
}

void Pedestrians::reset() {
    // aiMap::Reset: ResetRandomSeed first (OpenMM2: this stream's own seed).
    m_rng.seed(static_cast<std::uint32_t>(m_seed));
    // aiPath::Reset: each road's pedestrian list (+0x20), its players' mask
    // and its link in the populated list (+0x34); aiMap +0x180 emptied.
    std::ranges::fill(m_pathHead, -1);
    std::ranges::fill(m_pathActive, 0);
    std::ranges::fill(m_activeNext, -1);
    m_activeHead = -1;
    // aiIntersection::Reset: its prop list (+0x28) emptied.
    for (auto& list : m_nodeObstacles)
        list.clear();
    // aiPedestrian::Reset() (the voice) for each pedestrian, then
    // aiMap::AddPedestrian in index order: the last is taken first.
    m_poolHead = -1;
    for (std::size_t i = 0; i < m_peds.size(); ++i) {
        Ped& p = m_peds[i];
        p.lost = false;
        p.path = p.prevPath = -1;
        poolAdd(static_cast<int>(i));
    }
    m_started = false;
    m_room = 0;
    publish();
}

// --- Props ----------------------------------------------------------------------

void Pedestrians::setObstacles(std::vector<PedObstacle> props, ObstacleStanding standing) {
    m_obstacles = std::move(props);
    m_standing = std::move(standing);
    m_sectionObstacles.clear();
    m_nodeObstacles.clear();
    const city::AiMap* map = m_net.source();
    if (!map)
        return;
    // A room's instance list (lvlRoomInfo +4) holds its movable instances
    // newest first (lvlLevel::MoveToRoom), so the props of a room are walked
    // in the reverse of their placing order.
    std::map<int, std::vector<int>> byRoom;
    for (int i = static_cast<int>(m_obstacles.size()) - 1; i >= 0; --i)
        byRoom[m_obstacles[static_cast<std::size_t>(i)].room].push_back(i);
    auto inRoom = [&](int room) -> const std::vector<int>* {
        const auto it = byRoom.find(room);
        return it != byRoom.end() ? &it->second : nullptr;
    };
    // aiIntersection::AddBangersToObsMap: the props in the intersection's
    // room that nothing breaks loose (a break threshold above 7.5e7), each
    // added to the front of the list.
    m_nodeObstacles.resize(map->intersections.size());
    for (std::size_t n = 0; n < map->intersections.size(); ++n)
        if (const auto* list = inRoom(map->intersections[n].room))
            for (const int i : *list)
                if (7.5e+07f < m_obstacles[static_cast<std::size_t>(i)].impulseLimit2)
                    m_nodeObstacles[n].insert(m_nodeObstacles[n].begin(), i);
    // aiPath::AddBangersToObsMap: for each section, the props in the road's
    // rooms that are not drivable and lie along it (measured from the
    // section's centre point back along its z axis, within the centre line
    // length to the point before), listed for the side of the centre line
    // they are on (the x axis side: -1).
    m_sectionObstacles.resize(map->paths.size());
    for (std::size_t k = 0; k < map->paths.size(); ++k) {
        const city::AiPath& path = map->paths[k];
        const int n = static_cast<int>(path.center.size());
        if (n < 2 || path.xAxis.size() < path.center.size() || path.zAxis.size() < path.center.size())
            continue; // OpenMM2 guard: every retail road has its frames
        auto cum = [&](int i) {
            if (i == 0)
                return std::bit_cast<float>(path.unknown); // the first centre length
            const auto j = static_cast<std::size_t>(i - 1);
            return j < path.centerLengths.size() ? path.centerLengths[j] : 0.0f;
        };
        auto& sections = m_sectionObstacles[k];
        sections.resize(static_cast<std::size_t>(n));
        for (int idx = 1; idx < n; ++idx) {
            const auto s = static_cast<std::size_t>(idx);
            const Vec3& c = path.center[s];
            const Vec3& z = path.zAxis[s];
            const Vec3& x = path.xAxis[s];
            for (const auto room : path.rooms) {
                const auto* list = inRoom(room);
                if (!list)
                    continue;
                for (const int i : *list) {
                    const PedObstacle& o = m_obstacles[static_cast<std::size_t>(i)];
                    const float dz = o.position.z - c.z;
                    const float dx = o.position.x - c.x;
                    const float along = dx * z.x + dz * z.z;
                    if (!(0.0f < along) || !(along < cum(idx) - cum(idx - 1)) || o.drivable)
                        continue;
                    const int side = -x.z * dz + -x.x * dx < 0.0f ? -1 : 1;
                    auto& out = sections[s][side == 1 ? 0 : 1];
                    out.insert(out.begin(), i);
                }
            }
        }
    }
}

// aiBanger::IsBlockingTarget: how far ahead along the way from `from` to `to`
// the prop's ground origin lies when it is in the way: ahead, nearer than
// `to` plus `reach`, within the prop's radius (at most 2 m) + half `width`
// + 1 m to either side and less than 0.7 rad off the line; -1 when not.
float Pedestrians::isBlockingTarget(const PedObstacle& o, const Vec3& from, const Vec3& to, float reach,
                                    float width) const {
    Vec3 d{to.x - from.x, to.y - from.y, to.z - from.z};
    const float len2 = d.x * d.x + d.y * d.y + d.z * d.z;
    const float inv = len2 == 0.0f ? 0.0f : 1.0f / std::sqrt(len2);
    d = {d.x * inv, d.y * inv, d.z * inv}; // Vector3::Scale
    const float nx = -d.z, nz = d.x;
    const float fx = from.x - to.x, fz = from.z - to.z;
    const float span = std::sqrt(fx * fx + fz * fz);
    const float ox = o.origin.x - from.x, oz = o.origin.z - from.z;
    const float lateral = ox * nx + nz * oz;
    const float along = ox * d.x + oz * d.z;
    const float r = (std::min(o.yRadius, 2.0f) + width * 0.5f) + 1.0f; // aiBanger::Radius
    const float angle = std::atan2(lateral, along);
    if (-r < lateral && lateral < r && 0.0f < along && along < span + reach && -0.7f < angle && angle < 0.7f)
        return along;
    return -1.0f;
}

// aiPedestrian::DetectBangerCollision: the first prop on the list of the
// section the pedestrian walks (on a corner, the intersection it is heading
// for) that blocks its way to its target point, else the first on the next
// section's list (past the road's end, the intersection's).
bool Pedestrians::detectBangerCollision(const Ped& p, int& obstacle, float& along) const {
    if (p.path < 0 || static_cast<std::size_t>(p.path) >= m_sectionObstacles.size())
        return false;
    const auto& sections = m_sectionObstacles[static_cast<std::size_t>(p.path)];
    const int n = sections.empty() ? 0 : static_cast<int>(sections.size());
    auto test = [&](const std::vector<int>& list) {
        for (const int i : list) {
            const PedObstacle& o = m_obstacles[static_cast<std::size_t>(i)];
            const float a = isBlockingTarget(o, p.position, p.target, 0.0f, 0.5f);
            if (0.0f < a) {
                along = a;
                obstacle = i;
                return true;
            }
        }
        return false;
    };
    static const std::vector<int> kNone;
    auto nodeList = [&]() -> const std::vector<int>& {
        const int node = m_net.paths()[static_cast<std::size_t>(p.path)].intersection[p.dir == 1 ? 0 : 1];
        if (node < 0 || static_cast<std::size_t>(node) >= m_nodeObstacles.size())
            return kNone; // OpenMM2 guard: every retail road end has an intersection
        return m_nodeObstacles[static_cast<std::size_t>(node)];
    };
    auto sectionList = [&](int idx) -> const std::vector<int>& {
        return sections[static_cast<std::size_t>(idx)][p.side == 1 ? 0 : 1];
    };
    int idx = p.idx;
    if (idx == 0 || idx == n) {
        if (test(nodeList()))
            return true;
    } else if (idx > 0 && idx < n && test(sectionList(idx))) {
        return true;
    }
    idx = p.dir == 1 ? idx + 1 : idx - 1;
    if (idx != 0 && idx != n) {
        if (idx < 1 || n <= idx)
            return false;
        return test(sectionList(idx));
    }
    return test(nodeList());
}

// aiPedestrian::AvoidBanger: step round the prop's centre at its radius
// + 1 m (YRadius while it stands, else the model's radius); a standing
// pedestrian walks on.
void Pedestrians::avoidBanger(Ped& p, int obstacle) {
    const PedObstacle& o = m_obstacles[static_cast<std::size_t>(obstacle)];
    const bool standing = !m_standing || m_standing(static_cast<std::size_t>(obstacle));
    const float radius = (standing ? o.yRadius : o.modelRadius) + 1.0f;
    avoidObstacle(p, o.position, radius);
    const Seqs& s = seqs(p);
    if (p.seq == s.stand)
        queueSeq(p, s.standWalk);
}

// --- Reactions -------------------------------------------------------------------

// aiPedestrian::DetectPlayerForwardCollision: ahead of the car (behind it
// in reverse gear), between a quarter of its length and 20 m, within its half
// width + 2 m.
bool Pedestrians::forwardCollision(const Ped& p, const PlayerCar& c, float& along) const {
    along = kNoHit;
    if (!(c.speed() > 0.0f))
        return false;
    const Vec3 rel = p.position - c.transform.m3;
    const Vec3 fwd = c.reversing ? c.transform.m2 : -c.transform.m2;
    const Vec3 lat = c.reversing ? -c.transform.m0 : c.transform.m0;
    const float a = fwd.dot(rel);
    if (c.length * 0.25f < a && a < 20.0f) {
        const float side = lat.dot(rel);
        const float half = c.width * 0.5f + 2.0f;
        if (-half < side && side < half) {
            along = a;
            return true;
        }
    }
    return false;
}

// aiPedestrian::DetectPlayerAnticipate: the same up to 35 m, within the half
// width + 4 m (measured along -m0 whatever the gear).
bool Pedestrians::anticipateCollision(const Ped& p, const PlayerCar& c, float& along) const {
    along = kNoHit;
    if (!(c.speed() > 0.0f))
        return false;
    const Vec3 rel = p.position - c.transform.m3;
    const Vec3 fwd = c.reversing ? c.transform.m2 : -c.transform.m2;
    const float a = fwd.dot(rel);
    if (c.length * 0.25f < a && a < 35.0f) {
        const float side = (-c.transform.m0).dot(rel);
        const float half = c.width * 0.5f + 4.0f;
        if (-half < side && side < half) {
            along = a;
            return true;
        }
    }
    return false;
}

// aiPedestrian::DetectPlayerCollision: the car within 6 m ahead of the
// pedestrian, within its radius either side (in the matrix of the last
// update).
bool Pedestrians::playerCollision(const Ped& p, const PlayerCar& c, float& ahead) const {
    if (!c.valid)
        return false;
    const Mat34 m = frameOf(p.frameHeading, p.position);
    const float dx = c.transform.m3.x - p.position.x;
    const float dz = c.transform.m3.z - p.position.z;
    const float fwd = -m.m2.x * dx + -m.m2.z * dz;
    const float side = dx * m.m0.x + dz * m.m0.z;
    const float r = c.radius;
    if (-r < fwd && fwd < kPedLookAhead && -r < side && side < r) {
        ahead = fwd;
        return true;
    }
    return false;
}

// The wall probe of Anticipate and Avoid: from 2 m on the road side to 10 m
// on the building side, 1 m up.
bool Pedestrians::wallProbe(Ped& p) {
    p.wall = false;
    if (!m_probe)
        return false;
    const Vec3 x = axisX(p.path, p.idx);
    const float s = p.side == 1 ? 1.0f : -1.0f;
    const Vec3 up{0, 1, 0};
    Vec3 hit;
    if (m_probe(p.position + x * (2.0f * s) + up, p.position - x * (10.0f * s) + up, hit)) {
        p.wall = true;
        p.wallHit = hit;
    }
    return p.wall;
}

// Anticipate: backed against the wall it ran to, 0.2 m out from it, facing the
// road.
void Pedestrians::backupAt(Ped& p) {
    const Vec3 x = axisX(p.path, p.idx);
    if (p.side == 1) {
        p.position.x = x.x * 0.2f + p.wallHit.x;
        p.position.z = x.z * 0.2f + p.wallHit.z;
        p.heading = std::atan2(x.x, x.z);
    } else {
        p.position.x = p.wallHit.x - x.x * 0.2f;
        p.position.z = p.wallHit.z - x.z * 0.2f;
        p.heading = std::atan2(-x.x, -x.z);
    }
    startSeq(p, seqs(p).backup);
}

// aiPedestrian::AvoidObstacle: step round an obstacle on the walkway (to the
// side with room within the 1.5 m half width), or turn back when it blocks
// the whole width. For small angles it turns the wrong way, as coded.
void Pedestrians::avoidObstacle(Ped& p, const Vec3& obstacle, float radius) {
    int n = sections(p.path);
    int i = p.idx == n ? p.idx - 1 : p.idx;
    Vec3 d = p.dir == 1 ? -axisX(p.path, i) : axisX(p.path, i);
    const Vec3 v = sv(p.path, p.side, i);
    const float pedLat = (p.position.x - v.x) * d.x + (p.position.z - v.z) * d.z;
    const float obsLat = (obstacle.x - v.x) * d.x + (obstacle.z - v.z) * d.z;
    float r = radius;
    if (obsLat + radius <= kPedMaxLateral || -kPedMaxLateral <= obsLat - radius) {
        if (pedLat <= obsLat) {
            if (!(obsLat - radius < -kPedMaxLateral))
                r = -radius;
        } else if (!(obsLat + radius <= kPedMaxLateral)) {
            r = -radius;
        }
    } else if (p.idx == 0 || n <= p.idx) {
        // On a corner: back onto the previous road, turned round. MM2 parks
        // the old previous side (an int) in the radius argument's slot and
        // later loads that slot as the float offset: +1 reads as the smallest
        // denormal (the target is the obstacle itself), -1 as a NaN, which
        // turns the heading and then the position into NaN: the pedestrian
        // is gone until its road is cleared (OpenMM2: `lost`).
        const int oldPrevSide = p.prevSide, oldPrevDir = p.prevDir, oldPrevPath = p.prevPath;
        r = std::bit_cast<float>(static_cast<std::int32_t>(oldPrevSide));
        if (std::isnan(r))
            p.lost = true;
        p.prevSide = p.side;
        p.prevDir = p.dir;
        const int from = p.path;
        p.side = oldPrevSide;
        p.dir = -oldPrevDir;
        moveToPath(p, oldPrevPath);
        p.prevPath = from;
        p.heading += kHalfTurn;
        n = sections(p.path);
        p.idx = p.dir == 1 ? 0 : n;
        i = p.idx == n ? p.idx - 1 : p.idx;
        d = p.dir == 1 ? -axisX(p.path, i) : axisX(p.path, i);
        p.dist = roadDistance(p);
        calcCurve(p, p.idx - 1, p.idx, p.lateral);
    } else {
        // Turned round where it is, heading taken from row 0 (the first lane's
        // lengths) at the last road distance; `d` keeps the old direction.
        p.dir = -p.dir;
        p.heading = getHeading(p.path, p.dist, 0, p.dir);
    }
    if (p.lost)
        return;
    p.target = d * r + obstacle;
    const Mat34 m = frameOf(p.frameHeading, p.position);
    const Vec3 t = p.target - p.position;
    const float angle = std::atan2((t.z * m.m0.z + t.y * m.m0.y) + t.x * m.m0.x,
                                   -((t.z * m.m2.z + t.y * m.m2.y) + t.x * m.m2.x));
    if (kPedTurnRate < angle)
        p.heading -= kPedTurnRate;
    else if (-kPedTurnRate <= angle)
        p.heading = angle + p.heading;
    else
        p.heading = kPedTurnRate + p.heading;
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
    p.dist = roadDistance(p);
    float ahead = kNoHit;
    // Within 6 m of the player (squared XZ distance from the start of the
    // update).
    if (c.valid && sq(c.transform.m3.z - p.position.z) + sq(c.transform.m3.x - p.position.x) < 36.0f)
        playerCollision(p, c, ahead);
    float prop = kNoHit;
    int obstacle = -1;
    detectBangerCollision(p, obstacle, prop);
    if (kNoHit <= prop || ahead <= prop) {
        // (MM2's branch for both beyond 9999 m, AvoidPedCollision, is
        // never taken: both start at 9999.)
        if (prop <= ahead || kNoHit <= ahead) {
            if (p.seq == s.stand)
                queueSeq(p, s.standWalk);
            solveTargetPoint(p, static_cast<float>(p.dir) * kPedLookAhead + p.dist);
            steer(p, p.target);
            solveRoadSegment(p, p.dist);
            return;
        }
        // aiPedestrian::AvoidPlayer: round the car at its radius + 1 m.
        avoidObstacle(p, c.transform.m3, c.radius + 1.0f);
        if (p.seq == s.stand)
            queueSeq(p, s.standWalk);
    } else {
        avoidBanger(p, obstacle);
    }
    calcCurve(p, p.idx - 1, p.idx, p.lateral);
}

void Pedestrians::anticipate(Ped& p, const PlayerCar& c) {
    const Seqs& s = seqs(p);
    const int n = sections(p.path);
    // Anticipate looks at the reaction only (not the crossing state).
    const bool entering = p.reaction != p.lastReaction;
    auto queueBrace = [&] {
        if (p.seq == s.walk || p.seq == s.standWalk)
            queueSeq(p, s.walkAntic);
        else if (p.seq == s.stand || p.seq == s.walkStand)
            queueSeq(p, s.standAntic);
    };
    auto faceCar = [&] {
        p.heading = std::atan2(c.transform.m3.x - p.position.x, c.transform.m3.z - p.position.z);
    };
    if (p.idx == 0 || p.idx == n) {
        if (entering) {
            queueBrace();
            p.lastReaction = p.reaction;
        }
        faceCar();
        return;
    }
    wallProbe(p);
    if (entering) {
        const Vec3 x = axisX(p.path, p.idx);
        if (p.wall) {
            if (std::sqrt(sq(p.position.z - p.wallHit.z) + sq(p.position.x - p.wallHit.x)) >= 1.25f) {
                // To the buildings.
                p.heading = p.side == 1 ? std::atan2(-x.x, -x.z) : std::atan2(x.x, x.z);
                startSeq(p, s.run);
            } else {
                backupAt(p);
            }
        } else if (p.seq == s.walk) {
            if (m_rng.frand() >= 0.5f) {
                // Run along the road the way the car is going.
                startSeq(p, s.run);
                const auto& centre = m_net.source()->paths[static_cast<std::size_t>(p.path)].center;
                const Vec3 along = centre[static_cast<std::size_t>(p.idx)] - centre[static_cast<std::size_t>(p.idx - 1)];
                const Vec3 travel = c.reversing ? c.transform.m2 : -c.transform.m2;
                const float dot = along.x * travel.x + (along.y * travel.y + along.z * travel.z);
                const int newDir = dot > 0.0f ? 1 : -1;
                if (newDir != p.dir) {
                    p.dir = newDir;
                    p.dist = roadDistance(p);
                    // Heading from row 0 at the new distance; as coded,
                    // direction +1 then faces back (Wander turns it round).
                    p.heading = getHeading(p.path, p.dist, 0, p.dir);
                    if (p.dir == 1)
                        p.heading = p.heading <= 0.0f ? p.heading + kHalfTurn : p.heading - kHalfTurn;
                }
            } else {
                startSeq(p, s.walkAntic);
            }
        } else {
            queueBrace();
        }
        p.lastReaction = p.reaction;
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
    // Avoid looks at the reaction only (not the crossing state).
    const bool entering = p.reaction != p.lastReaction;
    const bool onSegment = p.idx != 0 && p.idx != n;
    // On a corner MM2 neither probes nor clears the wall flag.
    if (onSegment)
        wallProbe(p);
    auto axes = [&](Vec3& a, Vec3& b) {
        a = p.reversingAtDive ? -c.transform.m0 : c.transform.m0;
        b = p.reversingAtDive ? -c.transform.m2 : c.transform.m2;
    };
    const Vec3 rel = p.position - c.transform.m3;
    if (entering && (!onSegment || !p.wall)) {
        // Dive, facing against the car's way, to the side it steers away
        // from or, going straight, away from its centre line.
        p.reversingAtDive = c.reversing;
        Vec3 a, b;
        axes(a, b);
        p.heading = std::atan2(b.x, b.z);
        p.sideDist0 = onSegment ? a.x * rel.x + a.y * rel.y + a.z * rel.z
                                : a.y * rel.y + a.z * rel.z + a.x * rel.x;
        bool right;
        if (c.steering > 0.85f)
            right = true;
        else if (c.steering < -0.85f)
            right = false;
        else
            right = !(0.0f < p.sideDist0);
        const bool walking = p.seq == s.walk;
        startSeq(p, right ? (walking ? s.walkRDive : s.anticRDive) : (walking ? s.walkLDive : s.anticLDive));
        p.scream = true;
        p.lastReaction = p.reaction;
    } else if (entering) {
        const Vec3 x = axisX(p.path, p.idx);
        if (p.position.dist(p.wallHit) >= 1.25f) {
            p.heading = std::atan2(-x.x, -x.z); // as coded, whatever the side
            p.scream = true;
            startSeq(p, s.run);
        } else {
            // Unlike Anticipate, whatever the side.
            p.heading = std::atan2(x.x, x.z);
            p.position.x = x.x * 0.2f + p.wallHit.x;
            p.position.z = x.z * 0.2f + p.wallHit.z;
            startSeq(p, s.backup);
        }
        p.lastReaction = p.reaction;
    }
    if (onSegment) {
        if (p.seq == s.run) {
            // MM2 measures from this update's probe point (left over from the
            // probe when nothing was hit).
            if (p.wall && p.position.dist(p.wallHit) < 1.25f) {
                const Vec3 x = axisX(p.path, p.idx);
                p.heading = std::atan2(x.x, x.z);
                p.position.x = x.x * 0.2f + p.wallHit.x;
                p.position.z = x.z * 0.2f + p.wallHit.z;
                startSeq(p, s.backup);
            }
            return;
        }
        if (p.seq == s.backup)
            return;
    }
    // Keep up with the dive: faster sideways when behind schedule.
    Vec3 a, b;
    axes(a, b);
    const float now = onSegment ? a.x * rel.x + a.y * rel.y + a.z * rel.z : a.y * rel.y + a.z * rel.z + a.x * rel.x;
    const float v = latSpeed(p, p.seq) * kFrameSeconds;
    const float f = static_cast<float>(p.frame);
    if (p.seq == s.lDiveGround || p.seq == s.rDiveGround) {
        if (std::abs(now) < std::abs(f * v + p.sideDist0 + 2.54f))
            latScale = 5.0f;
    } else if (p.frame > 12 && std::abs(now) < std::abs(f * v + p.sideDist0)) {
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
    return axisZ(path, 0) * 2.5f + curb.front();
}

// PreCrossStreet's near curb point and WaitCrossStreet's far one.
void Pedestrians::crossTargets(const Ped& p, Vec3& nearSide, Vec3& farSide) const {
    int road, side;
    bool atEnd;
    if (p.crossChoice == 2) {
        road = p.prevPath;
        side = p.prevSide;
        atEnd = p.prevDir == 1;
    } else {
        road = p.path;
        const bool nodeAtEnd = m_net.paths()[static_cast<std::size_t>(road)].intersection[0] == crossedNode(p);
        if (p.prevDir == p.prevSide)
            side = nodeAtEnd ? -1 : 1;
        else
            side = !nodeAtEnd ? -1 : 1;
        atEnd = nodeAtEnd;
    }
    nearSide = curbPoint(road, side, atEnd);
    farSide = curbPoint(road, -side, atEnd);
}

// aiPedestrian::Accident: a car out of normal driving in the intersection
// being crossed or near this road's end.
bool Pedestrians::accident(const Ped& p) const {
    return m_accident && m_accident(crossedNode(p), p.path, p.dir);
}

// Back to the sidewalk it came from, turned round (PreCrossStreet,
// WaitCrossStreet, CrossStreet).
void Pedestrians::abortCrossing(Ped& p) {
    p.dir = -p.prevDir;
    p.heading += kHalfTurn;
    p.side = p.prevSide;
    p.cross = 0;
    moveToPath(p, p.prevPath);
    p.idx = p.dir == 1 ? 0 : sections(p.path);
    queueSeq(p, seqs(p).walk);
}

void Pedestrians::preCross(Ped& p, const PlayerCar& c) {
    if (p.cross != p.lastCross || p.reaction != p.lastReaction) {
        Vec3 nearSide, farSide;
        crossTargets(p, nearSide, farSide);
        p.target = nearSide;
        queueSeq(p, seqs(p).walk);
        p.lastReaction = p.reaction;
        p.lastCross = p.cross;
    }
    steer(p, p.target);
    float ahead;
    if (c.speed() < 1.0f && playerCollision(p, c, ahead))
        abortCrossing(p);
    else if (sq(p.target.z - p.position.z) + sq(p.target.x - p.position.x) < 1.0f)
        p.cross = 2;
}

void Pedestrians::waitCross(Ped& p, const PlayerCar& c) {
    if (p.cross != p.lastCross || p.reaction != p.lastReaction) {
        Vec3 nearSide, farSide;
        crossTargets(p, nearSide, farSide);
        p.target = farSide;
        queueSeq(p, m_rng.frand() < 0.5f ? seqs(p).stand : seqs(p).stand2);
        p.lastReaction = p.reaction;
        p.lastCross = p.cross;
    }
    steer(p, p.target);
    float ahead;
    if (accident(p) || (c.speed() < 1.0f && playerCollision(p, c, ahead))) {
        abortCrossing(p);
        return;
    }
    if (sq(p.target.z - p.position.z) + sq(p.target.x - p.position.x) < 1.0f)
        p.cross = 1;
    // Across once the lights' pedestrian phase shows WALK (light 0 of the
    // set; this can override the line above).
    const int node = crossedNode(p);
    if (m_lights && m_lights->walkPhaseAt(node) && m_lights->firstLightAt(node) == LightState::Walk)
        p.cross = 3;
}

void Pedestrians::crossStreet(Ped& p, const PlayerCar& c) {
    if (p.cross != p.lastCross || p.reaction != p.lastReaction) {
        queueSeq(p, seqs(p).walk);
        p.lastReaction = p.reaction;
        p.lastCross = p.cross;
    }
    steer(p, p.target);
    const int node = crossedNode(p);
    if (m_lights && m_lights->hasLights(node) && m_lights->firstLightAt(node) == LightState::WalkEnd)
        queueSeq(p, seqs(p).run);
    float ahead;
    if (c.speed() < 1.0f && playerCollision(p, c, ahead))
        abortCrossing(p);
    else if (sq(p.target.z - p.position.z) + sq(p.target.x - p.position.x) < 1.5f)
        p.cross = 0;
}

// --- Update -------------------------------------------------------------------

// aiPedestrian::Update. (MM2 also hands a pedestrian with an attached
// physics entity over to it; nothing attaches one in OpenMM2.)
void Pedestrians::update(int idx, float dt, const PlayerCar& c) {
    Ped& p = m_peds[static_cast<std::size_t>(idx)];
    if (!p.active || p.path < 0)
        return;
    if (p.lost) {
        // With a NaN position and heading nothing else changes in MM2.
        animate(p, dt);
        return;
    }
    const Seqs& s = seqs(p);
    p.scream = false;
    // The nearest player's squared XZ distance (a single player here).
    const float d2 = c.valid ? sq(p.position.z - c.transform.m3.z) + sq(p.position.x - c.transform.m3.x) : kFarAway;
    const float speed = c.valid ? c.speed() : 0.0f;
    float latScale = 1.0f;
    if (p.seq == s.backup) {
        // Out of the corner once the car has gone by.
        if (c.valid) {
            // aiVehiclePlayer::Update: the unit direction of the car's motion.
            const Vec3& v = c.velocity;
            const Vec3 m = scaledUnit(v, v.z * v.z + v.y * v.y + v.x * v.x);
            const float r = c.radius;
            if ((p.position - c.transform.m3).dot(m) < 0.0f && (r + r) * (r + r) < d2) {
                p.reaction = 0;
                p.lateral = std::sin(m_rng.frand() * 6.2831f) * kPedMaxLateral;
                calcCurve(p, p.idx - 1, p.idx, p.lateral);
            }
        }
    } else if ((p.seq == s.run && p.reaction == static_cast<int>(p.wall)) || p.seq == s.anticLDive ||
               p.seq == s.anticRDive || p.seq == s.walkLDive || p.seq == s.walkRDive ||
               p.seq == s.lDiveGround || p.seq == s.rDiveGround || p.seq == s.groundStandL ||
               p.seq == s.groundStandR) {
        // Busy: keep the reaction.
    } else {
        int reaction = 0;
        if (d2 < sq(kPedAwareRadius)) {
            // aiPedestrian::TimeToCollision: (distance - 2) / speed, with
            // aiVehiclePlayer's inverse speed (0 below 0.001 m/s).
            const float inv = 0.001f < speed ? 1.0f / speed : 0.0f;
            float along;
            if (forwardCollision(p, c, along)) {
                const float ttc = (along - 2.0f) * inv;
                if (1.0f <= speed)
                    reaction = ttc < 0.75f ? 2 : ttc < 2.3f ? 1 : 0;
            } else if (anticipateCollision(p, c, along)) {
                const float ttc = (along - 2.0f) * inv;
                if (1.0f <= speed && ttc < 2.3f)
                    reaction = 1;
            }
        }
        p.reaction = reaction;
    }
    switch (p.reaction) {
    case 1:
        anticipate(p, c);
        break;
    case 2:
        avoid(p, c, latScale);
        break;
    case 0:
        switch (p.cross) {
        case 0:
            wander(p, c);
            break;
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
            break;
        }
        break;
    default:
        break;
    }
    if (p.lost) {
        animate(p, dt);
        return;
    }
    // The matrix from the new heading; move by the current sequence's speeds,
    // free of the path.
    p.frameHeading = p.heading;
    const Mat34 m = frameOf(p.heading, p.position);
    const float fwd = fwdSpeed(p, p.seq) * dt;
    const float lat = latScale * latSpeed(p, p.seq) * dt;
    p.position.x -= fwd * m.m2.x + lat * m.m0.x;
    p.position.y -= fwd * m.m2.y + lat * m.m0.y;
    p.position.z -= fwd * m.m2.z + lat * m.m0.z;
    // Ground: a probe 2 m up and down, taken when within 0.5 m; flat roads
    // keep the first curb's height beyond 50 m of the player.
    const PathInfo& info = m_net.paths()[static_cast<std::size_t>(p.path)];
    if ((info.flags & 0x8) && d2 > 2500.0f && !walk(p.path, 1).curb.empty()) {
        p.position.y = walk(p.path, 1).curb.front().y;
    } else if (m_probe) {
        Vec3 hit;
        if (m_probe(p.position + Vec3{0, 2, 0}, p.position - Vec3{0, 2, 0}, hit) &&
            std::abs(hit.y - p.position.y) < 0.5f)
            p.position.y = hit.y;
    } else if (const int w = walk(p.path, p.side).sidewalk; w >= 0) {
        // OpenMM2 without the game's collision: the sidewalk line's height.
        const auto& line = m_net.sidewalks()[static_cast<std::size_t>(w)].centre;
        p.position.y = line.pointAt(line.project(p.position)).y;
    }
    p.target.y = p.position.y;
    animate(p, dt);
}

// pedAnimationInstance::PreUpdate and Update: the frame clock advances by
// dt x 30 frames per pedestrian update, the whole frames go to this
// pedestrian, the fraction stays for the next one; at the end of the sequence
// the queued one starts.
void Pedestrians::animate(Ped& p, float dt) {
    m_animClock += dt * kPedAnimFps;
    const float whole = std::floor(m_animClock);
    const int step = static_cast<int>(whole);
    m_animClock -= whole;
    if (frameCount(p, p.seq) <= 1) {
        if (step != 0)
            startSeq(p, p.queued ? p.queued : p.seq);
    } else {
        p.frame += step;
        if (frameCount(p, p.seq) <= p.frame)
            startSeq(p, p.queued ? p.queued : p.seq);
    }
}

// aiPath::UpdatePedestrians: down the road's list from its head. A pedestrian
// that moves to another road takes the walk on into that road's list (its
// new neighbours update now, the rest of this road waits), as in MM2; it stops
// on reaching this road's head again.
void Pedestrians::updateRoad(int path, float dt, const PlayerCar& player) {
    int idx = m_pathHead[static_cast<std::size_t>(path)];
    // OpenMM2 guard against a walk that never ends.
    std::size_t guard = 4 * m_peds.size() + 4;
    while (idx >= 0 && guard-- > 0) {
        update(idx, dt, player);
        idx = m_peds[static_cast<std::size_t>(idx)].next;
        if (idx == m_pathHead[static_cast<std::size_t>(path)])
            break;
    }
}

void Pedestrians::step(float dt, const PlayerCar& player, int room) {
    const city::AiMap* map = m_net.source();
    static const std::vector<std::uint16_t> kNone;
    auto list = [&](int r) -> const std::vector<std::uint16_t>& {
        if (!map || r < 0 || static_cast<std::size_t>(r) >= map->roomPathsIn.size())
            return kNone;
        return map->roomPathsIn[static_cast<std::size_t>(r)];
    };
    for (Ped& p : m_peds)
        p.placed = false;
    if (!m_started) {
        // aiMap::Reset: the player's first room.
        m_started = true;
        m_room = room;
        if (m_populateAll) {
            std::vector<std::uint16_t> all;
            for (std::size_t p = 0; p < m_net.paths().size(); ++p)
                all.push_back(static_cast<std::uint16_t>(p));
            adjust(kNone, all);
            m_populateAll = false;
        } else {
            adjust(list(0), list(room));
        }
    } else if (room != 0 && room != m_room) {
        // aiMap::Update: the player entered another room.
        adjust(list(m_room), list(room));
        m_room = room;
    }
    for (int path = m_activeHead; path >= 0; path = m_activeNext[static_cast<std::size_t>(path)])
        updateRoad(path, dt, player);
    publish();
}

void Pedestrians::publish() {
    m_public.clear();
    for (std::size_t i = 0; i < m_peds.size(); ++i) {
        const Ped& p = m_peds[i];
        if (!p.active || p.lost || !p.seq)
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
        out.placed = p.placed;
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
