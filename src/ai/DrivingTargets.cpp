// aiVehiclePhysics' target half (MM2 build 3393, MM2Recomp): the junction
// turns of the road window (InitRoadTurns, CalcRoadTurns,
// CalcTurnIntersection and the width and offset helpers), the turn circles
// (InSharpTurn, CalcSharpTurnTarget, SaveTurnTarget) and the next target
// down the road (CalcRoadTarget) or to the destination
// (CalcDestinationTarget).
#include "ai/Driving.h"
#include "ai/MapView.h"
#include "ai/PathGeometry.h"
#include "ai/Traffic.h"
#include "phys/vehicle/CarSim.h"

#include <algorithm>
#include <cmath>

namespace mm2::ai {

namespace {

const Vec3& at(const std::vector<Vec3>& a, int i) {
    static const Vec3 zero;
    return i >= 0 && static_cast<std::size_t>(i) < a.size() ? a[static_cast<std::size_t>(i)] : zero;
}

float dotXZ(const Vec3& a, const Vec3& b) { return a.x * b.x + a.z * b.z; }

float distXZ(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

// Vector3::Normalize (a zero vector stays zero).
Vec3 normalized(const Vec3& v) {
    const float m2 = (v.z * v.z + v.y * v.y) + v.x * v.x;
    if (m2 == 0.0f)
        return {};
    const float inv = 1.0f / std::sqrt(m2);
    return {v.x * inv, v.y * inv, v.z * inv};
}

Vec3 unitXZ(Vec3 v) {
    v.y = 0.0f;
    return normalized(v);
}

// The number of arc steps of a turn: a tenth of its length, rounded up to an
// even number, at least 2.
int arcSteps(float length) {
    int n = static_cast<int>(length * 0.1f);
    n += n % 2;
    return n < 2 ? 2 : n;
}

} // namespace

// --- the junction turns of the window ---------------------------------------------

void PhysicsDriver::initRoadTurns() {
    // aiVehiclePhysics::InitRoadTurns: the turn from each window road into the
    // next, seen at the end of the first along the second's first section.
    for (int w = 0; w < 2; ++w) {
        const city::AiPath* a = road(w);
        const city::AiPath* b = road(w + 1);
        if (!a || !b) {
            m_turnAngle[w] = 0.0f;
            m_turnDir[w] = 0.0f;
            continue;
        }
        const int n = static_cast<int>(a->center.size());
        const int m = static_cast<int>(b->center.size());
        const Vec3 d = m_roadDir[w + 1] ? at(b->center, 1) - at(b->center, 0)
                                        : at(b->center, m - 2) - at(b->center, m - 1);
        float angle;
        if (m_roadDir[w]) {
            const Vec3& x = at(a->xAxis, n - 1);
            const Vec3& z = at(a->zAxis, n - 1);
            angle = std::atan2(d.z * -x.z + d.x * -x.x, d.z * -z.z + d.x * -z.x);
        } else {
            const Vec3& x = at(a->xAxis, 0);
            const Vec3& z = at(a->zAxis, 0);
            angle = std::atan2(d.z * x.z + d.x * x.x, d.z * z.z + d.x * z.x);
        }
        m_turnAngle[w] = angle;
        m_turnDir[w] = angle >= 0.7f ? 1.0f : (angle <= -0.7f ? -1.0f : 0.0f);
    }
}

void PhysicsDriver::calcRoadTurns() {
    // aiVehiclePhysics::CalcRoadTurns: each junction turn's circle, through
    // its corner (CalcTurnIntersection) with the room it leaves.
    for (int w = 0; w < 2; ++w) {
        const city::AiPath* a = road(w);
        if (m_turnDir[w] == 0.0f || !a || !road(w + 1))
            continue;
        const float d = calcTurnIntersection(w);
        const float h = (3.14f - std::abs(m_turnAngle[w])) * 0.5f;
        const float r = d / (1.0f - std::sin(h));
        m_turnRadius[w] = r;
        const float t = std::cos(h) * r;
        m_turnSetback[w] = t;
        const float k = (r - d) * m_turnDir[w];
        const int n = static_cast<int>(a->center.size());
        // The travel frame at the road's exit: forward and left.
        Vec3 fwd, left;
        if (m_roadDir[w]) {
            fwd = -at(a->zAxis, n - 1);
            left = at(a->xAxis, n - 1);
        } else {
            fwd = at(a->zAxis, 0);
            left = -at(a->xAxis, 0);
        }
        const Vec3& corner = m_turnCorner[w];
        const Vec3 back = -fwd;
        Vec3& c = m_turnCenter[w];
        c = {(back.x * t + corner.x) - left.x * k, (back.y * t + corner.y) - left.y * k,
             (back.z * t + corner.z) - left.z * k};
        const float rd = r * m_turnDir[w];
        m_turnStartDir[w] = unitXZ(
            {(left.x * rd + c.x) - c.x, (left.y * rd + c.y) - c.y, (left.z * rd + c.z) - c.z});
        const float e = m_turnAngle[w] * m_turnDir[w];
        const float s = std::sin(e) * r;
        const float cs = std::cos(e) * r * m_turnDir[w];
        const Vec3 q{(c.x - back.x * s) + left.x * cs, (c.y - back.y * s) + left.y * cs,
                     (c.z - back.z * s) + left.z * cs};
        m_turnEndDir[w] = unitXZ(q - c);
    }
}

float PhysicsDriver::laneTrafficIntrusion(int roadId, bool rightList, int bucket, const Vec3& base,
                                          const Vec3& side) const {
    // How far the cars waiting in a lane list reach past a curb point
    // (towards `side`), plus 1.25 m; 0 when none.
    const Traffic* traffic = m_map ? m_map->traffic() : nullptr;
    float most = 0.0f;
    if (!traffic)
        return most;
    for (int id : traffic->roadVehicles(roadId, rightList ? 1 : -1, bucket)) {
        const TrackedCar* o = ambientObstacle(id);
        if (!o)
            continue;
        const float reach = (o->position.x - base.x) * side.x + side.z * (o->position.z - base.z) + 1.25f;
        if (reach > most)
            most = reach;
    }
    return most;
}

float PhysicsDriver::calcCurrentMaxWidthAdjustment(int w) const {
    // aiVehiclePhysics::CalcCurrentMaxWidthAdjustment: traffic waiting at the
    // outside curb of road w's end narrows the turn.
    const city::AiPath* a = road(w);
    if (!a)
        return 0.0f;
    const int n = static_cast<int>(a->center.size());
    const bool rd = m_roadDir[w];
    if (!m_map->driveOnLeft()) {
        if (m_turnDir[w] > 0.0f)
            return 0.0f;
        return laneTrafficIntrusion(m_roads[w], rd, n - 1, rd ? at(pathBoundary(a->right, 0), n - 1)
                                                               : at(pathBoundary(a->left, 0), 0),
                                    rd ? at(a->xAxis, n - 1) : -at(a->xAxis, 0));
    }
    if (m_turnDir[w] <= 0.0f)
        return 0.0f;
    return laneTrafficIntrusion(m_roads[w], rd, n - 1, rd ? at(pathBoundary(a->left, 0), n - 1)
                                                           : at(pathBoundary(a->right, 0), 0),
                                rd ? -at(a->xAxis, n - 1) : at(a->xAxis, 0));
}

float PhysicsDriver::calcCurrentRdOffset(int w) const {
    // aiVehiclePhysics::CalcCurrentRdOffset: how far road w's inside line is
    // from its inside curb.
    const city::AiPath* a = road(w);
    if (!a)
        return 0.0f;
    const int n = static_cast<int>(a->center.size());
    const bool rd = m_roadDir[w];
    if (!m_map->driveOnLeft()) {
        if (m_turnDir[w] <= 0.0f)
            return m_leftSide;
        return laneTrafficIntrusion(m_roads[w], rd, n - 1, rd ? at(pathBoundary(a->right, 0), n - 1)
                                                               : at(pathBoundary(a->left, 0), 0),
                                    rd ? at(a->xAxis, n - 1) : -at(a->xAxis, 0)) +
               m_rightSide;
    }
    if (m_turnDir[w] > 0.0f)
        return m_rightSide;
    return laneTrafficIntrusion(m_roads[w], rd, n - 1, rd ? at(pathBoundary(a->left, 0), n - 1)
                                                           : at(pathBoundary(a->right, 0), 0),
                                rd ? -at(a->xAxis, n - 1) : at(a->xAxis, 0)) +
           m_leftSide;
}

float PhysicsDriver::calcNextMaxWidthAdjustment(int w) const {
    // aiVehiclePhysics::CalcNextMaxWidthAdjustment: the same for the road the
    // turn goes into (its oncoming lanes' last section).
    const city::AiPath* b = road(w + 1);
    if (!b)
        return 0.0f;
    const int m = static_cast<int>(b->center.size());
    const bool rd = m_roadDir[w + 1];
    if (!m_map->driveOnLeft()) {
        if (m_turnDir[w] <= 0.0f)
            return 0.0f;
        return laneTrafficIntrusion(m_roads[w + 1], !rd, m - 1, rd ? at(pathBoundary(b->left, 0), 0)
                                                                    : at(pathBoundary(b->right, 0), m - 1),
                                    rd ? -at(b->xAxis, 0) : at(b->xAxis, m - 1));
    }
    if (m_turnDir[w] > 0.0f)
        return 0.0f;
    return laneTrafficIntrusion(m_roads[w + 1], !rd, m - 1, rd ? at(pathBoundary(b->right, 0), 0)
                                                                : at(pathBoundary(b->left, 0), m - 1),
                                rd ? at(b->xAxis, 0) : -at(b->xAxis, m - 1));
}

float PhysicsDriver::calcNextRdOffset(int w) const {
    // aiVehiclePhysics::CalcNextRdOffset.
    const city::AiPath* b = road(w + 1);
    if (!b)
        return 0.0f;
    const int m = static_cast<int>(b->center.size());
    const bool rd = m_roadDir[w + 1];
    if (!m_map->driveOnLeft()) {
        if (m_turnDir[w] > 0.0f)
            return m_rightSide;
        return laneTrafficIntrusion(m_roads[w + 1], !rd, m - 1, rd ? at(pathBoundary(b->left, 0), 0)
                                                                    : at(pathBoundary(b->right, 0), m - 1),
                                    rd ? -at(b->xAxis, 0) : at(b->xAxis, m - 1)) +
               m_leftSide;
    }
    if (m_turnDir[w] <= 0.0f)
        return m_leftSide;
    return laneTrafficIntrusion(m_roads[w + 1], !rd, m - 1, rd ? at(pathBoundary(b->right, 0), 0)
                                                                : at(pathBoundary(b->left, 0), m - 1),
                                rd ? at(b->xAxis, 0) : -at(b->xAxis, m - 1)) +
           m_rightSide;
}

float PhysicsDriver::calcTurnIntersection(int w) {
    // aiVehiclePhysics::CalcTurnIntersection: the corner of junction turn w,
    // where the two roads' inside curbs (moved in by the offsets) cross, and
    // the room the car has for the turn: its distance from the corner across
    // road w, kept between its width and what the two roads leave.
    const city::AiPath* a = road(w);
    const city::AiPath* b = road(w + 1);
    const int n = static_cast<int>(a->center.size());
    const int m = static_cast<int>(b->center.size());
    const float offC = calcCurrentRdOffset(w);
    const float offN = calcNextRdOffset(w);
    const auto& aLeft = pathBoundary(a->left, 0);
    const auto& aRight = pathBoundary(a->right, 0);
    const auto& bLeft = pathBoundary(b->left, 0);
    const auto& bRight = pathBoundary(b->right, 0);
    Vec3 pa, da, pb, db;
    float cornerY;
    if (m_turnDir[w] <= 0.0f) {
        if (!m_roadDir[w]) {
            pa = at(a->xAxis, 0) * offC + at(aRight, 0);
            da = -at(a->zAxis, 0);
        } else {
            pa = at(aLeft, n - 1) - at(a->xAxis, n - 1) * offC;
            da = -at(a->zAxis, n - 1);
        }
        if (!m_roadDir[w + 1]) {
            pb = at(b->xAxis, m - 1) * offN + at(bRight, m - 1);
            db = at(b->zAxis, m - 1);
            cornerY = at(bRight, m - 1).y;
        } else {
            pb = at(bLeft, 0) - at(b->xAxis, 0) * offN;
            db = -at(b->zAxis, 0);
            cornerY = at(bLeft, 0).y;
        }
    } else {
        if (!m_roadDir[w]) {
            pa = at(aLeft, 0) - at(a->xAxis, 0) * offC;
            da = -at(a->zAxis, 0);
        } else {
            pa = at(a->xAxis, n - 1) * offC + at(aRight, n - 1);
            da = -at(a->zAxis, n - 1);
        }
        if (m_roadDir[w + 1]) {
            pb = at(b->xAxis, 0) * offN + at(bRight, 0);
            db = at(b->zAxis, 0);
            cornerY = at(bRight, 0).y;
        } else {
            pb = at(bLeft, m - 1) - at(b->xAxis, m - 1) * offN;
            db = at(b->zAxis, m - 1);
            cornerY = at(bLeft, m - 1).y;
        }
    }
    // Parallel curbs (a window that doubles back on one road, which MM2
    // reaches only when a car skips two waypoints): MM2 divides by zero;
    // OpenMM2 takes the next road's curb point (inferred guard).
    const float den = db.x * da.z - db.z * da.x;
    const float t = den == 0.0f ? 0.0f : (pa.x * da.z + ((pb.z * da.x - pa.z * da.x) - pb.x * da.z)) / den;
    Vec3& corner = m_turnCorner[w];
    corner.y = cornerY;
    corner.x = db.x * t + pb.x;
    corner.z = db.z * t + pb.z;
    const float sb = m_turnSetback[w] + 12.0f;
    const float rx = m_turnRef.x - corner.x, rz = m_turnRef.z - corner.z;
    const float far = sb * sb < rz * rz + rx * rx ? 1.0f : 0.0f;
    float lateral;
    if (m_roadDir[w]) {
        const Vec3& x = at(a->xAxis, n - 1);
        lateral = (rx * x.x + rz * x.z) * m_turnDir[w];
    } else {
        const Vec3& x = at(a->xAxis, 0);
        lateral = (-x.x * rx + -x.z * rz) * m_turnDir[w];
    }
    lateral = lateral + far;
    const float adjC = calcCurrentMaxWidthAdjustment(w);
    const float adjN = calcNextMaxWidthAdjustment(w);
    const float lo = m_leftSide + m_rightSide;
    // What a road leaves: twice its last lane's (or the sidewalk's) outer
    // limit on the second side, less the car's right side, the offset and
    // the waiting traffic.
    auto roomOf = [&](const city::AiPath& p, float off, float adj) {
        const int k = p.right.numSidewalks - 1 + p.right.numLanes;
        const int idx = k < 1 ? 2 * k + 1 : 2 * k;
        const float wv = idx >= 0 && idx < 10 ? p.right.params[static_cast<std::size_t>(idx)] : 0.0f;
        const float room = (((wv + wv) - m_rightSide) - off) - adj;
        if (room < lo)
            return lo;
        return room > 9999.0f ? 9999.0f : room;
    };
    const float maxC = roomOf(*a, offC, adjC);
    lateral = lateral < lo ? lo : std::min(lateral, maxC);
    const float maxN = roomOf(*b, offN, adjN);
    return lateral < lo ? lo : std::min(lateral, maxN);
}

// --- turn circles ---------------------------------------------------------------------

int PhysicsDriver::inSharpTurn(int i) {
    // aiVehiclePhysics::InSharpTurn: is node i on a turn circle? 0 no, w + 1
    // the window's junction turn w, s + 3 sharp turn s of the node's road
    // (whose slot it then takes).
    RouteNode& node = m_nodes[static_cast<std::size_t>(i)];
    const Vec3 pos = node.pos;
    int s = -1;
    if (const city::AiPath* p = road(node.road)) {
        const PathInfo* info = roadInfo(node.road);
        const int n = static_cast<int>(p->center.size());
        const bool rd = node.road <= 2 ? m_roadDir[node.road] : false;
        for (int v = node.vert; v < n && s < 0; ++v)
            s = isSharpTurn(*p, info->sharpTurns, v, rd);
    }
    for (int w = node.road; w <= 2; ++w) {
        const city::AiPath* p = road(w);
        const PathInfo* info = roadInfo(w);
        if (p && info && s > -1) {
            const auto& turns = info->sharpTurns;
            const bool rd = m_roadDir[w];
            for (int t = s; t < static_cast<int>(turns.size()); ++t) {
                const SharpTurn& st = sharpTurn(turns, t, rd);
                const float reach = st.radius + 15.0f;
                const float dx = pos.x - st.center.x, dz = pos.z - st.center.z;
                if (dz * dz + dx * dx >= reach * reach)
                    continue;
                const float a = st.dir * (st.startDir.x * dz - st.startDir.z * dx);
                const float b = st.dir * (st.endDir.z * dx - st.endDir.x * dz);
                const bool inside = rd ? (a >= -0.01f && b >= 0.01f) : (b >= -0.01f && a >= 0.01f);
                if (inside) {
                    node.road = w;
                    return t + 3;
                }
            }
            s = 0;
        }
        const bool straightFirst = -0.2f < m_turnAngle[0] && m_turnAngle[0] < 0.2f;
        if ((w == 0 || (w == 1 && (node.turnCode == 1 || straightFirst))) &&
            m_turnDir[w] != 0.0f) {
            const float reach = m_turnRadius[w] + 15.0f;
            const float dx = pos.x - m_turnCenter[w].x, dz = pos.z - m_turnCenter[w].z;
            if (dz * dz + dx * dx < reach * reach) {
                const Vec3& sr = m_turnStartDir[w];
                const Vec3& er = m_turnEndDir[w];
                const float a = m_turnDir[w] * (sr.x * dz - sr.z * dx);
                const float b = m_turnDir[w] * (er.z * dx - er.x * dz);
                if (a >= -0.01f && b >= 0.01f)
                    return w + 1;
            }
        }
    }
    return 0;
}

void PhysicsDriver::saveTurnTarget(int i, bool calcAngle) {
    // aiVehiclePhysics::SaveTurnTarget.
    RouteNode& n = m_nodes[static_cast<std::size_t>(i)];
    const RouteNode& p = m_nodes[static_cast<std::size_t>(i - 1)];
    n.kind = 1;
    n.pos.y += 1.0f;
    const float dz = p.pos.z - n.pos.z, dx = p.pos.x - n.pos.x;
    n.dist = std::sqrt(dz * dz + dx * dx) + p.dist;
    if (calcAngle)
        nodeTurnAndDistance(i);
}

int PhysicsDriver::calcSharpTurnTarget(int& idx, int turnNode) {
    // aiVehiclePhysics::CalcSharpTurnTarget: targets on the circle of the
    // turn coded in node `turnNode`: its entry while the previous target is
    // before it (0), else points along it in tenths of its length (1 once
    // past its middle).
    const RouteNode tn = m_nodes[static_cast<std::size_t>(turnNode)];
    const int code = tn.turnCode;
    auto prevPos = [&]() { return m_nodes[static_cast<std::size_t>(idx - 1)].pos; };
    if (code >= 2) {
        const int w = tn.road;
        const int s = code - 2;
        const city::AiPath* p = road(w);
        const PathInfo* info = roadInfo(w);
        if (!p || !info || s >= static_cast<int>(info->sharpTurns.size()))
            return 0;
        const bool rd = w <= 2 ? m_roadDir[w] : false;
        const auto& turns = info->sharpTurns;
        const int vi = sharpTurnVertIndex(*p, turns, s, rd);
        const SharpTurn& st = sharpTurn(turns, s, rd);
        const float dir = st.dir, r = st.radius, ang = st.angle;
        RouteNode& node = m_nodes[static_cast<std::size_t>(idx)];
        node.turnCode = w;
        const int n = arcSteps(ang * r * dir);
        const float total = ang * dir;
        const float step = total / static_cast<float>(n);
        const int verts = static_cast<int>(p->center.size());
        const int k = std::clamp(rd ? vi : verts - vi - 1, 0, verts - 1);
        const Vec3& x = at(p->xAxis, k);
        const Vec3& z = at(p->zAxis, k);
        const Vec3& c = st.center;
        auto arcPoint = [&](float a) {
            const float sr = std::sin(a) * r;
            const float cr = std::cos(a) * r * dir;
            return Vec3{c.x - z.x * sr + x.x * cr, c.y, c.z - z.z * sr + x.z * cr};
        };
        Vec3 p0;
        if (rd)
            p0 = c + Vec3{x.x * r * dir, 0.0f, x.z * r * dir};
        else
            p0 = arcPoint(static_cast<float>(n) * step);
        const Vec3 u = normalized(p0 - c);
        const Vec3 pp = prevPos();
        const float dx = pp.x - c.x, dz = pp.z - c.z;
        float theta = std::atan2(u.x * dz - u.z * dx, u.x * dx + u.z * dz);
        if ((dir < 0.0f && rd) || (dir > 0.0f && !rd))
            theta = -theta;
        if (theta < -0.1f) {
            node.pos = p0;
            node.vert = pathRoadVertice(*p, node.pos, rd ? 1 : 0);
            node.road = tn.road;
            saveTurnTarget(idx, true);
            return 0;
        }
        for (int i = 1; i <= n; ++i) {
            if (!(theta < static_cast<float>(i) * step - step * 0.5f))
                continue;
            const float a = static_cast<float>(rd ? i : n - i) * step;
            node.pos = arcPoint(a);
            node.road = w;
            node.vert = pathRoadVertice(*p, node.pos, rd ? 1 : 0);
            if (node.vert >= verts) {
                node.road += 1;
                node.vert = 0;
            }
            saveTurnTarget(idx, false);
            return i > 1 - static_cast<int>(static_cast<float>(n) * -0.5f) ? 1 : 0;
        }
        // Past the circle: the previous node takes the vertex of its end.
        m_nodes[static_cast<std::size_t>(idx - 1)].vert = pathIndex(*p, arcPoint(total));
        return 1;
    }
    // A junction turn of the window.
    const int w = code;
    if (w < 0 || w > 1)
        return 0;
    const city::AiPath* a = road(w);
    const city::AiPath* b = road(w + 1);
    if (!a)
        return 0;
    const int na = static_cast<int>(a->center.size());
    Vec3 left, fwd;
    if (m_roadDir[w]) {
        left = at(a->xAxis, na - 1);
        fwd = at(a->zAxis, na - 1);
    } else {
        left = -at(a->xAxis, 0);
        fwd = -at(a->zAxis, 0);
    }
    const float r = m_turnRadius[w], dir = m_turnDir[w], ang = m_turnAngle[w];
    const Vec3& c = m_turnCenter[w];
    RouteNode* node = &m_nodes[static_cast<std::size_t>(idx)];
    const Vec3 p0 = left * (r * dir) + c;
    node->turnCode = code;
    const Vec3& sr = m_turnStartDir[w];
    const Vec3 pp = prevPos();
    const float dx = pp.x - c.x, dz = pp.z - c.z;
    float theta = std::atan2(sr.x * dz - sr.z * dx, dx * sr.x + dz * sr.z);
    if (dir < 0.0f)
        theta = -theta;
    if (theta < -0.1f) {
        node->pos = p0;
        saveTurnTarget(idx, true);
        node->road = node->turnCode;
        node->vert = na - 1;
        node->kind = 0;
        return 0;
    }
    const int n = arcSteps(r * dir * ang);
    const float step = (dir * ang) / static_cast<float>(n);
    for (int i = 1; i <= n; ++i) {
        if (!(theta < static_cast<float>(i) * step - step * 0.5f))
            continue;
        node = &m_nodes[static_cast<std::size_t>(idx)];
        const float ai = static_cast<float>(i) * step;
        const float s = std::sin(ai) * r;
        const float cc = std::cos(ai) * r * dir;
        node->pos = {(c.x - fwd.x * s) + left.x * cc, (c.y - fwd.y * s) + left.y * cc,
                     (c.z - fwd.z * s) + left.z * cc};
        saveTurnTarget(idx, false);
        if (dir * ang * 0.5f <= ai) {
            node->road = w + 1;
            node->vert = b ? pathRoadVertice(*b, node->pos, m_roadDir[w + 1] ? 1 : 0) : 0;
            if (b && node->vert >= static_cast<int>(b->center.size())) {
                node->road += 1;
                node->vert = 0;
            }
            if (w + 1 >= m_numWayPtRoads)
                m_toDestination = true;
        } else {
            node->road = w;
            node->vert = na - 1;
        }
        node->turnCode = code;
        if (2 * i > n && b && pathIsPosOnRoad(*b, node->pos, 0.0f) < 3) {
            node->turnCode += 1;
            m_turnRef = node->pos;
            return 1;
        }
        if (i < n) {
            if (idx + 1 >= kMaxNodes)
                return 1;
            ++idx;
        }
    }
    return 1;
}

// --- the next target -----------------------------------------------------------------

namespace {

// A road's arrays for the target functions (aiPath +0x104 centre, +0x108 X,
// +0x110 Z, +0x80 / +0xe4 curbs).
struct RoadArrays {
    const city::AiPath* p = nullptr;
    int n = 0;
    bool divided = false;
    float halfWidth = 0.0f;
    const Vec3& c(int k) const { return at(p->center, k); }
    const Vec3& x(int k) const { return at(p->xAxis, k); }
    const Vec3& z(int k) const { return at(p->zAxis, k); }
    const Vec3& b80(int k) const { return at(pathBoundary(p->left, 0), k); }
    const Vec3& be4(int k) const { return at(pathBoundary(p->right, 0), k); }
    // From the 0xe4 curb towards the 0x80 one.
    Vec3 w(int k) const { return normalized(c(k) - be4(k)); }
};

RoadArrays arraysOf(const city::AiPath* p) {
    RoadArrays a;
    a.p = p;
    if (p) {
        a.n = static_cast<int>(p->center.size());
        a.divided = (p->flags & 1) == 1;
        a.halfWidth = p->halfWidth;
    }
    return a;
}

} // namespace

void PhysicsDriver::calcRoadTarget(int i, Vec3& from) {
    // aiVehiclePhysics::CalcRoadTarget: the farthest point down the window's
    // roads that can be reached in a straight line between the curbs. The
    // directions to the left and right curb points (moved in by the car's
    // side distances + 1 m; the centre line a curb on a divided road) narrow
    // vertex by vertex; when the left one swings past the right (the road
    // bends right) the target is the right limit's curb point, and the other
    // way round. A road that runs out first leaves the target at its end,
    // across the road where the car is (or by the limit that stopped the
    // walk).
    const auto ui = static_cast<std::size_t>(i);
    RouteNode& node = m_nodes[ui];
    const RouteNode prev = m_nodes[ui - 1];
    const Vec3 P = prev.pos;
    from = P;
    auto dirOf = [&](int slot) { return slot >= 0 && slot <= 2 ? m_roadDir[slot] : false; };
    auto turnAngleAt = [&](int slot) {
        // MM2's two-entry arrays run on into the turn directions.
        return slot < 2 ? m_turnAngle[slot] : m_turnDir[slot - 2];
    };
    auto turnCornerAt = [&](int slot) { return slot < 2 ? m_turnCorner[slot] : m_turnCenter[slot - 2]; };

    int r = prev.road;
    int v = prev.vert;
    RoadArrays ra = arraysOf(road(r));
    if (!ra.p) {
        setTargetPtToDestination(i);
        return;
    }
    if (i > 1 && m_nodes[ui - 2].kind != 1) {
        if (v + 1 < ra.n) {
            ++v;
        } else if (road(r + 1)) {
            ++r;
            v = 0;
            ra = arraysOf(road(r));
        }
    }
    // The setup frame: the vertex itself on a road driven with the index,
    // else the vertex counted from the other end (one past the geometry
    // vertex, as coded).
    const int setupIdx = ra.n - v == ra.n ? ra.n - 1 : ra.n - v;
    const bool setupFwd = dirOf(r);
    const int a0 = ra.n - 1 - v;
    const float lat0 = setupFwd ? dotXZ(ra.x(v), ra.c(v) - P) : dotXZ(ra.x(a0), P - ra.c(a0));

    auto lPoint = [&](const RoadArrays& q, bool fwd, int vv) {
        if (fwd) {
            const Vec3 base = q.divided && lat0 >= 0.0f ? q.c(vv) : q.b80(vv);
            return base - q.w(vv) * (m_leftSide + 1.0f);
        }
        const int k = q.n - 1 - vv;
        const Vec3 base = q.divided && lat0 >= 0.0f ? q.c(k) : q.be4(k);
        return base + q.w(k) * (m_leftSide + 1.0f);
    };
    auto rPoint = [&](const RoadArrays& q, bool fwd, int vv) {
        if (fwd) {
            const Vec3 base = q.divided && lat0 < 0.0f ? q.c(vv) : q.be4(vv);
            return base + q.w(vv) * (m_rightSide + 1.0f);
        }
        const int k = q.n - 1 - vv;
        const Vec3 base = q.divided && lat0 < 0.0f ? q.c(k) : q.b80(k);
        return base - q.w(k) * (m_rightSide + 1.0f);
    };
    // The angle of a point seen from P in a road frame (positive to the
    // right).
    auto frameAngle = [&](const Vec3& pt, const RoadArrays& fr, bool fwd, int k, bool latRule, float latValue,
                          bool clampLon) {
        const Vec3 d = pt - P;
        float lat = fwd ? -dotXZ(fr.x(k), d) : dotXZ(fr.x(k), d);
        float lon = fwd ? -dotXZ(fr.z(k), d) : dotXZ(fr.z(k), d);
        if (latRule && -0.01f < lat && lat < 0.01f)
            lat = latValue;
        if (clampLon && lon < 1.0f)
            lon = 1.0f;
        return std::atan2(lat, lon);
    };

    // The funnel at the start vertex.
    const int setupK = setupFwd ? v : setupIdx;
    float aL = frameAngle(lPoint(ra, setupFwd, v), ra, setupFwd, setupK, false, 0.0f, i == 1);
    float aR = frameAngle(rPoint(ra, setupFwd, v), ra, setupFwd, setupK, false, 0.0f, i == 1);
    int rL = r, vL = v, rR = r, vR = v;

    bool placed = false;  // a closure or a junction corner
    bool limited = false; // a placement after the walk
    // The next vertex; at the end of a road into a sharp junction, its
    // corner.
    if (v + 1 == ra.n && road(r + 1)) {
        const float ta = turnAngleAt(r);
        if (ta < -0.7f || 0.7f < ta) {
            node.pos = turnCornerAt(r);
            placed = true;
        } else if (r + 1 != m_numWayPtRoads) {
            ++r;
            v = 0;
            ra = arraysOf(road(r));
        }
    } else if (v + 1 != ra.n) {
        ++v;
    }

    if (!placed) {
        // The target by default: the end of the road, as far across it as
        // the car (or `from`) is.
        const bool fwd = dirOf(r);
        const float h = (ra.halfWidth - m_rightSide) - 1.0f;
        float off;
        if (fwd)
            off = (ra.c(v).x - from.x) * ra.x(v).x + (ra.c(v).z - from.z) * ra.x(v).z;
        else
            off = -ra.x(setupIdx).z * (ra.c(ra.n - 1 - v).z - from.z) +
                  -ra.x(setupIdx).x * (ra.c(ra.n - 1 - v).x - from.x);
        if (i == 1) {
            if (v >= 1)
                m_roadOffset = clampAcross(off, h);
            off = m_roadOffset;
        } else {
            off = off < -h ? -h : (h < off ? h : off);
        }
        const float t = off / h;
        if (fwd) {
            const Vec3 edge = ra.be4(ra.n - 1) + ra.x(v) * (m_rightSide + 1.0f);
            const Vec3& c = ra.c(ra.n - 1);
            node.pos = {(edge.x - c.x) * t + c.x, (edge.y - c.y) * t + c.y, (edge.z - c.z) * t + c.z};
        } else {
            const Vec3 edge = ra.b80(0) - ra.x(0) * (m_rightSide + 1.0f);
            const Vec3& c = ra.c(0);
            node.pos = {(edge.x - c.x) * t + c.x, (edge.y - c.y) * t + c.y, (edge.z - c.z) * t + c.z};
        }

        // The walk, in the previous target's frame.
        const int q = prev.road;
        const RoadArrays qa = arraysOf(road(q));
        const bool qfwd = dirOf(q);
        int qk = qfwd ? prev.vert : qa.n - prev.vert;
        if (!qfwd && qk == qa.n)
            qk = qa.n - 1;
        bool walking = qa.p != nullptr;
        while (walking && v < ra.n) {
            const bool rfwd = dirOf(r);
            const float fL = frameAngle(lPoint(ra, rfwd, v), qa, qfwd, qk, true, -1.0f, true);
            if (fL > aR) {
                // The left side swung past the right limit: the road bends
                // right; aim at the right limit's curb point.
                const RoadArrays la = arraysOf(road(rR));
                node.pos = rPoint(la, dirOf(rR), vR);
                if (rfwd && dirOf(rR) && vR == la.n - 1 && turnAngleAt(rR) > 0.7f)
                    node.pos = turnCornerAt(rR);
                r = rR;
                v = vR;
                placed = true;
                break;
            }
            if (fL > aL - 0.001f) {
                aL = fL;
                rL = r;
                vL = v;
            }
            const float fR = frameAngle(rPoint(ra, rfwd, v), qa, qfwd, qk, true, 0.0f, true);
            if (fR < aL) {
                const RoadArrays la = arraysOf(road(rL));
                node.pos = lPoint(la, dirOf(rL), vL);
                if (rfwd && dirOf(rL) && vL == la.n - 1 && turnAngleAt(rL) < -0.7f)
                    node.pos = turnCornerAt(rL);
                r = rL;
                v = vL;
                placed = true;
                break;
            }
            if (fR < aR + 0.001f) {
                aR = fR;
                rR = r;
                vR = v;
            }
            if (v == ra.n - 1) {
                const float ta = turnAngleAt(r);
                if (r < 2 && rL == r && vL == v && rR == r && vR == v && (ta < -0.7f || 0.7f < ta)) {
                    node.pos = turnCornerAt(r);
                    placed = true;
                    break;
                }
                if (params.lookAhead <= distXZ(from, node.pos) + prev.dist || r >= 2)
                    break;
                if (m_numWayPtRoads <= r + 1) {
                    if (m_destCompType == kIntersectionComponent) {
                        setTargetPtToDestination(i);
                        return;
                    }
                    m_toDestination = true;
                    break;
                }
                if (!road(r + 1))
                    break;
                // On into the next road: its end, as far across as the
                // target so far.
                ++r;
                ra = arraysOf(road(r));
                const float hn = (ra.halfWidth - m_rightSide) - 1.0f;
                if (!dirOf(r)) {
                    const int last = ra.n - 1;
                    float o = dotXZ(ra.c(last) - node.pos, -ra.x(last));
                    o = clampAcross(o, hn) / hn;
                    const Vec3 edge = ra.b80(0) - ra.x(0) * (m_rightSide + 1.0f);
                    const Vec3& c = ra.c(0);
                    node.pos = {(edge.x - c.x) * o + c.x, (edge.y - c.y) * o + c.y, (edge.z - c.z) * o + c.z};
                } else {
                    float o = dotXZ(ra.c(0) - node.pos, ra.x(0));
                    o = clampAcross(o, hn) / hn;
                    const int last = ra.n - 1;
                    const Vec3 edge = ra.be4(last) + ra.x(last) * (m_rightSide + 1.0f);
                    const Vec3& c = ra.c(last);
                    node.pos = {(edge.x - c.x) * o + c.x, (edge.y - c.y) * o + c.y, (edge.z - c.z) * o + c.z};
                }
                v = -1;
            }
            ++v;
        }

        if (!placed) {
            // The walk ran out: keep the car's distance from the curb by the
            // limit vertex, the first that applies of the right limit on a
            // road driven against its index, the left one there, the right
            // one on a road driven with it, the left one there.
            auto valid = [&](int lr, int lv) {
                const RoadArrays la = arraysOf(road(lr));
                return la.p && (lr < r || lv < la.n - 1);
            };
            const int pv = prev.vert;
            const int kp = pv < 1 ? 1 : pv;
            // P's distance from the right curb of the previous road (the
            // reversed-road placements).
            auto rightKeepReverse = [&](const RoadArrays& la, int k) {
                if (!qfwd) {
                    const int qq = qa.n - kp;
                    const float d = dotXZ(P - qa.b80(qq - 1), -qa.x(qq));
                    return la.b80(k) - qa.x(qq) * d;
                }
                const float d = dotXZ(P - qa.be4(kp), qa.x(kp));
                return la.b80(k) + qa.x(kp) * d;
            };
            if (!dirOf(rR) && valid(rR, vR)) {
                const RoadArrays la = arraysOf(road(rR));
                const int k = la.n - 1 - vR;
                if (!la.divided || 0.0f <= lat0)
                    node.pos = rightKeepReverse(la, k);
                else
                    node.pos = la.c(k) - la.w(k) * (m_rightSide + 1.0f);
                r = rR;
                v = vR;
                limited = true;
            } else if (!dirOf(rL) && valid(rL, vL)) {
                const RoadArrays la = arraysOf(road(rL));
                const int k = la.n - 1 - vL;
                if (!la.divided || lat0 <= 0.0f)
                    node.pos = rightKeepReverse(la, k);
                else
                    node.pos = la.c(k) + la.w(k) * (m_rightSide + 1.0f);
                r = rL;
                v = vL;
                limited = true;
            } else if (dirOf(rR) && valid(rR, vR)) {
                const RoadArrays la = arraysOf(road(rR));
                if (!la.divided || lat0 < 0.0f) {
                    float d;
                    if (!qfwd) {
                        const int kk = qa.n - pv - 1;
                        d = dotXZ(P - qa.b80(kk), -qa.x(kk));
                    } else {
                        d = dotXZ(P - qa.be4(pv), qa.x(pv));
                    }
                    if (d < m_rightSide)
                        d = m_rightSide;
                    node.pos = la.be4(vR) + la.x(vR) * d;
                } else {
                    node.pos = la.c(vR) + la.w(vR) * (m_rightSide + 1.0f);
                }
                r = rR;
                v = vR;
                limited = true;
            } else if (dirOf(rL) && valid(rL, vL)) {
                const RoadArrays la = arraysOf(road(rL));
                if (!la.divided || 0.0f <= lat0) {
                    float d;
                    if (!qfwd) {
                        const int kk = qa.n - pv - 1;
                        d = dotXZ(P - qa.be4(kk), qa.x(kk));
                    } else {
                        d = dotXZ(P - qa.b80(pv), -qa.x(pv));
                    }
                    if (d < m_leftSide)
                        d = m_leftSide;
                    node.pos = la.b80(vL) - la.x(vL) * d;
                } else {
                    node.pos = la.c(vL) - la.x(vL) * (m_leftSide + 1.0f);
                }
                r = rL;
                v = vL;
                limited = true;
            }
            if (limited)
                m_toDestination = false;
        }
    }

    node.kind = 0;
    node.vert = v;
    node.road = r;
    node.turnCode = r;
    node.pos.y += 1.0f;
    node.dist = distXZ(P, node.pos) + prev.dist;
    nodeTurnAndDistance(i);
}

void PhysicsDriver::calcDestinationTarget(int i, Vec3& viewOrigin) {
    // aiVehiclePhysics::CalcDestinationTarget: on the destination's road, the
    // same funnel along the road the target is on (that road only), the
    // destination itself once it is no farther than the target found.
    const auto ui = static_cast<std::size_t>(i);
    RouteNode& node = m_nodes[ui];
    const RouteNode prev = m_nodes[ui - 1];
    const Vec3 P = prev.pos;
    viewOrigin = P;
    auto dirOf = [&](int slot) { return slot >= 0 && slot <= 2 ? m_roadDir[slot] : false; };
    int r = prev.road;
    int v = prev.vert;
    RoadArrays ra = arraysOf(road(r));
    if (!ra.p) {
        setTargetPtToDestination(i);
        return;
    }
    if (i > 1 && m_nodes[ui - 2].kind != 1) {
        ++v;
        if (v == ra.n) {
            if (road(r + 1)) {
                ++r;
                v = 0;
                ra = arraysOf(road(r));
            } else {
                v = ra.n - 1;
            }
        }
    }
    if (m_destCompType == kNoComponent || m_destCompType == kIntersectionComponent) {
        setTargetPtToDestination(i);
        return;
    }
    if (m_destCompType != kRoadComponent && m_destCompType != kShortcutComponent)
        return;

    const float L = m_leftSide, R = m_rightSide;
    const int revIdx = ra.n - v;
    const int dirIdx = revIdx == ra.n ? ra.n - 1 : revIdx;
    const int k0 = revIdx - 1;
    float angL, angR;
    const bool fwd0 = dirOf(r);
    if (!fwd0) {
        const Vec3 nrm = normalized(ra.c(k0) - ra.be4(k0));
        const Vec3 pl = ra.be4(k0) + nrm * (L + 1.0f);
        Vec3 d = pl - P;
        float a = dotXZ(d, ra.x(dirIdx)), b = dotXZ(d, ra.z(dirIdx));
        if (a > 0.0f && i == 1) {
            viewOrigin = m_nodes[0].pos + ra.x(dirIdx) * a;
            a = 0.0f;
        }
        if (b < 1.0f && i == 1)
            b = 1.0f;
        angL = std::atan2(a, b);
        const Vec3 pr = ra.b80(k0) - nrm * (R + 1.0f);
        d = pr - P;
        a = dotXZ(d, ra.x(dirIdx));
        b = dotXZ(d, ra.z(dirIdx));
        if (a < 0.0f && i == 1) {
            viewOrigin = m_nodes[0].pos - ra.x(dirIdx) * a;
            a = 0.0f;
        }
        if (b < 1.0f && i == 1)
            b = 1.0f;
        angR = std::atan2(a, b);
    } else {
        const Vec3 pl = ra.divided ? ra.c(v) - ra.x(v) * (L + 1.0f) : ra.b80(v) - ra.x(v) * L;
        Vec3 d = pl - P;
        float a = -dotXZ(d, ra.x(v)), b = -dotXZ(d, ra.z(v));
        if (-0.01f < a && a < 0.01f)
            a = -1.0f;
        angL = std::atan2(a, b);
        const Vec3 pr = ra.be4(v) + ra.x(v) * R;
        d = pr - P;
        a = -dotXZ(d, ra.x(v));
        b = -dotXZ(d, ra.z(v));
        if (-0.01f < a && a < 0.01f)
            a = 1.0f;
        angR = std::atan2(a, b);
    }
    int leftV = v, rightV = v, leftSlot = r, rightSlot = r;
    bool found = false;
    int finalSlot = r;

    int j = v + 1;
    if (j == ra.n) {
        if (road(r + 1)) {
            ++r;
            j = 0;
            ra = arraysOf(road(r));
        } else {
            j = ra.n;
        }
    }
    // The previous target's frame.
    const int q = prev.road;
    const RoadArrays qa = arraysOf(road(q));
    const bool qfwd = dirOf(q);
    const int qk = qfwd ? prev.vert : std::min(qa.n - prev.vert - 1, qa.n - 1);
    auto project = [&](const Vec3& pt, float& a, float& b) {
        const Vec3 d = pt - viewOrigin;
        if (qfwd) {
            a = -dotXZ(d, qa.x(qk));
            b = -dotXZ(d, qa.z(qk));
        } else {
            a = dotXZ(d, qa.x(qk));
            b = dotXZ(d, qa.z(qk));
        }
    };
    const bool fwd = dirOf(r);
    for (; j < ra.n && qa.p; ++j) {
        float a, b;
        int kj = fwd ? j : ra.n - j - 1;
        const Vec3 pl = fwd ? (ra.divided ? ra.c(j) : ra.b80(j)) - ra.x(j) * (L + 1.0f)
                            : ra.be4(kj) + normalized(ra.c(kj) - ra.be4(kj)) * (L + 1.0f);
        project(pl, a, b);
        if (-0.01f < a && a < 0.01f)
            a = -1.0f;
        if (!fwd && b < 1.0f)
            b = 1.0f;
        const float al = std::atan2(a, b);
        if (al > angR) {
            // Crossed to the right: the right limit's curb point.
            found = true;
            const int s = rightSlot;
            const RoadArrays sa = arraysOf(road(s));
            if (fwd) {
                int rv = rightV;
                if (i > 1 && rightSlot == prev.road && rv == prev.vert && rv + 1 <= sa.n - 1)
                    rv = rv + 1;
                rightV = rv;
                node.pos = (sa.divided ? sa.c(rv) : sa.be4(rv)) + sa.x(rv) * (R + 1.0f);
            } else if (dirOf(s)) {
                node.pos = sa.be4(rightV) + normalized(sa.c(rightV) - sa.be4(rightV)) * (R + 1.0f);
            } else {
                const int kk = sa.n - rightV - 1;
                node.pos = sa.b80(kk) + normalized(sa.c(kk) - sa.b80(kk)) * (R + 1.0f);
            }
            finalSlot = rightSlot;
            break;
        }
        if (al > angL - 0.001f) {
            angL = al;
            leftV = j;
            leftSlot = r;
        }
        const Vec3 pr = fwd ? ra.be4(j) + ra.x(j) * (R + 1.0f) : ra.b80(kj) - ra.x(dirIdx) * (R + 1.0f);
        project(pr, a, b);
        if (-0.01f < a && a < 0.01f)
            a = 1.0f;
        if (!fwd && b < 1.0f)
            b = 1.0f;
        const float ar = std::atan2(a, b);
        if (ar < angL) {
            // Crossed to the left: the left limit's curb point.
            found = true;
            const int s = leftSlot;
            const RoadArrays sa = arraysOf(road(s));
            if (fwd) {
                int lv = leftV;
                if (i > 1 && leftSlot == prev.road && lv == prev.vert && lv + 1 <= sa.n - 1)
                    lv = lv + 1;
                leftV = lv;
                node.pos = (sa.divided ? sa.c(lv) : sa.b80(lv)) - sa.x(lv) * (L + 1.0f);
            } else if (!dirOf(s)) {
                const int kk = sa.n - leftV - 1;
                node.pos = sa.be4(kk) + normalized(sa.c(kk) - sa.be4(kk)) * (L + 1.0f);
            } else {
                node.pos = (sa.divided ? sa.c(leftV) : sa.b80(leftV)) +
                           normalized(sa.c(leftV) - sa.b80(leftV)) * (L + 1.0f);
            }
            finalSlot = leftSlot;
            break;
        }
        if (ar < angR + 0.001f) {
            angR = ar;
            rightV = j;
            rightSlot = r;
        }
    }
    int finalV = std::min(leftV, rightV);
    if (!found) {
        finalSlot = r;
        auto caseRoad = [&](int slot) { return arraysOf(road(slot)); };
        const RoadArrays rs = caseRoad(rightSlot);
        const RoadArrays ls = caseRoad(leftSlot);
        if (rs.p && !dirOf(rightSlot) && rightV < rs.n - 1) {
            const float w = (rs.halfWidth - R) - 1.0f;
            const float s = clampAcross(dotXZ(P - rs.c(rs.n - 1), rs.x(rs.n - 1)), w);
            const int kk = rs.n - rightV - 1;
            const float off = w - w * (s / w);
            node.pos = rs.b80(kk) - rs.x(kk) * (R + 1.0f) - rs.x(kk) * off;
            finalSlot = rightSlot;
            finalV = rightV;
        } else if (ls.p && !dirOf(leftSlot) && leftV < ls.n - 1) {
            const float w = (ls.halfWidth - R) - 1.0f;
            const float s = clampAcross(dotXZ(P - ls.c(ls.n - 1), ls.x(ls.n - 1)), w);
            const int kk = ls.n - leftV - 1;
            const float f = (s / w + 1.0f) * ((ls.halfWidth - L) - 1.0f);
            node.pos = ls.be4(kk) + ls.x(kk) * (R + 1.0f) + ls.x(kk) * f;
            finalSlot = leftSlot;
            finalV = leftV;
        } else if (rs.p && dirOf(rightSlot) && rightV < rs.n - 1) {
            const float w = (rs.halfWidth - R) - 1.0f;
            const float s = clampAcross(dotXZ(rs.c(0) - P, rs.x(0)), w);
            const float off = w - w * (s / w);
            node.pos = rs.be4(rightV) + rs.x(rightV) * (R + 1.0f) + rs.x(rightV) * off;
            finalSlot = rightSlot;
            finalV = rightV;
        } else if (ls.p && dirOf(leftSlot) && leftV < ls.n - 1) {
            const float w = (ls.halfWidth - L) - 1.0f;
            const float s = clampAcross(dotXZ(ls.c(0) - P, ls.x(0)), w);
            if (ls.divided) {
                node.pos = ls.c(leftV) - ls.x(leftV) * (L + 1.0f);
            } else {
                const float off = w - w * (s / w);
                node.pos = ls.b80(leftV) - ls.x(leftV) * (L + 1.0f) - ls.x(leftV) * off;
            }
            finalSlot = leftSlot;
            finalV = leftV;
        }
    }
    // The destination once it is no farther along than the target.
    if (const city::AiPath* fp = road(finalSlot)) {
        const int dv = pathRoadVertice(*fp, m_dest, dirOf(finalSlot) ? 1 : 0);
        if (finalV >= dv) {
            setTargetPtToDestination(i);
            return;
        }
    }
    node.kind = 0;
    node.vert = finalV;
    node.road = finalSlot;
    node.turnCode = finalSlot;
    node.pos.y += 1.0f;
    const float dx = P.x - node.pos.x, dz = P.z - node.pos.z;
    node.dist = std::sqrt(dx * dx + dz * dz) + prev.dist;
    nodeTurnAndDistance(i);
}

} // namespace mm2::ai
