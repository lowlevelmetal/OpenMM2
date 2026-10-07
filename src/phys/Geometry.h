#pragma once

// The Angel engine's geometry helpers that MM2's collision routines call
// (the free functions of vector7's geometry module, phCollisionPrim and
// phBoundCollision), ported from the code of midtown2.exe build 3393.
// Parameters keep the originals' order; outputs are references.

#include "core/Math.h"

namespace mm2::phys {
struct Segment;
struct IntersectionPoint;
class BoundPolygonal;
} // namespace mm2::phys

namespace mm2::phys::geom {

// --- vector7 geometry (Geometry.cpp) ---
// Several of these take a segment as (start, DIRECTION) rather than two
// points, and some take SQUARED radii; the names below say which. The full
// contracts are in the comments above each definition in Geometry.cpp.

// FindImpactPolygonToSphere: the closest feature of a convex polygon
// (`count` <= 4 vertices, unit face `normal`) to a sphere. Returns 0 for a
// vertex, 1 for an edge (verts[feature] -> verts[feature + 1]), 2 for the
// face (feature 0), 3 when the sphere does not reach it. contactNormal: unit,
// from the polygon towards the centre (the face normal for 2); depth: radius
// - distance; position: the midpoint of the penetration.
int findImpactPolygonToSphere(const Vec3& center, float radius, const Vec3* verts, int count, const Vec3& normal,
                              Vec3& position, int& feature, Vec3& contactNormal, float& depth);
// FindImpactEdgeToShaft: a capsule shaft (shaftA, shaftB) of `radius`
// against an edge (edgeA, edgeB). Returns 1 (closest points inside both
// segments, feature 0), 0 (an edge end is closest: feature 1 = edgeA, 2 =
// edgeB), 3 (apart) or 4 (beyond a shaft end; only `position` written).
// normal: unit, from the edge towards the shaft; position: the point on the
// shaft.
int findImpactEdgeToShaft(const Vec3& shaftA, const Vec3& shaftB, const Vec3& edgeA, const Vec3& edgeB,
                          float radius, int& feature, Vec3& normal, float& depth, Vec3& position);
// IsPointBehindPlane: (point - planePoint) . planeNormal < tolerance.
// IsPointNearPlane: |(point - planePoint) . planeNormal| <= tolerance.
bool isPointBehindPlane(const Vec3& point, const Vec3& planePoint, const Vec3& planeNormal, float tolerance);
bool isPointNearPlane(const Vec3& point, const Vec3& planePoint, const Vec3& planeNormal, float tolerance);
// IsPointInBox: |x| <= hx and so on.
bool isPointInBox(const Vec3& point, float hx, float hy, float hz);
// DistanceLineToPoint: the distance from `point` to the line a + t * dir.
float distanceLineToPoint(const Vec3& a, const Vec3& dir, const Vec3& point);
// FindTValuesSegToSeg: segments a0 + ta * da and b0 + tb * db; true when
// the lines' closest points lie strictly inside both (else an endpoint
// approximation in ta, tb).
bool findTValuesSegToSeg(const Vec3& a0, const Vec3& da, const Vec3& b0, const Vec3& db, float& ta, float& tb);
// FindTValuesLineToBoxFace: the line a + t * dir against the rectangle of
// the box face with normal `faceNormal` (box half sizes `halfSize`).
bool findTValuesLineToBoxFace(const Vec3& a, const Vec3& dir, const Vec3& faceNormal, const Vec3& halfSize,
                              float& t0, float& t1, int& e0, int& e1);
// SegmentToSphereIntersections / SegmentToHemisphereIntersections /
// SegmentToUprightCylIsects / SegmentToBoxIntersections: the t values where
// start + t * dir (t in [0, 1]) crosses the surface (shapes at the origin;
// radius2 is the squared radius; the cylinder's y0/y1 are heights as
// y / height + 0.5).
int segmentToSphereIntersections(const Vec3& start, const Vec3& dir, float radius2, float& t0, float& t1);
int segmentToHemisphereIntersections(const Vec3& start, const Vec3& dir, float radius2, float& t0, float& t1,
                                     bool top);
int segmentToUprightCylIsects(const Vec3& start, const Vec3& dir, float height, float radius2, float& t0,
                              float& t1, float& y0, float& y1);
int segmentToBoxIntersections(const Vec3& start, const Vec3& dir, float hx, float hy, float hz, float& t0,
                              float& t1, Vec3& n0, Vec3& n1, int& f0, int& f1);
// OrderIntersections: when t0 > t1 swaps (t0, t1), (y0, y1) and (f0, f1).
void orderIntersections(float& t0, float& t1, float& y0, float& y1, int& f0, int& f1);

// --- phCollisionPrim (Geometry.cpp) ---

// SphereToPolygonal (bound space): resets depth to 0, keeps the deepest
// polygon contact shallower than radius; position = contact + normal *
// depth / 2. The Mat34 overload (world space) does NOT reset depth (callers
// initialise it) and returns position = contact - normal * depth / 2.
bool sphereToPolygonal(const Vec3& center, float radius, const BoundPolygonal& bound, Vec3& position,
                       Vec3& normal, float& depth);
bool sphereToPolygonal(const Vec3& center, float radius, const BoundPolygonal& bound, const Mat34& m,
                       Vec3& position, Vec3& normal, float& depth);
// SegmentToSphere: the entry point; writes only out.position, out.normal
// and out.t.
bool segmentToSphere(const Vec3& center, float radius, const Segment& seg, IntersectionPoint& out);
// SegmentSphereTest: whether the segment comes within the sphere. The
// Vector3-pair overloads take the squared radius, the phSegment overloads
// the radius (as the originals).
bool segmentSphereTest(const Vec3& center, float radius2, const Vec3& a, const Vec3& b);
bool segmentSphereTest(const Vec3& center, float radius, const Segment& seg);
bool segmentSphereTest(float radius, const Segment& seg);
bool segmentSphereTest(float radius2, const Vec3& a, const Vec3& b);

// --- phBoundCollision (BoundCollision.cpp) ---

// phBoundCollision::SegSegDistNorm: the closest points of segments
// (a0, a1) and (b0, b1) (directions da = a1 - a0, db = b1 - b0), the unit
// normal between them (pointing from b towards a), their distance; ok is 0
// when the segments are parallel or the closest points fall outside them.
void segSegDistNorm(const Vec3& a0, const Vec3& a1, const Vec3& da, const Vec3& b0, const Vec3& b1,
                    const Vec3& db, Vec3& normal, Vec3& pointA, Vec3& pointB, float& distance, int& ok);
// phBoundCollision::GetDisp: how far the world point p (on a body now at m)
// moved since the body was at last.
Vec3 getDisp(const Mat34& m, const Mat34& last, const Vec3& p);

} // namespace mm2::phys::geom
