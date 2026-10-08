// phBoundPolygonal's collision routines: the segment tests of one polygonal
// bound against another (TestBoundPolyPoly, TestBoundPolyPolyUseDot,
// TestBoundPolyPolyUseDotSmall with GetAllSegments, RewindSegments and
// GetNextSegment) and the impact search over the resulting intersections
// (FindImpactsPolyToPoly, AddInteriorEdges, FindImpacts, MakeBsInside,
// DoEndPtSearch, CheckSaveEdgeEdge, GetCollideEdgePoly, ResetVertNeedsH,
// RetryVertPolyCollide), plus FindImpactsSphereToPoly. Ported from the code
// of midtown2.exe build 3393 (MM2Recomp). See docs/physics.md, "Collision".
//
// How it works: every vertex of one bound sweeps from where it was at the
// end of the previous sample (relative to the other bound) to where it is
// now, and every edge is tested as it stands; each crossing of the other
// bound's polygons is an intersection. FindImpacts then turns them into
// impacts: a vertex inside a face (DoEndPtSearch), two edges crossing
// (CheckSaveEdgeEdge), or an edge through a face that it entered across one
// of the face's edges (GetCollideEdgePoly).

#include "phys/AgeMath.h"
#include "phys/Bound.h"
#include "phys/Collider.h"
#include "phys/Collision.h"
#include "phys/Geometry.h"
#include "phys/Level.h"

#include <cmath>
#include <vector>

namespace mm2::phys {
namespace {

// Matrix34::Transform4's summation order (the current poses of
// TestBoundPolyPolyUseDotSmall and lvlSDL::CollidePolyToLevel).
Vec3 xform(const Mat34& m, const Vec3& p) {
    return {p.x * m.m0.x + m.m2.x * p.z + m.m1.x * p.y + m.m3.x,
            m.m2.y * p.z + m.m0.y * p.x + m.m1.y * p.y + m.m3.y,
            m.m2.z * p.z + m.m0.z * p.x + m.m1.z * p.y + m.m3.z};
}

// The routines below inline other transforms, each with its own summation
// order (read from the original's x87 code); they are spelled out where
// they are used. This one serves the edge normals (TestBoundPolyPolyUseDot,
// TestBoundPolyPolyUseDotSmall, lvlSDL::CollidePolyToLevel): z, y, then x.
Vec3 rotateZyx(const Mat34& m, const Vec3& v) {
    return {(m.m2.x * v.z + m.m1.x * v.y) + m.m0.x * v.x, (m.m2.y * v.z + m.m1.y * v.y) + m.m0.y * v.x,
            (m.m2.z * v.z + m.m1.z * v.y) + m.m0.z * v.x};
}

// a * inverse(b) for rigid matrices (Matrix34::FastInverse, Matrix34::Dot).
Mat34 relative(const Mat34& a, const Mat34& b) {
    return age::dot(a, b.fastInverse());
}

// Vector3::Dot: z, then y, then x.
float dot(const Vec3& a, const Vec3& b) {
    return (a.z * b.z + a.y * b.y) + a.x * b.x;
}

// The polygon's material index as an intersection carries it.
int materialOf(const Intersection& s) {
    return s.poly ? s.poly->material : s.material;
}

// --- Segment lists of TestBoundPolyPolyUseDotSmall (GetAllSegments) ---

// phBoundPolygonal::DispSegment: a vertex's sweep from its previous position.
struct DispSegment {
    std::uint16_t vertex = 0;
    std::uint16_t polygon = 0xffff; // the polygon it enters first
    float t = 2.0f;
    float depth = 0.0f;
    Vec3 previous;
};

// phBoundPolygonal::Segment: an edge and the polygons it enters and leaves.
struct EdgeSegment {
    int edge = 0;
    std::uint16_t polyEnter = 0xffff;
    std::uint16_t polyExit = 0xffff;
    float tEnter = 2.0f;
    float tExit = -1.0f;
    float depthEnter = 0.0f;
    float depthExit = 0.0f;
};

struct Scratch {
    std::vector<std::uint8_t> flags;
    std::vector<Vec3> verts;
    std::vector<float> dist;
    std::vector<DispSegment> sweeps;
    std::vector<EdgeSegment> edges;
};

Scratch& scratch() {
    thread_local Scratch s;
    return s;
}

// phBoundPolygonal::GetAllSegments: flags the vertices whose projection on
// dir lies below threshold (they cannot reach the other bound), then lists
// the sweeps of the others and the edges with at least one such vertex that
// pass within the other bound's bounding sphere (radius2, at the origin of
// the space the vertices are in). A flagged vertex used by an edge is
// marked 2 so its distances are still computed.
void getAllSegments(const BoundPolygonal& self, float radius2, bool sweep, const Mat34& relLast, float threshold,
                    const Vec3& dir, Scratch& s) {
    const int nv = self.numVertices();
    for (int i = nv - 1; i >= 0; --i) {
        const Vec3& p = self.vertex(i);
        s.flags[static_cast<std::size_t>(i)] = (p.z * dir.z + p.y * dir.y) + dir.x * p.x < threshold ? 1 : 0;
    }
    s.sweeps.clear();
    if (sweep) {
        for (int i = nv - 1; i >= 0; --i) {
            if (s.flags[static_cast<std::size_t>(i)] != 0)
                continue;
            const Vec3& v = self.vertex(i);
            const Mat34& m = relLast;
            const Vec3 prev{((m.m1.x * v.y + m.m2.x * v.z) + m.m0.x * v.x) + m.m3.x,
                            ((m.m0.y * v.x + m.m1.y * v.y) + m.m2.y * v.z) + m.m3.y,
                            ((m.m0.z * v.x + m.m1.z * v.y) + m.m2.z * v.z) + m.m3.z};
            if (geom::segmentSphereTest(radius2, prev, s.verts[static_cast<std::size_t>(i)])) {
                DispSegment d;
                d.vertex = static_cast<std::uint16_t>(i);
                d.previous = prev;
                s.sweeps.push_back(d);
            }
        }
    }
    s.edges.clear();
    for (int e = self.numEdges() - 1; e >= 0; --e) {
        const auto& ed = self.edges[static_cast<std::size_t>(e)];
        std::uint8_t& fa = s.flags[ed[0]];
        std::uint8_t& fb = s.flags[ed[1]];
        if ((fa == 0 || fb == 0) && geom::segmentSphereTest(radius2, s.verts[ed[0]], s.verts[ed[1]])) {
            fa = static_cast<std::uint8_t>(fa << (fa & 1));
            fb = static_cast<std::uint8_t>(fb << (fb & 1));
            EdgeSegment seg;
            seg.edge = e;
            s.edges.push_back(seg);
        }
    }
}

// The per-polygon pass of TestBoundPolyPolyUseDotSmall and
// lvlSDL::CollidePolyToLevel: vertex sweeps that cross the polygon's plane
// inside its edges, and edges that cross it, keeping each edge's first entry
// and last exit.
// lvlSDL::CollidePolyToLevel tests the sweeps undirected (either face) and
// the edges only when `testEdges` (the bound's centre near the plane).
void collidePolygon(const Polygon& poly, int polyIndex, std::span<const Vec3> polyVerts, float penetration,
                    const BoundPolygonal& self, Scratch& s, bool directedSweeps = true, bool testEdges = true) {
    const Vec3& p0 = polyVerts[poly.v[0]];
    const Vec3& n = poly.normal;
    for (int i = self.numVertices() - 1; i >= 0; --i) {
        const auto k = static_cast<std::size_t>(i);
        if (s.flags[k] != 1) {
            const Vec3& v = s.verts[k];
            s.dist[k] = ((v.z - p0.z) * n.z + (v.y - p0.y) * n.y) + (v.x - p0.x) * n.x;
        }
    }
    for (int k = static_cast<int>(s.sweeps.size()) - 1; k >= 0; --k) {
        DispSegment& d = s.sweeps[static_cast<std::size_t>(k)];
        const float now = s.dist[d.vertex];
        if (!(now < 0.0f))
            continue;
        float before =
            ((d.previous.z - p0.z) * n.z + (d.previous.y - p0.y) * n.y) + (d.previous.x - p0.x) * n.x;
        if (!(0.0f < before)) {
            if (!(penetration * -1.5f < before))
                continue;
            backupDispByPenetration(d.previous, s.verts[d.vertex], penetration);
            before =
                ((d.previous.z - p0.z) * n.z + (d.previous.y - p0.y) * n.y) + (d.previous.x - p0.x) * n.x;
            if (!(0.0f < before))
                continue;
        }
        const float t = before / (before - now);
        if (t < d.t && (directedSweeps ? poly.detectSegmentDirected(polyVerts, d.previous, s.verts[d.vertex])
                                       : poly.detectSegmentUndirected(polyVerts, d.previous, s.verts[d.vertex]))) {
            d.t = t;
            d.polygon = static_cast<std::uint16_t>(polyIndex);
            d.depth = -now;
        }
    }
    if (!testEdges)
        return;
    for (int k = static_cast<int>(s.edges.size()) - 1; k >= 0; --k) {
        EdgeSegment& e = s.edges[static_cast<std::size_t>(k)];
        const auto& ed = self.edges[static_cast<std::size_t>(e.edge)];
        const float da = s.dist[ed[0]];
        const float db = s.dist[ed[1]];
        if (std::signbit(da) == std::signbit(db))
            continue;
        const float t = da / (da - db);
        if (!(e.tExit < t || t < e.tEnter))
            continue;
        if (!poly.detectSegmentUndirected(polyVerts, s.verts[ed[0]], s.verts[ed[1]]))
            continue;
        const float depth = da <= 0.0f ? da : -db;
        if (t <= e.tEnter && (0.0f < da || t < e.tEnter)) {
            e.polyEnter = static_cast<std::uint16_t>(polyIndex);
            e.tEnter = t;
            e.depthEnter = depth;
        }
        if (e.tExit <= t && (da < 0.0f || e.tExit < t)) {
            e.polyExit = static_cast<std::uint16_t>(polyIndex);
            e.tExit = t;
            e.depthExit = depth;
        }
    }
}

// Writes the intersections the sweeps and edges found (the output part of
// TestBoundPolyPolyUseDotSmall and lvlSDL::CollidePolyToLevel). Edge normals
// go to the world through `selfWorld`.
int writeIntersections(const BoundPolygonal& self, const Bound& other, std::span<const Polygon> otherPolys,
                       Collider* otherCollider, const Mat34& selfWorld, const Scratch& s, Intersection* out,
                       int capacity) {
    int count = 0;
    for (const DispSegment& d : s.sweeps) {
        if (d.polygon == 0xffff || count >= capacity)
            continue;
        Intersection& is = out[count++];
        is = Intersection{};
        is.t = d.t;
        is.depth = d.depth;
        is.bInside = true;
        is.a = d.previous;
        is.b = s.verts[d.vertex];
        is.position = {(is.b.x - is.a.x) * d.t + is.a.x, (is.b.y - is.a.y) * d.t + is.a.y,
                       (is.b.z - is.a.z) * d.t + is.a.z};
        is.collider = otherCollider;
        is.otherBound = &other;
        is.polygon = d.polygon;
        is.poly = &otherPolys[d.polygon];
        is.material = is.poly->material;
        is.normal = is.poly->normal;
        is.vertexA = -1;
        is.bound = &self;
        is.element = d.vertex;
        is.vertexB = d.vertex;
        is.flags = static_cast<std::uint16_t>((is.flags & ~Intersection::kInterior) | Intersection::kVertex);
    }
    auto edgeHit = [&](const EdgeSegment& e, std::uint16_t polygon, float t, float depth) {
        if (count >= capacity)
            return;
        Intersection& is = out[count++];
        is = Intersection{};
        is.t = t;
        if (0.0f < depth) {
            is.bInside = true;
            is.depth = depth;
        } else {
            is.depth = -depth;
            is.bInside = false;
        }
        const auto& ed = self.edges[static_cast<std::size_t>(e.edge)];
        is.a = s.verts[ed[0]];
        is.b = s.verts[ed[1]];
        is.position = {(is.b.x - is.a.x) * t + is.a.x, (is.b.y - is.a.y) * t + is.a.y,
                       (is.b.z - is.a.z) * t + is.a.z};
        is.collider = otherCollider;
        is.otherBound = &other;
        is.polygon = polygon;
        is.poly = &otherPolys[polygon];
        is.material = is.poly->material;
        is.normal = is.poly->normal;
        is.flags = static_cast<std::uint16_t>(is.flags & ~(Intersection::kVertex | Intersection::kInterior));
        is.element = e.edge;
        is.bound = &self;
        is.vertexA = ed[0];
        is.vertexB = ed[1];
        is.edgeNormal = rotateZyx(selfWorld, self.edgeNormal(e.edge));
    };
    for (const EdgeSegment& e : s.edges) {
        if (e.polyEnter != 0xffff)
            edgeHit(e, e.polyEnter, e.tEnter, e.depthEnter);
        if (e.polyExit != 0xffff && e.polyExit != e.polyEnter)
            edgeHit(e, e.polyExit, e.tExit, e.depthExit);
    }
    return count;
}

// --- FindImpacts helpers ---

// phBoundPolygonal::MakeBsInside: every edge intersection is turned so that
// its end b is the one behind the polygon; an edge that enters and leaves
// next to itself marks both crossings.
void makeBsInside(Intersection* list, int count) {
    for (int i = 0; i < count; ++i) {
        Intersection& s = list[i];
        s.flags = static_cast<std::uint16_t>(s.flags & 0xfe07);
        s.timeToImpact = FLT_MAX;
        s.edgeEdgeIndex = -1;
        if (s.flags & Intersection::kInterior) {
            s.flags |= Intersection::kSearched;
            continue;
        }
        if (!s.bInside) {
            std::swap(s.vertexA, s.vertexB);
            s.bInside = true;
            s.t = 1.0f - s.t;
            std::swap(s.a, s.b);
        }
        if (i > 0) {
            Intersection& prev = list[i - 1];
            if (prev.element == s.element && prev.vertexB == s.vertexA) {
                if (s.t <= 1.03f - prev.t) {
                    prev.flags |= Intersection::kPaired;
                    s.flags |= Intersection::kPaired;
                } else {
                    prev.flags |= Intersection::kSkip | Intersection::kUsed;
                    s.flags |= Intersection::kSkip | Intersection::kUsed;
                }
            }
        }
    }
}

// phBoundPolygonal::ResetVertNeedsH (both variants).
void resetVertNeedsH(Intersection* s, Intersection* other, bool pairedSelf, bool pairedOther) {
    s->flags &= static_cast<std::uint16_t>(~Intersection::kNeedsRetry);
    if (pairedSelf)
        s[1].flags &= static_cast<std::uint16_t>(~Intersection::kNeedsRetry);
    if (other) {
        other->flags &= static_cast<std::uint16_t>(~Intersection::kNeedsRetry);
        if (pairedOther)
            other[1].flags &= static_cast<std::uint16_t>(~Intersection::kNeedsRetry);
    }
}

void resetVertNeedsH(Intersection* s, Intersection* other, bool pairedSelf, bool pairedOther, float depth) {
    const float half = depth * 0.5f;
    if (half < s->depth)
        s->flags &= static_cast<std::uint16_t>(~Intersection::kNeedsRetry);
    if (pairedSelf && half < s[1].depth)
        s[1].flags &= static_cast<std::uint16_t>(~Intersection::kNeedsRetry);
    if (other) {
        if (half < other->depth)
            other->flags &= static_cast<std::uint16_t>(~Intersection::kNeedsRetry);
        if (pairedOther && half < other[1].depth)
            other[1].flags &= static_cast<std::uint16_t>(~Intersection::kNeedsRetry);
    }
}

// phBoundPolygonal::CheckSaveEdgeEdge: keeps the best edge-edge candidate
// for an intersection (the most recent crossing, else the shallowest that
// fits).
void checkSaveEdgeEdge(Intersection& s, int partner, bool recent, bool nearlyParallel, const Vec3& point,
                       const Vec3& otherPoint, const Vec3& normal, float depth, float depth2, bool valid,
                       float time, float soonLimit) {
    const std::uint16_t f = s.flags;
    if ((f & Intersection::kSkip) || (f & Intersection::kInterior))
        return;
    float along;
    if (recent) {
        along = dot(otherPoint - s.position, s.normal);
        if (!(s.edgeEdgeIndex < 0 || s.edgeEdgeDistance < along || soonLimit < s.timeToImpact ||
              (f & Intersection::kEdgeEdge) == 0))
            return;
    } else {
        if (nearlyParallel || !(depth <= s.depth * 1.2f) || !(soonLimit < s.timeToImpact))
            return;
        along = dot(otherPoint - s.position, s.normal);
        if (s.edgeEdgeIndex < 0) {
            if (!(along * along + depth2 < s.depth * s.depth))
                return;
        } else if (!(depth2 * 4.0f + along * along <
                     s.edgeEdgeDistance * s.edgeEdgeDistance + s.edgeEdgeDepth * s.edgeEdgeDepth * 4.0f)) {
            return;
        }
    }
    s.timeToImpact = time;
    s.edgeEdgeNormal = normal;
    s.edgeEdgeDepth = depth;
    s.flags = static_cast<std::uint16_t>((f & ~Intersection::kEdgeEdge) | (valid ? Intersection::kEdgeEdge : 0));
    s.edgeEdgePoint = point;
    s.edgeEdgeDistance = along;
    s.edgeEdgeIndex = partner;
}

// phBoundPolygonal::GetCollideEdgePoly: for an edge through a face, the
// edge of the face the intersection point entered across (within 1.5
// samples, judged from the relative velocity relVel moved into the face's
// space by invM), else the nearest one; the impact between the two edges
// when they form a convex enough pair. `m` places the face's bound.
bool getCollideEdgePoly(const Intersection& s, const Vec3& relVel, const Mat34& m, const Mat34& invM,
                        const Vec3& edgeDir, Vec3& normal, Vec3& position, float& depth, int& faceEdge,
                        bool& soon) {
    const Polygon& poly = *s.poly;
    const Bound& owner = *s.otherBound;
    const Vec3& rv = relVel;
    const Vec3& sp = s.position;
    const Vec3& m0 = invM.m0;
    const Vec3& m1 = invM.m1;
    const Vec3& m2 = invM.m2;
    const Vec3& m3 = invM.m3;
    Vec3 lv{(rv.y * m1.x + rv.z * m2.x) + rv.x * m0.x, (rv.z * m2.y + rv.y * m1.y) + rv.x * m0.y,
            (rv.z * m2.z + rv.y * m1.z) + rv.x * m0.z};
    const Vec3 lp{((sp.y * m1.x + sp.z * m2.x) + sp.x * m0.x) + m3.x,
                  ((sp.z * m2.y + sp.y * m1.y) + sp.x * m0.y) + m3.y,
                  ((sp.z * m2.z + sp.y * m1.z) + sp.x * m0.z) + m3.z};
    const Vec3& d = edgeDir;
    const Vec3 ld{(d.y * m1.x + d.x * m0.x) + d.z * m2.x, (d.x * m0.y + d.z * m2.y) + d.y * m1.y,
                  (d.z * m2.z + d.y * m1.z) + d.x * m0.z};
    // The velocity without its part along the edge.
    const float along = -((ld.y * lv.y + ld.x * lv.x) + lv.z * ld.z);
    lv = {ld.x * along + lv.x, ld.y * along + lv.y, ld.z * along + lv.z};
    const Vec3 c = ld.cross(poly.normal);
    const float c2 = (c.x * c.x + c.z * c.z) + c.y * c.y;
    const float nd = std::abs(dot(ld, poly.normal));
    if (nd == 0.0f) {
        lv = {};
    } else if (1e-6f <= c2) {
        // Slide the motion into the face's plane along the edge.
        const float k = -(((c.x * lv.x + lv.z * c.z) + lv.y * c.y) / c2);
        const Vec3 t{k * c.x + lv.x, c.y * k + lv.y, k * c.z + lv.z};
        const float stretch = 1.0f / nd - 1.0f;
        lv = {t.x * stretch + lv.x, t.y * stretch + lv.y, t.z * stretch + lv.z};
    }
    float best = FLT_MAX;
    int bestEdge = -1;
    float closest = FLT_MAX;
    int closestEdge = -1;
    const int n = poly.vertexCount();
    Vec3 prev = owner.vertex(poly.v[static_cast<std::size_t>(n - 1)]);
    for (int k = 0; k < n; ++k) {
        const Vec3& cur = owner.vertex(poly.v[static_cast<std::size_t>(k)]);
        Vec3 e{prev.x - cur.x, prev.y - cur.y, prev.z - cur.z};
        e = e * age::invMag(e);
        e = e.cross(poly.normal);
        const float dist = ((lp.x - cur.x) * e.x + e.z * (lp.z - cur.z)) + e.y * (lp.y - cur.y);
        float beyond = dist - kPenetration;
        if (beyond < 0.0f)
            beyond = 0.0f;
        const float approach = (e.x * lv.x + e.z * lv.z) + e.y * lv.y;
        if (0.0f < approach && beyond < approach * best) {
            best = beyond / approach;
            bestEdge = k;
        }
        if (dist < closest) {
            closestEdge = k;
            closest = dist;
        }
        prev = cur;
    }
    int chosen;
    if (sampleTime().seconds * 1.5f <= best) {
        soon = false;
        chosen = closestEdge;
        if (chosen < 0)
            return false;
    } else {
        soon = true;
        chosen = bestEdge;
    }
    const int prevSlot = (chosen > 0 ? chosen : n) - 1;
    const Vec3& pa0 = owner.vertex(poly.v[static_cast<std::size_t>(prevSlot)]);
    const Vec3& pb0 = owner.vertex(poly.v[static_cast<std::size_t>(chosen)]);
    const Vec3 ea{((pa0.z * m.m2.x + pa0.y * m.m1.x) + pa0.x * m.m0.x) + m.m3.x,
                  ((pa0.z * m.m2.y + pa0.y * m.m1.y) + pa0.x * m.m0.y) + m.m3.y,
                  ((pa0.x * m.m0.z + pa0.z * m.m2.z) + pa0.y * m.m1.z) + m.m3.z};
    const Vec3 eb{((pb0.z * m.m2.x + pb0.y * m.m1.x) + pb0.x * m.m0.x) + m.m3.x,
                  ((pb0.y * m.m1.y + pb0.z * m.m2.y) + pb0.x * m.m0.y) + m.m3.y,
                  ((pb0.x * m.m0.z + pb0.y * m.m1.z) + pb0.z * m.m2.z) + m.m3.z};
    const Vec3 de = eb - ea;
    const Vec3 di = s.b - s.a;
    Vec3 pa, pb;
    int ok = 0;
    geom::segSegDistNorm(ea, eb, de, s.a, s.b, di, normal, pa, pb, depth, ok);
    if (ok == 0)
        return false;
    const float cosine = s.bound ? static_cast<const BoundPolygonal*>(s.bound)->edgeCosine(s.element) : 0.0f;
    if (!(dot(s.edgeNormal, normal) < -((cosine - 0.25881904f) - 0.01f)))
        return false;
    position = {(pb.x + pa.x) * 0.5f, (pa.y + pb.y) * 0.5f, (pa.z + pb.z) * 0.5f};
    faceEdge = poly.edges[static_cast<std::size_t>(prevSlot)];
    return true;
}

// phBoundPolygonal::DoEndPtSearch: the impact of `vertex` against the face
// it reached first (or will reach soonest), from the intersections of the
// list ending at it.
void doEndPtSearch(const Mat34* m1, const Mat34* last1, const Mat34* m2, const Mat34* last2, Collider* ca,
                   Collider* cb, int vertex, Intersection* list, int count, Impact*& imp, int& left, bool isA) {
    const float soonLimit = sampleTime().seconds * 1.5f;
    const float invDt = sampleTime().invSeconds;
    // Relative velocity at the first intersection's inside end.
    Vec3 d1{};
    if (m1)
        d1 = geom::getDisp(*m1, *last1, list[0].b) * invDt;
    Vec3 v;
    if (!m2) {
        v = -d1;
    } else {
        const Vec3 d2 = geom::getDisp(*m2, *last2, list[0].b);
        v = {d2.x * invDt - d1.x, d2.y * invDt - d1.y, d2.z * invDt - d1.z};
    }
    if (left <= 0)
        return; // OpenMM2 guard: the original writes past a full impact table
    Impact& out = *imp;
    int primary = -1, secondary = -1;
    float primaryTime = FLT_MAX, primaryDepth = FLT_MAX;
    float secondaryTime = FLT_MAX, secondaryDepth = FLT_MAX;
    bool found = false;
    bool blocked = false;
    bool keepPaired = true;
    int primaryEdge = -1;
    for (int i = 0; i < count; ++i) {
        Intersection& s = list[i];
        if (s.vertexA == vertex && !(s.flags & Intersection::kUsed) && !(s.flags & Intersection::kInterior))
            blocked = true;
        if (s.vertexB != vertex)
            continue;
        const std::uint16_t f = s.flags;
        if (f & Intersection::kInterior)
            continue;
        if (f & Intersection::kPaired)
            blocked = true;
        s.flags = static_cast<std::uint16_t>(f | Intersection::kSearched);
        float time;
        if (!(f & Intersection::kVertex)) {
            if (0.0f <= s.depth - kPenetration) {
                const float approach = (v.x * s.normal.x + v.z * s.normal.z) + v.y * s.normal.y;
                // Only a closing speed divides (Ghidra prints this test as
                // "0 <= approach"; the code tests "0 < approach").
                time = 0.0f < approach ? (s.depth - kPenetration) / approach : FLT_MAX;
            } else {
                time = 0.0f;
            }
            s.flags = static_cast<std::uint16_t>((f & ~Intersection::kSoon) | Intersection::kSearched |
                                                 (time < soonLimit ? Intersection::kSoon : 0));
        } else {
            time = (1.0f - s.t) * sampleTime().seconds * 0.9f;
            s.flags = static_cast<std::uint16_t>(f | Intersection::kSoon | Intersection::kSearched);
        }
        const bool isSoon = (s.flags & Intersection::kSoon) != 0;
        if ((s.flags & Intersection::kVertex) || (isSoon && time < primaryTime) ||
            (soonLimit < primaryTime && s.depth < primaryDepth && 0.001f < s.t && !blocked)) {
            secondaryTime = primaryTime;
            secondary = primary;
            secondaryDepth = primaryDepth;
            primaryDepth = s.depth;
            out.elementA = vertex;
            out.kind = Impact::VertexA;
            out.elementB = s.polygon;
            out.colliderA = ca;
            out.colliderB = cb;
            found = true;
            out.normal = s.normal;
            out.depth = s.depth;
            out.penetration = kPenetration;
            primaryEdge = (s.flags & Intersection::kVertex) ? -1 : s.element;
            out.componentA = -1;
            out.componentB = -1;
            const bool moving = (s.flags & Intersection::kVertex) || 1e-5f < time;
            primary = i;
            primaryTime = time;
            if (!(s.flags & Intersection::kVertex)) {
                if (moving && (s.flags & Intersection::kSoon)) {
                    const float h = time * 0.5f;
                    out.position = {h * v.x + s.b.x, v.y * h + s.b.y, h * v.z + s.b.z};
                    keepPaired = (s.flags & Intersection::kUsed) != 0;
                } else {
                    const float h = s.depth * 0.5f;
                    out.position = {h * s.normal.x + s.b.x, h * s.normal.y + s.b.y, h * s.normal.z + s.b.z};
                    keepPaired = !(moving && !(s.flags & Intersection::kUsed));
                }
            } else {
                keepPaired = false;
                const float w = (1.0f - s.t) * 0.5f;
                const float r = 1.0f - w;
                out.position = {r * s.b.x + w * s.a.x, r * s.b.y + w * s.a.y, r * s.b.z + w * s.a.z};
            }
        } else if (((isSoon && time < secondaryTime) ||
                    (soonLimit < secondaryTime && s.depth < secondaryDepth && 0.001f < s.t && !blocked)) &&
                   found && s.polygon != out.elementB) {
            secondaryDepth = s.depth;
            secondaryTime = time;
            secondary = i;
        }
        s.timeToImpact = time;
    }
    if (!found)
        return;
    if (blocked) {
        if (!keepPaired)
            list[primary].flags |= Intersection::kNeedsRetry;
        return;
    }
    bool otherSide = false;
    for (int k = 0; k < count; ++k) {
        Intersection& s = list[k];
        if (s.flags & Intersection::kInterior)
            continue;
        if (s.vertexB == vertex) {
            if (s.polygon == out.elementB) {
                s.flags |= Intersection::kSkip;
            } else if (k != primary && ((s.flags & Intersection::kVertex) || s.element != primaryEdge)) {
                const Vec3 e = s.b - s.a;
                const float dn = e.x * out.normal.x + e.y * out.normal.y + e.z * out.normal.z;
                const float lim = (e.y * e.y + e.z * e.z + e.x * e.x) * 0.25881904f;
                if (dn < 0.0f && lim < dn * dn)
                    s.flags |= Intersection::kSkip;
                else if (0.0f < dn && lim < dn * dn)
                    otherSide = true;
            }
        } else if (s.vertexA == vertex && (list[primary].flags & Intersection::kVertex)) {
            if (s.polygon == out.elementB) {
                s.flags |= Intersection::kSkip;
            } else if (k != primary) {
                const Vec3 e = s.a - s.b;
                const float dn = e.x * out.normal.x + e.y * out.normal.y + e.z * out.normal.z;
                const float lim = (e.y * e.y + e.z * e.z + e.x * e.x) * 0.25881904f;
                if (dn < 0.0f && lim < dn * dn)
                    s.flags |= Intersection::kSkip;
                else if (0.0f < dn && lim < dn * dn)
                    otherSide = true;
            }
        }
    }
    if (otherSide && secondary != -1)
        list[secondary].flags |= Intersection::kNeedsRetry;
    if (!isA) {
        out.kind = Impact::VertexB;
        std::swap(out.elementA, out.elementB);
        std::swap(out.colliderA, out.colliderB);
        std::swap(out.componentA, out.componentB);
        out.normal = -out.normal;
    }
    --left;
    ++imp;
}

// phBoundPolygonal::RetryVertPolyCollide: a vertex left over by the edge
// passes gets an impact against its face unless it is deep inside a long
// edge it did not reach recently. `vertexOwner` is the collider of the bound
// whose vertex it is, `faceOwner` that of the face; an A vertex makes a
// VertexA impact (A = vertexOwner), a B vertex a VertexB one
// (A = faceOwner).
void retryVertPolyCollide(Collider* vertexOwner, Collider* faceOwner, const Intersection& s, Impact*& imp,
                          int& left, bool isA) {
    if (!(s.flags & Intersection::kSoon) && s.t <= 0.75f) {
        const Vec3 e = s.b - s.a;
        if (((e.z * e.z + e.y * e.y) + e.x * e.x) * 0.04f <= s.depth * s.depth)
            return;
    }
    if (left <= 0)
        return; // OpenMM2 guard: the original writes past a full impact table
    Impact& out = *imp;
    if (isA) {
        out.kind = Impact::VertexA;
        out.normal = s.normal;
        out.elementA = s.vertexB;
        out.elementB = s.polygon;
        out.colliderA = vertexOwner;
        out.colliderB = faceOwner;
    } else {
        out.kind = Impact::VertexB;
        out.normal = -s.normal;
        out.elementA = s.polygon;
        out.elementB = s.vertexB;
        out.colliderA = faceOwner;
        out.colliderB = vertexOwner;
    }
    out.componentA = -1;
    out.componentB = -1;
    out.depth = s.depth;
    out.penetration = kPenetration;
    const float h = s.depth * 0.5f;
    out.position = {h * s.normal.x + s.b.x, h * s.normal.y + s.b.y, h * s.normal.z + s.b.z};
    --left;
    ++imp;
}

void writeEdgeEdgeImpact(Impact& out, Collider* ca, Collider* cb, int elemA, int elemB, const Intersection& s) {
    out.kind = Impact::EdgeEdge;
    out.colliderA = ca;
    out.colliderB = cb;
    out.componentA = -1;
    out.componentB = -1;
    out.elementA = elemA;
    out.elementB = elemB;
    out.position = s.edgeEdgePoint;
    out.normal = -s.edgeEdgeNormal;
    out.depth = s.edgeEdgeDepth;
    out.penetration = kPenetration;
}

bool pairedWithNext(const Intersection* list, int count, int i) {
    return i < count - 1 && list[i].element == list[i + 1].element;
}

// phBoundPolygonal::AddInteriorEdges: for each face of `self` pierced by
// the other bound's edges, an edge of that face whose two ends lie inside
// the other bound (judged by self's intersections), appended to self's list.
int addInteriorEdges(const BoundPolygonal& self, Intersection* selfList, int selfCount,
                     const Intersection* otherList, int otherCount, int max) {
    int added = 0;
    int outIndex = selfCount;
    for (int j = 0; j < otherCount; ++j) {
        bool seen = false;
        for (int k = 0; k < j; ++k)
            if (otherList[k].polygon == otherList[j].polygon) {
                seen = true;
                break;
            }
        if (seen)
            continue;
        const Polygon& poly = *otherList[j].poly;
        const int n = poly.vertexCount();
        bool inside[4] = {};
        for (int v = 0; v < n; ++v) {
            inside[v] = false;
            const int vi = poly.v[static_cast<std::size_t>(v)];
            for (int i = 0; i < selfCount; ++i) {
                const Intersection& s = selfList[i];
                const int insideEnd = s.bInside ? s.vertexB : s.vertexA;
                const int outsideEnd = s.bInside ? s.vertexA : s.vertexB;
                if (insideEnd == vi) {
                    inside[v] = true;
                } else if (outsideEnd == vi) {
                    inside[v] = false;
                    break;
                }
            }
        }
        int from = -1, to = -1;
        if (!inside[0]) {
            if (!inside[1]) {
                if (n == 4 && inside[2] && inside[3]) {
                    from = 2;
                    to = 3;
                }
            } else if (inside[2] && (n != 4 || !inside[3])) {
                from = 1;
                to = 2;
            }
        } else if (!inside[1]) {
            if (inside[n - 1]) {
                from = n - 1;
                to = 0;
            }
        } else {
            from = 0;
            to = 1;
        }
        if (from < 0)
            continue;
        if (max <= outIndex)
            return added;
        Intersection& is = selfList[outIndex];
        is = Intersection{};
        is.position = {};
        is.normal = {1.0f, 0.0f, 0.0f};
        is.flags = static_cast<std::uint16_t>((is.flags & ~Intersection::kVertex) | Intersection::kInterior);
        is.element = poly.edges[static_cast<std::size_t>(from)];
        is.vertexA = poly.v[static_cast<std::size_t>(from)];
        is.vertexB = poly.v[static_cast<std::size_t>(to)];
        is.t = 0.0f;
        is.a = self.vertex(is.vertexA);
        is.b = self.vertex(is.vertexB);
        is.edgeNormal = self.edgeNormal(is.element);
        is.bound = &self;
        is.material = -1;
        is.polygon = 0;
        is.poly = nullptr;
        ++added;
        ++outIndex;
    }
    return added;
}

} // namespace

// --- Shared with the level (Level.cpp) ---------------------------------------------------

void toWorldCoords(Intersection* isects, int count, const Mat34& m) {
    // The per-intersection world transform FindImpactsPolyToPoly applies
    // (points, the polygon normal, or an interior edge's normal), each sum
    // in the original's order.
    auto point = [&m](const Vec3& p) {
        return Vec3{((p.x * m.m0.x + m.m2.x * p.z) + m.m1.x * p.y) + m.m3.x,
                    ((m.m2.y * p.z + m.m1.y * p.y) + m.m0.y * p.x) + m.m3.y,
                    ((m.m1.z * p.y + m.m2.z * p.z) + m.m0.z * p.x) + m.m3.z};
    };
    for (int i = 0; i < count; ++i) {
        Intersection& s = isects[i];
        s.a = point(s.a);
        s.b = point(s.b);
        if (!(s.flags & Intersection::kInterior)) {
            s.position = point(s.position);
            const Vec3 n = s.normal;
            s.normal = {(m.m2.x * n.z + m.m0.x * n.x) + m.m1.x * n.y,
                        (m.m2.y * n.z + m.m1.y * n.y) + m.m0.y * n.x,
                        (m.m1.z * n.y + m.m0.z * n.x) + m.m2.z * n.z};
        } else {
            const Vec3 n = s.edgeNormal;
            s.edgeNormal = {(m.m0.x * n.x + m.m2.x * n.z) + m.m1.x * n.y,
                            (m.m2.y * n.z + m.m1.y * n.y) + m.m0.y * n.x,
                            (m.m1.z * n.y + m.m2.z * n.z) + m.m0.z * n.x};
        }
    }
}

// --- Segment tests -----------------------------------------------------------------------

int testBoundPolyPoly(const BoundPolygonal& a, const BoundPolygonal& b, const Mat34& ma, const Mat34& lastA,
                      const Mat34& mb, const Mat34& lastB, Collider* ca, Collider* cb, Intersection* isectsA,
                      Intersection* isectsB, int, int& countA, int& countB, const Vec3& relPos, bool sweep) {
    // phBoundPolygonal::TestBoundPolyPoly.
    Vec3 localA, localB;
    const float maxA = a.maxDot(relPos, ma, localA);
    const float minB = b.minDot(relPos, mb, localB);
    if (maxA < minB) {
        countB = 0;
        countA = 0;
        return 0;
    }
    const float thresholdA = minB - ((ma.m3.y * relPos.y + ma.m3.z * relPos.z) + relPos.x * ma.m3.x);
    testBoundPolyPolyUseDotSmall(a, b, cb, ma, lastA, mb, lastB, isectsA, countA, thresholdA, localA, sweep);
    const float thresholdB = ((mb.m3.y * relPos.y + mb.m3.z * relPos.z) + relPos.x * mb.m3.x) + -maxA;
    testBoundPolyPolyUseDotSmall(b, a, ca, mb, lastB, ma, lastA, isectsB, countB, thresholdB, localB, sweep);
    return countA + countB;
}

int testBoundPolyPolyUseDotSmall(const BoundPolygonal& self, const BoundPolygonal& other, Collider* otherCollider,
                                 const Mat34& m, const Mat34& last, const Mat34& otherM, const Mat34& otherLast,
                                 Intersection* out, int& count, float threshold, const Vec3& dir, bool sweep) {
    // phBoundPolygonal::TestBoundPolyPolyUseDotSmall: everything happens in
    // the other bound's space; the intersections stay there (the caller
    // moves them to the world).
    const Mat34 rel = relative(m, otherM);
    Mat34 relLast;
    if (sweep)
        relLast = relative(last, otherLast);
    const float penetration = other.penetration < self.penetration ? other.penetration : self.penetration;
    Scratch& s = scratch();
    const auto nv = static_cast<std::size_t>(self.numVertices());
    s.flags.assign(nv, 0);
    s.verts.resize(nv);
    s.dist.assign(nv, 0.0f);
    for (std::size_t i = 0; i < nv; ++i)
        s.verts[i] = xform(rel, self.vertices[i]);
    getAllSegments(self, other.radius * other.radius, sweep, relLast, threshold, dir, s);
    for (int p = other.numPolygons() - 1; p >= 0; --p)
        collidePolygon(other.polygons[static_cast<std::size_t>(p)], p, other.vertices, penetration, self, s);
    count = writeIntersections(self, other, other.polygons, otherCollider, m, s, out, kMaxIntersections);
    return count;
}

int testBoundPolyPolyUseDot(const BoundPolygonal& self, const BoundPolygonal& other, Collider* otherCollider,
                            const Mat34& m, const Mat34& last, const Mat34* otherM, const Mat34* otherLast,
                            Intersection* out, int max, int& count, float threshold, const Vec3* dir, bool sweep) {
    // phBoundPolygonal::TestBoundPolyPolyUseDot: the segments one at a time
    // through the other bound's TestSegment (its own TestEdge / TestProbe),
    // in the other bound's space when its matrices are given (else in the
    // world).
    Mat34 prev, cur;
    int vertexIndex;
    if (!otherM) {
        // RewindSegments (no relative frame).
        if (sweep) {
            vertexIndex = 0;
            prev = last;
        } else {
            vertexIndex = self.numVertices();
        }
        cur = m;
    } else {
        if (sweep) {
            vertexIndex = 0;
            prev = relative(last, *otherLast);
        } else {
            vertexIndex = self.numVertices();
        }
        cur = relative(m, *otherM);
    }
    int edgeIndex = 0;
    Scratch& s = scratch();
    const auto nv = static_cast<std::size_t>(self.numVertices());
    s.flags.assign(nv, 0);
    s.verts.resize(nv);
    for (int i = self.numVertices() - 1; i >= 0; --i) {
        const auto k = static_cast<std::size_t>(i);
        const Vec3& p = self.vertices[k];
        const bool below = (p.y * dir->y + p.z * dir->z) + p.x * dir->x < threshold;
        s.flags[k] = below ? 1 : 0;
        if (!below)
            s.verts[k] = {((cur.m2.x * p.z + cur.m1.x * p.y) + cur.m0.x * p.x) + cur.m3.x,
                          ((cur.m1.y * p.y + cur.m2.y * p.z) + cur.m0.y * p.x) + cur.m3.y,
                          ((cur.m1.z * p.y + cur.m0.z * p.x) + cur.m2.z * p.z) + cur.m3.z};
    }
    int remaining = max;
    for (;;) {
        // phBound::TestSegment refuses a probe with fewer than 1 slot left
        // and an edge with fewer than 2; stop here instead.
        if (remaining <= 0)
            break;
        // phBoundPolygonal::GetNextSegment (the UseDot variant): vertex
        // sweeps first, then edges with at least one vertex in reach.
        Segment seg;
        int element = 0;
        bool isVertex;
        while (vertexIndex < self.numVertices() && s.flags[static_cast<std::size_t>(vertexIndex)] != 0)
            ++vertexIndex;
        if (vertexIndex < self.numVertices()) {
            const auto k = static_cast<std::size_t>(vertexIndex);
            const Vec3& p = self.vertices[k];
            seg.a = {((prev.m1.x * p.y + prev.m2.x * p.z) + p.x * prev.m0.x) + prev.m3.x,
                     ((prev.m1.y * p.y + prev.m0.y * p.x) + prev.m2.y * p.z) + prev.m3.y,
                     ((prev.m1.z * p.y + prev.m0.z * p.x) + prev.m2.z * p.z) + prev.m3.z};
            seg.b = s.verts[k];
            backupAByPenetration(seg, kPenetration);
            seg.kind = Segment::Probe;
            element = vertexIndex++;
            isVertex = true;
        } else {
            while (edgeIndex < self.numEdges()) {
                const auto& ed = self.edges[static_cast<std::size_t>(edgeIndex)];
                if (s.flags[ed[0]] == 0 || s.flags[ed[1]] == 0)
                    break;
                ++edgeIndex;
            }
            if (edgeIndex >= self.numEdges())
                break;
            const auto& ed = self.edges[static_cast<std::size_t>(edgeIndex)];
            // A vertex below the threshold is placed when an edge needs it
            // (each end with its own summation order).
            if (s.flags[ed[0]] == 1) {
                const Vec3& p = self.vertices[ed[0]];
                s.verts[ed[0]] = {((cur.m2.x * p.z + cur.m1.x * p.y) + p.x * cur.m0.x) + cur.m3.x,
                                  ((cur.m0.y * p.x + cur.m2.y * p.z) + cur.m1.y * p.y) + cur.m3.y,
                                  ((cur.m0.z * p.x + cur.m2.z * p.z) + cur.m1.z * p.y) + cur.m3.z};
                s.flags[ed[0]] = 2;
            }
            if (s.flags[ed[1]] == 1) {
                const Vec3& p = self.vertices[ed[1]];
                s.verts[ed[1]] = {((cur.m2.x * p.z + cur.m1.x * p.y) + p.x * cur.m0.x) + cur.m3.x,
                                  ((cur.m2.y * p.z + cur.m1.y * p.y) + cur.m0.y * p.x) + cur.m3.y,
                                  ((cur.m2.z * p.z + cur.m1.z * p.y) + cur.m0.z * p.x) + cur.m3.z};
                s.flags[ed[1]] = 2;
            }
            seg.a = s.verts[ed[0]];
            seg.b = s.verts[ed[1]];
            seg.kind = Segment::Edge;
            element = edgeIndex++;
            isVertex = false;
        }
        const int n = other.testSegment(seg, out, remaining);
        remaining -= n;
        for (int i = 0; i < n; ++i) {
            Intersection& is = out[i];
            is.a = seg.a;
            is.b = seg.b;
            is.collider = otherCollider;
            is.otherBound = &other;
            is.poly = &other.polygons[static_cast<std::size_t>(is.polygon)];
            is.material = is.poly->material;
            is.flags = static_cast<std::uint16_t>(is.flags & ~Intersection::kInterior);
            is.element = element;
            is.bound = &self;
            if (isVertex) {
                is.flags |= Intersection::kVertex;
                is.vertexA = -1;
                is.vertexB = element;
            } else {
                is.flags = static_cast<std::uint16_t>(is.flags & ~Intersection::kVertex);
                const auto& ed = self.edges[static_cast<std::size_t>(element)];
                is.vertexA = ed[0];
                is.vertexB = ed[1];
                is.edgeNormal = rotateZyx(m, self.edgeNormal(element));
            }
        }
        out += n;
    }
    count = max - remaining;
    return count;
}

// --- Impacts -----------------------------------------------------------------------------

int findImpactsPolyToPoly(const BoundPolygonal& a, const BoundPolygonal& b, const Mat34& ma, const Mat34& lastA,
                          const Mat34& mb, const Mat34& lastB, Collider* ca, Collider* cb, Intersection* isectsA,
                          Intersection* isectsB, Impact* impacts, int maxIsects, int maxImpacts, int& countA,
                          int& countB) {
    // phBoundPolygonal::FindImpactsPolyToPoly.
    const int addedA = addInteriorEdges(a, isectsA, countA, isectsB, countB, maxIsects);
    const int addedB = addInteriorEdges(b, isectsB, countB, isectsA, countA, maxIsects);
    toWorldCoords(isectsB, countB, ma);
    toWorldCoords(isectsA, countA, mb);
    if (addedA != 0) {
        toWorldCoords(isectsA + countA, addedA, ma);
        countA += addedA;
    }
    if (addedB != 0) {
        toWorldCoords(isectsB + countB, addedB, mb);
        countB += addedB;
    }
    return findImpacts(a, b, &ma, &lastA, &mb, &lastB, ca, cb, isectsA, isectsB, countA, countB, impacts,
                       maxImpacts);
}

int findImpacts(const BoundPolygonal& /*a*/, const Bound& b, const Mat34* ma, const Mat34* lastA,
                const Mat34* mb, const Mat34* lastB, Collider* ca, Collider* cb, Intersection* isectsA,
                Intersection* isectsB, int countA, int countB, Impact* impacts, int maxImpacts) {
    // phBoundPolygonal::FindImpacts (A's own data is reached through the
    // intersections).
    const float soonLimit = sampleTime().seconds * 1.5f;
    const float invDt = sampleTime().invSeconds;
    Impact* imp = impacts;
    int left = maxImpacts;
    int material = 0;
    if (countA != 0)
        material = materialOf(isectsA[0]);
    else if (countB != 0)
        material = materialOf(isectsB[0]);
    makeBsInside(isectsA, countA);
    makeBsInside(isectsB, countB);

    // Vertices that reached faces.
    int edgesA = 0;
    for (int i = 0; i < countA; ++i) {
        const Intersection& s = isectsA[i];
        if (!(s.flags & Intersection::kVertex) && !(s.flags & Intersection::kInterior))
            ++edgesA;
        if (!(isectsA[i].flags & Intersection::kSearched) && left != 0)
            doEndPtSearch(ma, lastA, mb, lastB, ca, cb, isectsA[i].vertexB, isectsA, countA, imp, left, true);
    }
    int edgesB = 0;
    for (int i = 0; i < countB; ++i) {
        const Intersection& s = isectsB[i];
        if (!(s.flags & Intersection::kVertex))
            ++edgesB;
        if (!(isectsB[i].flags & Intersection::kSearched) && !(isectsB[i].flags & Intersection::kPaired) &&
            left != 0)
            doEndPtSearch(mb, lastB, ma, lastA, cb, ca, isectsB[i].vertexB, isectsB, countB, imp, left, false);
    }
    // An edge's two crossings share their skip flag and the earlier time.
    auto mergePairs = [](Intersection* list, int count) {
        for (int i = 0; i < count - 1; ++i) {
            if (list[i].element != list[i + 1].element)
                continue;
            if (list[i].flags & Intersection::kSkip)
                list[i + 1].flags |= Intersection::kSkip;
            else if (list[i + 1].flags & Intersection::kSkip)
                list[i].flags |= Intersection::kSkip;
            const float t = list[i].timeToImpact < list[i + 1].timeToImpact ? list[i].timeToImpact
                                                                              : list[i + 1].timeToImpact;
            list[i].timeToImpact = t;
            list[i + 1].timeToImpact = t;
            ++i;
        }
    };
    mergePairs(isectsA, countA);
    mergePairs(isectsB, countB);

    // Which list's edges go through the other's faces (only when just one
    // bound has edge crossings); edge-edge pairs when both do.
    Intersection* faceList = nullptr;
    int faceCount = 0;
    bool aOwnsEdges = false;
    const Mat34* edgeM = nullptr;
    const Mat34* edgeLast = nullptr;
    const Mat34* polyM = nullptr;
    const Mat34* polyLast = nullptr;
    if (edgesA == 0) {
        if (edgesB != 0) {
            faceList = isectsB;
            faceCount = countB;
            aOwnsEdges = false;
            edgeM = mb;
            edgeLast = lastB;
            polyM = ma;
            polyLast = lastA;
        }
    } else if (edgesB != 0) {
        for (int j = 0; j < countB;) {
            Intersection& sb = isectsB[j];
            bool pairedB = false;
            if (!(sb.flags & Intersection::kVertex)) {
                pairedB = pairedWithNext(isectsB, countB, j);
                const Vec3 db = sb.b - sb.a;
                const float lenB2 = db.x * db.x + db.y * db.y + db.z * db.z;
                for (int k = 0; k < countA;) {
                    Intersection& sa = isectsA[k];
                    bool pairedA = false;
                    if (!(sa.flags & Intersection::kVertex)) {
                        pairedA = pairedWithNext(isectsA, countA, k);
                        const std::uint16_t fb = sb.flags;
                        if (!(fb & Intersection::kInterior) &&
                            (!(fb & Intersection::kSkip) || !(sa.flags & Intersection::kSkip))) {
                            const Vec3 da = sa.b - sa.a;
                            Vec3 n, pa, pb;
                            float dist = 0.0f;
                            int ok = 0;
                            geom::segSegDistNorm(sa.a, sa.b, da, sb.a, sb.b, db, n, pa, pb, dist, ok);
                            if (ok != 0) {
                                bool valid;
                                if (dist == 0.0f) {
                                    valid = true;
                                } else {
                                    const float cosB =
                                        static_cast<const BoundPolygonal*>(sb.bound)->edgeCosine(sb.element);
                                    const float cosA =
                                        static_cast<const BoundPolygonal*>(sa.bound)->edgeCosine(sa.element);
                                    valid = dot(n, sb.edgeNormal) <= -((cosB - 0.25881904f) - 0.01f) &&
                                            (cosA - 0.25881904f) - 0.01f <= dot(n, sa.edgeNormal);
                                }
                                const float dist2 = dist * dist;
                                const float cx = db.y * da.z - db.z * da.y;
                                const float cy = db.z * da.x - da.z * db.x;
                                const float cz = da.y * db.x - db.y * da.x;
                                const float c2 = cx * cx + cy * cy + cz * cz;
                                const bool nearlyParallel =
                                    c2 < dist2 * lenB2 || c2 < (da.x * da.x + da.y * da.y + da.z * da.z) * dist2;
                                float time;
                                if (dist == 0.0f) {
                                    time = 0.0f;
                                } else {
                                    const Vec3 dispA = geom::getDisp(*ma, *lastA, pa);
                                    const Vec3 dispB = geom::getDisp(*mb, *lastB, pb);
                                    const Vec3 rv{(dispA.x - dispB.x) * invDt, (dispA.y - dispB.y) * invDt,
                                                  (dispA.z - dispB.z) * invDt};
                                    const float approach = (n.z * rv.z + n.y * rv.y) + rv.x * n.x;
                                    if (0.0f < approach)
                                        time = 0.0f <= dist - kPenetration ? (dist - kPenetration) / approach : 0.0f;
                                    else
                                        time = FLT_MAX;
                                }
                                const Vec3 mid{(pa.x + pb.x) * 0.5f, (pa.y + pb.y) * 0.5f, (pa.z + pb.z) * 0.5f};
                                bool recent;
                                if (soonLimit <= time) {
                                    recent = false;
                                    resetVertNeedsH(&sa, &sb, pairedA, pairedB, dist);
                                } else {
                                    recent = true;
                                    resetVertNeedsH(&sa, &sb, pairedA, pairedB);
                                }
                                checkSaveEdgeEdge(sa, j, recent, nearlyParallel, mid, pb, n, dist, dist2, valid, time,
                                                  soonLimit);
                                if (pairedA)
                                    checkSaveEdgeEdge(isectsA[k + 1], j, recent, nearlyParallel, mid, pb, n, dist,
                                                      dist2, valid, time, soonLimit);
                                checkSaveEdgeEdge(sb, k, recent, nearlyParallel, mid, pa, n, dist, dist2, valid, time,
                                                  soonLimit);
                                if (pairedB)
                                    checkSaveEdgeEdge(isectsB[j + 1], k, recent, nearlyParallel, mid, pa, n, dist,
                                                      dist2, valid, time, soonLimit);
                            }
                        }
                    }
                    k += pairedA ? 2 : 1;
                }
            }
            j += pairedB ? 2 : 1;
        }
        // Edge-edge impacts, first from A's side.
        for (int k = 0; k < countA; ++k) {
            Intersection& sa = isectsA[k];
            const int partner = sa.edgeEdgeIndex;
            if (partner < 0 || !(sa.flags & Intersection::kEdgeEdge))
                continue;
            if (k >= 1 && isectsA[k - 1].element == sa.element && isectsA[k - 1].edgeEdgeIndex == partner)
                continue;
            if (isectsB[partner].flags & Intersection::kSkip)
                continue;
            sa.flags |= Intersection::kUsed;
            if (0 < left) {
                writeEdgeEdgeImpact(*imp, ca, cb, sa.element, isectsB[partner].element, sa);
                --left;
                ++imp;
            }
        }
        // Then B's, unless A's side already made the same pair.
        for (int j = 0; j < countB; ++j) {
            Intersection& sb = isectsB[j];
            const int partner = sb.edgeEdgeIndex;
            const bool samePrev = j >= 1 && isectsB[j - 1].element == sb.element;
            if (partner < 0 || !(sb.flags & Intersection::kEdgeEdge))
                continue;
            if (samePrev && isectsB[j - 1].edgeEdgeIndex == partner)
                continue;
            const Intersection& sa = isectsA[partner];
            if (sa.flags & Intersection::kSkip)
                continue;
            sb.flags |= Intersection::kUsed;
            const int index = samePrev ? j - 1 : j;
            if (sa.edgeEdgeIndex != index &&
                (countA - 1 <= partner || isectsA[partner + 1].element != sa.element ||
                 isectsA[partner + 1].edgeEdgeIndex != index) &&
                0 < left) {
                writeEdgeEdgeImpact(*imp, ca, cb, sa.element, sb.element, sb);
                --left;
                ++imp;
            }
        }
    } else {
        faceList = isectsA;
        faceCount = countA;
        aOwnsEdges = true;
        edgeM = ma;
        edgeLast = lastA;
        polyM = mb;
        polyLast = lastB;
    }

    // Edges through faces.
    if (faceList && faceCount != 0 && polyM && edgeM) {
        const Mat34 invM = polyM->fastInverse();
        for (int i = 0; i < faceCount;) {
            Intersection& s = faceList[i];
            bool paired = false;
            if (!(s.flags & Intersection::kVertex) && !(s.flags & Intersection::kInterior)) {
                paired = pairedWithNext(faceList, faceCount, i);
                if (!(s.flags & Intersection::kSkip)) {
                    Vec3 dir = s.b - s.a;
                    const float len2 = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z;
                    const float inv = len2 == 0.0f ? 0.0f : 1.0f / std::sqrt(len2);
                    dir = {inv * dir.x, dir.y * inv, dir.z * inv};
                    auto relVelAt = [&](const Vec3& p) {
                        const Vec3 de = geom::getDisp(*edgeM, *edgeLast, p);
                        const Vec3 dp = geom::getDisp(*polyM, *polyLast, p);
                        return Vec3{(de.x - dp.x) * invDt, (de.y - dp.y) * invDt, (de.z - dp.z) * invDt};
                    };
                    Vec3 n, pos;
                    float depth = 0.0f;
                    int faceEdge = -1;
                    bool soon = false;
                    auto emit = [&](int elementOfEdge, int edge) {
                        if (left <= 0)
                            return;
                        Impact& out = *imp;
                        out.kind = Impact::EdgeEdge;
                        if (!aOwnsEdges) {
                            out.colliderA = ca;
                            out.colliderB = cb;
                            out.elementA = edge;
                            out.elementB = elementOfEdge;
                            out.normal = -n;
                        } else {
                            out.colliderA = ca;
                            out.colliderB = cb;
                            out.elementA = elementOfEdge;
                            out.elementB = edge;
                            out.normal = n;
                        }
                        out.componentA = -1;
                        out.componentB = -1;
                        out.position = pos;
                        out.depth = depth;
                        out.penetration = kPenetration;
                        --left;
                        ++imp;
                    };
                    if (getCollideEdgePoly(s, relVelAt(s.position), *polyM, invM, dir, n, pos, depth, faceEdge,
                                           soon)) {
                        s.flags |= Intersection::kUsed;
                        emit(s.element, faceEdge);
                    }
                    if (!soon)
                        resetVertNeedsH(&s, nullptr, paired, false, depth);
                    else
                        resetVertNeedsH(&s, nullptr, paired, false);
                    if (paired) {
                        Intersection& s2 = faceList[i + 1];
                        int faceEdge2 = -1;
                        if (getCollideEdgePoly(s2, relVelAt(s2.position), *polyM, invM, dir, n, pos, depth, faceEdge2,
                                               soon)) {
                            s.flags |= Intersection::kUsed;
                            if (faceEdge2 != faceEdge)
                                emit(s2.element, faceEdge2);
                        }
                        if (!soon)
                            resetVertNeedsH(&s, nullptr, paired, false, depth);
                        else
                            resetVertNeedsH(&s, nullptr, paired, false);
                    }
                }
            }
            i += paired ? 2 : 1;
        }
    }

    // Vertices the edge passes flagged for another look.
    for (int i = 0; i < countA; ++i) {
        const std::uint16_t f = isectsA[i].flags;
        if ((f & Intersection::kNeedsRetry) && !(f & Intersection::kSkip) && !(f & Intersection::kUsed))
            retryVertPolyCollide(ca, cb, isectsA[i], imp, left, true);
    }
    for (int i = 0; i < countB; ++i) {
        const std::uint16_t f = isectsB[i].flags;
        if ((f & Intersection::kNeedsRetry) && !(f & Intersection::kSkip) && !(f & Intersection::kUsed))
            retryVertPolyCollide(cb, ca, isectsB[i], imp, left, false);
    }
    const int used = maxImpacts - left;
    // Terrain bounds give every impact the material of the first pierced
    // polygon.
    if (b.type == BoundType::Terrain || b.type == BoundType::TerrainLocal) {
        for (int i = 0; i < used; ++i) {
            if (impacts[i].colliderB == cb)
                impacts[i].componentB = material;
            else
                impacts[i].componentA = material;
        }
    }
    return used;
}

int findImpactsSphereToPoly(const BoundPolygonal& poly, const BoundSphere& sphere, const Mat34& sphereM,
                            const Mat34& polyM, Collider* sphereCollider, Collider* polyCollider, Impact* impacts,
                            int max, const Vec3& relPos, const Vec3& relDisp) {
    // phBoundPolygonal::FindImpactsSphereToPoly: the sphere's centre in the
    // polygonal bound's space against each polygon's closest feature; a
    // sphere that missed every feature but crossed a face during the sample
    // (judged with relDisp, which the original does not rotate into the
    // bound's space) still hits that face.
    Vec3 center{dot(polyM.m0, relPos), dot(polyM.m1, relPos), dot(polyM.m2, relPos)};
    if (sphere.isOffset) {
        const Vec3& c = sphere.centroid;
        const Vec3 off{(sphereM.m1.x * c.y + sphereM.m2.x * c.z) + sphereM.m0.x * c.x,
                       (sphereM.m1.y * c.y + sphereM.m2.y * c.z) + sphereM.m0.y * c.x,
                       (sphereM.m1.z * c.y + sphereM.m2.z * c.z) + sphereM.m0.z * c.x};
        const float ox = dot(polyM.m0, off);
        const float oy = dot(polyM.m1, off);
        const float oz = dot(polyM.m2, off);
        center = {ox + center.x, oy + center.y, oz + center.z};
    }
    const Vec3 back = -relDisp;
    int count = 0;
    for (int p = 0; p < poly.numPolygons(); ++p) {
        const Polygon& pg = poly.polygons[static_cast<std::size_t>(p)];
        const int n = pg.vertexCount();
        Vec3 verts[4];
        for (int k = 0; k < n; ++k)
            verts[k] = poly.vertex(pg.v[static_cast<std::size_t>(k)]);
        const Vec3 normal = pg.normal;
        Vec3 position, contactNormal;
        int feature = 0;
        float depth = 0.0f;
        int result = geom::findImpactPolygonToSphere(center, sphere.radius, verts, n, normal, position, feature,
                                                     contactNormal, depth);
        int element;
        if (result == 0) {
            element = pg.v[static_cast<std::size_t>(feature)];
        } else if (result == 1) {
            element = pg.edges[static_cast<std::size_t>(feature)];
        } else if (result == 2) {
            element = p;
        } else {
            if (!(dot(normal, back) < 0.0f))
                continue;
            Segment seg;
            seg.kind = Segment::Probe;
            seg.a = {center.x - back.x, center.y - back.y, center.z - back.z};
            seg.b = center;
            IntersectionPoint hit;
            if (!pg.testSegmentDirected(poly.vertices, seg, hit, 2.0f))
                continue;
            const float r = sphere.radius;
            contactNormal = normal;
            position = {center.x - r * normal.x, center.y - normal.y * r, center.z - r * normal.z};
            result = 2;
            depth = dot(verts[0] - position, normal);
            const float h = depth * 0.5f;
            position = {h * normal.x + position.x, normal.y * h + position.y, normal.z * h + position.z};
            element = p;
        }
        impacts[count].startMakingNewImpact(depth, contactNormal, position, sphereCollider, polyCollider, &polyM, 0,
                                            -1, -1);
        if (count > 0 && !Impact::addImpactSpherePlaneTest(impacts, count, sphereM.m3, sphere.radius))
            continue;
        impacts[count].finishMakingNewImpact(result, element, sphere, poly, 0);
        if (++count == max)
            break;
    }
    if (count > 1)
        Impact::cullImpactList(impacts, count, relDisp);
    return count;
}

// --- lvlSDL (Level.h) -----------------------------------------------------------------------
//
// The level's routines live here because they share phBoundPolygonal's
// segment bookkeeping and impact helpers.

void LevelBound::clear() {
    vertices.assign(1, Vec3{});
    polygons.clear();
}

void LevelBound::addPolygon(const Vec3* corners, int count, const Vec3& normal, std::uint8_t material) {
    // Each polygon gets its own copies of its corners (the level's edges
    // are never used, so sharing does not matter); index 0 stays unused so
    // that v[3] == 0 still marks a triangle.
    if (vertices.empty())
        vertices.emplace_back();
    Polygon poly;
    for (int i = 0; i < count; ++i) {
        poly.v[static_cast<std::size_t>(i)] = static_cast<std::uint16_t>(vertices.size());
        vertices.push_back(corners[i]);
    }
    poly.normal = normal;
    poly.material = material;
    poly.computeEdgeNormalCross(vertices);
    polygons.push_back(poly);
}

const Vec3& LevelBound::vertex(int index) const {
    return vertices[static_cast<std::size_t>(index)];
}

const Material& LevelBound::material(int index) const {
    // lvlLevelBound::GetMaterial.
    return level ? level->material(index) : defaultBoundMaterial();
}

const Material& Level::material(int) const {
    return defaultBoundMaterial();
}

int collidePolyToLevel(const LevelBound& level, const BoundPolygonal& bound, Collider* collider, const Mat34& m,
                       const Mat34& last, Intersection* out, int max, int& count, bool sweep) {
    // lvlSDL::CollidePolyToLevel after the polygon collection: every vertex
    // sweeps from where `last` had it (no reach test, unlike
    // TestBoundPolyPolyUseDotSmall) and every edge is tested, against each
    // polygon in collection order; edges only against polygons whose plane
    // the bound's origin is within its radius of.
    Scratch& s = scratch();
    const auto nv = static_cast<std::size_t>(bound.numVertices());
    s.flags.assign(nv, 0);
    s.verts.resize(nv);
    s.dist.assign(nv, 0.0f);
    for (std::size_t i = 0; i < nv; ++i)
        s.verts[i] = xform(m, bound.vertices[i]);
    s.sweeps.clear();
    if (sweep) {
        for (int i = 0; i < bound.numVertices(); ++i) {
            DispSegment d;
            d.vertex = static_cast<std::uint16_t>(i);
            const Vec3& v = bound.vertex(i);
            d.previous = {((last.m1.x * v.y + last.m2.x * v.z) + last.m0.x * v.x) + last.m3.x,
                          ((last.m1.y * v.y + last.m2.y * v.z) + last.m0.y * v.x) + last.m3.y,
                          ((last.m1.z * v.y + last.m2.z * v.z) + last.m0.z * v.x) + last.m3.z};
            s.sweeps.push_back(d);
        }
    }
    s.edges.clear();
    for (int e = 0; e < bound.numEdges(); ++e) {
        EdgeSegment seg;
        seg.edge = e;
        s.edges.push_back(seg);
    }
    const float penetration = level.penetration < bound.penetration ? level.penetration : bound.penetration;
    for (int p = 0; p < static_cast<int>(level.polygons.size()); ++p) {
        const Polygon& poly = level.polygons[static_cast<std::size_t>(p)];
        const Vec3& p0 = level.vertices[poly.v[0]];
        const Vec3& n = poly.normal;
        const bool near = dot(m.m3 - p0, n) <= bound.radius;
        collidePolygon(poly, p, level.vertices, penetration, bound, s, false, near);
    }
    count = writeIntersections(bound, level, level.polygons, collider, m, s, out, max);
    return count;
}

int findLevelImpacts(const LevelBound& level, const BoundPolygonal& bound, const Mat34& m, const Mat34& last,
                     Collider* levelCollider, Collider* collider, Intersection* isects, int count, Impact* impacts,
                     int maxImpacts) {
    // lvlSDL's impact search (FindImpacts specialised for the level, which
    // does not move and whose edges are not tested): the bound's
    // intersections form the "B" list. Edge-through-face impacts are made
    // with friction 1 and elasticity 0.25, which phImpact::CalcCollision
    // replaces with the materials' values.
    const float invDt = sampleTime().invSeconds;
    const Mat34 identity = Mat34::identity();
    Impact* imp = impacts;
    int left = maxImpacts;
    const int material = count != 0 ? materialOf(isects[0]) : 0;
    const float penetration = bound.penetration < level.penetration ? bound.penetration : level.penetration;
    makeBsInside(isects, count);
    int edges = 0;
    for (int i = 0; i < count; ++i) {
        const std::uint16_t f = isects[i].flags;
        if (!(f & Intersection::kVertex) && !(f & Intersection::kInterior))
            ++edges;
        if (!(isects[i].flags & Intersection::kSearched) && !(isects[i].flags & Intersection::kPaired))
            doEndPtSearch(&m, &last, nullptr, nullptr, collider, levelCollider, isects[i].vertexB, isects, count,
                          imp, left, false);
    }
    for (int i = 0; i < count - 1; ++i) {
        if (isects[i].element != isects[i + 1].element)
            continue;
        if (isects[i].flags & Intersection::kSkip)
            isects[i + 1].flags |= Intersection::kSkip;
        else if (isects[i + 1].flags & Intersection::kSkip)
            isects[i].flags |= Intersection::kSkip;
        const float t = isects[i].timeToImpact < isects[i + 1].timeToImpact ? isects[i].timeToImpact
                                                                            : isects[i + 1].timeToImpact;
        isects[i].timeToImpact = t;
        isects[i + 1].timeToImpact = t;
        ++i;
    }
    if (edges != 0) {
        auto write = [&](int elementA, int elementB, const Vec3& n, const Vec3& pos, float depth) {
            if (left <= 0)
                return;
            Impact& out = *imp;
            out.kind = Impact::EdgeEdge;
            out.colliderA = levelCollider;
            out.colliderB = collider;
            out.componentA = -1;
            out.componentB = -1;
            out.elementB = elementB;
            out.elementA = elementA;
            out.position = pos;
            out.normal = -n;
            out.depth = depth;
            out.penetration = penetration;
            out.friction = 1.0f;
            out.elasticity = 0.25f;
            --left;
            ++imp;
        };
        for (int k = 0; k < count;) {
            Intersection& s = isects[k];
            bool paired = false;
            if (!(s.flags & Intersection::kVertex) && !(s.flags & Intersection::kInterior)) {
                paired = pairedWithNext(isects, count, k);
                if (!(s.flags & Intersection::kSkip)) {
                    Vec3 dir = s.b - s.a;
                    const float len2 = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z;
                    const float inv = len2 == 0.0f ? 0.0f : 1.0f / std::sqrt(len2);
                    dir = {inv * dir.x, inv * dir.y, inv * dir.z};
                    Vec3 rv = geom::getDisp(m, last, s.position);
                    rv = {rv.x * invDt, rv.y * invDt, rv.z * invDt};
                    Vec3 n, pos;
                    float depth = 0.0f;
                    int faceEdge = -1;
                    bool soon = false;
                    if (getCollideEdgePoly(s, rv, identity, identity, dir, n, pos, depth, faceEdge, soon)) {
                        s.flags |= Intersection::kUsed;
                        write(0, s.element, n, pos, depth);
                    }
                    if (!soon)
                        resetVertNeedsH(&s, nullptr, paired, false, depth);
                    else
                        resetVertNeedsH(&s, nullptr, paired, false);
                    if (paired) {
                        const Intersection& s2 = isects[k + 1];
                        rv = geom::getDisp(m, last, s2.position);
                        rv = {rv.x * invDt, rv.y * invDt, rv.z * invDt};
                        int faceEdge2 = -1;
                        if (getCollideEdgePoly(s2, rv, identity, identity, dir, n, pos, depth, faceEdge2, soon)) {
                            s.flags |= Intersection::kUsed;
                            if (faceEdge2 != faceEdge)
                                write(1, s.element, n, pos, depth);
                        }
                        if (!soon)
                            resetVertNeedsH(&s, nullptr, paired, false, depth);
                        else
                            resetVertNeedsH(&s, nullptr, paired, false);
                    }
                }
            }
            k += paired ? 2 : 1;
        }
    }
    for (int i = 0; i < count; ++i) {
        const std::uint16_t f = isects[i].flags;
        if ((f & Intersection::kNeedsRetry) && !(f & Intersection::kSkip) && !(f & Intersection::kUsed))
            retryVertPolyCollide(collider, levelCollider, isects[i], imp, left, false);
    }
    const int used = maxImpacts - left;
    for (int i = 0; i < used; ++i) {
        if (impacts[i].colliderA == levelCollider)
            impacts[i].componentA = material;
        else
            impacts[i].componentB = material;
    }
    return used;
}

} // namespace mm2::phys
