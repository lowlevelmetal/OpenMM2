// MM2's bound family: phBound, phPolygon, phBoundPolygonal, phBoundGeometry,
// phBoundBox, phBoundSphere, phBoundHotdog and phBoundTerrain's loading,
// ported from the code of midtown2.exe build 3393 (MM2Recomp). The collision
// routines of each bound type live in BoundPolygonal.cpp, BoundBox.cpp,
// BoundSphere.cpp, BoundHotdog.cpp and BoundTerrain.cpp. See
// docs/physics.md, "Collision".

#include "phys/Bound.h"

#include <cmath>

namespace mm2::phys {
namespace {

// The edge order of the box (phBoundBox's static edge table).
constexpr std::array<std::array<std::uint16_t, 2>, 12> kBoxEdges{{
    {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
}};
// The box's faces (+x, -x, +y, -y, +z, -z) and the edge index of each of
// their sides (v[i], v[i+1]).
constexpr std::array<std::array<std::uint16_t, 4>, 6> kBoxFaces{{
    {4, 0, 3, 7}, {2, 1, 5, 6}, {1, 0, 4, 5}, {7, 3, 2, 6}, {3, 0, 1, 2}, {5, 4, 7, 6},
}};
constexpr std::array<std::array<std::uint16_t, 4>, 6> kBoxFaceEdges{{
    {8, 3, 11, 7}, {1, 9, 5, 10}, {0, 8, 4, 9}, {11, 2, 10, 6}, {3, 0, 1, 2}, {4, 7, 6, 5},
}};

float sqrtf32(float v) {
    return std::sqrt(v);
}

// Vector4::Cross on the xyz parts.
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

bool signBit(float v) {
    return std::signbit(v);
}

// phPolygon::SegEdgeCheckDirected: the segment from `a` along `dir` passes
// outside the edge p->q (whose inward normal is `edgeNormal`).
bool segEdgeCheckDirected(const Vec3& p, const Vec3& q, const Vec3& a, const Vec3& dir, const Vec3& edgeNormal) {
    const float ex = q.x - p.x, ey = q.y - p.y, ez = q.z - p.z;
    const float cz = ex * dir.y - ey * dir.x;
    const float cx = ey * dir.z - ez * dir.y;
    const float cy = ez * dir.x - ex * dir.z;
    const float side = (a.x - p.x) * cx + (a.y - p.y) * cy + (a.z - p.z) * cz;
    if (side == 0.0f)
        return false;
    return 0.0f < cx * edgeNormal.x + cy * edgeNormal.y + cz * edgeNormal.z && side < 0.0f;
}

// phPolygon::SegEdgeCheckUndirected: the line passes on the other side of
// the edge than the polygon's interior.
bool segEdgeCheckUndirected(const Vec3& p, const Vec3& q, const Vec3& a, const Vec3& dir, const Vec3& edgeNormal) {
    const float ex = q.x - p.x, ey = q.y - p.y, ez = q.z - p.z;
    const float cz = ex * dir.y - ey * dir.x;
    const float cx = ey * dir.z - ez * dir.y;
    const float cy = ez * dir.x - ex * dir.z;
    const float side = (a.x - p.x) * cx + (a.y - p.y) * cy + (a.z - p.z) * cz;
    if (side == 0.0f)
        return false;
    return signBit(side) != signBit(cx * edgeNormal.x + cy * edgeNormal.y + cz * edgeNormal.z);
}

// One side test of the Detect/Test routines: the line a + s * dir against
// the edge from `from` to `to` of a polygon. Directed tests reject lines
// passing outside; undirected tests reject lines on the other side.
bool outsideDirected(const Vec3& from, const Vec3& to, const Vec3& a, const Vec3& dir, const Vec3& edgeNormal) {
    const Vec3 c = cross(to - from, dir);
    const Vec3 w = a - from;
    const float side = w.x * c.x + w.y * c.y + w.z * c.z;
    if (side == 0.0f)
        return false;
    return 0.0f < edgeNormal.x * c.x + edgeNormal.y * c.y + edgeNormal.z * c.z && side < 0.0f;
}

bool outsideUndirected(const Vec3& from, const Vec3& to, const Vec3& a, const Vec3& dir, const Vec3& edgeNormal) {
    const Vec3 c = cross(to - from, dir);
    const Vec3 w = a - from;
    const float side = w.x * c.x + w.y * c.y + w.z * c.z;
    if (side == 0.0f)
        return false;
    return signBit(side) != signBit(edgeNormal.x * c.x + edgeNormal.y * c.y + edgeNormal.z * c.z);
}

const Vec3& at(std::span<const Vec3> verts, std::uint16_t i) {
    return verts[i];
}

} // namespace

// --- Segments and intersections ----------------------------------------------------------

void Segment::calculateInfo() {
    // lvlSegment::CalculateInfo.
    if (a.x == b.x && a.z == b.z) {
        vertical = true;
        invLength = 1.0f / std::abs(a.y - b.y);
        return;
    }
    vertical = false;
    const float len2 = (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z);
    invLength = len2 != 0.0f ? 1.0f / sqrtf32(len2) : 0.0f;
}

void IntersectionPoint::transform(const Mat34& m) {
    // phIntersectionPoint::Transform.
    const Vec3 p = position;
    position = {p.y * m.m1.x + p.z * m.m2.x + p.x * m.m0.x + m.m3.x,
                p.y * m.m1.y + p.z * m.m2.y + p.x * m.m0.y + m.m3.y,
                p.x * m.m0.z + p.y * m.m1.z + p.z * m.m2.z + m.m3.z};
    const Vec3 n = normal;
    normal = {n.y * m.m1.x + n.z * m.m2.x + n.x * m.m0.x, n.y * m.m1.y + n.z * m.m2.y + n.x * m.m0.y,
              n.x * m.m0.z + n.y * m.m1.z + n.z * m.m2.z};
}

void backupDispByPenetration(Vec3& a, const Vec3& b, float penetration) {
    // phBoundPolygonal::BackupDispByPenetration.
    const float dx = a.x - b.x;
    const float len2 = dx * dx + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z);
    if (penetration * penetration < len2) {
        Vec3 d{dx, a.y - b.y, a.z - b.z};
        const float scale = (penetration * 1.5f) / sqrtf32(len2) + 1.0f;
        d = {scale * d.x, scale * d.y, scale * d.z};
        a = {d.x + b.x, d.y + b.y, d.z + b.z};
        return;
    }
    const Vec3 twice{a.x + a.x, a.y + a.y, a.z + a.z};
    a = {twice.x - b.x, twice.y - b.y, twice.z - b.z};
}

void backupAByPenetration(Segment& seg, float penetration) {
    // phBoundPolygonal::BackupAbyPenetration: moves the start of the segment
    // away from its end.
    backupDispByPenetration(seg.a, seg.b, penetration);
}

// --- phPolygon ---------------------------------------------------------------------------

void Polygon::initTriangle(std::uint16_t a, std::uint16_t b, std::uint16_t c, std::span<const Vec3> verts) {
    v = {a, b, c, 0};
    calculateNormal(verts);
}

void Polygon::initQuad(std::uint16_t a, std::uint16_t b, std::uint16_t c, std::uint16_t d,
                       std::span<const Vec3> verts) {
    v = {a, b, c, d};
    calculateNormal(verts);
}

void Polygon::calculateNormal(std::span<const Vec3> verts) {
    // phPolygon::CalculateNormal: n = (v2 - v1) x (v0 - v1), area from the
    // triangle(s).
    const Vec3& p0 = at(verts, v[0]);
    const Vec3& p1 = at(verts, v[1]);
    const Vec3& p2 = at(verts, v[2]);
    const float nz = (p0.y - p1.y) * (p2.x - p1.x) - (p2.y - p1.y) * (p0.x - p1.x);
    const float nx = (p0.z - p1.z) * (p2.y - p1.y) - (p0.y - p1.y) * (p2.z - p1.z);
    const float ny = (p2.z - p1.z) * (p0.x - p1.x) - (p0.z - p1.z) * (p2.x - p1.x);
    const float len2 = nz * nz + nx * nx + ny * ny;
    area = sqrtf32(len2) * 0.5f;
    const float scale = len2 == 0.0f ? 0.0f : 1.0f / sqrtf32(len2);
    normal = {nx * scale, ny * scale, nz * scale};
    if (v[3] != 0) {
        const Vec3& p3 = at(verts, v[3]);
        const float qz = (p2.y - p3.y) * (p0.x - p3.x) - (p0.y - p3.y) * (p2.x - p3.x);
        const float qx = (p2.z - p3.z) * (p0.y - p3.y) - (p2.y - p3.y) * (p0.z - p3.z);
        const float qy = (p0.z - p3.z) * (p2.x - p3.x) - (p2.z - p3.z) * (p0.x - p3.x);
        area = sqrtf32(qx * qx + qz * qz + qy * qy) * 0.5f + area;
    }
    computeEdgeNormalCross(verts);
}

void Polygon::computeEdgeNormalCross(std::span<const Vec3> verts) {
    // phPolygon::ComputeEdgeNormalCross: for the side from v[i] to v[i+1],
    // normal x side, normalised (it points into the polygon).
    const int n = vertexCount();
    int prev = n - 1;
    for (int k = 0; k < n; ++k) {
        const Vec3& to = at(verts, v[static_cast<std::size_t>(k)]);
        const Vec3& from = at(verts, v[static_cast<std::size_t>(prev)]);
        const float dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
        Vec3 e{dz * normal.y - dy * normal.z, dx * normal.z - dz * normal.x, dy * normal.x - dx * normal.y};
        const float len2 = e.z * e.z + e.y * e.y + e.x * e.x;
        const float scale = len2 == 0.0f ? 0.0f : 1.0f / sqrtf32(len2);
        edgeNormals[static_cast<std::size_t>(prev)] = {scale * e.x, scale * e.y, scale * e.z};
        prev = k;
    }
}

bool Polygon::testSegmentDirected(std::span<const Vec3> verts, const Segment& seg, IntersectionPoint& out,
                                  float maxT) const {
    // phPolygon::TestSegmentDirected.
    const Vec3& p0 = at(verts, v[0]);
    const Vec3 w0 = seg.a - p0;
    const float da = w0.y * normal.y + w0.x * normal.x + w0.z * normal.z;
    if (da < 0.0f)
        return false;
    const float db = (seg.b.y - p0.y) * normal.y + (seg.b.x - p0.x) * normal.x + (seg.b.z - p0.z) * normal.z;
    if (0.0f <= db)
        return false;
    const float t = da / (da - db);
    if (maxT < t)
        return false;
    const Vec3 dir = seg.b - seg.a;
    const Vec3& p1 = at(verts, v[1]);
    const Vec3& p2 = at(verts, v[2]);
    if (outsideDirected(p0, p1, seg.a, dir, edgeNormals[0]))
        return false;
    if (outsideDirected(p1, p2, seg.a, dir, edgeNormals[1]))
        return false;
    if (v[3] == 0) {
        if (outsideDirected(p2, p0, seg.a, dir, edgeNormals[2]))
            return false;
    } else {
        const Vec3& p3 = at(verts, v[3]);
        if (outsideDirected(p2, p3, seg.a, dir, edgeNormals[2]))
            return false;
        if (segEdgeCheckDirected(p3, p0, seg.a, dir, edgeNormals[3]))
            return false;
    }
    out.position = seg.a + (seg.b - seg.a) * t;
    out.normal = normal;
    out.depth = -db;
    out.t = t;
    out.bInside = true;
    return true;
}

bool Polygon::testSegmentUndirected(std::span<const Vec3> verts, const Segment& seg, IntersectionPoint& out,
                                    float rejectFrom, float rejectTo) const {
    // phPolygon::TestSegmentUndirected.
    const Vec3& p0 = at(verts, v[0]);
    const float da = (seg.a.z - p0.z) * normal.z + (seg.a.x - p0.x) * normal.x + (seg.a.y - p0.y) * normal.y;
    const float db = (seg.b.z - p0.z) * normal.z + (seg.b.x - p0.x) * normal.x + (seg.b.y - p0.y) * normal.y;
    if (signBit(da) == signBit(db))
        return false;
    const float t = da / (da - db);
    if (t < rejectTo && rejectFrom < t)
        return false;
    const Vec3 dir = seg.b - seg.a;
    const Vec3& p1 = at(verts, v[1]);
    const Vec3& p2 = at(verts, v[2]);
    if (outsideUndirected(p0, p1, seg.a, dir, edgeNormals[0]))
        return false;
    if (outsideUndirected(p1, p2, seg.a, dir, edgeNormals[1]))
        return false;
    if (v[3] == 0) {
        if (outsideUndirected(p2, p0, seg.a, dir, edgeNormals[2]))
            return false;
    } else {
        const Vec3& p3 = at(verts, v[3]);
        if (outsideUndirected(p2, p3, seg.a, dir, edgeNormals[2]))
            return false;
        if (segEdgeCheckUndirected(p3, p0, seg.a, dir, edgeNormals[3]))
            return false;
    }
    out.position = seg.a + (seg.b - seg.a) * t;
    out.normal = normal;
    out.t = t;
    if (da <= 0.0f) {
        out.depth = -da;
        out.bInside = false;
    } else {
        out.depth = -db;
        out.bInside = true;
    }
    return true;
}

bool Polygon::detectSegmentDirected(std::span<const Vec3> verts, const Vec3& a, const Vec3& b) const {
    // phPolygon::DetectSegmentDirected.
    const Vec3 dir = b - a;
    const Vec3& p0 = at(verts, v[0]);
    const Vec3& p1 = at(verts, v[1]);
    const Vec3& p2 = at(verts, v[2]);
    if (outsideDirected(p0, p1, a, dir, edgeNormals[0]))
        return false;
    if (outsideDirected(p1, p2, a, dir, edgeNormals[1]))
        return false;
    if (v[3] == 0)
        return !outsideDirected(p2, p0, a, dir, edgeNormals[2]);
    const Vec3& p3 = at(verts, v[3]);
    if (outsideDirected(p2, p3, a, dir, edgeNormals[2]))
        return false;
    return !segEdgeCheckDirected(p3, p0, a, dir, edgeNormals[3]);
}

bool Polygon::detectSegmentUndirected(std::span<const Vec3> verts, const Vec3& a, const Vec3& b) const {
    // phPolygon::DetectSegmentUndirected.
    const Vec3 dir = b - a;
    const Vec3& p0 = at(verts, v[0]);
    const Vec3& p1 = at(verts, v[1]);
    const Vec3& p2 = at(verts, v[2]);
    if (outsideUndirected(p0, p1, a, dir, edgeNormals[0]))
        return false;
    if (outsideUndirected(p1, p2, a, dir, edgeNormals[1]))
        return false;
    if (v[3] == 0)
        return !outsideUndirected(p2, p0, a, dir, edgeNormals[2]);
    const Vec3& p3 = at(verts, v[3]);
    if (outsideUndirected(p2, p3, a, dir, edgeNormals[2]))
        return false;
    return !segEdgeCheckUndirected(p3, p0, a, dir, edgeNormals[3]);
}

// --- phBound -----------------------------------------------------------------------------

const Material& defaultBoundMaterial() {
    // lvlMaterial's constructor: elasticity 0.5, friction 1.
    static const Material m = [] {
        Material d;
        d.name = "default";
        d.elasticity = 0.5f;
        d.friction = 1.0f;
        d.width = 1.0f;
        return d;
    }();
    return m;
}

Bound::Bound(BoundType t) : type(t) {}

void Bound::makeOwnMaterial() {
    ownMaterial = std::make_shared<Material>(defaultBoundMaterial());
}

const Material& Bound::material(int) const {
    return ownMaterial ? *ownMaterial : defaultBoundMaterial();
}

void Bound::setFriction(float friction) {
    if (ownMaterial)
        ownMaterial->friction = friction;
}

void Bound::setElasticity(float elasticity) {
    if (ownMaterial)
        ownMaterial->elasticity = elasticity;
}

const Vec3& Bound::vertex(int) const {
    static const Vec3 dummy;
    return dummy;
}

int Bound::testEdge(Segment&, Intersection*, int) const {
    return 0;
}

bool Bound::testProbe(Segment&, Intersection&, float) const {
    return false;
}

int Bound::testSegment(Segment& seg, Intersection* out, int max) const {
    // phBound::TestSegment.
    if (seg.kind == Segment::Probe)
        return max < 1 ? 0 : (testProbe(seg, out[0], 2.0f) ? 1 : 0);
    if (seg.kind != Segment::Edge || max < 2)
        return 0;
    return testEdge(seg, out, max);
}

void Bound::calculateSphereFromBoundingBox() {
    // phBound::CalculateSphereFromBoundingBox.
    centroid = {(boxMax.x - boxMin.x) * 0.5f + boxMin.x, (boxMax.y - boxMin.y) * 0.5f + boxMin.y,
                (boxMax.z - boxMin.z) * 0.5f + boxMin.z};
    if (centroid.x != 0.0f || centroid.y != 0.0f || centroid.z != 0.0f)
        isOffset = true;
    radius = sqrtf32((centroid.x - boxMax.x) * (centroid.x - boxMax.x) + (centroid.y - boxMax.y) * (centroid.y - boxMax.y) +
                     (centroid.z - boxMax.z) * (centroid.z - boxMax.z));
}

void Bound::setOffset(const Vec3& offset) {
    // phBound::SetOffset.
    centroid = offset;
    if (centroid.x != 0.0f || centroid.y != 0.0f || centroid.z != 0.0f)
        isOffset = true;
}

Vec3 Bound::center(const Mat34& m) const {
    // phBound::GetCenter.
    if (!isOffset)
        return m.m3;
    return {centroid.x * m.m0.x + m.m1.x * centroid.y + m.m2.x * centroid.z + m.m3.x,
            m.m1.y * centroid.y + m.m2.y * centroid.z + m.m0.y * centroid.x + m.m3.y,
            m.m1.z * centroid.y + m.m2.z * centroid.z + m.m0.z * centroid.x + m.m3.z};
}

void Bound::setPenetration() {
    // phBound::SetPenetration: a fraction of the smallest extent (the
    // fraction is 0 in a race, see kPenetration).
    float minExtent = boxMax.z - boxMin.z;
    const float ey = boxMax.y - boxMin.y;
    const float ex = boxMax.x - boxMin.x;
    if (ey <= ex) {
        if (ey <= minExtent)
            minExtent = ey;
    } else if (ex <= minExtent) {
        minExtent = ex;
    }
    if (0.001f <= minExtent) {
        penetration = kPenetration * minExtent;
        barelyMoved = kBarelyMovedDistance * minExtent;
    }
}

// --- phBoundPolygonal --------------------------------------------------------------------

float BoundPolygonal::maxDot(const Vec3& dir, const Mat34& m, Vec3& local) const {
    // phBoundPolygonal::MaxDot.
    local = {dir.x * m.m0.x + m.m0.z * dir.z + m.m0.y * dir.y, m.m1.x * dir.x + m.m1.z * dir.z + m.m1.y * dir.y,
             dir.x * m.m2.x + m.m2.z * dir.z + m.m2.y * dir.y};
    float best = -FLT_MAX;
    for (int i = numVertices() - 1; i >= 0; --i) {
        const Vec3& p = vertex(i);
        const float d = local.x * p.x + p.y * local.y + p.z * local.z;
        if (best < d)
            best = d;
    }
    return dir.x * m.m3.x + m.m3.z * dir.z + m.m3.y * dir.y + best;
}

float BoundPolygonal::minDot(const Vec3& dir, const Mat34& m, Vec3& local) const {
    // phBoundPolygonal::MinDot.
    return -maxDot(-dir, m, local);
}

bool BoundPolygonal::testProbe(Segment& seg, Intersection& out, float maxT) const {
    // phBoundPolygonal::TestProbe: the nearest polygon the segment enters
    // through its front face, searching from the last polygon.
    out.t = maxT;
    bool found = false;
    for (int i = numPolygons() - 1; i >= 0; --i) {
        const Polygon& poly = polygons[static_cast<std::size_t>(i)];
        if (poly.testSegmentDirected(vertices, seg, out, out.t)) {
            out.polygon = i;
            out.poly = &poly;
            found = true;
        }
    }
    return found;
}

int BoundPolygonal::testEdge(Segment& seg, Intersection* out, int) const {
    // phBoundPolygonal::TestEdge: out[0] gets the first crossing where the
    // edge goes in (its end b behind the polygon), out[1] the last where it
    // comes out. Returns the number of crossings found, at most 2.
    float enterT = 2.0f;
    float exitT = -1.0f;
    int found = 0;
    IntersectionPoint hit;
    for (int i = numPolygons() - 1; i >= 0; --i) {
        const Polygon& poly = polygons[static_cast<std::size_t>(i)];
        if (!poly.testSegmentUndirected(vertices, seg, hit, enterT, exitT))
            continue;
        ++found;
        if (hit.t <= enterT && (hit.bInside || hit.t < enterT)) {
            static_cast<IntersectionPoint&>(out[0]) = hit;
            out[0].polygon = i;
            out[0].poly = &poly;
            enterT = hit.t;
        }
        if (exitT <= hit.t && (!hit.bInside || exitT < hit.t)) {
            out[1].polygon = i;
            static_cast<IntersectionPoint&>(out[1]) = hit;
            exitT = hit.t;
            out[1].poly = &poly;
        }
    }
    return found > 2 ? 2 : found;
}

// --- phBoundGeometry ---------------------------------------------------------------------

const Material& BoundGeometry::material(int index) const {
    // phBoundGeometry::GetMaterial (dgBoundGeometry and vehBound return their
    // own material).
    if (ownMaterial)
        return *ownMaterial;
    if (index >= 0 && static_cast<std::size_t>(index) < materials.size() && materials[static_cast<std::size_t>(index)])
        return *materials[static_cast<std::size_t>(index)];
    return defaultBoundMaterial();
}

void BoundGeometry::postLoadCompute() {
    computeEdges();
    computeEdgeNums();
    computeEdgeNormals();
    setQuickTestInfo();
}

namespace {

// phBoundGeometry::EdgeInList: the index of edge (a, b) in either
// direction, or -1.
int edgeInList(int a, int b, std::span<const std::array<std::uint16_t, 2>> list) {
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (a == list[i][0] && b == list[i][1])
            return static_cast<int>(i);
        if (a == list[i][1] && b == list[i][0])
            return static_cast<int>(i);
    }
    return -1;
}

} // namespace

void BoundGeometry::computeEdges() {
    // phBoundGeometry::ComputeEdges: every side of every polygon, (v[n-1],
    // v[0]) first, appended to the edges already declared by the file.
    std::vector<std::array<std::uint16_t, 2>> added;
    for (const Polygon& poly : polygons) {
        const int n = poly.vertexCount();
        std::uint16_t prev = poly.v[static_cast<std::size_t>(n - 1)];
        for (int k = 0; k < n; ++k) {
            const std::uint16_t cur = poly.v[static_cast<std::size_t>(k)];
            if (edgeInList(prev, cur, added) < 0 && edgeInList(prev, cur, edges) < 0)
                added.push_back({prev, cur});
            prev = cur;
        }
    }
    edges.insert(edges.end(), added.begin(), added.end());
}

void BoundGeometry::computeEdgeNums() {
    // phBoundGeometry::ComputeEdgeNums: polygon side i (v[i], v[i+1]) gets
    // its edge index (0 with a warning in the original when missing).
    for (Polygon& poly : polygons) {
        const int n = poly.vertexCount();
        int prevSlot = n - 1;
        std::uint16_t prev = poly.v[static_cast<std::size_t>(n - 1)];
        for (int k = 0; k < n; ++k) {
            const std::uint16_t cur = poly.v[static_cast<std::size_t>(k)];
            int e = edgeInList(prev, cur, edges);
            if (e < 0)
                e = 0;
            poly.edges[static_cast<std::size_t>(prevSlot)] = static_cast<std::uint16_t>(e);
            prevSlot = k;
            prev = cur;
        }
    }
}

void BoundGeometry::computeEdgeNormals() {
    // phBoundGeometry::ComputeEdgeNormals / ReComputeEdgeNormals: the sum of
    // the normals of the polygons on either side of the edge (the first one
    // found each way; a missing side mirrors the other), and the cosine
    // between it and the face that runs the edge backwards, or 2 when the
    // edge is not convex.
    edgeNormals.assign(edges.size(), Vec3{});
    edgeCosines.assign(edges.size(), 0.0f);
    for (std::size_t e = 0; e < edges.size(); ++e) {
        const int a = edges[e][0];
        const int b = edges[e][1];
        bool reverseFound = false;
        bool forwardFound = false;
        Vec3 forwardNormal, reverseNormal;
        for (const Polygon& poly : polygons) {
            const int v0 = poly.v[0], v1 = poly.v[1], v2 = poly.v[2];
            const bool triangle = poly.v[3] == 0;
            const int v3 = triangle ? v0 : poly.v[3];
            bool forward = (v0 == a && v1 == b) || (v1 == a && v2 == b) || (v2 == a && v3 == b);
            bool reverse = false;
            if (!forward) {
                if (!triangle && v3 == a && v0 == b)
                    forward = true;
                else
                    reverse = (v0 == b && v1 == a) || (v1 == b && v2 == a) || (v2 == b && v3 == a) ||
                              (!triangle && v3 == b && v0 == a);
            }
            if (forward) {
                forwardFound = true;
                forwardNormal = poly.normal;
                if (reverseFound)
                    break;
            } else if (reverse) {
                reverseFound = true;
                reverseNormal = poly.normal;
                if (forwardFound)
                    break;
            }
        }
        if (!reverseFound && !forwardFound) {
            edgeNormals[e] = {1.0f, 0.0f, 0.0f};
            edgeCosines[e] = -1.0f;
            continue;
        }
        if (!forwardFound)
            forwardNormal = -reverseNormal;
        else if (!reverseFound)
            reverseNormal = -forwardNormal;
        Vec3 n = reverseNormal;
        n = {forwardNormal.x + n.x, forwardNormal.y + n.y, forwardNormal.z + n.z};
        float len2 = n.z * n.z + n.y * n.y + n.x * n.x;
        if (len2 < 1e-6f) {
            const Vec3& pa = vertex(a);
            const Vec3& pb = vertex(b);
            const float dx = pb.x - pa.x, dy = pb.y - pa.y, dz = pb.z - pa.z;
            n = {reverseNormal.y * dz - reverseNormal.z * dy, reverseNormal.z * dx - dz * reverseNormal.x,
                 dy * reverseNormal.x - reverseNormal.y * dx};
            len2 = n.z * n.z + n.y * n.y + n.x * n.x;
            if (len2 < 1e-6f) {
                len2 = 1.0f;
                n = {0.0f, 1.0f, 0.0f};
            }
        }
        const float scale = 1.0f / sqrtf32(len2);
        n = {scale * n.x, scale * n.y, scale * n.z};
        edgeNormals[e] = n;
        const Vec3& pa = vertex(a);
        const Vec3& pb = vertex(b);
        const float dx = pb.x - pa.x, dy = pb.y - pa.y, dz = pb.z - pa.z;
        const float convex = (dz * n.y - dy * n.z) * reverseNormal.x + reverseNormal.y * (dx * n.z - dz * n.x) +
                             reverseNormal.z * (dy * n.x - dx * n.y);
        edgeCosines[e] = convex <= 0.0f
                             ? reverseNormal.x * n.x + reverseNormal.y * n.y + reverseNormal.z * n.z
                             : 2.0f;
    }
}

void BoundGeometry::setQuickTestInfo() {
    // phBoundGeometry::SetQuickTestInfo.
    boxMax = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    boxMin = {FLT_MAX, FLT_MAX, FLT_MAX};
    for (const Vec3& p : vertices) {
        if (boxMax.x < p.x)
            boxMax.x = p.x;
        if (p.x < boxMin.x)
            boxMin.x = p.x;
        if (boxMax.y < p.y)
            boxMax.y = p.y;
        if (p.y < boxMin.y)
            boxMin.y = p.y;
        if (boxMax.z < p.z)
            boxMax.z = p.z;
        if (p.z < boxMin.z)
            boxMin.z = p.z;
    }
    setPenetration();
    calculateSphereFromBoundingBox();
}

void BoundGeometry::shiftCentroid(const Vec3& shift) {
    // phBoundGeometry::ShiftCentroid.
    for (Vec3& p : vertices)
        p = {p.x + shift.x, shift.y + p.y, shift.z + p.z};
    setQuickTestInfo();
}

namespace {

// phBoundGeometry::Load's polygon setup, shared by geometry and terrain
// bounds. Text "quad" tokens with a last index of 0 are rotated so that the
// zero does not end the list (which would make it a triangle).
void loadPolygons(BoundGeometry& bound, const GeometryData& data) {
    bound.vertices = data.vertices;
    bound.materials = data.materials;
    bound.polygons.resize(data.polys.size());
    for (std::size_t i = 0; i < data.polys.size(); ++i) {
        const auto& p = data.polys[i];
        Polygon& poly = bound.polygons[i];
        if (p.quad && p.v[3] == 0)
            poly.initQuad(p.v[1], p.v[2], p.v[3], p.v[0], bound.vertices);
        else if (p.quad || p.v[3] != 0)
            poly.initQuad(p.v[0], p.v[1], p.v[2], p.v[3], bound.vertices);
        else
            poly.initTriangle(p.v[0], p.v[1], p.v[2], bound.vertices);
        poly.material = static_cast<std::uint8_t>(p.material);
    }
}

bool validGeometry(const GeometryData& data) {
    if (data.vertices.empty() || data.polys.empty())
        return false;
    for (const auto& p : data.polys)
        for (const auto i : p.v)
            if (i >= data.vertices.size())
                return false;
    return true;
}

} // namespace

std::unique_ptr<BoundGeometry> makeGeometryBound(const GeometryData& data) {
    // phBoundGeometry::Load + PostLoadCompute.
    if (!validGeometry(data))
        return nullptr;
    auto bound = std::make_unique<BoundGeometry>();
    loadPolygons(*bound, data);
    bound->postLoadCompute();
    return bound;
}

std::unique_ptr<BoundTerrain> makeTerrainBound(const GeometryData& geometry, const TerrainData* terrain, bool local) {
    // phBoundTerrain::Load: the geometry (phBoundTerrain's PostLoadCompute is
    // empty, so the polygons get no edges of their own), then edges, edge
    // numbers, normals, cosines and the grid from the .ter file.
    if (!validGeometry(geometry))
        return nullptr;
    std::unique_ptr<BoundTerrain> bound;
    if (local)
        bound = std::make_unique<BoundTerrainLocal>();
    else
        bound = std::make_unique<BoundTerrain>();
    loadPolygons(*bound, geometry);
    if (!terrain || terrain->polygonEdges.size() != bound->polygons.size() ||
        terrain->edgeNormals.size() != terrain->edges.size() || terrain->edgeCosines.size() != terrain->edges.size()) {
        // The original computes a plain geometry bound, grows its box by
        // 0.0001 and reports failure, which leaves the instance without a
        // bound (lvlInstance::InitBoundTerrain).
        return nullptr;
    }
    bound->edges = terrain->edges;
    for (std::size_t i = 0; i < bound->polygons.size(); ++i)
        for (std::size_t k = 0; k < 4; ++k)
            bound->polygons[i].edges[k] = static_cast<std::uint16_t>(terrain->polygonEdges[i][k]);
    bound->edgeNormals = terrain->edgeNormals;
    bound->edgeCosines = terrain->edgeCosines;
    bound->grid = terrain->grid;
    bound->boxMin = terrain->boxMin;
    bound->boxMax = terrain->boxMax;
    bound->setQuickTestInfo();
    bound->polyTouched.assign(bound->polygons.size(), 0);
    return bound;
}

// --- phBoundBox --------------------------------------------------------------------------

const std::array<Vec3, 8>& BoundBox::unitCorners() {
    static const std::array<Vec3, 8> corners{{
        {0.5f, 0.5f, 0.5f},
        {-0.5f, 0.5f, 0.5f},
        {-0.5f, -0.5f, 0.5f},
        {0.5f, -0.5f, 0.5f},
        {0.5f, 0.5f, -0.5f},
        {-0.5f, 0.5f, -0.5f},
        {-0.5f, -0.5f, -0.5f},
        {0.5f, -0.5f, -0.5f},
    }};
    return corners;
}

const std::array<Vec3, 6>& BoundBox::faceNormals() {
    static const std::array<Vec3, 6> normals{{
        {1.0f, 0.0f, 0.0f},
        {-1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, -1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, -1.0f},
    }};
    return normals;
}

const std::array<Vec3, 12>& BoundBox::edgeNormalTable() {
    // sqrt(1/2) as a 32-bit float.
    constexpr float h = 0.70710677f;
    static const std::array<Vec3, 12> normals{{
        {0.0f, h, h},
        {-h, 0.0f, h},
        {0.0f, -h, h},
        {h, 0.0f, h},
        {0.0f, h, -h},
        {-h, 0.0f, -h},
        {0.0f, -h, -h},
        {h, 0.0f, -h},
        {h, h, 0.0f},
        {-h, h, 0.0f},
        {-h, -h, 0.0f},
        {h, -h, 0.0f},
    }};
    return normals;
}

BoundBox::BoundBox() : BoundPolygonal(BoundType::Box) {
    // phBoundBox::phBoundBox: a unit box at the origin.
    size = {1.0f, 1.0f, 1.0f};
    vertices.assign(unitCorners().begin(), unitCorners().end());
    edges.assign(kBoxEdges.begin(), kBoxEdges.end());
    polygons.resize(6);
    for (std::size_t f = 0; f < 6; ++f) {
        const auto& q = kBoxFaces[f];
        polygons[f].initQuad(q[0], q[1], q[2], q[3], vertices);
        polygons[f].edges = kBoxFaceEdges[f];
        polygons[f].material = 0;
    }
    centroid = {};
    setQuickTestInfo();
}

BoundBox::BoundBox(const Vec3& s) : BoundBox() {
    setSize(s);
}

Vec3 BoundBox::edgeNormal(int edge) const {
    return edgeNormalTable()[static_cast<std::size_t>(edge)];
}

void BoundBox::setSize(const Vec3& s) {
    // phBoundBox::SetSize: corners scaled by the size around the offset.
    size = s;
    for (std::size_t i = 0; i < 8; ++i) {
        const Vec3& u = unitCorners()[i];
        vertices[i] = {u.x * s.x + centroid.x, centroid.y + u.y * s.y, u.z * s.z + centroid.z};
    }
    for (Polygon& poly : polygons)
        poly.calculateNormal(vertices);
    setQuickTestInfo();
}

void BoundBox::shiftCentroid(const Vec3& shift) {
    // phBoundBox::ShiftCentroid.
    for (Vec3& p : vertices)
        p = {p.x + shift.x, shift.y + p.y, p.z + shift.z};
    setQuickTestInfo();
}

void BoundBox::setQuickTestInfo() {
    // phBoundBox::SetQuickTestInfo.
    boxMax = {size.x * 0.5f, size.y * 0.5f, size.z * 0.5f};
    boxMin = -boxMax;
    radius = sqrtf32(boxMax.z * boxMax.z + boxMax.y * boxMax.y + boxMax.x * boxMax.x);
    boxMax = {centroid.x + boxMax.x, centroid.y + boxMax.y, centroid.z + boxMax.z};
    boxMin = {centroid.x + boxMin.x, centroid.y + boxMin.y, centroid.z + boxMin.z};
    setPenetration();
}

// --- phBoundSphere / phBoundHotdog --------------------------------------------------------

BoundSphere::BoundSphere(float r) : Bound(BoundType::Sphere) {
    sphereRadius = r;
    radius = r;
    boxMin = {-r, -r, -r};
    boxMax = {r, r, r};
    setPenetration();
}

void BoundSphere::setRadius(float r) {
    // phBoundSphere::SetRadius.
    sphereRadius = r;
    boxMax = {r, r, r};
    boxMin = {-r, -r, -r};
    radius = r;
    centroid = {};
    setPenetration();
}

BoundHotdog::BoundHotdog(float r, float h) : Bound(BoundType::Hotdog) {
    capRadius = r;
    height = h;
    calculateBoundingBox();
}

void BoundHotdog::setSize(float r, float h) {
    // phBoundHotdog::SetSize.
    capRadius = r;
    height = h;
    const float halfLength = h * 0.5f + r;
    radius = halfLength;
    boxMin = {-r, -halfLength, -r};
    boxMax = {capRadius, radius, capRadius};
    centroid = {};
    setPenetration();
}

void BoundHotdog::calculateBoundingBox() {
    // phBoundHotdog::CalculateBoundingBox.
    boxMax = {capRadius, height * 0.5f + capRadius, capRadius};
    boxMin = -boxMax;
    boxMax = {centroid.x + boxMax.x, centroid.y + boxMax.y, centroid.z + boxMax.z};
    boxMin = {centroid.x + boxMin.x, centroid.y + boxMin.y, centroid.z + boxMin.z};
    calculateSphereFromBoundingBox();
    setPenetration();
}

} // namespace mm2::phys
