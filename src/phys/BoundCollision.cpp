// phBoundCollision (SegSegDistNorm, testNoOverlap, GetDisp), ported from the
// code of midtown2.exe build 3393 (MM2Recomp). See docs/physics.md,
// "Collision".

#include "phys/Geometry.h"

#include <cmath>

namespace mm2::phys::geom {
namespace {

// phBoundCollision::testNoOverlap: s0 and s1 (the signed distances of a
// segment's ends from a plane) lie on the same side, neither within 1% of
// the other's size from the plane.
bool noOverlap(float s0, float s1) {
    if (0.0f < s0)
        return s0 * 0.01f < s1 && s1 * 0.01f < s0;
    return s1 < s0 * 0.01f && s0 < s1 * 0.01f;
}

} // namespace

void segSegDistNorm(const Vec3& a0, const Vec3& a1, const Vec3& da, const Vec3& b0, const Vec3& b1, const Vec3& db,
                    Vec3& normal, Vec3& pointA, Vec3& pointB, float& distance, int& ok) {
    // phBoundCollision::SegSegDistNorm.
    const float nx = db.z * da.y - da.z * db.y;
    const float ny = da.z * db.x - db.z * da.x;
    const float nz = da.x * db.y - db.x * da.y;
    const float len2 = nz * nz + ny * ny + nx * nx;
    if (len2 <= 0.0f) {
        ok = 0;
        return;
    }
    const Vec3 w{a0.x - b0.x, a0.y - b0.y, a0.z - b0.z};
    const float inv = 1.0f / std::sqrt(len2);
    normal = {nx * inv, ny * inv, nz * inv};
    // The normal points from b's line towards a's.
    distance = normal.y * w.y + normal.x * w.x + normal.z * w.z;
    if (distance < 0.0f) {
        distance = -distance;
        normal = -normal;
    }
    // Where a crosses the plane through b's line along the normal.
    const float cz = normal.x * db.y - db.x * normal.y;
    const float cx = db.z * normal.y - normal.z * db.y;
    const float cy = db.x * normal.z - normal.x * db.z;
    const float s0 = cz * w.z + cy * w.y + cx * w.x;
    const Vec3 e{a1.x - b0.x, a1.y - b0.y, a1.z - b0.z};
    const float s1 = e.z * cz + e.y * cy + cx * e.x;
    if (noOverlap(s0, s1)) {
        ok = 0;
        return;
    }
    const float ds = s1 - s0;
    if (ds == 0.0f) {
        pointA = Vec3{a0.x + a1.x, a1.y + a0.y, a1.z + a0.z} * 0.5f;
    } else {
        const Vec3 x = a1 * s0;
        const Vec3 y = a0 * s1;
        pointA = (y - x) / ds;
    }
    // And where b crosses the plane through a's line.
    const Vec3 c2 = normal.cross(da);
    const float t0 = -(w.dot(c2));
    const float t1 = (b1 - a0).dot(c2);
    if (noOverlap(t0, t1)) {
        ok = 0;
        return;
    }
    const float dt = t1 - t0;
    if (dt == 0.0f) {
        pointB = (b0 + b1) * 0.5f;
    } else {
        const Vec3 p = b1 * t0;
        const Vec3 q = b0 * t1;
        pointB = (q - p) / dt;
    }
    ok = 1;
}

Vec3 getDisp(const Mat34& m, const Mat34& last, const Vec3& p) {
    // phBoundCollision::GetDisp: p in m's frame, carried by the difference
    // of the two poses.
    const float dx = p.x - m.m3.x, dy = p.y - m.m3.y, dz = p.z - m.m3.z;
    const float lx = dz * m.m0.z + dy * m.m0.y + dx * m.m0.x;
    const float ly = dz * m.m1.z + dy * m.m1.y + dx * m.m1.x;
    const float lz = dz * m.m2.z + dy * m.m2.y + dx * m.m2.x;
    const Vec3 r0 = m.m0 - last.m0, r1 = m.m1 - last.m1, r2 = m.m2 - last.m2, r3 = m.m3 - last.m3;
    return {r2.x * lz + r1.x * ly + r0.x * lx + r3.x, r2.y * lz + r1.y * ly + r0.y * lx + r3.y,
            r2.z * lz + r1.z * ly + r0.z * lx + r3.z};
}

} // namespace mm2::phys::geom
