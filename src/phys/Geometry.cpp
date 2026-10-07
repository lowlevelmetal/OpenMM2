#include "phys/Geometry.h"

#include "phys/Bound.h"

#include <array>
#include <cmath>
#include <utility>

// The free geometry functions of the Angel engine's vector7 module and
// phCollisionPrim, ported from midtown2.exe build 3393 (MM2Recomp). The
// original runs the x87 FPU in single precision, so every intermediate is a
// float; the sums below are written in the order the original adds them
// (float addition does not associate, and a different order changes the
// last bit of a contact position).

namespace mm2::phys::geom {
namespace {

// 1 / sqrt(m2), or 0 for a zero vector (the original's normalise idiom).
float invSqrtOrZero(float m2) {
    return m2 == 0.0f ? 0.0f : 1.0f / std::sqrt(m2);
}

// DistanceParallelLineToLine (only reached from DistanceLineToLine): the
// distance between the parallel lines through p1 and p2 (direction d2) and
// the unit normal between them, pointing from line 2 towards line 1.
float distanceParallelLineToLine(const Vec3& p1, const Vec3& p2, const Vec3& d2, Vec3& normal) {
    const Vec3 w = p2 - p1;
    const Vec3 u = w.cross(d2);
    const Vec3 v = u.cross(d2);
    const float inv = invSqrtOrZero((v.z * v.z + v.y * v.y) + v.x * v.x);
    normal = {inv * v.x, inv * v.y, inv * v.z};
    return -((normal.y * w.y + normal.x * w.x) + normal.z * w.z);
}

// DistanceLineToLine: the distance between the lines p1 + s * d1 and
// p2 + t * d2 and the unit normal between them, pointing from line 2 towards
// line 1. Lines whose cross product is below `parallelTolerance` (relative
// to |d1|^2 |d2|^2) are treated as parallel.
float distanceLineToLine(const Vec3& p1, const Vec3& d1, const Vec3& p2, const Vec3& d2, Vec3& normal,
                         float parallelTolerance) {
    const Vec3 c = d2.cross(d1);
    const float c2 = (c.z * c.z + c.y * c.y) + c.x * c.x;
    const float len2 = (d2.x * d2.x + d2.z * d2.z) + d2.y * d2.y;
    const float len1 = (d1.x * d1.x + d1.y * d1.y) + d1.z * d1.z;
    if (c2 < len2 * len1 * parallelTolerance)
        return distanceParallelLineToLine(p1, p2, d2, normal);
    const float len = std::sqrt(c2);
    normal = {c.x / len, c.y / len, c.z / len};
    const Vec3 w = p1 - p2;
    float distance = (w.x * normal.x + w.y * normal.y) + w.z * normal.z;
    if (distance < 0.0f) {
        distance = -distance;
        normal = -normal;
    }
    return distance;
}

// FindTValueSegToOrigin: the parameter in [0, 1] of the point of segment
// p + t * d closest to the origin.
float tValueSegToOrigin(const Vec3& p, const Vec3& d) {
    const float s = -((p.z * d.z + p.y * d.y) + p.x * d.x);
    if (s <= 0.0f)
        return 0.0f;
    const float rest = ((d.x * d.x + d.y * d.y) + d.z * d.z) - s;
    if (rest <= 0.0f)
        return 1.0f;
    return s / (rest + s);
}

// FindTValueSegToPoint: the parameter in [0, 1] of the point of segment
// p + t * d closest to q.
float tValueSegToPoint(const Vec3& p, const Vec3& d, const Vec3& q) {
    return tValueSegToOrigin(p - q, d);
}

// FindTValuesLineToLine: the parameters of the closest points of the lines
// p1 + t1 * d1 and p2 + t2 * d2; true when both lie strictly inside (0, 1).
// Parallel lines give NaN and false.
bool tValuesLineToLine(const Vec3& p1, const Vec3& d1, const Vec3& p2, const Vec3& d2, float& t1, float& t2) {
    const float d1d2 = (d2.z * d1.z + d2.y * d1.y) + d1.x * d2.x;

    const Vec3 w = p1 - p2;
    const float inv2 = 1.0f / ((d2.x * d2.x + d2.y * d2.y) + d2.z * d2.z);
    const float k1 = ((w.y * d2.y + w.z * d2.z) + w.x * d2.x) * inv2;
    const Vec3 r{w.x - k1 * d2.x, w.y - k1 * d2.y, w.z - k1 * d2.z};
    const float k2 = d1d2 * inv2;
    const Vec3 e{d1.x - k2 * d2.x, d1.y - k2 * d2.y, d1.z - k2 * d2.z};
    t1 = -(((r.z * e.z + r.y * e.y) + e.x * r.x) / ((e.z * e.z + e.x * e.x) + e.y * e.y));

    const Vec3 w2 = p2 - p1;
    const float inv1 = 1.0f / ((d1.x * d1.x + d1.y * d1.y) + d1.z * d1.z);
    const float k3 = ((w2.x * d1.x + w2.y * d1.y) + w2.z * d1.z) * inv1;
    const Vec3 r2{w2.x - k3 * d1.x, w2.y - k3 * d1.y, w2.z - k3 * d1.z};
    const float k4 = d1d2 * inv1;
    const Vec3 e2{d2.x - k4 * d1.x, d2.y - k4 * d1.y, d2.z - k4 * d1.z};
    t2 = -(((r2.z * e2.z + r2.y * e2.y) + e2.x * r2.x) / ((e2.z * e2.z + e2.x * e2.x) + e2.y * e2.y));

    return 0.0f < t1 && t1 < 1.0f && 0.0f < t2 && t2 < 1.0f;
}

// AddIntersection (the face normal version, SegmentToBoxIntersections):
// keeps the smallest crossing in (t0, n0, f0) and the largest in
// (t1, n1, f1). Returns the new count (1 for the first crossing, else 2).
int addIntersection(float t, float& t0, float& t1, const Vec3& n, Vec3& n0, Vec3& n1, int count, int face,
                    int& f0, int& f1) {
    if (count == 0) {
        t0 = t;
        n0 = n;
        f0 = face;
        t1 = t;
        n1 = n;
        f1 = face;
        return 1;
    }
    if (t < t0) {
        t0 = t;
        n0 = n;
        f0 = face;
        return 2;
    }
    if (t1 < t) {
        t1 = t;
        n1 = n;
        f1 = face;
    }
    return 2;
}

// AddIntersection (the edge index version, FindTValuesLineToBoxFace).
int addIntersection(float t, float& t0, float& t1, int edge, int& e0, int& e1, int count) {
    if (count == 0) {
        t0 = t;
        e0 = edge;
        t1 = t;
        e1 = edge;
        return 1;
    }
    if (t < t0) {
        t0 = t;
        e0 = edge;
        return 2;
    }
    if (t1 < t) {
        t1 = t;
        e1 = edge;
    }
    return 2;
}

} // namespace

// FindImpactPolygonToSphere. `center`/`radius`: the sphere; `verts`/`count`:
// a convex polygon of at most 4 vertices (MM2 keeps the per-edge
// classification in a 4 entry stack array) with unit face `normal`.
// Returns 3 when the sphere does not reach the polygon (outputs untouched,
// or partly written on a vertex/edge miss). Otherwise `contactNormal` is the
// unit direction from the polygon towards the sphere centre, `depth` the
// penetration (radius - distance), `position` the midpoint of the
// penetration (the polygon's closest point moved depth / 2 away from the
// sphere) and:
//   0: a vertex is closest, `feature` = its index in verts;
//   1: an edge is closest, `feature` = i for the edge verts[i] -> verts[i+1];
//   2: the face, `feature` = 0, contactNormal = normal (the centre may be on
//      either side; depth = radius - signed distance).
int findImpactPolygonToSphere(const Vec3& center, float radius, const Vec3* verts, int count, const Vec3& normal,
                              Vec3& position, int& feature, Vec3& contactNormal, float& depth) {
    if (!isPointNearPlane(center, verts[0], normal, radius))
        return 3;

    // Per edge: 1 the centre is inside the edge, -1 outside and before the
    // edge's start, -2 outside and past its end.
    std::array<int, 4> edgeSide{};
    bool insideAll = true;
    bool outsideAll = true;
    int vertex = 0;
    if (count > 0) {
        for (int i = 0; i < count; ++i) {
            const int next = (i + 1) % count;
            vertex = next;
            const Vec3 d = center - verts[i];
            const Vec3 e = verts[next] - verts[i];
            // normal x e, the in-plane edge normal pointing into the polygon.
            const float cz = e.y * normal.x - e.x * normal.y;
            const float cx = e.z * normal.y - e.y * normal.z;
            const float cy = e.x * normal.z - e.z * normal.x;
            if (!((cz * d.z + cy * d.y) + cx * d.x < 0.0f)) {
                outsideAll = false;
                edgeSide[static_cast<std::size_t>(i)] = 1;
                continue;
            }
            insideAll = false;
            if ((d.y * e.y + d.z * e.z) + e.x * d.x < 0.0f) {
                edgeSide[static_cast<std::size_t>(i)] = -1;
                continue;
            }
            const Vec3 f = center - verts[next];
            if ((f.y * e.y + f.z * e.z) + f.x * e.x > 0.0f) {
                edgeSide[static_cast<std::size_t>(i)] = -2;
                continue;
            }

            // The centre projects onto this edge: the distance from the
            // triangle (start, end, centre) by its side lengths.
            const float dd = (d.z * d.z + d.y * d.y) + d.x * d.x;
            const float ff = (f.z * f.z + f.y * f.y) + f.x * f.x;
            const float ee = (e.y * e.y + e.z * e.z) + e.x * e.x;
            const float diff = dd - ff;
            const float sum = ff + dd;
            const float dist2 = (((sum + sum) - ee) - (diff * diff) / ee) * 0.25f;
            if (dist2 > radius * radius)
                return 3;
            feature = i;
            depth = radius - std::sqrt(dist2);
            float along = (dd - dist2) / ee;
            along = along > 0.0f ? std::sqrt(along) : 0.0f;
            position = e * along + verts[i];
            contactNormal = center - position;
            contactNormal *= invSqrtOrZero(
                (contactNormal.x * contactNormal.x + contactNormal.y * contactNormal.y) +
                contactNormal.z * contactNormal.z);
            const float half = depth * 0.5f;
            position -= contactNormal * half;
            return 1;
        }

        if (!insideAll && !outsideAll) {
            // The closest vertex: the end of an edge the centre is past
            // whose successor the centre is before (the last such end, or
            // vertex 0, when none matches).
            for (int i = 0; i < count; ++i) {
                if (edgeSide[static_cast<std::size_t>(i)] == -2) {
                    vertex = (i + 1) % count;
                    if (edgeSide[static_cast<std::size_t>(vertex)] == -1)
                        break;
                }
            }
            position = verts[vertex];
            contactNormal = center - position;
            const float dist2 = (contactNormal.x * contactNormal.x + contactNormal.y * contactNormal.y) +
                                contactNormal.z * contactNormal.z;
            if (dist2 > radius * radius)
                return 3;
            const float dist = std::sqrt(dist2);
            feature = vertex;
            contactNormal = {contactNormal.x / dist, contactNormal.y / dist, contactNormal.z / dist};
            depth = radius - dist;
            const float half = depth * 0.5f;
            position -= contactNormal * half;
            return 0;
        }
    }

    const Vec3 d = center - verts[0];
    const float distance = (d.x * normal.x + d.z * normal.z) + d.y * normal.y;
    if (std::fabs(distance) > radius)
        return 3;
    feature = 0;
    contactNormal = normal;
    depth = radius - distance;
    const float half = (distance + radius) * 0.5f;
    position = center - normal * half;
    return 2;
}

// FindImpactEdgeToShaft. The first pair is the SHAFT (a hotdog's axis
// segment, on which the contact position lies), the second pair the EDGE;
// the 7th argument receives the normal and the 9th the position.
//   1: the closest points lie strictly inside both segments: `normal` is the
//      unit normal from the edge's line towards the shaft's line, `depth` =
//      radius - line distance, `position` the closest point on the shaft,
//      `feature` = 0.
//   0: the closest point of the shaft (strictly inside it) is nearest an end
//      of the edge: `feature` = 1 for edgeA, 2 for edgeB, `normal` the unit
//      direction from that end towards `position` (on the shaft), `depth` =
//      radius - their distance.
//   3: farther apart than radius (outputs may be partly written).
//   4: the closest point is at or beyond an end of the shaft: only
//      `position` is written (the nearer shaft end).
int findImpactEdgeToShaft(const Vec3& shaftA, const Vec3& shaftB, const Vec3& edgeA, const Vec3& edgeB,
                          float radius, int& feature, Vec3& normal, float& depth, Vec3& position) {
    const Vec3 edgeDir = edgeB - edgeA;
    const Vec3 shaftDir = shaftB - shaftA;
    Vec3 lineNormal;
    const float distance = distanceLineToLine(edgeA, edgeDir, shaftA, shaftDir, lineNormal, 0.0012217f);
    if (distance > radius)
        return 3;

    float tEdge = 0;
    float tShaft = 0;
    if (findTValuesSegToSeg(edgeA, edgeDir, shaftA, shaftDir, tEdge, tShaft)) {
        feature = 0;
        normal = -lineNormal;
        depth = radius - distance;
        position = shaftDir * tShaft + shaftA;
        return 1;
    }
    if (tShaft <= 0.0f || !(tShaft < 1.0f)) {
        position = tShaft < 0.5f ? shaftA : shaftB;
        return 4;
    }
    if (tEdge < 0.5f) {
        feature = 1;
        normal = edgeA;
    } else {
        feature = 2;
        normal = edgeB;
    }
    position = shaftDir * tShaft + shaftA;
    normal -= position;
    const float dist2 = (normal.z * normal.z + normal.y * normal.y) + normal.x * normal.x;
    if (dist2 > radius * radius)
        return 3;
    const float dist = std::sqrt(dist2);
    depth = radius - dist;
    const float negDist = -dist;
    normal = {normal.x / negDist, normal.y / negDist, normal.z / negDist};
    return 0;
}

// IsPointBehindPlane: (point - planePoint) . planeNormal < tolerance.
bool isPointBehindPlane(const Vec3& point, const Vec3& planePoint, const Vec3& planeNormal, float tolerance) {
    const Vec3 d = point - planePoint;
    return (d.z * planeNormal.z + d.y * planeNormal.y) + d.x * planeNormal.x < tolerance;
}

// IsPointNearPlane: |(point - planePoint) . planeNormal| <= tolerance.
bool isPointNearPlane(const Vec3& point, const Vec3& planePoint, const Vec3& planeNormal, float tolerance) {
    const Vec3 d = point - planePoint;
    return std::fabs((d.y * planeNormal.y + d.z * planeNormal.z) + d.x * planeNormal.x) <= tolerance;
}

// IsPointInBox: inside or on the box of half sizes (hx, hy, hz) at the
// origin.
bool isPointInBox(const Vec3& point, float hx, float hy, float hz) {
    return point.x <= hx && -hx <= point.x && point.y <= hy && -hy <= point.y && point.z <= hz && -hz <= point.z;
}

// DistanceLineToPoint. NOTE: the second argument is the line's DIRECTION
// (not a second point): the distance from `point` to the line a + t * dir
// (0 when dir is zero or the point lies on the line).
float distanceLineToPoint(const Vec3& a, const Vec3& dir, const Vec3& point) {
    const Vec3 d = a - point;
    const Vec3 e = d.cross(dir).cross(dir);
    const float inv = invSqrtOrZero((e.z * e.z + e.y * e.y) + e.x * e.x);
    return -(((inv * e.y) * d.y + (inv * e.z) * d.z) + (inv * e.x) * d.x);
}

// FindTValuesSegToSeg. NOTE: the second and fourth arguments are the
// segments' DIRECTIONS: segment A is a0 + ta * da, segment B b0 + tb * db,
// t in [0, 1]. Returns true when the closest points of the two lines lie
// strictly inside both segments (ta, tb from the line solution). Otherwise
// false, with ta, tb an endpoint approximation of the closest points: the
// parameter of one segment's end (0 or 1) and the other segment's
// parameter closest to it, picking the pair with the smaller distance.
bool findTValuesSegToSeg(const Vec3& a0, const Vec3& da, const Vec3& b0, const Vec3& db, float& ta, float& tb) {
    if (tValuesLineToLine(a0, da, b0, db, ta, tb))
        return true;

    const Vec3 a1 = a0 + da;
    const Vec3 b1 = b0 + db;
    const float sa0 = tValueSegToPoint(b0, db, a0);
    const float sa1 = tValueSegToPoint(b0, db, a1);

    // The candidates: A's end a0/a1 against its closest point on B, or B's
    // end b0/b1 against its closest point on A.
    const auto a0OnB = [&] {
        ta = 0.0f;
        tb = sa0;
    };
    const auto a1OnB = [&] {
        ta = 1.0f;
        tb = sa1;
    };
    const auto b0OnA = [&] {
        ta = tValueSegToPoint(a0, da, b0);
        tb = 0.0f;
    };
    const auto b1OnA = [&] {
        ta = tValueSegToPoint(a0, da, b1);
        tb = 1.0f;
    };

    if (sa0 == 0.0f) {
        if (sa1 == 0.0f) {
            b0OnA();
        } else if (sa1 == 1.0f) {
            const float toB0 = distanceLineToPoint(a0, da, b0);
            if (distanceLineToPoint(a0, da, b1) <= toB0)
                b1OnA();
            else
                b0OnA();
        } else {
            const float toB0 = distanceLineToPoint(a0, da, b0);
            if (distanceLineToPoint(b0, db, a1) <= toB0)
                a1OnB();
            else
                b0OnA();
        }
    } else if (sa0 == 1.0f) {
        if (sa1 == 0.0f) {
            const float toB0 = distanceLineToPoint(a0, da, b0);
            if (distanceLineToPoint(a0, da, b1) <= toB0)
                b1OnA();
            else
                b0OnA();
        } else if (sa1 == 1.0f) {
            b1OnA();
        } else {
            const float toB1 = distanceLineToPoint(a0, da, b1);
            if (distanceLineToPoint(b0, db, a1) <= toB1)
                a1OnB();
            else
                b1OnA();
        }
    } else {
        if (sa1 == 0.0f) {
            const float toB0 = distanceLineToPoint(a0, da, b0);
            if (distanceLineToPoint(b0, db, a0) <= toB0)
                a0OnB();
            else
                b0OnA();
        } else if (sa1 == 1.0f) {
            const float toB1 = distanceLineToPoint(a0, da, b1);
            if (distanceLineToPoint(b0, db, a0) <= toB1)
                a0OnB();
            else
                b1OnA();
        } else {
            const float toA0 = distanceLineToPoint(b0, db, a0);
            if (distanceLineToPoint(b0, db, a1) <= toA0)
                a1OnB();
            else
                a0OnB();
        }
    }
    return false;
}

// FindTValuesLineToBoxFace. `a` + t * `dir` (the second argument is a
// DIRECTION) is a line, `faceNormal` (p3) one of a box's axis face normals
// and `halfSize` (p4) the box's half sizes. The face's two in-plane axes
// (u, v) are chosen by the normal's sign: +x (y, z), -x (z, y), +y (z, x),
// -y (x, z), +z (x, y), -z (y, x). In those coordinates the function finds
// where the line crosses the face rectangle's edge lines (u = +hu edge 0,
// u = -hu edge 2, v = +hv edge 1, v = -hv edge 3) within the rectangle, and
// keeps the smallest crossing in (t0, e0) and the largest in (t1, e1).
// Returns false with t0 = t1 = 0.5 when there is none; otherwise whether
// (t0 > 0 or t1 > 0) and (t0 < 1 or t1 < 1). (A zero normal leaves the
// original reading uninitialised locals; here it finds no crossing.)
bool findTValuesLineToBoxFace(const Vec3& a, const Vec3& dir, const Vec3& faceNormal, const Vec3& halfSize,
                              float& t0, float& t1, int& e0, int& e1) {
    float pu = 0, pv = 0, du = 0, dv = 0, hu = 0, hv = 0;
    const auto axes = [&](int u, int v) {
        pu = a[u];
        pv = a[v];
        du = dir[u];
        dv = dir[v];
        hu = halfSize[u];
        hv = halfSize[v];
    };
    if (faceNormal.x > 0.0f)
        axes(1, 2);
    else if (faceNormal.x < 0.0f)
        axes(2, 1);
    else if (faceNormal.y > 0.0f)
        axes(2, 0);
    else if (faceNormal.y < 0.0f)
        axes(0, 2);
    else if (faceNormal.z > 0.0f)
        axes(0, 1);
    else if (faceNormal.z < 0.0f)
        axes(1, 0);

    int count = 0;
    if (du != 0.0f) {
        const float inv = 1.0f / du;
        float t = (hu - pu) * inv;
        if (std::fabs(dv * t + pv) <= hv)
            count = addIntersection(t, t0, t1, 0, e0, e1, 0);
        t = (-hu - pu) * inv;
        if (std::fabs(dv * t + pv) <= hv)
            count = addIntersection(t, t0, t1, 2, e0, e1, count);
    }
    if (dv != 0.0f && count < 2) {
        const float inv = 1.0f / dv;
        float t = (hv - pv) * inv;
        if (std::fabs(t * du + pu) <= hu)
            count = addIntersection(t, t0, t1, 1, e0, e1, count);
        if (count < 2) {
            t = (-hv - pv) * inv;
            if (std::fabs(t * du + pu) <= hu)
                count = addIntersection(t, t0, t1, 3, e0, e1, count);
        }
    }
    if (count == 0) {
        t0 = 0.5f;
        t1 = 0.5f;
        return false;
    }
    return (t0 > 0.0f || t1 > 0.0f) && (t0 < 1.0f || t1 < 1.0f);
}

// SegmentToSphereIntersections. NOTE: the second argument is the segment's
// DIRECTION (p(t) = start + t * dir, t in [0, 1]) and the third the SQUARED
// radius of the sphere at the origin. Returns
//   0: no crossing in [0, 1] (both ends inside, or the line misses, or the
//      entry lies outside [0, 1]);
//   1: start inside (|start|^2 <= radius2), end outside: t0 = the exit; or
//      start outside, entry t0 in [0, 1] and exit t1 > 1 (t1 written);
//   2: start outside, entry t0 and exit t1 both in [0, 1].
int segmentToSphereIntersections(const Vec3& start, const Vec3& dir, float radius2, float& t0, float& t1) {
    const float dot = (start.y * dir.y + start.x * dir.x) + dir.z * start.z;
    const float ss = (start.x * start.x + start.y * start.y) + start.z * start.z;
    const float dd = (dir.x * dir.x + dir.y * dir.y) + dir.z * dir.z;
    const float disc = ((dot * dot) / dd - (ss - radius2)) / dd;
    if (disc < 0.0f)
        return 0;
    const Vec3 end{start.x + dir.x, start.y + dir.y, dir.z + start.z};
    const bool startInside = ss <= radius2;
    const bool endInside = (end.z * end.z + end.x * end.x) + end.y * end.y <= radius2;
    if (startInside) {
        if (endInside)
            return 0;
        t0 = std::sqrt(disc) - dot / dd;
        return 1;
    }
    const float mid = dot / dd;
    const float half = std::sqrt(disc);
    const float entry = -mid - half;
    t0 = entry;
    if (entry < 0.0f || entry > 1.0f)
        return 0;
    const float exit = half - mid;
    t1 = exit;
    return exit > 1.0f ? 1 : 2;
}

// SegmentToHemisphereIntersections: as SegmentToSphereIntersections (start,
// DIRECTION, SQUARED radius), counting only crossings of the half of the
// sphere with y > 0 (`top`) or y < 0 (not `top`); the flat side is not a
// surface. Returns
//   0: none;
//   1: start inside the sphere and the exit (t0) on the half; or start
//      outside with one crossing on the half in [0, 1] (t0; when the entry
//      is off the half t0 is the exit); t1 may hold an exit beyond 1;
//   2: start outside, entry t0 and exit t1 both on the half within [0, 1].
int segmentToHemisphereIntersections(const Vec3& start, const Vec3& dir, float radius2, float& t0, float& t1,
                                     bool top) {
    const float dot = (start.y * dir.y + dir.z * start.z) + start.x * dir.x;
    const float ss = (start.x * start.x + start.y * start.y) + start.z * start.z;
    const float dd = (dir.x * dir.x + dir.y * dir.y) + dir.z * dir.z;
    const float disc = ((dot * dot) / dd - (ss - radius2)) / dd;
    if (disc < 0.0f)
        return 0;
    const float side = top ? 1.0f : -1.0f;
    const Vec3 end{start.x + dir.x, start.y + dir.y, dir.z + start.z};
    const bool startInside = ss <= radius2;
    const bool endInside = (end.z * end.z + end.x * end.x) + end.y * end.y <= radius2;
    const auto onHalf = [&](float t) { return (t * dir.y + start.y) * side > 0.0f; };
    if (startInside) {
        if (endInside)
            return 0;
        const float exit = std::sqrt(disc) - dot / dd;
        t0 = exit;
        return onHalf(exit) ? 1 : 0;
    }
    const float mid = dot / dd;
    const float half = std::sqrt(disc);
    const float entry = -mid - half;
    t0 = entry;
    if (entry < 0.0f || entry > 1.0f)
        return 0;
    const float exit = half - mid;
    if (onHalf(entry)) {
        t1 = exit;
        if (exit > 1.0f)
            return 1;
        return onHalf(exit) ? 2 : 1;
    }
    t0 = exit;
    if (exit > 1.0f)
        return 0;
    return onHalf(exit) ? 1 : 0;
}

// SegmentToUprightCylIsects: (start, DIRECTION, height, SQUARED radius,
// t0, t1, y0, y1). The cylinder is the infinite one around the y axis;
// t0/t1 are the crossings of start + t * dir and y0/y1 their heights as a
// fraction of `height`:
// (y / height) + 0.5, so 0..1 spans y = -height/2..height/2 (callers check
// the range). Returns
//   0: both ends inside, the line misses, or the entry is outside [0, 1];
//   1: start inside: t0 = the exit (y0 set); or start outside with the
//      entry t0 in [0, 1] and the end inside (t1 = the exit beyond, y0 set);
//   2: start and end outside: entry t0 in [0, 1], exit t1, y0 and y1 set.
int segmentToUprightCylIsects(const Vec3& start, const Vec3& dir, float height, float radius2, float& t0,
                              float& t1, float& y0, float& y1) {
    const float c = (start.x * start.x + start.z * start.z) - radius2;
    const bool startInside = c < 0.0f;
    const float ex = dir.x + start.x;
    const float ez = dir.z + start.z;
    const bool endInside = ez * ez + ex * ex < radius2;
    if (startInside && endInside)
        return 0;
    const float b = dir.x * start.x + dir.z * start.z;
    const float a = dir.z * dir.z + dir.x * dir.x;
    const float disc = ((b * b) / a - c) / a;
    if (disc < 0.0f)
        return 0;
    const float half = std::sqrt(disc);
    const float mid = b / a;
    if (startInside) {
        const float exit = half - mid;
        t0 = exit;
        y0 = (dir.y * exit + start.y) / height + 0.5f;
        return 1;
    }
    const float entry = -mid - half;
    t0 = entry;
    if (entry < 0.0f || entry > 1.0f)
        return 0;
    t1 = half - mid;
    y0 = (dir.y * t0 + start.y) / height + 0.5f;
    if (endInside)
        return 1;
    y1 = (dir.y * t1 + start.y) / height + 0.5f;
    return 2;
}

// SegmentToBoxIntersections. NOTE: the second argument is the segment's
// DIRECTION (start + t * dir). The box has half sizes (hx, hy, hz) at the
// origin. Each face plane crossed at 0 < t < 1 within the face is added in
// the order +x, -x, +y, -y, +z, -z (face indices 0..5, outward unit
// normals); (t0, n0, f0) keeps the smallest t, (t1, n1, f1) the largest.
// Returns the number of crossings kept: 0, 1 (t0 = t1) or 2.
int segmentToBoxIntersections(const Vec3& start, const Vec3& dir, float hx, float hy, float hz, float& t0,
                              float& t1, Vec3& n0, Vec3& n1, int& f0, int& f1) {
    int count = 0;
    const auto add = [&](float t, const Vec3& n, int face) {
        count = addIntersection(t, t0, t1, n, n0, n1, count, face, f0, f1);
    };
    if (dir.x != 0.0f) {
        float t = (hx - start.x) / dir.x;
        if (0.0f < t && t < 1.0f && std::fabs(t * dir.y + start.y) <= hy && std::fabs(t * dir.z + start.z) <= hz)
            add(t, {1.0f, 0.0f, 0.0f}, 0);
        t = (-hx - start.x) / dir.x;
        if (0.0f < t && t < 1.0f && std::fabs(t * dir.y + start.y) <= hy && std::fabs(t * dir.z + start.z) <= hz)
            add(t, {-1.0f, 0.0f, 0.0f}, 1);
    }
    if (dir.y != 0.0f) {
        float t = (hy - start.y) / dir.y;
        if (0.0f < t && t < 1.0f && std::fabs(t * dir.x + start.x) <= hx && std::fabs(t * dir.z + start.z) <= hz)
            add(t, {0.0f, 1.0f, 0.0f}, 2);
        t = (-hy - start.y) / dir.y;
        if (0.0f < t && t < 1.0f && std::fabs(t * dir.x + start.x) <= hx && std::fabs(t * dir.z + start.z) <= hz)
            add(t, {0.0f, -1.0f, 0.0f}, 3);
    }
    if (dir.z != 0.0f) {
        float t = (hz - start.z) / dir.z;
        if (0.0f < t && t < 1.0f && std::fabs(t * dir.y + start.y) <= hy && std::fabs(t * dir.x + start.x) <= hx)
            add(t, {0.0f, 0.0f, 1.0f}, 4);
        t = (-hz - start.z) / dir.z;
        if (0.0f < t && t < 1.0f && std::fabs(t * dir.y + start.y) <= hy && std::fabs(t * dir.x + start.x) <= hx)
            add(t, {0.0f, 0.0f, -1.0f}, 5);
    }
    return count;
}

// OrderIntersections: when t0 > t1, swaps (t0, t1), (y0, y1) and (f0, f1).
void orderIntersections(float& t0, float& t1, float& y0, float& y1, int& f0, int& f1) {
    if (t0 > t1) {
        std::swap(t0, t1);
        std::swap(y0, y1);
        std::swap(f0, f1);
    }
}

// phCollisionPrim::SphereToPolygonal (bound space). Nothing when the sphere
// misses the bound's bounding sphere (centroid, radius). Otherwise `depth`
// is reset to 0 and, over the polygons whose FindImpactPolygonToSphere
// contact is shallower than `radius`, the deepest one is kept: `depth`, its
// unit `normal` (from the polygon towards the centre) and `position` = that
// contact's position + normal * depth / 2 (back on the polygon's surface).
// Returns whether any polygon was kept.
bool sphereToPolygonal(const Vec3& center, float radius, const BoundPolygonal& bound, Vec3& position,
                       Vec3& normal, float& depth) {
    const Vec3 d = center - bound.centroid;
    const float reach = bound.radius + radius;
    if (reach * reach < (d.z * d.z + d.y * d.y) + d.x * d.x)
        return false;
    depth = 0.0f;
    bool found = false;
    std::array<Vec3, 4> verts;
    for (const Polygon& poly : bound.polygons) {
        const int count = poly.vertexCount();
        for (int i = 0; i < count; ++i)
            verts[static_cast<std::size_t>(i)] = bound.vertex(poly.v[static_cast<std::size_t>(i)]);
        Vec3 contact, contactNormal;
        int feature = 0;
        float contactDepth = 0;
        const int result = findImpactPolygonToSphere(center, radius, verts.data(), count, poly.normal, contact,
                                                     feature, contactNormal, contactDepth);
        if (result != 3 && contactDepth < radius && depth < contactDepth) {
            depth = contactDepth;
            found = true;
            normal = contactNormal;
            const float half = depth * 0.5f;
            position = {contactNormal.x * half + contact.x, half * normal.y + contact.y,
                        half * normal.z + contact.z};
        }
    }
    return found;
}

// phCollisionPrim::SphereToPolygonal (world space, the bound placed by m).
// Nothing when the sphere misses the bounding sphere around m's origin (MM2
// ignores the centroid here). The sphere centre is taken into the bound's
// space; over the polygons whose contact is shallower than `radius` and
// deeper than `depth` (NOT reset: the caller initialises it, usually to 0)
// the deepest is kept: `depth`, the world `normal` (from the polygon towards
// the centre) and the world `position` = contact position - normal *
// depth / 2 (the sphere's deepest point). Returns whether any was kept.
bool sphereToPolygonal(const Vec3& center, float radius, const BoundPolygonal& bound, const Mat34& m,
                       Vec3& position, Vec3& normal, float& depth) {
    const Vec3 d = center - m.m3;
    const float reach = bound.radius + radius;
    if (reach * reach < (d.z * d.z + d.y * d.y) + d.x * d.x)
        return false;
    // Vector3::Dot with each basis row (m^T d).
    const auto rowDot = [&](const Vec3& row) { return (d.z * row.z + d.y * row.y) + d.x * row.x; };
    const Vec3 local{rowDot(m.m0), rowDot(m.m1), rowDot(m.m2)};

    bool found = false;
    Vec3 bestPosition, bestNormal;
    std::array<Vec3, 4> verts;
    for (const Polygon& poly : bound.polygons) {
        const int count = poly.vertexCount();
        for (int i = 0; i < count; ++i)
            verts[static_cast<std::size_t>(i)] = bound.vertex(poly.v[static_cast<std::size_t>(i)]);
        Vec3 contact, contactNormal;
        int feature = 0;
        float contactDepth = 0;
        const int result = findImpactPolygonToSphere(local, radius, verts.data(), count, poly.normal, contact,
                                                     feature, contactNormal, contactDepth);
        if (result != 3 && contactDepth < radius && depth < contactDepth) {
            const float half = contactDepth * 0.5f;
            depth = contactDepth;
            found = true;
            bestNormal = contactNormal;
            bestPosition = {contact.x - contactNormal.x * half, contact.y - contactNormal.y * half,
                            contact.z - contactNormal.z * half};
        }
    }
    if (found) {
        const Vec3& p = bestPosition;
        position = {((p.x * m.m0.x + p.z * m.m2.x) + p.y * m.m1.x) + m.m3.x,
                    ((p.x * m.m0.y + p.z * m.m2.y) + p.y * m.m1.y) + m.m3.y,
                    ((p.x * m.m0.z + p.z * m.m2.z) + p.y * m.m1.z) + m.m3.z};
        const Vec3& n = bestNormal;
        normal = {(n.y * m.m1.x + n.x * m.m0.x) + n.z * m.m2.x, (n.y * m.m1.y + n.x * m.m0.y) + n.z * m.m2.y,
                  (n.y * m.m1.z + n.x * m.m0.z) + n.z * m.m2.z};
    }
    return found;
}

// phCollisionPrim::SegmentToSphere: where seg.a -> seg.b enters the sphere.
// True when the closest point of the line to the centre lies at or after
// seg.a (t >= 0) within the sphere, and before seg.b or with seg.b inside,
// and the entry t lies in [0, 1]. Writes only out.position (the entry),
// out.normal ((position - center) / radius, outward) and out.t.
bool segmentToSphere(const Vec3& center, float radius, const Segment& seg, IntersectionPoint& out) {
    const float radius2 = radius * radius;
    const Vec3 a = seg.a - center;
    const Vec3 b = seg.b - center;
    const Vec3 d = b - a;
    const float dd = (d.z * d.z + d.y * d.y) + d.x * d.x;
    const float dot = (d.z * a.z + d.y * a.y) + d.x * a.x;
    const float tClosest = -(dot / dd);
    const Vec3 closest{d.x * tClosest + a.x, d.y * tClosest + a.y, d.z * tClosest + a.z};
    if (tClosest < 0.0f)
        return false;
    if ((closest.z * closest.z + closest.y * closest.y) + closest.x * closest.x > radius2)
        return false;
    if (tClosest > 1.0f && (b.z * b.z + b.y * b.y) + b.x * b.x > radius2)
        return false;
    const float twoDot = dot + dot;
    const float disc = twoDot * twoDot - (((a.z * a.z + a.y * a.y) + a.x * a.x) - radius2) * dd * 4.0f;
    if (disc < 0.0f)
        return false;
    const float t = (-twoDot - std::sqrt(disc)) / (dd + dd);
    if (t < 0.0f || t > 1.0f)
        return false;
    out.position = {(seg.b.x - seg.a.x) * t + seg.a.x, (seg.b.y - seg.a.y) * t + seg.a.y,
                    (seg.b.z - seg.a.z) * t + seg.a.z};
    out.normal = out.position - center;
    const float invRadius = 1.0f / radius;
    out.normal = {invRadius * out.normal.x, invRadius * out.normal.y, invRadius * out.normal.z};
    out.t = t;
    return true;
}

// phCollisionPrim::SegmentSphereTest (centre, SQUARED radius, a, b): whether
// the segment a -> b comes within the sphere (closest point distance^2 <=
// radius2).
bool segmentSphereTest(const Vec3& center, float radius2, const Vec3& a, const Vec3& b) {
    Vec3 d = a - center;
    const Vec3 e = b - a;
    const float dot = (d.z * e.z + e.x * d.x) + d.y * e.y;
    if (dot < 0.0f) {
        const float ee = (e.z * e.z + e.x * e.x) + e.y * e.y;
        if (-dot < ee) {
            const float dd = (d.z * d.z + d.y * d.y) + d.x * d.x;
            return !(ee * radius2 < dd * ee - dot * dot);
        }
        d = b - center;
    }
    return (d.z * d.z + d.y * d.y) + d.x * d.x <= radius2;
}

// phCollisionPrim::SegmentSphereTest (centre, radius, segment).
bool segmentSphereTest(const Vec3& center, float radius, const Segment& seg) {
    Vec3 d = seg.a - center;
    const Vec3 e = seg.b - seg.a;
    const float dot = (d.z * e.z + d.y * e.y) + e.x * d.x;
    if (dot < 0.0f) {
        const float ee = (e.z * e.z + e.y * e.y) + e.x * e.x;
        if (-dot < ee) {
            const float dd = (d.z * d.z + d.y * d.y) + d.x * d.x;
            return !(ee * radius * radius < dd * ee - dot * dot);
        }
        d = seg.b - center;
    }
    return !(radius * radius < (d.z * d.z + d.y * d.y) + d.x * d.x);
}

// phCollisionPrim::SegmentSphereTest (radius, segment): the sphere at the
// origin.
bool segmentSphereTest(float radius, const Segment& seg) {
    const Vec3& a = seg.a;
    const Vec3 e = seg.b - seg.a;
    const float dot = (e.z * a.z + e.y * a.y) + e.x * a.x;
    Vec3 p = a;
    if (dot < 0.0f) {
        const float ee = (e.z * e.z + e.y * e.y) + e.x * e.x;
        if (-dot < ee) {
            const float aa = (a.x * a.x + a.y * a.y) + a.z * a.z;
            return !(ee * radius * radius < aa * ee - dot * dot);
        }
        p = seg.b;
    }
    return !(radius * radius < (p.x * p.x + p.y * p.y) + p.z * p.z);
}

// phCollisionPrim::SegmentSphereTest (SQUARED radius, a, b): the sphere at
// the origin.
bool segmentSphereTest(float radius2, const Vec3& a, const Vec3& b) {
    const Vec3 e = b - a;
    const float dot = (e.z * a.z + e.y * a.y) + e.x * a.x;
    Vec3 p = a;
    if (dot < 0.0f) {
        const float ee = (e.z * e.z + e.x * e.x) + e.y * e.y;
        if (-dot < ee) {
            const float aa = (a.z * a.z + a.y * a.y) + a.x * a.x;
            return !(ee * radius2 < aa * ee - dot * dot);
        }
        p = b;
    }
    return (p.z * p.z + p.y * p.y) + p.x * p.x <= radius2;
}

} // namespace mm2::phys::geom
