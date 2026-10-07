#include "phys/Bound.h"
#include "phys/Collision.h"
#include "phys/Geometry.h"

#include <array>
#include <cfloat>
#include <cmath>
#include <span>

// phBoundHotdog's segment tests and impact searches, ported from
// midtown2.exe build 3393 (MM2Recomp). A hotdog (capsule) runs along its
// local y axis: a shaft of `height` between the centres of two end spheres
// of `capRadius`. Ends and caps are numbered 0 (bottom, -y) and 1 (top, +y);
// 2 is the shaft. Sums follow the original's order (32-bit float math).

namespace mm2::phys {
namespace {

float invSqrtOrZero(float m2) {
    return m2 == 0.0f ? 0.0f : 1.0f / std::sqrt(m2);
}

// Vector3::Dot(const Vector3&) with a matrix row: (v.z r.z + v.y r.y) + v.x r.x.
float rowDot(const Vec3& row, const Vec3& v) {
    return (v.z * row.z + v.y * row.y) + v.x * row.x;
}

// phBoundHotdog::IsInsideHotdog (p in the hotdog's centred space).
bool isInsideHotdog(const BoundHotdog& hotdog, const Vec3& p) {
    const float half = hotdog.height * 0.5f;
    const float reach = half + hotdog.capRadius;
    if (reach < p.y || p.y < -reach)
        return false;
    const float radial2 = p.x * p.x + p.z * p.z;
    const float cap2 = hotdog.capRadius * hotdog.capRadius;
    if (cap2 < radial2)
        return false;
    if (half < p.y) {
        const float dy = p.y - half;
        if (cap2 < dy * dy + radial2)
            return false;
    }
    if (p.y < -half) {
        const float dy = half + p.y;
        if (cap2 < dy * dy + radial2)
            return false;
    }
    return true;
}

// phBoundHotdog::FindHotdogIsectNormal: the outward unit normal at the
// surface point p (centred space) of feature 0 (bottom cap), 1 (top cap)
// or 2 (shaft).
Vec3 findHotdogIsectNormal(const BoundHotdog& hotdog, const Vec3& p, int feature) {
    Vec3 v;
    if (feature == 2)
        v = {p.x, 0.0f, p.z};
    else if (feature == 1)
        v = {p.x, p.y - hotdog.height * 0.5f, p.z};
    else
        v = {p.x, hotdog.height * 0.5f + p.y, p.z};
    const float inv = invSqrtOrZero((v.z * v.z + v.x * v.x) + v.y * v.y);
    return {inv * v.x, inv * v.y, inv * v.z};
}

// phBoundHotdog::SegmentToHotdogIntersections: where start + t * dir
// crosses the hotdog's surface. `start` is in a space shifted up by
// height / 2 (the callers add it), so the bottom cap's sphere is centred at
// the origin and the top cap's at y = height. Writes up to two crossings in
// t order: t, the height fraction y (0 on the bottom cap, 1 on the top cap,
// else the cylinder's) and the feature f (0 bottom, 1 top, 2 shaft).
// Returns the number found.
//
// As the original: the cylinder's height fraction is measured from the
// centred convention (SegmentToUprightCylIsects adds 0.5) although `start`
// is shifted, so the shaft only counts crossings at y in [-height/2,
// height/2] of the shifted space (centred y in [-height, 0]); the caps
// still cover their own spheres. (Inferred to be an original slip; kept.)
int segmentToHotdogIntersections(const BoundHotdog& hotdog, const Vec3& start, const Vec3& dir, float& t0, float& t1,
                                 float& y0, float& y1, int& f0, int& f1) {
    const float height = hotdog.height;
    const Vec3 topStart{start.x, start.y - height, start.z};
    const float radius2 = hotdog.capRadius * hotdog.capRadius;
    float scratch = 0; // second crossings the original discards
    const auto bottomCap = [&](float& ta, float& tb) {
        return geom::segmentToHemisphereIntersections(start, dir, radius2, ta, tb, false);
    };
    const auto topCap = [&](float& ta, float& tb) {
        return geom::segmentToHemisphereIntersections(topStart, dir, radius2, ta, tb, true);
    };
    const auto onShaft = [](float y) { return !(y < 0.0f) && !(y > 1.0f); };
    const auto order = [&] { geom::orderIntersections(t0, t1, y0, y1, f0, f1); };

    const int cylinder = geom::segmentToUprightCylIsects(start, dir, height, radius2, t0, t1, y0, y1);
    if (cylinder == 1 && onShaft(y0)) {
        // Starts inside the infinite cylinder, leaves through the shaft;
        // the other crossing (if any) is on a cap.
        f0 = 2;
        if (bottomCap(t1, scratch) != 0) {
            y1 = 0.0f;
            f1 = 0;
            order();
            return 2;
        }
        if (topCap(t1, scratch) == 0)
            return 1;
        y1 = 1.0f;
        f1 = 1;
        order();
        return 2;
    }
    if (cylinder == 2) {
        if (onShaft(y0) && onShaft(y1)) {
            f0 = 2;
            f1 = 2;
            return 2;
        }
        if (y0 < 0.0f && y1 < 0.0f) {
            const int count = bottomCap(t0, t1);
            if (count <= 0)
                return count;
            y0 = 0.0f;
            y1 = 0.0f;
            f0 = 0;
            f1 = 0;
            return count;
        }
        if (y0 > 1.0f && y1 > 1.0f) {
            const int count = topCap(t0, t1);
            if (count > 0) {
                y0 = 1.0f;
                y1 = 1.0f;
                f0 = 1;
                f1 = 1;
            }
            return count;
        }
        if ((y0 < 0.0f && y1 > 1.0f) || (y0 > 1.0f && y1 < 0.0f)) {
            // Through both cap regions.
            if (bottomCap(t0, scratch) == 0) {
                topCap(t0, scratch);
                y0 = 1.0f;
                f0 = 1;
                return 1;
            }
            y0 = 0.0f;
            f0 = 0;
            if (topCap(t1, scratch) == 0)
                return 1;
            y1 = 1.0f;
            f1 = 1;
            order();
            return 2;
        }
        // One crossing on the shaft, the other in a cap region.
        if (onShaft(y0)) {
            f0 = 2;
            if (y1 < 0.0f) {
                bottomCap(t1, scratch);
                y1 = 0.0f;
                f1 = 0;
            } else {
                topCap(t1, scratch);
                y1 = 1.0f;
                f1 = 1;
            }
        } else {
            if (y0 < 0.0f) {
                bottomCap(t0, scratch);
                y0 = 0.0f;
                f0 = 0;
            } else {
                topCap(t0, scratch);
                y0 = 1.0f;
                f0 = 1;
            }
            f1 = 2;
        }
        order();
        return 2;
    }

    // No usable cylinder crossing: the caps alone.
    int count = bottomCap(t0, t1);
    if (count == 2) {
        y0 = 0.0f;
        y1 = 0.0f;
        f0 = 0;
        f1 = 0;
        return 2;
    }
    if (count == 1) {
        y0 = 0.0f;
        f0 = 0;
        if (topCap(t1, scratch) == 0)
            return 1;
        y1 = 1.0f;
        f1 = 1;
        order();
        return 2;
    }
    count = topCap(t0, t1);
    if (count > 0) {
        y0 = 1.0f;
        y1 = 1.0f;
        f0 = 1;
        f1 = 1;
    }
    return count;
}

// phIntersectionPoint::Set plus the polygon index, which hotdogs use
// for the feature (0 bottom cap, 1 top cap, 2 shaft).
void setIntersection(Intersection& out, const Vec3& position, const Vec3& normal, float t, float depth,
                     bool endBehind, int feature) {
    out.position = position;
    out.normal = normal;
    out.t = t;
    out.depth = depth;
    out.bInside = endBehind;
    out.polygon = feature;
}

// Matrix34 transform of a point in the order MM2's hotdog code evaluates it
// in several places: ((x m0 + z m2) + y m1) + m3, per component.
Vec3 transformXZY(const Mat34& m, const Vec3& p) {
    return {((p.x * m.m0.x + p.z * m.m2.x) + p.y * m.m1.x) + m.m3.x,
            ((p.x * m.m0.y + p.z * m.m2.y) + p.y * m.m1.y) + m.m3.y,
            ((p.x * m.m0.z + p.z * m.m2.z) + p.y * m.m1.z) + m.m3.z};
}

} // namespace

// phBoundHotdog::TestEdge: where the segment (bound space) crosses the
// surface. Nothing when both ends are inside. Each crossing gets the
// position, the outward unit normal of its feature, t, depth = normal .
// (position - seg.b), bInside = "a is outside" and the feature in
// `polygon`. Returns the number written. Two quirks of the original are
// kept: the second crossing's position is computed as b + (a - b) * t1 + b
// and its normal from b + (a - b) * t1 + (b - centroid) (it looks like a
// slip; that crossing is rarely used), and `max` is ignored by the original
// (here the second crossing is only written when max >= 2).
int BoundHotdog::testEdge(Segment& seg, Intersection* out, int max) const {
    Vec3 a = seg.a;
    Vec3 b = seg.b;
    if (isOffset) {
        a -= centroid;
        b -= centroid;
    }
    const bool startInside = isInsideHotdog(*this, a);
    if (startInside && isInsideHotdog(*this, b))
        return 0;

    const Vec3 start{a.x, height * 0.5f + a.y, a.z};
    const Vec3 dir = seg.b - seg.a;
    float t0 = 0, t1 = 0, y0 = 0, y1 = 0;
    int f0 = 0, f1 = 0;
    const int count = segmentToHotdogIntersections(*this, start, dir, t0, t1, y0, y1, f0, f1);
    if (count == 0)
        return 0;

    const Vec3 along = dir * t0;
    const Vec3 normal0 = findHotdogIsectNormal(*this, along + a, f0);
    const Vec3 position0 = seg.a + along;
    const Vec3 w0 = position0 - seg.b;
    setIntersection(out[0], position0, normal0, t0, (normal0.z * w0.z + normal0.y * w0.y) + normal0.x * w0.x,
                    !startInside, f0);
    if (count == 1 || max < 2)
        return 1;

    const Vec3 back{(seg.a.x - seg.b.x) * t1 + seg.b.x, (seg.a.y - seg.b.y) * t1 + seg.b.y,
                    (seg.a.z - seg.b.z) * t1 + seg.b.z};
    const Vec3 normal1 = findHotdogIsectNormal(*this, back + b, f1);
    const Vec3 position1 = back + seg.b;
    const Vec3 w1 = position1 - seg.b;
    setIntersection(out[1], position1, normal1, t1, (normal1.z * w1.z + normal1.y * w1.y) + normal1.x * w1.x,
                    !startInside, f1);
    return 2;
}

// phBoundHotdog::TestProbe: where a segment starting outside the hotdog
// (bound space) first crosses its surface, if at t <= maxT: position,
// outward unit normal, t, depth = normal . (position - seg.b), bInside
// true and the feature (0 bottom cap, 1 top cap, 2 shaft) in `polygon`.
bool BoundHotdog::testProbe(Segment& seg, Intersection& out, float maxT) const {
    Vec3 a = seg.a;
    if (isOffset)
        a -= centroid;
    if (isInsideHotdog(*this, a))
        return false;

    const Vec3 start{a.x, height * 0.5f + a.y, a.z};
    const Vec3 dir = seg.b - seg.a;
    float t0 = 0, t1 = 0, y0 = 0, y1 = 0;
    int f0 = 0, f1 = 0;
    if (segmentToHotdogIntersections(*this, start, dir, t0, t1, y0, y1, f0, f1) == 0 || t0 > maxT)
        return false;
    const Vec3 along = dir * t0;
    const Vec3 normal = findHotdogIsectNormal(*this, along + a, f0);
    const Vec3 position = seg.a + along;
    const Vec3 w = position - seg.b;
    setIntersection(out, position, normal, t0, (normal.z * w.z + normal.y * w.y) + normal.x * w.x, true, f0);
    return true;
}

// phBoundHotdog::FindImpactSphereToHotdog. relPos is the sphere's matrix
// origin minus the hotdog's (world); the sphere's centroid is added through
// sphereM, the hotdog's subtracted in its own space. The impact has A = the
// sphere, B = the hotdog, kind 0, elementA 0 and elementB the hotdog
// feature (0 bottom cap, 1 top cap, 2 shaft); the normal (world, unit)
// points from the hotdog towards the sphere, depth = radii - distance and
// the position lies depth / 2 inside the hotdog's surface along the normal
// (measured from the cap centre / axis placed by hotdogM, ignoring the
// hotdog's centroid as MM2 does). False (no impact) when apart.
bool findImpactSphereToHotdog(const BoundHotdog& hotdog, const BoundSphere& sphere, const Mat34& sphereM,
                              const Mat34& hotdogM, Collider* sphereCollider, Collider* hotdogCollider,
                              Impact& impact, const Vec3& relPos) {
    const Mat34& m = hotdogM;
    const float half = hotdog.height * 0.5f;
    Vec3 p{rowDot(m.m0, relPos), rowDot(m.m1, relPos), rowDot(m.m2, relPos)};
    if (hotdog.isOffset)
        p -= hotdog.centroid;
    if (sphere.isOffset) {
        const Vec3& c = sphere.centroid;
        const Mat34& s = sphereM;
        const Vec3 offset{(s.m2.x * c.z + s.m1.x * c.y) + s.m0.x * c.x, (s.m2.y * c.z + s.m1.y * c.y) + s.m0.y * c.x,
                          (s.m2.z * c.z + s.m1.z * c.y) + s.m0.z * c.x};
        p += Vec3{rowDot(m.m0, offset), rowDot(m.m1, offset), rowDot(m.m2, offset)};
    }
    const float reach = sphere.radius + hotdog.capRadius;

    if (half < p.y || p.y < -half) {
        const bool top = half < p.y;
        const float dy = top ? p.y - half : p.y + half;
        const float dist2 = (dy * dy + p.x * p.x) + p.z * p.z;
        if (reach * reach < dist2)
            return false;
        // The cap-centre-to-sphere direction in world space.
        Vec3 normal;
        if (top) {
            normal = {(p.x * m.m0.x + dy * m.m1.x) + p.z * m.m2.x, (p.z * m.m2.y + dy * m.m1.y) + p.x * m.m0.y,
                      (p.z * m.m2.z + dy * m.m1.z) + p.x * m.m0.z};
        } else {
            normal = {(dy * m.m1.x + p.z * m.m2.x) + p.x * m.m0.x, (p.z * m.m2.y + dy * m.m1.y) + p.x * m.m0.y,
                      (p.z * m.m2.z + dy * m.m1.z) + p.x * m.m0.z};
        }
        normal *= invSqrtOrZero((normal.x * normal.x + normal.y * normal.y) + normal.z * normal.z);
        const float depth = (sphere.radius + hotdog.capRadius) - std::sqrt(dist2);
        const Vec3 cap = top ? Vec3{half * m.m1.x + m.m3.x, half * m.m1.y + m.m3.y, half * m.m1.z + m.m3.z}
                             : Vec3{m.m3.x - half * m.m1.x, m.m3.y - half * m.m1.y, m.m3.z - half * m.m1.z};
        const float out = hotdog.capRadius - depth * 0.5f;
        const Vec3 position{normal.x * out + cap.x, normal.y * out + cap.y, normal.z * out + cap.z};
        impact.makeNewImpact(sphereCollider, hotdogCollider, position, normal, depth, sphere, hotdog, 0, 0,
                             top ? 1 : 0);
        return true;
    }

    const float radial2 = p.x * p.x + p.z * p.z;
    if (radial2 > reach * reach)
        return false;
    const float dist = std::sqrt(radial2);
    Vec3 normal{p.z * m.m2.x + p.x * m.m0.x, p.z * m.m2.y + p.x * m.m0.y, p.z * m.m2.z + p.x * m.m0.z};
    normal *= invSqrtOrZero((normal.z * normal.z + normal.y * normal.y) + normal.x * normal.x);
    const float depth = (sphere.radius + hotdog.capRadius) - dist;
    const Vec3 axisPoint{p.y * m.m1.x + m.m3.x, p.y * m.m1.y + m.m3.y, p.y * m.m1.z + m.m3.z};
    const float out = hotdog.capRadius - depth * 0.5f;
    const Vec3 position{out * normal.x + axisPoint.x, normal.y * out + axisPoint.y, normal.z * out + axisPoint.z};
    impact.makeNewImpact(sphereCollider, hotdogCollider, position, normal, depth, sphere, hotdog, 0, 0, 2);
    return true;
}

// phBoundHotdog::FindImpactsHotdogToPoly. A = the hotdog (hotdogCollider),
// B = the polygonal bound; relPos is B's matrix origin minus the hotdog's
// (world), relDisp is used as the hotdog's displacement back to where it was
// at the start of the sample, relative to B (an end at e was at e +
// relDisp). Normals point from B towards the hotdog. Four passes, stopping
// when `max` impacts are made:
//  1. each end sphere against each polygon (FindImpactPolygonToSphere),
//     keeping vertex/edge contacts in front of the end (along the axis
//     outward) and face contacts whose face is not turned along the axis by
//     more than 0.06; an end that does not touch a face it is moving
//     towards (normal . relDisp > 0, normal . outward < 0.1) but swept
//     through it this sample (phPolygon::TestSegmentDirected from e +
//     relDisp to e) gets a face impact as deep as the face lies behind the
//     sphere's far point. elementA = the end (0, 1), elementB = vertex,
//     edge or polygon index; deduplicated with ImpactIsInList and
//     AddImpactSpherePlaneTest.
//  2. each edge against the shaft (FindImpactEdgeToShaft): elementA 2,
//     elementB the nearer vertex or the edge; when the hotdog moves away
//     from the edge faster than capRadius per sample (normal . relDisp < 0,
//     |relDisp| > capRadius) the axis has passed the edge, so the normal is
//     flipped and depth = 2 capRadius - depth. AddImpactShaftPlaneTest
//     deduplicates.
//  3. the axis segment through B's polygons (phBound::TestSegment, 4
//     intersections): per pierced polygon, the polygon edge whose closest
//     point to the axis gives the best push (against relDisp when within
//     |relDisp|, else the nearest edge whose end region has no impact yet)
//     makes one edge impact (elementA 2) of depth distance + capRadius.
//     If any is made, passes 4 is skipped.
//  4. when |relDisp| > capRadius: each vertex of B swept (relative to the
//     hotdog) through the hotdog (TestProbe): elementA = the feature hit,
//     elementB = the vertex; it replaces an existing impact of the same
//     pair.
// Finally CullImpactList along relDisp. Returns the number of impacts.
int findImpactsHotdogToPoly(const BoundHotdog& hotdog, const BoundPolygonal& poly, const Mat34& hotdogM,
                            const Mat34& polyM, Collider* hotdogCollider, Collider* polyCollider, Impact* impacts,
                            int max, const Vec3& relPos, const Vec3& relDisp) {
    const Mat34& hm = hotdogM;
    const Mat34& pm = polyM;
    const float capRadius = hotdog.capRadius;

    // The hotdog's centre and axis in B's space.
    Vec3 rel = relPos;
    if (hotdog.isOffset) {
        const Vec3& c = hotdog.centroid;
        rel -= Vec3{(hm.m2.x * c.z + hm.m1.x * c.y) + c.x * hm.m0.x, (hm.m2.y * c.z + hm.m0.y * c.x) + hm.m1.y * c.y,
                    (hm.m2.z * c.z + hm.m0.z * c.x) + hm.m1.z * c.y};
    }
    const Vec3 center{-((rel.x * pm.m0.x + rel.z * pm.m0.z) + rel.y * pm.m0.y), -rowDot(pm.m1, rel),
                      -rowDot(pm.m2, rel)};
    const Vec3& up = hm.m1;
    const Vec3 axis{(up.z * pm.m0.z + up.y * pm.m0.y) + up.x * pm.m0.x, rowDot(pm.m1, up), rowDot(pm.m2, up)};
    // Per end: the outward axis direction and the end sphere's centre.
    const std::array<Vec3, 2> outward{-axis, axis};
    const float half = hotdog.height * 0.5f;
    std::array<Vec3, 2> ends;
    for (std::size_t i = 0; i < 2; ++i)
        ends[i] = center + outward[i] * half;

    const Vec3 disp{(pm.m0.z * relDisp.z + pm.m0.y * relDisp.y) + relDisp.x * pm.m0.x, rowDot(pm.m1, relDisp),
                    rowDot(pm.m2, relDisp)};
    const float disp2 = (disp.z * disp.z + disp.y * disp.y) + disp.x * disp.x;

    std::array<Intersection, 4> isects{};
    const std::span<const Vec3> verts(poly.vertices);
    int count = 0;
    bool full = false;
    // Per end: a pass 1 or 2 impact was made at (or nearest) it.
    std::array<bool, 2> endHit{};

    // Pass 1: the end spheres against the polygons.
    for (int polyIndex = 0; polyIndex < poly.numPolygons() && !full; ++polyIndex) {
        const Polygon& polygon = poly.polygons[static_cast<std::size_t>(polyIndex)];
        const int vertexCount = polygon.vertexCount();
        std::array<Vec3, 4> corners;
        for (int k = 0; k < vertexCount; ++k)
            corners[static_cast<std::size_t>(k)] = poly.vertex(polygon.v[static_cast<std::size_t>(k)]);
        const Vec3& n = polygon.normal;

        for (int end = 0; end < 2 && !full; ++end) {
            const Vec3& e = ends[static_cast<std::size_t>(end)];
            const Vec3& out = outward[static_cast<std::size_t>(end)];
            Vec3 position, normal;
            int feature = 0;
            float depth = 0;
            int result = geom::findImpactPolygonToSphere(e, capRadius, corners.data(), vertexCount, n, position,
                                                         feature, normal, depth);
            int elementB = 0;
            if (result == 0 || result == 1) {
                const Vec3 w = position - e;
                if ((w.x * out.x + w.z * out.z) + w.y * out.y < 0.0f)
                    continue;
                const auto slot = static_cast<std::size_t>(feature);
                elementB = result == 0 ? polygon.v[slot] : polygon.edges[slot];
            } else if (result == 2) {
                if ((n.x * out.x + n.z * out.z) + n.y * out.y > 0.06f)
                    continue;
                elementB = polyIndex;
            } else {
                if ((n.z * disp.z + n.y * disp.y) + n.x * disp.x <= 0.0f)
                    continue;
                if (!((n.x * out.x + n.z * out.z) + n.y * out.y < 0.1f))
                    continue;
                Segment sweep;
                sweep.kind = Segment::Probe;
                sweep.a = e + disp;
                sweep.b = e;
                if (!polygon.testSegmentDirected(verts, sweep, isects[0], 2.0f))
                    continue;
                normal = n;
                result = 2;
                position = {e.x - capRadius * n.x, e.y - n.y * capRadius, e.z - capRadius * n.z};
                const Vec3 w = corners[0] - position;
                depth = (w.z * n.z + w.y * n.y) + w.x * n.x;
                const float h = depth * 0.5f;
                position = {h * n.x + position.x, n.y * h + position.y, n.z * h + position.z};
                elementB = polyIndex;
            }

            if (count > 0 && Impact::impactIsInList(end, elementB, result, impacts, count) >= 0)
                continue;
            impacts[count].startMakingNewImpact(depth, normal, position, hotdogCollider, polyCollider, &polyM, end,
                                                -1, -1);
            if (count > 0) {
                const Vec3 endWorld{((pm.m2.x * e.z + pm.m1.x * e.y) + e.x * pm.m0.x) + pm.m3.x,
                                    ((pm.m2.y * e.z + pm.m1.y * e.y) + pm.m0.y * e.x) + pm.m3.y,
                                    ((pm.m2.z * e.z + pm.m1.z * e.y) + pm.m0.z * e.x) + pm.m3.z};
                if (!Impact::addImpactSpherePlaneTest(impacts, count, endWorld, capRadius))
                    continue;
            }
            impacts[count].finishMakingNewImpact(result, elementB, hotdog, poly, end);
            ++count;
            endHit[static_cast<std::size_t>(end)] = true;
            if (count == max)
                full = true;
        }
    }

    // Pass 2: the edges against the shaft.
    for (int edge = 0; edge < poly.numEdges() && !full; ++edge) {
        const auto& edgeVerts = poly.edges[static_cast<std::size_t>(edge)];
        const Vec3 v0 = poly.vertex(edgeVerts[0]);
        const Vec3 v1 = poly.vertex(edgeVerts[1]);
        int feature = 0;
        Vec3 normal, shaftPoint;
        float depth = 0;
        const int result =
            geom::findImpactEdgeToShaft(ends[0], ends[1], v0, v1, capRadius, feature, normal, depth, shaftPoint);
        if (result != 0 && result != 1)
            continue;
        const int elementB = result == 1 ? edge : (feature == 1 ? edgeVerts[0] : edgeVerts[1]);
        const float out = capRadius - depth * 0.5f;
        Vec3 position{shaftPoint.x - out * normal.x, shaftPoint.y - normal.y * out, shaftPoint.z - normal.z * out};
        if ((normal.x * disp.x + normal.z * disp.z) + normal.y * disp.y < 0.0f && capRadius * capRadius < disp2) {
            depth = (capRadius + capRadius) - depth;
            position = {capRadius * normal.x + position.x, normal.y * capRadius + position.y,
                        normal.z * capRadius + position.z};
            normal = -normal;
        }
        if (count > 0 && Impact::impactIsInList(2, elementB, result, impacts, count) >= 0)
            continue;
        impacts[count].startMakingNewImpact(depth, normal, position, hotdogCollider, polyCollider, &polyM, 2, -1,
                                            -1);
        if (count > 0 && !Impact::addImpactShaftPlaneTest(impacts, count, hm.m3, capRadius, hm.m1))
            continue;
        impacts[count].finishMakingNewImpact(result, elementB, hotdog, poly, 2);
        ++count;
        const Vec3 fromBottom = shaftPoint - ends[0];
        if ((fromBottom.z * fromBottom.z + fromBottom.y * fromBottom.y) + fromBottom.x * fromBottom.x <= half * half)
            endHit[0] = true;
        else
            endHit[1] = true;
        if (count == max)
            full = true;
    }

    // Pass 3: the axis through B's polygons.
    int pierced = 0;
    if (!full) {
        Segment axisSegment;
        axisSegment.kind = Segment::Edge;
        axisSegment.a = ends[0];
        axisSegment.b = ends[1];
        pierced = poly.testSegment(axisSegment, isects.data(), 4);
    }
    if (pierced > 0 && !full) {
        const Vec3 axisVector = ends[1] - ends[0];
        bool madeEdgeImpact = false;
        for (int i = 0; i < pierced && !full; ++i) {
            const Polygon& polygon = poly.polygons[static_cast<std::size_t>(isects[static_cast<std::size_t>(i)].polygon)];
            float best = FLT_MAX;
            bool found = false;
            bool within = false; // the kept edge point lies within |relDisp| of the axis
            int elementB = 0;
            Vec3 push, edgePoint;
            const int vertexCount = polygon.vertexCount();
            for (int k = 0; k < vertexCount; ++k) {
                const int edgeIndex = polygon.edges[static_cast<std::size_t>(k)];
                const auto& edgeVerts = poly.edges[static_cast<std::size_t>(edgeIndex)];
                Vec3 q = verts[edgeVerts[0]];
                const Vec3 d = verts[edgeVerts[1]] - q;
                float tEdge = 0;
                float tAxis = 0;
                geom::findTValuesSegToSeg(q, d, ends[0], axisVector, tEdge, tAxis);
                q = {d.x * tEdge + q.x, d.y * tEdge + q.y, d.z * tEdge + q.z};
                const Vec3 s{axisVector.x * tAxis + ends[0].x, axisVector.y * tAxis + ends[0].y,
                             axisVector.z * tAxis + ends[0].z};
                const Vec3 w = s - q;
                const float dist2 = (w.z * w.z + w.y * w.y) + w.x * w.x;
                const bool near = !(disp2 < dist2);
                float value = 0;
                if (!near) {
                    const bool endTaken = tAxis <= 0.5f ? endHit[0] : endHit[1];
                    if (endTaken || within || !(dist2 < best))
                        continue;
                    value = dist2;
                } else {
                    value = -((w.z * disp.z + w.y * disp.y) + w.x * disp.x);
                    if (within && !(best < value))
                        continue;
                }
                best = value;
                elementB = edgeIndex;
                push = -w;
                edgePoint = q;
                found = true;
                if (near)
                    within = true;
            }
            if (!found)
                continue;
            if (count > 0 && Impact::impactIsInList(2, elementB, 1, impacts, count) >= 0)
                continue;
            const float len = std::sqrt((push.x * push.x + push.y * push.y) + push.z * push.z);
            push = {push.x / len, push.y / len, push.z / len};
            const float depth = len + capRadius;
            const float h = depth * 0.5f;
            const Vec3 position{edgePoint.x - h * push.x, edgePoint.y - push.y * h, edgePoint.z - push.z * h};
            impacts[count].startMakingNewImpact(depth, push, position, hotdogCollider, polyCollider, &polyM, 2, -1,
                                                -1);
            impacts[count].finishMakingNewImpact(1, elementB, hotdog, poly, 2);
            ++count;
            madeEdgeImpact = true;
            if (count == max)
                full = true;
        }
        if (madeEdgeImpact) {
            if (count > 1)
                Impact::cullImpactList(impacts, count, relDisp);
            return count;
        }
    }

    // Pass 4: B's vertices swept through the hotdog (fast motion only).
    if (capRadius * capRadius < disp2 && !full) {
        const auto toHotdog = [&](const Vec3& p) {
            const Vec3 u = p - hm.m3;
            return Vec3{(u.x * hm.m0.x + u.z * hm.m0.z) + u.y * hm.m0.y, (u.z * hm.m1.z + u.y * hm.m1.y) + u.x * hm.m1.x,
                        (u.z * hm.m2.z + u.y * hm.m2.y) + u.x * hm.m2.x};
        };
        for (int k = 0; k < poly.numVertices(); ++k) {
            const Vec3& v = poly.vertex(k);
            const Vec3 now{((pm.m2.x * v.z + v.y * pm.m1.x) + v.x * pm.m0.x) + pm.m3.x,
                           ((v.z * pm.m2.y + v.y * pm.m1.y) + v.x * pm.m0.y) + pm.m3.y,
                           ((v.z * pm.m2.z + v.y * pm.m1.z) + v.x * pm.m0.z) + pm.m3.z};
            const Vec3 before = now - relDisp;
            Segment sweep;
            sweep.kind = Segment::Probe;
            sweep.b = toHotdog(now);
            sweep.a = toHotdog(before);
            Intersection& hit = isects[0];
            if (!hotdog.testProbe(sweep, hit, 2.0f))
                continue;
            const int feature = hit.polygon;
            if (count > 0) {
                const int index = Impact::impactIsInList(feature, k, 0, impacts, count);
                if (index >= 0) {
                    // Replaces the earlier impact of this pair.
                    --count;
                    for (int m = index; m < count; ++m)
                        impacts[m] = impacts[m + 1];
                }
            }
            // The original does not check max here and would write past
            // the caller's buffer; skip instead.
            if (count >= max)
                continue;
            Impact& impact = impacts[count];
            impact.depth = hit.depth;
            const Vec3& n = hit.normal;
            impact.normal = {-((n.x * hm.m0.x + n.z * hm.m2.x) + n.y * hm.m1.x),
                             -((n.z * hm.m2.y + n.y * hm.m1.y) + n.x * hm.m0.y),
                             -((n.z * hm.m2.z + n.y * hm.m1.z) + n.x * hm.m0.z)};
            const Vec3& p = hit.position;
            impact.position = {((p.x * hm.m0.x + p.z * hm.m2.x) + p.y * hm.m1.x) + hm.m3.x,
                               ((p.z * hm.m2.y + p.y * hm.m1.y) + p.x * hm.m0.y) + hm.m3.y,
                               ((p.z * hm.m2.z + p.y * hm.m1.z) + p.x * hm.m0.z) + hm.m3.z};
            impact.colliderA = hotdogCollider;
            impact.colliderB = polyCollider;
            impact.componentA = -1;
            impact.componentB = -1;
            impact.elementA = feature;
            if (count > 0) {
                bool keep = false;
                if (feature == 2) {
                    keep = Impact::addImpactShaftPlaneTest(impacts, count, hm.m3, capRadius, hm.m1);
                } else {
                    const Vec3& e = ends[static_cast<std::size_t>(impact.elementA)];
                    const Vec3 endWorld{((pm.m2.x * e.z + e.x * pm.m0.x) + pm.m1.x * e.y) + pm.m3.x,
                                        ((pm.m2.y * e.z + pm.m1.y * e.y) + e.x * pm.m0.y) + pm.m3.y,
                                        ((e.x * pm.m0.z + e.z * pm.m2.z) + e.y * pm.m1.z) + pm.m3.z};
                    keep = Impact::addImpactSpherePlaneTest(impacts, count, endWorld, capRadius);
                }
                if (!keep)
                    continue;
            }
            impacts[count].finishMakingNewImpact(0, k, hotdog, poly, feature);
            ++count;
        }
    }

    if (count > 1)
        Impact::cullImpactList(impacts, count, relDisp);
    return count;
}

// phBoundHotdog::FindImpactsHotdogToHotdog. Works in B's space: the closest
// points of the two axis segments (FindTValuesSegToSeg). Apart (radii <
// distance): 0. The normal (world, unit) points from B's axis towards A's
// and every impact uses it; kind 0; elements are 0 (an axis' start, -y
// end), 1 (its +y end) or 2 (the shaft between); positions lie depth / 2
// inside A's surface from A's axis point.
//  - Not nearly parallel (|dA x dB|^2 >= 0.001225 (hA hB)^2), or both
//    closest-point parameters outside (0, 1): one impact at the closest
//    points.
//  - Nearly parallel and overlapping: up to two impacts at the two ends of
//    the overlap along A (the second only when max > 1 and its own depth,
//    from its distance to B's axis line, is positive).
// Returns the number of impacts.
int findImpactsHotdogToHotdog(const BoundHotdog& a, const BoundHotdog& b, const Mat34& ma, const Mat34& mb,
                              Collider* ca, Collider* cb, Impact* impacts, int max) {
    // B's axis in its own space.
    Vec3 bStart{0.0f, b.height * -0.5f, 0.0f};
    if (b.isOffset)
        bStart += b.centroid;
    const Vec3 bDir{0.0f, b.height, 0.0f};

    // A's axis in A's space, then in B's.
    Vec3 aStart{0.0f, a.height * -0.5f, 0.0f};
    if (a.isOffset)
        aStart = {a.centroid.x, a.height * -0.5f + a.centroid.y, a.centroid.z};
    const Vec3 aDir{0.0f, a.height, 0.0f};
    const Vec3 aStartWorld{((aStart.z * ma.m2.x + aStart.x * ma.m0.x) + aStart.y * ma.m1.x) + ma.m3.x,
                           ((aStart.z * ma.m2.y + aStart.y * ma.m1.y) + aStart.x * ma.m0.y) + ma.m3.y,
                           ((aStart.x * ma.m0.z + aStart.z * ma.m2.z) + aStart.y * ma.m1.z) + ma.m3.z};
    const Vec3 w = aStartWorld - mb.m3;
    const Vec3 aStartB{rowDot(mb.m0, w), rowDot(mb.m1, w), rowDot(mb.m2, w)};
    const Vec3 aDirWorld{(aDir.z * ma.m2.x + aDir.x * ma.m0.x) + aDir.y * ma.m1.x,
                         (aDir.y * ma.m1.y + aDir.z * ma.m2.y) + aDir.x * ma.m0.y,
                         (aDir.x * ma.m0.z + aDir.z * ma.m2.z) + aDir.y * ma.m1.z};
    const Vec3 aDirB{(aDirWorld.x * mb.m0.x + aDirWorld.z * mb.m0.z) + aDirWorld.y * mb.m0.y,
                     rowDot(mb.m1, aDirWorld), rowDot(mb.m2, aDirWorld)};

    float tA = 0;
    float tB = 0;
    geom::findTValuesSegToSeg(aStartB, aDirB, bStart, bDir, tA, tB);
    const Vec3 pointA{aDirB.x * tA + aStartB.x, aDirB.y * tA + aStartB.y, aDirB.z * tA + aStartB.z};
    const Vec3 pointB{bDir.x * tB + bStart.x, bDir.y * tB + bStart.y, bDir.z * tB + bStart.z};
    const Vec3 diff = pointA - pointB;
    const float dist = std::sqrt((diff.y * diff.y + diff.z * diff.z) + diff.x * diff.x);
    const float depth = (b.capRadius + a.capRadius) - dist;
    if (depth < 0.0f)
        return 0;

    const float inv = 1.0f / dist;
    const Vec3 n = diff * inv;
    const Vec3 normal{(n.x * mb.m0.x + n.z * mb.m2.x) + n.y * mb.m1.x, (n.x * mb.m0.y + n.z * mb.m2.y) + n.y * mb.m1.y,
                      (n.x * mb.m0.z + n.z * mb.m2.z) + n.y * mb.m1.z};

    // A point on A's axis (B space) to the impact position (world).
    const auto impactPosition = [&](const Vec3& onA, float impactDepth) {
        const Vec3 world = transformXZY(mb, onA);
        const float out = a.capRadius - impactDepth * 0.5f;
        return Vec3{world.x - out * normal.x, world.y - normal.y * out, world.z - normal.z * out};
    };

    const float cz = aDirB.x * bDir.y - bDir.x * aDirB.y;
    const float cx = bDir.z * aDirB.y - aDirB.z * bDir.y;
    const float cy = aDirB.z * bDir.x - bDir.z * aDirB.x;
    const float heights = b.height * a.height;
    const bool nearlyParallel = !(heights * heights * 0.001225f <= (cz * cz + cy * cy) + cx * cx);
    const bool overlap = (0.0f < tA && tA < 1.0f) || (0.0f < tB && tB < 1.0f);
    if (!nearlyParallel || !overlap) {
        const int elementA = tA <= 0.0f ? 0 : (tA < 1.0f ? 2 : 1);
        const int elementB = tB <= 0.0f ? 0 : (tB < 1.0f ? 2 : 1);
        impacts[0].makeNewImpact(ca, cb, impactPosition(pointA, depth), normal, depth, a, b, 0, elementA, elementB);
        return 1;
    }

    // Nearly parallel: the overlap's ends along A (as fractions of A's axis).
    const float aEndY = aDirB.y + aStartB.y;
    const float bEndY = bDir.y + bStart.y;
    float t1 = 0, t2 = 0;
    int elementA1 = 0, elementB1 = 0, elementA2 = 0, elementB2 = 0;
    if (aStartB.y < bStart.y) {
        elementA1 = 2;
        elementB1 = 0;
        t1 = (bStart.y - aStartB.y) / a.height;
        t2 = (bEndY - aStartB.y) / a.height;
        if (t2 > 1.0f) {
            t2 = 1.0f;
            elementA2 = 1;
            elementB2 = 2;
        } else {
            elementA2 = 2;
            elementB2 = 1;
        }
    } else if (aStartB.y > bEndY) {
        elementA1 = 2;
        elementB1 = 1;
        t1 = (bEndY - aStartB.y) / a.height;
        t2 = (bStart.y - aStartB.y) / a.height;
        if (t2 < 0.0f) {
            t2 = 0.0f;
            elementA2 = 1;
            elementB2 = 2;
        } else {
            elementA2 = 2;
            elementB2 = 0;
        }
    } else {
        elementA1 = 0;
        elementB1 = 2;
        t1 = 0.0f;
        const float u = (aEndY - bStart.y) / b.height;
        if (u < 0.0f) {
            t2 = (bStart.y - aStartB.y) / a.height;
            elementA2 = 2;
            elementB2 = 0;
        } else if (u <= 1.0f) {
            t2 = 1.0f;
            elementA2 = 1;
            elementB2 = 2;
        } else {
            t2 = (bEndY - aStartB.y) / a.height;
            elementA2 = 2;
            elementB2 = 1;
        }
    }

    const Vec3 first{aDirB.x * t1 + aStartB.x, aDirB.y * t1 + aStartB.y, aDirB.z * t1 + aStartB.z};
    impacts[0].makeNewImpact(ca, cb, impactPosition(first, depth), normal, depth, a, b, 0, elementA1, elementB1);
    if (max == 1)
        return 1;
    const Vec3 second{aDirB.x * t2 + aStartB.x, t2 * aDirB.y + aStartB.y, aDirB.z * t2 + aStartB.z};
    const float reach = b.capRadius + a.capRadius;
    const float depth2 = reach - geom::distanceLineToPoint(bStart, bDir, second);
    if (depth2 <= 0.0f)
        return 1;
    impacts[1].makeNewImpact(ca, cb, impactPosition(second, depth2), normal, depth2, a, b, 0, elementA2, elementB2);
    return 2;
}

} // namespace mm2::phys
