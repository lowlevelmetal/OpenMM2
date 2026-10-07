#include "game/session/Gate.h"

#include <algorithm>
#include <cmath>

namespace mm2::game::session {

void calculateGatePoints(const Vec3& p, float headingDeg, float radius, Vec2& a, Vec2& b) {
    const float h = headingDeg * 0.017453292f;
    const float c = std::cos(h) * radius, s = std::sin(h) * radius;
    a = {c + p.x, s + p.z};
    b = {p.x - c, p.z - s};
}

bool lineIntersect(Vec2 p1, Vec2 p2, Vec2 q1, Vec2 q2, float tol) {
    const float pMinX = std::min(p1.x, p2.x) - tol, pMinY = std::min(p1.y, p2.y) - tol;
    const float pMaxX = std::max(p1.x, p2.x) + tol, pMaxY = std::max(p1.y, p2.y) + tol;
    const float qMinX = std::min(q1.x, q2.x) - tol, qMinY = std::min(q1.y, q2.y) - tol;
    const float qMaxX = std::max(q1.x, q2.x) + tol, qMaxY = std::max(q1.y, q2.y) + tol;

    // Lines y = m x + c; a segment with no x extent counts as slope 0 here
    // and is handled as the vertical line through its first point below.
    const float pdx = p1.x - p2.x, qdx = q1.x - q2.x;
    const float pm = pdx == 0.0f ? 0.0f : (p1.y - p2.y) / pdx;
    const float qm = qdx == 0.0f ? 0.0f : (q1.y - q2.y) / qdx;
    const float pc = p1.y - pm * p1.x;
    const float qc = q1.y - qm * q1.x;
    float x = 0.0f, y = 0.0f;
    if (pdx == 0.0f) {
        x = p1.x;
        y = qm * p1.x + qc;
    } else {
        if (qdx == 0.0f) {
            x = q1.x;
        } else {
            // Parallel lines never meet (the original divides by zero and
            // the comparisons below fail on the infinity / NaN).
            if (pm == qm)
                return false;
            x = (qc - pc) / (pm - qm);
        }
        y = pm * x + pc;
    }
    return pMinX <= x && x <= pMaxX && pMinY <= y && y <= pMaxY && qMinX <= x && x <= qMaxX && qMinY <= y &&
           y <= qMaxY;
}

bool planeHit(const Checkpoint& cp, const Mat34& m, Vec2 p1, Vec2 p2, const Vec3& size) {
    if (lineIntersect(p2, p1, cp.gateA, cp.gateB, size.x))
        return true;
    const Vec3& pos = m.m3;
    const Vec2 up1{-size.y * m.m1.x + pos.x, -size.y * m.m1.z + pos.z};
    const Vec2 up2{m.m1.x * size.y + pos.x, m.m1.z * size.y + pos.z};
    if (lineIntersect(up1, up2, cp.gateA, cp.gateB, size.x))
        return true;
    const Vec2 side1{m.m0.x * -size.x + pos.x, -size.x * m.m0.z + pos.z};
    const Vec2 side2{m.m0.x * size.x + pos.x, m.m0.z * size.x + pos.z};
    return lineIntersect(side1, side2, cp.gateA, cp.gateB, 0.0f);
}

bool radiusHit(const Checkpoint& cp, const Vec3& p) {
    const float d2 = sq(p.x - cp.position.x) + sq(p.y - cp.position.y) + sq(p.z - cp.position.z);
    return d2 < cp.radius * cp.radius;
}

bool playerGateHit(const Checkpoint& cp, const Mat34& car, const Vec3& box) {
    // mmWaypoints::Init halves the InertiaBox width and length.
    const Vec3 size{box.x * 0.5f, box.y, box.z * 0.5f};
    const float front = size.z, back = size.z + 2.0f;
    const Vec3& pos = car.m3;
    const Vec2 p1{-front * car.m2.x + pos.x, -front * car.m2.z + pos.z};
    const Vec2 p2{back * car.m2.x + pos.x, back * car.m2.z + pos.z};
    return planeHit(cp, car, p1, p2, size);
}

bool aiGateHit(const Checkpoint& cp, const Mat34& car, const Vec3& box) {
    const Vec3& pos = car.m3;
    const Vec2 p1{car.m2.x * box.z + pos.x, box.z * car.m2.z + pos.z};
    const Vec2 p2{car.m2.x * -box.z + pos.x, car.m2.z * -box.z + pos.z};
    return planeHit(cp, car, p1, p2, box * 5.0f);
}

Checkpoint makeCheckpoint(const Vec3& position, float headingDeg, float radius) {
    Checkpoint cp;
    cp.position = position;
    cp.headingDeg = headingDeg;
    cp.radius = radius;
    calculateGatePoints(position, headingDeg, radius, cp.gateA, cp.gateB);
    return cp;
}

} // namespace mm2::game::session
