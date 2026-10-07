#include "game/session/Gate.h"

#include <algorithm>
#include <cmath>

namespace mm2::game::session {

void calculateGatePoints(const Vec3& p, float h, float w, Vec2& a, Vec2& b) {
    const float s = std::sin(h), c = std::cos(h);
    a = {p.x + c * w, p.z + s * w};
    b = {p.x - c * w, p.z - s * w};
}

bool lineIntersect(Vec2 p1, Vec2 p2, Vec2 q1, Vec2 q2, float tol) {
    // Bounding boxes of both segments, grown by the tolerance, must overlap.
    const float pMinX = std::min(p1.x, p2.x) - tol, pMaxX = std::max(p1.x, p2.x) + tol;
    const float pMinY = std::min(p1.y, p2.y) - tol, pMaxY = std::max(p1.y, p2.y) + tol;
    const float qMinX = std::min(q1.x, q2.x) - tol, qMaxX = std::max(q1.x, q2.x) + tol;
    const float qMinY = std::min(q1.y, q2.y) - tol, qMaxY = std::max(q1.y, q2.y) + tol;
    if (pMaxX < qMinX || qMaxX < pMinX || pMaxY < qMinY || qMaxY < pMinY)
        return false;
    // Intersection point of the two infinite lines, then inside both boxes.
    const Vec2 r = p2 - p1, s = q2 - q1;
    if (r.mag2() < 1e-12f || s.mag2() < 1e-12f)
        return false; // a point never crosses a gate
    const float denom = r.cross(s);
    if (std::abs(denom) < 1e-9f) {
        // Parallel: only touching when collinear and overlapping.
        return std::abs((q1 - p1).cross(r)) < 1e-6f;
    }
    const float t = (q1 - p1).cross(s) / denom;
    const Vec2 x = p1 + r * t;
    return x.x >= pMinX && x.x <= pMaxX && x.y >= pMinY && x.y <= pMaxY && x.x >= qMinX && x.x <= qMaxX &&
           x.y >= qMinY && x.y <= qMaxY;
}

bool gateHit(const Checkpoint& cp, const Vec3& prev, const Mat34& car, const CarExtent& e) {
    const Vec3 pos = car.m3;
    // 1. The path travelled since the last test, 2. the car's long axis (both
    // with the car's half width as slack, as WPHit passes Size.x), 3. its
    // lateral axis without slack.
    if (lineIntersect({prev.x, prev.z}, {pos.x, pos.z}, cp.gateA, cp.gateB, e.halfWidth))
        return true;
    const Vec3 front = car.transform({0, 0, -e.halfLength}), back = car.transform({0, 0, e.halfLength});
    if (lineIntersect({front.x, front.z}, {back.x, back.z}, cp.gateA, cp.gateB, e.halfWidth))
        return true;
    const Vec3 left = car.transform({-e.halfWidth, 0, 0}), right = car.transform({e.halfWidth, 0, 0});
    return lineIntersect({left.x, left.z}, {right.x, right.z}, cp.gateA, cp.gateB, 0.0f);
}

bool aiGateHit(const Checkpoint& cp, const Vec3& prev, const Vec3& pos, float radius) {
    const float dx = pos.x - cp.position.x, dz = pos.z - cp.position.z;
    if (dx * dx + dz * dz < radius * radius)
        return true;
    return lineIntersect({prev.x, prev.z}, {pos.x, pos.z}, cp.gateA, cp.gateB, 0.0f);
}

Checkpoint makeCheckpoint(const Vec3& position, float headingDeg, float radius) {
    Checkpoint cp;
    cp.position = position;
    cp.headingDeg = headingDeg;
    cp.radius = radius > 0.0f ? radius : 15.0f;
    calculateGatePoints(position, headingDeg * kDegToRad, cp.radius, cp.gateA, cp.gateB);
    return cp;
}

} // namespace mm2::game::session
