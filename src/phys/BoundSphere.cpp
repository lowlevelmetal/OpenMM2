#include "phys/Bound.h"
#include "phys/Collision.h"
#include "phys/Geometry.h"

#include <cmath>

// phBoundSphere's segment tests and sphere-sphere impact, ported from
// midtown2.exe build 3393 (MM2Recomp). Sums follow the original's order
// (32-bit float math). The bounding radius (phBound's, which SetRadius
// keeps equal to the sphere radius) is what these routines read.

namespace mm2::phys {
namespace {

float invSqrtOrZero(float m2) {
    return m2 == 0.0f ? 0.0f : 1.0f / std::sqrt(m2);
}

// phIntersectionPoint::Set plus the polygon index, which spheres
// set to 0.
void setIntersection(Intersection& out, const Vec3& position, const Vec3& normal, float t, bool endBehind) {
    out.position = position;
    out.normal = normal;
    out.t = t;
    out.depth = 0.0f;
    out.bInside = endBehind;
    out.polygon = 0;
}

} // namespace

// phBoundSphere::TestEdge: where the segment (bound space) crosses the
// sphere surface. Nothing when both ends are inside. Each crossing gets
// the position, the outward unit normal there, t, depth 0, bInside = "a is
// outside" and polygon 0. Returns the number written (1 or 2). The original
// ignores `max` and always writes the second crossing; here it is only
// written when max >= 2 (otherwise 1 is returned).
int BoundSphere::testEdge(Segment& seg, Intersection* out, int max) const {
    Vec3 a = seg.a;
    Vec3 b = seg.b;
    const float radius2 = radius * radius;
    if (isOffset) {
        a -= centroid;
        b -= centroid;
    }
    const bool startInside = (a.z * a.z + a.y * a.y) + a.x * a.x < radius2;
    const bool endInside = (b.z * b.z + b.y * b.y) + b.x * b.x < radius2;
    if (startInside && endInside)
        return 0;

    const Vec3 dir = seg.b - seg.a;
    float t0 = 0;
    float t1 = 0;
    const int count = geom::segmentToSphereIntersections(a, dir, radius2, t0, t1);
    if (count == 0)
        return 0;

    const Vec3 along0 = dir * t0;
    Vec3 normal = along0 + a;
    normal *= invSqrtOrZero((normal.z * normal.z + normal.y * normal.y) + normal.x * normal.x);
    setIntersection(out[0], seg.a + along0, normal, t0, !startInside);
    if (count == 1 || max < 2)
        return 1;

    const Vec3 along1 = (seg.b - seg.a) * t1;
    normal = along1 + a;
    normal *= invSqrtOrZero((normal.z * normal.z + normal.y * normal.y) + normal.x * normal.x);
    setIntersection(out[1], along1 + seg.a, normal, t1, !startInside);
    return 2;
}

// phBoundSphere::TestProbe: where a segment starting outside the sphere
// (bound space) enters it, if at t <= maxT: position, outward unit normal,
// t, depth 0, bInside true, polygon 0.
bool BoundSphere::testProbe(Segment& seg, Intersection& out, float maxT) const {
    Vec3 a = seg.a;
    const float radius2 = radius * radius;
    if (isOffset)
        a -= centroid;
    if ((a.z * a.z + a.y * a.y) + a.x * a.x < radius2)
        return false;

    const Vec3 dir = seg.b - seg.a;
    float t0 = 0;
    float t1 = 0;
    if (geom::segmentToSphereIntersections(a, dir, radius2, t0, t1) == 0 || t0 > maxT)
        return false;
    const Vec3 along = dir * t0;
    Vec3 normal = along + a;
    const float inv = invSqrtOrZero((normal.z * normal.z + normal.y * normal.y) + normal.x * normal.x);
    normal = {inv * normal.x, normal.y * inv, normal.z * inv};
    setIntersection(out, seg.a + along, normal, t0, true);
    return true;
}

// phBoundSphere::FindImpactSphereToSphere. relPos is B's position minus
// A's (the matrices' origins, world); both centroids are added through the
// matrices' rotations. Touching or apart (distance >= sum of the radii): no
// impact. Otherwise one impact (kind 0, elements 0/0) with the unit normal
// from B's centre towards A's, depth = radii - distance and the position
// on the line between the centres at (distance + rB - rA) / 2 from A's
// matrix origin (MM2 measures from the origin, not the offset centre).
bool findImpactSphereToSphere(const BoundSphere& a, const BoundSphere& b, const Mat34& ma, const Mat34& mb,
                              Collider* ca, Collider* cb, Impact& impact, const Vec3& relPos) {
    Vec3 rel = relPos;
    if (a.isOffset) {
        const Vec3& c = a.centroid;
        rel -= Vec3{(ma.m2.x * c.z + ma.m1.x * c.y) + ma.m0.x * c.x, (ma.m2.y * c.z + ma.m0.y * c.x) + ma.m1.y * c.y,
                    (ma.m2.z * c.z + ma.m0.z * c.x) + ma.m1.z * c.y};
    }
    if (b.isOffset) {
        const Vec3& c = b.centroid;
        rel += Vec3{(mb.m2.x * c.z + mb.m1.x * c.y) + mb.m0.x * c.x, (mb.m2.y * c.z + mb.m1.y * c.y) + mb.m0.y * c.x,
                    (mb.m2.z * c.z + mb.m1.z * c.y) + mb.m0.z * c.x};
    }
    const float dist2 = (rel.z * rel.z + rel.y * rel.y) + rel.x * rel.x;
    const float reach = b.radius + a.radius;
    if (reach * reach <= dist2)
        return false;

    const float dist = std::sqrt(dist2);
    const float depth = reach - dist;
    const Vec3 normal{-rel.x / dist, -rel.y / dist, -rel.z / dist};
    const float fromA = (dist - a.radius) + depth * 0.5f;
    const Vec3 position = ma.m3 - normal * fromA;
    impact.makeNewImpact(ca, cb, position, normal, depth, a, b, 0, 0, 0);
    return true;
}

} // namespace mm2::phys
