// MM2's terrain bounds (phBoundTerrain / phBoundTerrainLocal) at query time:
// the section grid walk (InitPolyIterator, CalculateBuckets,
// ClearPolyTouched), the segment tests (TestEdge, TestProbe), the polygonal
// bound tests (TestBoundTerrainPoly, TestBoundTerrainEdgesVsPoly,
// TestBoundPolyTerrain) and the sphere and hotdog impact searches
// (FindImpactsSphereToTerrain, FindImpactsHotdogToTerrain,
// FindImpactsHotdogToTerrainLocal), ported from the code of midtown2.exe
// build 3393 (MM2Recomp). Operation order and float32 arithmetic follow the
// originals, which run the x87 in single precision; comparisons keep the
// x87's handling of NaN where it differs from C++'s. Loading lives in
// Bound.cpp. See docs/physics.md, "Collision", and docs/formats/bnd.md.

#include "phys/Bound.h"
#include "phys/Collision.h"
#include "phys/Geometry.h"
#include "phys/Impact.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace mm2::phys {
namespace {

// The sections a query visits, in visiting order (phBoundTerrain's bucket
// buffer and count). Only sections that list polygons are kept.
using SectionList = std::vector<std::uint16_t>;

// __ftol: truncation toward zero through a 64-bit fistp, returning the low
// half. NaN and values beyond the 64-bit range give the integer indefinite,
// whose low half is 0.
int ftol(float v) {
    if (!(v >= -0x1p63f && v < 0x1p63f))
        return 0;
    const auto wide = static_cast<std::uint64_t>(static_cast<std::int64_t>(v));
    return static_cast<int>(static_cast<std::uint32_t>(wide));
}

// fcomp followed by `test ah, 0x40`: equal, or unordered.
bool equalOrUnordered(float a, float b) {
    return !(a < b) && !(a > b);
}

int clampCell(int cell, int count) {
    if (cell < 0)
        return 0;
    return cell > count - 1 ? count - 1 : cell;
}

// Vector3::Dot.
float dot3(const Vec3& a, const Vec3& b) {
    return a.z * b.z + a.y * b.y + a.x * b.x;
}

// phBoundTerrain::ClearPolyTouched: one byte per polygon instead of the
// original's bits. (MM2 skips the clear when no polygon can be marked; the
// result is the same.)
void clearPolyTouched(const BoundTerrain& t) {
    t.polyTouched.assign(static_cast<std::size_t>(t.numPolygons()), 0);
}

// The test-and-set of a polygon's touched mark: true the first time the
// current query reaches the polygon.
bool touchPolygon(const BoundTerrain& t, std::uint16_t polygon) {
    if (polygon >= t.polyTouched.size())
        return false; // OpenMM2: a malformed section list
    if (t.polyTouched[polygon] != 0)
        return false;
    t.polyTouched[polygon] = 1;
    return true;
}

// Appends a section that lists polygons.
void addSection(const TerrainGrid& g, int section, SectionList& out) {
    if (section < 0 || section >= static_cast<int>(g.sectionCounts.size()))
        return; // OpenMM2: the original indexes without checking
    if (g.sectionCounts[static_cast<std::size_t>(section)] != 0)
        out.push_back(static_cast<std::uint16_t>(section));
}

// The polygons a section lists.
std::span<const std::uint16_t> sectionPolygons(const TerrainGrid& g, std::uint16_t section) {
    const std::size_t offset = g.sectionOffsets[section];
    if (offset > g.sectionPolygons.size())
        return {};
    const std::size_t count =
        std::min<std::size_t>(g.sectionCounts[section], g.sectionPolygons.size() - offset);
    return {g.sectionPolygons.data() + offset, count};
}

// Calls f(polygon index) for each polygon of the listed sections that the
// current query has not touched yet, in the original's order; f returns
// false to stop the walk.
template <class F>
void forEachNewPolygon(const BoundTerrain& t, const SectionList& sections, F&& f) {
    for (const std::uint16_t section : sections) {
        for (const std::uint16_t polygon : sectionPolygons(t.grid, section)) {
            if (!touchPolygon(t, polygon))
                continue;
            if (!f(static_cast<int>(polygon)))
                return;
        }
    }
}

// phBoundTerrain::InitPolyIterator(const Vector3&, float): the sections a
// sphere overlaps. The grid is 2D over x and z (section = z * width + x);
// the sphere's y range only has to meet the box. The row of the centre takes
// the sphere's whole x range, the other rows the chord at their nearest
// edge, walking down and then up from the centre's row until the chord
// misses the grid.
SectionList sectionsNearSphere(const BoundTerrain& t, const Vec3& center, float radius) {
    clearPolyTouched(t);
    SectionList out;
    const TerrainGrid& g = t.grid;
    const int width = g.widthSections;
    const int depth = g.depthSections;
    const float factorX = g.sectionSizeFactors.x;
    const float factorZ = g.sectionSizeFactors.z;

    const float highZ = radius + center.z;
    const float highX = radius + center.x;
    const float lowCellX = ((center.x - radius) - t.boxMin.x) * factorX;
    const float highCellX = (highX - t.boxMin.x) * factorX;
    const float lowCellZ = ((center.z - radius) - t.boxMin.z) * factorZ;
    const float highCellZ = (highZ - t.boxMin.z) * factorZ;
    if (!(static_cast<float>(width) > lowCellX) || !(highCellX >= 0.0f))
        return out;
    if (!(static_cast<float>(depth) > lowCellZ) || !(highCellZ >= 0.0f))
        return out;
    if (center.y - radius > t.boxMax.y || !(radius + center.y >= t.boxMin.y))
        return out;

    const int xLo = clampCell(ftol(lowCellX), width);
    const int xHi = clampCell(ftol(highCellX), width);
    const int zLo = clampCell(ftol(lowCellZ), depth);
    const int zHi = clampCell(ftol(highCellZ), depth);

    // The centre's row.
    const int centerRow = ftol((center.z - t.boxMin.z) * factorZ);
    if (centerRow >= 0 && centerRow < depth && xLo <= xHi) {
        int section = width * centerRow + xLo;
        for (int n = xHi - xLo + 1; n != 0; --n)
            addSection(g, section++, out);
    }

    // The distance from the centre to the low z edge of its row; each row
    // further away adds (or removes) one row height.
    const float centerX = center.x - t.boxMin.x;
    const float fromRowEdge = (center.z - t.boxMin.z) - static_cast<float>(centerRow) / factorZ;
    // The chord of the sphere at distance d from its centre, as a range of
    // sections in `row`; false when the chord ends below the grid (or does
    // not exist: sqrt of a negative number is NaN), which ends the walk.
    auto addChord = [&](int row, float d) {
        const float half = std::sqrt(radius * radius - d * d);
        const float hiCell = (centerX + half) * factorX;
        if (!(hiCell >= 0.0f))
            return false;
        int hi = ftol(hiCell);
        if (hi >= width - 1)
            hi = width - 1;
        int lo = ftol((centerX - half) * factorX);
        if (lo <= 0)
            lo = 0;
        int section = width * row + lo;
        for (int n = hi - lo; n >= 0; --n)
            addSection(g, section++, out);
        return true;
    };

    if (centerRow > 0) {
        float d = fromRowEdge;
        for (int row = centerRow - 1; row >= zLo; --row) {
            if (row > depth - 1) {
                d = 1.0f / factorZ + d;
                continue;
            }
            if (!addChord(row, d))
                break;
            d = 1.0f / factorZ + d;
        }
    }
    if (centerRow < depth - 1) {
        float d = fromRowEdge;
        for (int row = centerRow + 1; row <= zHi; ++row) {
            d = d - 1.0f / factorZ;
            if (row < 0)
                continue;
            if (!addChord(row, d))
                break;
        }
    }
    return out;
}

// phBoundTerrain::CalculateBuckets: the sections the segment (x0, z0) ->
// (x1, z1) crosses. The segment is clipped to the box, pulled in by 0.0001
// of its length at clipped ends, and walked cell by cell (a single column or
// row, or a 2D DDA stepping across whichever cell boundary comes first).
void calculateBuckets(const BoundTerrain& t, float x0, float z0, float x1, float z1, SectionList& out) {
    const TerrainGrid& g = t.grid;
    const float dx = x1 - x0;
    const float dz = z1 - z0;
    float tIn = 0.0f;
    float tOut = 1.0f;

    if (equalOrUnordered(dx, 0.0f)) {
        if (x0 > t.boxMax.x || !(x0 >= t.boxMin.x))
            return;
    } else {
        const float inv = 1.0f / dx;
        const float tMax = (t.boxMax.x - x0) * inv;
        if (!(x0 >= x1)) {
            if (!(tMax >= 0.0f))
                return;
            if (!(tMax > 1.0f))
                tOut = tMax;
            const float tMin = (t.boxMin.x - x0) * inv;
            if (tMin > 1.0f)
                return;
            if (tMin >= 0.0f)
                tIn = tMin;
        } else {
            if (tMax > 1.0f)
                return;
            if (tMax >= 0.0f)
                tIn = tMax;
            const float tMin = (t.boxMin.x - x0) * inv;
            if (!(tMin >= 0.0f))
                return;
            if (!(tMin > 1.0f))
                tOut = tMin;
        }
        if (tIn > tOut)
            return;
    }

    if (equalOrUnordered(dz, 0.0f)) {
        if (z0 > t.boxMax.z || !(z0 >= t.boxMin.z))
            return;
    } else {
        const float inv = 1.0f / dz;
        const float tMax = (t.boxMax.z - z0) * inv;
        if (!(z0 >= z1)) {
            if (!(tMax >= 0.0f))
                return;
            if (!(tMax > 1.0f) && !(tMax >= tOut))
                tOut = tMax;
            const float tMin = (t.boxMin.z - z0) * inv;
            if (tMin > 1.0f)
                return;
            if (tMin >= 0.0f && tMin > tIn)
                tIn = tMin;
        } else {
            if (tMax > 1.0f)
                return;
            if (tMax >= 0.0f && tMax > tIn)
                tIn = tMax;
            const float tMin = (t.boxMin.z - z0) * inv;
            if (!(tMin >= 0.0f))
                return;
            if (!(tMin > 1.0f) && !(tMin >= tOut))
                tOut = tMin;
        }
    }
    if (tIn > tOut)
        return;

    if (tIn > 0.0f)
        tIn = tIn + 0.0001f;
    if (tOut < 1.0f)
        tOut = tOut - 0.0001f;
    // The clipped ends in cell units.
    const float xIn = ((dx * tIn + x0) - t.boxMin.x) * g.sectionSizeFactors.x;
    const float xOut = ((dx * tOut + x0) - t.boxMin.x) * g.sectionSizeFactors.x;
    const float zIn = ((dz * tIn + z0) - t.boxMin.z) * g.sectionSizeFactors.z;
    const float zOut = ((dz * tOut + z0) - t.boxMin.z) * g.sectionSizeFactors.z;
    const bool zUp = z1 > z0;
    const bool xUp = x1 > x0;

    const int width = g.widthSections;
    const int count = static_cast<int>(g.sectionCounts.size());
    const int firstColumn = ftol(xIn);
    int x = clampCell(firstColumn, width);
    const int firstRow = ftol(zIn);
    int z = clampCell(firstRow, g.depthSections);
    const float spanX = xOut - xIn;
    const float spanZ = zOut - zIn;
    int section = width * z + x;

    if (ftol(xOut) == firstColumn) {
        // A single column.
        if (section >= count)
            return;
        for (;;) {
            if (section < 0)
                return;
            addSection(g, section, out);
            if (zUp) {
                ++z;
                section += width;
                if (static_cast<float>(z) >= zOut)
                    return;
            } else {
                --z;
                section -= width;
                if (zOut - 1.0f >= static_cast<float>(z))
                    return;
            }
            if (section >= count)
                return;
        }
    }
    if (ftol(zOut) == firstRow) {
        // A single row.
        if (section >= count)
            return;
        for (;;) {
            if (section < 0)
                return;
            addSection(g, section, out);
            if (xUp) {
                ++x;
                ++section;
                if (static_cast<float>(x) >= xOut)
                    return;
            } else {
                --x;
                --section;
                if (xOut - 1.0f >= static_cast<float>(x))
                    return;
            }
            if (section >= count)
                return;
        }
    }

    // The 2D walk: tx and tz are the fractions of the clipped segment at
    // which it crosses the next column and row boundary.
    if (section >= count)
        return;
    int nextX = x + 1;
    int nextZ = z + 1;
    for (;;) {
        if (section < 0)
            return;
        addSection(g, section, out);
        const float tx = (static_cast<float>(xUp ? nextX : x) - xIn) / spanX;
        const float tz = (static_cast<float>(zUp ? nextZ : z) - zIn) / spanZ;
        if (tx > 1.0f && tz > 1.0f)
            return;
        if (!(tx >= tz)) {
            if (xUp) {
                ++x;
                ++nextX;
            } else {
                --x;
            }
        } else if (zUp) {
            ++z;
            ++nextZ;
        } else {
            --z;
        }
        section = width * z + x;
        if (section >= count)
            return;
    }
}

// phBoundTerrain::InitPolyIterator(const phSegment&): the sections a segment
// crosses. A vertical segment (equal x and z) takes its own cell without a
// y test; otherwise both ends must not lie beyond the same face of the box,
// a segment within one cell takes that cell and longer ones go through
// CalculateBuckets.
SectionList sectionsAlongSegment(const BoundTerrain& t, const Segment& seg) {
    clearPolyTouched(t);
    SectionList out;
    const TerrainGrid& g = t.grid;
    const Vec3& a = seg.a;
    const Vec3& b = seg.b;
    auto cellOf = [&](const Vec3& p) {
        const int z = clampCell(ftol((p.z - t.boxMin.z) * g.sectionSizeFactors.z), g.depthSections);
        const int x = clampCell(ftol((p.x - t.boxMin.x) * g.sectionSizeFactors.x), g.widthSections);
        return g.widthSections * z + x;
    };

    if (equalOrUnordered(a.x, b.x) && equalOrUnordered(a.z, b.z)) {
        if (!(a.x >= t.boxMin.x) || a.x > t.boxMax.x || !(a.z >= t.boxMin.z) || a.z > t.boxMax.z)
            return out;
        addSection(g, cellOf(a), out);
        return out;
    }

    if (!(a.x >= t.boxMin.x) && !(b.x >= t.boxMin.x))
        return out;
    if (a.x > t.boxMax.x && b.x > t.boxMax.x)
        return out;
    if (!(a.y >= t.boxMin.y) && !(b.y >= t.boxMin.y))
        return out;
    if (a.y > t.boxMax.y && b.y > t.boxMax.y)
        return out;
    if (!(a.z >= t.boxMin.z) && !(b.z >= t.boxMin.z))
        return out;
    if (a.z > t.boxMax.z && b.z > t.boxMax.z)
        return out;

    const int cellA = cellOf(a);
    if (cellA == cellOf(b)) {
        addSection(g, cellA, out);
        return out;
    }
    calculateBuckets(t, a.x, a.z, b.x, b.z, out);
    return out;
}

// phBoundTerrain::TestBoundTerrainEdgesVsPoly: with hot edges on, the
// terrain's edges near the polygonal bound (sphere of its bounding radius
// around centroid + position: MM2 does not rotate the centroid) against the
// bound's polygons through its TestEdge, in the bound's space. Each edge is
// tested once per call. Without hot edges nothing is tested.
int testBoundTerrainEdgesVsPoly(const BoundTerrain& terrain, const BoundPolygonal& poly,
                                Collider* terrainCollider, const Mat34& polyM, Intersection* out, int max,
                                int& count) {
    const float radius = poly.radius;
    const float radius2 = radius * radius;
    if (!terrain.grid.useHotEdges) {
        count = 0;
        return 0;
    }
    // The hot edge marks, one byte per edge here.
    std::vector<std::uint8_t> edgeTested(static_cast<std::size_t>(terrain.numEdges()), 0);
    const Vec3 center{poly.centroid.x + polyM.m3.x, poly.centroid.y + polyM.m3.y,
                      poly.centroid.z + polyM.m3.z};
    const SectionList sections = sectionsNearSphere(terrain, center, radius);

    int remaining = max;
    forEachNewPolygon(terrain, sections, [&](int p) {
        const Polygon& polygon = terrain.polygons[static_cast<std::size_t>(p)];
        for (int k = polygon.vertexCount() - 1; k >= 0; --k) {
            const std::uint16_t e = polygon.edges[static_cast<std::size_t>(k)];
            if (e >= edgeTested.size() || edgeTested[e] != 0)
                continue;
            edgeTested[e] = 1;
            const std::array<std::uint16_t, 2>& edge = terrain.edges[e];
            const Vec3& va = terrain.vertex(edge[0]);
            const Vec3& vb = terrain.vertex(edge[1]);
            if (!geom::segmentSphereTest(center, radius2, va, vb))
                continue;
            // OpenMM2: TestEdge may write two entries whatever the room left;
            // stop when there is no room for them (MM2 overflows the list).
            if (remaining < 2)
                return false;
            const Vec3 da{va.x - polyM.m3.x, va.y - polyM.m3.y, va.z - polyM.m3.z};
            const Vec3 db{vb.x - polyM.m3.x, vb.y - polyM.m3.y, vb.z - polyM.m3.z};
            Segment seg;
            seg.kind = Segment::Edge; // MM2 leaves the kind unset; TestEdge does not read it
            seg.a = {dot3(polyM.m0, da), dot3(polyM.m1, da), dot3(polyM.m2, da)};
            seg.b = {dot3(polyM.m0, db), dot3(polyM.m1, db), dot3(polyM.m2, db)};
            const int hits = poly.testEdge(seg, out, remaining);
            remaining -= hits;
            for (int i = 0; i < hits; ++i, ++out) {
                out->a = seg.a;
                out->b = seg.b;
                out->collider = terrainCollider;
                out->otherBound = &poly;
                out->poly = &poly.polygons[static_cast<std::size_t>(out->polygon)];
                out->flags &= static_cast<std::uint16_t>(~(Intersection::kVertex | Intersection::kInterior));
                out->material = out->poly->material;
                out->element = e;
                out->bound = &terrain;
                out->vertexA = edge[0];
                out->vertexB = edge[1];
                out->edgeNormal = terrain.edgeNormal(e);
            }
        }
        return true;
    });
    count = max - remaining;
    return count;
}

// phBoundTerrain::TestBoundPolyTerrain: the vertex sweeps (last pose to
// current, when `sweep`) and edges of a polygonal bound with fewer than 31
// vertices against the terrain polygons near it, in world space. Each sweep
// keeps the first polygon it enters; each edge keeps the polygon it crosses
// at the smallest t (preferring entries) and the one at the largest t
// (preferring exits). MM2 does not check `max`; OpenMM2 stops writing when
// the list is full.
int testBoundPolyTerrain(const BoundTerrain& terrain, const BoundPolygonal& poly, Collider* polyCollider,
                         const Mat34& m, const Mat34& last, Intersection* out, int max, int& count,
                         bool sweep) {
    constexpr std::uint16_t kNone = 0xffff;
    struct EdgeCrossings {
        std::uint16_t first = kNone, second = kNone;
        float tFirst = 2.0f, tSecond = -1.0f;
        float depthFirst = 0.0f, depthSecond = 0.0f;
    };
    struct VertexSweep {
        std::uint16_t polygon = kNone;
        float t = 2.0f;
        float depth = 0.0f;
        Vec3 last; // the vertex at the last pose, in the world
    };
    const int numVertices = poly.numVertices();
    const int numEdges = poly.numEdges();
    std::vector<EdgeCrossings> crossings(static_cast<std::size_t>(numEdges));
    std::vector<float> dots(static_cast<std::size_t>(numVertices));
    std::vector<Vec3> world(static_cast<std::size_t>(numVertices));
    std::vector<VertexSweep> sweeps(sweep ? static_cast<std::size_t>(numVertices) : 0);

    // Matrix34::Transform4.
    for (int i = 0; i < numVertices; ++i) {
        const Vec3& v = poly.vertex(i);
        world[static_cast<std::size_t>(i)] = {m.m2.x * v.z + m.m0.x * v.x + v.y * m.m1.x + m.m3.x,
                                              m.m0.y * v.x + v.z * m.m2.y + v.y * m.m1.y + m.m3.y,
                                              m.m0.z * v.x + v.z * m.m2.z + v.y * m.m1.z + m.m3.z};
    }
    for (int i = static_cast<int>(sweeps.size()) - 1; i >= 0; --i) {
        const Vec3& v = poly.vertex(i);
        sweeps[static_cast<std::size_t>(i)].last = {
            last.m1.x * v.y + last.m0.x * v.x + last.m2.x * v.z + last.m3.x,
            last.m1.y * v.y + last.m2.y * v.z + v.x * last.m0.y + last.m3.y,
            last.m1.z * v.y + last.m2.z * v.z + v.x * last.m0.z + last.m3.z};
    }

    const SectionList sections = sectionsNearSphere(terrain, m.m3, poly.radius);
    const float penetration = terrain.penetration < poly.penetration ? terrain.penetration : poly.penetration;
    std::span<const Vec3> verts = terrain.vertices;

    forEachNewPolygon(terrain, sections, [&](int p) {
        const Polygon& polygon = terrain.polygons[static_cast<std::size_t>(p)];
        const Vec3& v0 = terrain.vertex(polygon.v[0]);
        const Vec3& n = polygon.normal;
        auto planeDot = [&](const Vec3& w) {
            return (w.z - v0.z) * n.z + (w.y - v0.y) * n.y + (w.x - v0.x) * n.x;
        };
        for (int i = numVertices - 1; i >= 0; --i)
            dots[static_cast<std::size_t>(i)] = planeDot(world[static_cast<std::size_t>(i)]);

        // Vertices now behind the plane that were in front of it.
        for (int i = static_cast<int>(sweeps.size()) - 1; i >= 0; --i) {
            VertexSweep& s = sweeps[static_cast<std::size_t>(i)];
            const float dot = dots[static_cast<std::size_t>(i)];
            if (dot >= 0.0f)
                continue;
            float lastDot = planeDot(s.last);
            if (!(lastDot > 0.0f)) {
                if (!(lastDot > penetration * -1.5f))
                    continue;
                backupDispByPenetration(s.last, world[static_cast<std::size_t>(i)], penetration);
                lastDot = planeDot(s.last);
                if (!(lastDot > 0.0f))
                    continue;
            }
            const float t = lastDot / (lastDot - dot);
            if (t >= s.t)
                continue;
            if (polygon.detectSegmentDirected(verts, s.last, world[static_cast<std::size_t>(i)])) {
                s.t = t;
                s.polygon = static_cast<std::uint16_t>(p);
                s.depth = -dot;
            }
        }

        // Edges whose ends lie on both sides of the plane.
        for (int e = numEdges - 1; e >= 0; --e) {
            EdgeCrossings& c = crossings[static_cast<std::size_t>(e)];
            const std::array<std::uint16_t, 2>& edge = poly.edges[static_cast<std::size_t>(e)];
            const float da = dots[edge[0]];
            const float db = dots[edge[1]];
            if (std::signbit(da) == std::signbit(db))
                continue;
            const float t = da / (da - db);
            if (!(t > c.tSecond) && t >= c.tFirst)
                continue;
            if (!polygon.detectSegmentUndirected(verts, world[edge[0]], world[edge[1]]))
                continue;
            if (!(t > c.tFirst) && (da > 0.0f || !(t >= c.tFirst))) {
                c.first = static_cast<std::uint16_t>(p);
                c.tFirst = t;
                c.depthFirst = da > 0.0f ? -db : da;
            }
            if (t >= c.tSecond && (da < 0.0f || t > c.tSecond)) {
                c.second = static_cast<std::uint16_t>(p);
                c.tSecond = t;
                c.depthSecond = da > 0.0f ? -db : da;
            }
        }
        return true;
    });

    count = 0;
    for (int i = 0; i < static_cast<int>(sweeps.size()) && count < max; ++i) {
        const VertexSweep& s = sweeps[static_cast<std::size_t>(i)];
        if (s.polygon == kNone)
            continue;
        Intersection& o = out[count++];
        o.t = s.t;
        o.depth = s.depth;
        o.bInside = true;
        o.a = s.last;
        o.b = world[static_cast<std::size_t>(i)];
        o.position = {(o.b.x - o.a.x) * o.t + o.a.x, (o.b.y - o.a.y) * o.t + o.a.y,
                      (o.b.z - o.a.z) * o.t + o.a.z};
        o.collider = polyCollider;
        o.otherBound = &terrain;
        o.polygon = s.polygon;
        o.poly = &terrain.polygons[s.polygon];
        o.material = o.poly->material;
        o.normal = o.poly->normal;
        o.vertexA = -1;
        o.bound = &poly;
        o.element = i;
        o.vertexB = i;
        o.flags = static_cast<std::uint16_t>((o.flags & ~Intersection::kInterior) | Intersection::kVertex);
    }

    auto addEdge = [&](int e, std::uint16_t polygon, float t, float depth) {
        Intersection& o = out[count++];
        o.t = t;
        if (depth > 0.0f) {
            o.bInside = true;
            o.depth = depth;
        } else {
            o.depth = -depth;
            o.bInside = false;
        }
        const std::array<std::uint16_t, 2>& edge = poly.edges[static_cast<std::size_t>(e)];
        o.a = world[edge[0]];
        o.b = world[edge[1]];
        o.position = {(o.b.x - o.a.x) * t + o.a.x, (o.b.y - o.a.y) * t + o.a.y, (o.b.z - o.a.z) * t + o.a.z};
        o.collider = polyCollider;
        o.otherBound = &terrain;
        o.polygon = polygon;
        o.poly = &terrain.polygons[polygon];
        o.material = o.poly->material;
        o.normal = o.poly->normal;
        o.flags &= static_cast<std::uint16_t>(~(Intersection::kVertex | Intersection::kInterior));
        o.element = e;
        o.bound = &poly;
        o.vertexA = edge[0];
        o.vertexB = edge[1];
        const Vec3 local = poly.edgeNormal(e);
        o.edgeNormal = {m.m2.x * local.z + m.m1.x * local.y + local.x * m.m0.x,
                        m.m2.y * local.z + m.m1.y * local.y + local.x * m.m0.y,
                        m.m2.z * local.z + m.m1.z * local.y + local.x * m.m0.z};
    };
    for (int e = 0; e < numEdges && count < max; ++e) {
        const EdgeCrossings& c = crossings[static_cast<std::size_t>(e)];
        if (c.first != kNone)
            addEdge(e, c.first, c.tFirst, c.depthFirst);
        if (count < max && c.second != kNone && c.second != c.first)
            addEdge(e, c.second, c.tSecond, c.depthSecond);
    }
    return count;
}

// --- Hotdogs (FindImpactsHotdogToTerrain / FindImpactsHotdogToTerrainLocal) ---

// The hotdog in the terrain's frame, as each version sets it up.
struct HotdogInTerrain {
    Vec3 axis;                // the hotdog's local y
    std::array<Vec3, 2> caps; // -axis, +axis: the outward direction of each end cap
    Vec3 center;
    Vec3 relDisp;
    float halfHeight = 0.0f;
    // phBoundTerrainLocal's routine sums the end cap test in another order
    // and stores the polygon's material in componentB a second time.
    bool local = false;
};

// The body both versions share: each nearby polygon against the two end
// spheres (FindImpactPolygonToSphere), its edges against the shaft
// (FindImpactEdgeToShaft), and the shaft through its face (closest polygon
// edge to the shaft). Impacts are built in the terrain's frame and moved
// into the world by terrainM.
int hotdogToTerrain(const BoundTerrain& terrain, const BoundHotdog& hotdog, const Mat34& hotdogM,
                    const Mat34& terrainM, Collider* hotdogCollider, Collider* terrainCollider,
                    Impact* impacts, int max, const Vec3& cullDir, const HotdogInTerrain& q) {
    if (max <= 0)
        return 0; // OpenMM2: MM2 writes the first impact regardless
    const float halfH = q.halfHeight;
    std::array<Vec3, 2> ends;
    for (std::size_t k = 0; k < 2; ++k) {
        const Vec3 e{halfH * q.caps[k].x, halfH * q.caps[k].y, halfH * q.caps[k].z};
        ends[k] = {q.center.x + e.x, q.center.y + e.y, q.center.z + e.z};
    }
    const Vec3& rd = q.relDisp;
    const float r = hotdog.capRadius;
    const Vec3 prev{q.center.x - rd.x, q.center.y - rd.y, q.center.z - rd.z};
    float nearest2 = prev.z * prev.z + prev.y * prev.y + prev.x * prev.x;
    const float now2 = q.center.x * q.center.x + q.center.z * q.center.z + q.center.y * q.center.y;
    if (now2 < nearest2)
        nearest2 = now2;
    const float reach = r + terrain.radius + halfH;
    if (nearest2 > reach * reach)
        return 0;

    const Vec3 mid{rd.x * 0.5f + prev.x, rd.y * 0.5f + prev.y, rd.z * 0.5f + prev.z};
    const float rdx2 = rd.x * rd.x;
    const float sweepRadius = std::sqrt(rd.y * rd.y + rd.z * rd.z + rdx2) * 0.5f + r + halfH;
    const float rd2 = rd.z * rd.z + rd.y * rd.y + rdx2;
    const SectionList sections = sectionsNearSphere(terrain, mid, sweepRadius);
    std::span<const Vec3> tverts = terrain.vertices;

    int count = 0;
    bool done = false;
    forEachNewPolygon(terrain, sections, [&](int p) {
        const Polygon& polygon = terrain.polygons[static_cast<std::size_t>(p)];
        const int n = polygon.vertexCount();
        std::array<Vec3, 4> verts{};
        for (int k = 0; k < n; ++k)
            verts[static_cast<std::size_t>(k)] = terrain.vertex(polygon.v[static_cast<std::size_t>(k)]);
        const Vec3 normal = polygon.normal;
        const int material = polygon.material;
        auto finish = [&](int result, int element, int elementA) {
            impacts[count].finishMakingNewImpact(result, element, hotdog, terrain, elementA);
            if (q.local)
                impacts[count].componentB = material;
            if (++count == max)
                done = true;
        };

        // The end spheres.
        for (std::size_t k = 0; k < 2 && !done; ++k) {
            const Vec3 end = ends[k];
            Vec3 position, contactNormal;
            int feature = 0;
            float depth = 0.0f;
            int result = geom::findImpactPolygonToSphere(ends[k], r, verts.data(), n, normal, position,
                                                         feature, contactNormal, depth);
            int element = 0;
            if (result == 0 || result == 1) {
                // A vertex or edge contact counts only on the outer half of
                // the end sphere.
                const Vec3 d{position.x - end.x, position.y - end.y, position.z - end.z};
                const Vec3& cap = q.caps[k];
                const float side = q.local ? d.x * cap.x + d.z * cap.z + d.y * cap.y
                                           : d.z * cap.z + d.y * cap.y + d.x * cap.x;
                if (!(side >= 0.0f))
                    continue;
                element = result == 0 ? polygon.v[static_cast<std::size_t>(feature)]
                                      : polygon.edges[static_cast<std::size_t>(feature)];
            } else if (result == 2) {
                element = p;
            } else {
                // Not touching: did the end sphere's centre pass through the
                // face during the sample?
                if (normal.z * rd.z + normal.y * rd.y + normal.x * rd.x >= 0.0f)
                    continue;
                Segment seg;
                seg.kind = Segment::Probe;
                seg.a = {end.x - rd.x, end.y - rd.y, end.z - rd.z};
                seg.b = end;
                IntersectionPoint hit;
                if (!polygon.testSegmentDirected(tverts, seg, hit, 2.0f))
                    continue;
                contactNormal = normal;
                result = 2;
                const Vec3 deepest{end.x - normal.x * r, end.y - normal.y * r, end.z - normal.z * r};
                const Vec3& v0 = verts[0];
                depth = (v0.z - deepest.z) * normal.z + (v0.y - deepest.y) * normal.y +
                        (v0.x - deepest.x) * normal.x;
                const float half = depth * 0.5f;
                position = {half * normal.x + deepest.x, normal.y * half + deepest.y,
                            normal.z * half + deepest.z};
                element = p;
            }
            const int elementA = static_cast<int>(k);
            impacts[count].startMakingNewImpact(depth, contactNormal, position, hotdogCollider,
                                                terrainCollider, &terrainM, elementA, -1, material);
            if (count > 0 && !Impact::addImpactSpherePlaneTest(impacts, count, end, r))
                continue;
            finish(result, element, elementA);
        }

        // The polygon's edges against the shaft.
        for (int k = 0; k < n && !done; ++k) {
            const int k1 = (k + 1) % n;
            const Vec3& va = terrain.vertex(polygon.v[static_cast<std::size_t>(k)]);
            const Vec3& vb = terrain.vertex(polygon.v[static_cast<std::size_t>(k1)]);
            int feature = 0;
            Vec3 contactNormal, point;
            float depth = 0.0f;
            // FindImpactEdgeToShaft(shaft end, shaft end, edge start, edge
            // end, radius, &feature, &normal, &depth, &point on the shaft).
            const int result = geom::findImpactEdgeToShaft(ends[0], ends[1], va, vb, r, feature,
                                                           contactNormal, depth, point);
            int element = 0;
            if (result == 0)
                element = polygon.v[static_cast<std::size_t>(feature == 1 ? k : k1)];
            else if (result == 1)
                element = polygon.edges[static_cast<std::size_t>(k)];
            else
                continue;
            const float s = r - depth * 0.5f;
            const Vec3 position{point.x - s * contactNormal.x, point.y - contactNormal.y * s,
                                point.z - contactNormal.z * s};
            impacts[count].startMakingNewImpact(depth, contactNormal, position, hotdogCollider,
                                                terrainCollider, &terrainM, 2, -1, material);
            if (count > 0 && !Impact::addImpactShaftPlaneTest(impacts, count, hotdogM.m3, r, q.axis))
                continue;
            finish(result, element, 2);
        }
        if (done)
            return false;

        // The shaft through the face: push it off the polygon edge closest
        // to it, preferring edges it is moving towards.
        Segment shaft;
        shaft.kind = Segment::Edge;
        shaft.a = ends[0];
        shaft.b = ends[1];
        IntersectionPoint hit;
        if (!polygon.testSegmentUndirected(tverts, shaft, hit, 2.0f, -2.0f))
            return true;
        const Vec3 shaftDir{ends[1].x - ends[0].x, ends[1].y - ends[0].y, ends[1].z - ends[0].z};
        float best = FLT_MAX;
        bool found = false;
        bool facing = false;
        int element = 0;
        Vec3 contactNormal, position;
        for (int k = 0; k < n; ++k) {
            const Vec3& va = terrain.vertex(polygon.v[static_cast<std::size_t>(k)]);
            const Vec3& vb = terrain.vertex(polygon.v[static_cast<std::size_t>((k + 1) % n)]);
            const Vec3 edgeDir{vb.x - va.x, vb.y - va.y, vb.z - va.z};
            float tEdge = 0.0f, tShaft = 0.0f;
            geom::findTValuesSegToSeg(va, edgeDir, ends[0], shaftDir, tEdge, tShaft);
            const Vec3 onEdge{edgeDir.x * tEdge + va.x, edgeDir.y * tEdge + va.y, edgeDir.z * tEdge + va.z};
            const Vec3 onShaft{shaftDir.x * tShaft + ends[0].x, shaftDir.y * tShaft + ends[0].y,
                               shaftDir.z * tShaft + ends[0].z};
            const Vec3 diff{onShaft.x - onEdge.x, onShaft.y - onEdge.y, onShaft.z - onEdge.z};
            const float dist2 = diff.z * diff.z + diff.y * diff.y + diff.x * diff.x;
            if (facing && dist2 > rd2)
                continue;
            if (dist2 >= best)
                continue;
            best = dist2;
            found = true;
            element = polygon.edges[static_cast<std::size_t>(k)];
            contactNormal = diff;
            position = onEdge;
            if (!(dist2 >= rd2) && diff.z * rd.z + diff.y * rd.y + diff.x * rd.x > 0.0f)
                facing = true;
        }
        if (!found)
            return true;
        const float len = std::sqrt(contactNormal.y * contactNormal.y + contactNormal.z * contactNormal.z +
                                    contactNormal.x * contactNormal.x);
        contactNormal = {contactNormal.x / len, contactNormal.y / len, contactNormal.z / len};
        const float depth = len + r;
        const float half = depth * 0.5f;
        position = {half * contactNormal.x + position.x, contactNormal.y * half + position.y,
                    contactNormal.z * half + position.z};
        contactNormal = {-contactNormal.x, -contactNormal.y, -contactNormal.z};
        impacts[count].startMakingNewImpact(depth, contactNormal, position, hotdogCollider, terrainCollider,
                                            &terrainM, 2, -1, material);
        if (count > 0 && !Impact::addImpactShaftPlaneTest(impacts, count, hotdogM.m3, r, q.axis))
            return true;
        finish(1, element, 2);
        return !done;
    });

    if (count > 1)
        Impact::cullImpactList(impacts, count, cullDir);
    return count;
}

} // namespace

// --- phBoundTerrain segment tests -----------------------------------------------------------

int BoundTerrain::testEdge(Segment& seg, Intersection* out, int max) const {
    // phBoundTerrain::TestEdge: as phBoundPolygonal::TestEdge over the
    // polygons of the sections the segment crosses. out[0] gets the first
    // crossing (an entry wins a tie), out[1] the last (an exit wins a tie);
    // returns the number of crossings, at most 2. (MM2 writes out[1] whatever
    // `max` is; OpenMM2 writes only the entries that fit.)
    float firstT = 2.0f;
    float lastT = -1.0f;
    int found = 0;
    const SectionList sections = sectionsAlongSegment(*this, seg);
    forEachNewPolygon(*this, sections, [&](int p) {
        const Polygon& poly = polygons[static_cast<std::size_t>(p)];
        IntersectionPoint hit;
        if (!poly.testSegmentUndirected(vertices, seg, hit, firstT, lastT))
            return true;
        ++found;
        if (!(hit.t > firstT) && (hit.bInside || !(hit.t >= firstT))) {
            if (max >= 1) {
                static_cast<IntersectionPoint&>(out[0]) = hit;
                out[0].polygon = p;
                out[0].poly = &poly;
            }
            firstT = hit.t;
        }
        if (hit.t >= lastT && (!hit.bInside || hit.t > lastT)) {
            if (max >= 2) {
                static_cast<IntersectionPoint&>(out[1]) = hit;
                out[1].polygon = p;
                out[1].poly = &poly;
            }
            lastT = hit.t;
        }
        return true;
    });
    return found > 2 ? 2 : found;
}

bool BoundTerrain::testProbe(Segment& seg, Intersection& out, float maxT) const {
    // phBoundTerrain::TestProbe: the nearest polygon the segment enters
    // through its front face, among the sections it crosses.
    bool found = false;
    out.t = maxT;
    out.poly = nullptr;
    const SectionList sections = sectionsAlongSegment(*this, seg);
    forEachNewPolygon(*this, sections, [&](int p) {
        if (polygons[static_cast<std::size_t>(p)].testSegmentDirected(vertices, seg, out, out.t)) {
            found = true;
            out.polygon = p;
        }
        return true;
    });
    if (!found)
        return false;
    out.a = seg.a;
    out.b = seg.b;
    out.poly = &polygons[static_cast<std::size_t>(out.polygon)];
    out.otherBound = this;
    out.material = out.poly->material;
    return true;
}

// --- Polygonal bounds against terrain ---------------------------------------------------------

int testBoundTerrainPoly(const BoundTerrain& terrain, const BoundPolygonal& poly, const Mat34& polyM,
                         const Mat34& polyLast, Collider* polyCollider, Collider* terrainCollider,
                         Intersection* isectsTerrain, Intersection* isectsPoly, int max, int& countTerrain,
                         int& countPoly, const Vec3& relPos, bool sweep) {
    // phBoundTerrain::TestBoundTerrainPoly. Every intersection holds the
    // collider of the bound that owns its segment: terrainCollider in
    // isectsTerrain, polyCollider in isectsPoly (as MM2: TestBoundGeneric
    // passes the polygonal bound's collider as polyCollider).
    const Vec3 dir{-relPos.x, -relPos.y, -relPos.z};
    testBoundTerrainEdgesVsPoly(terrain, poly, terrainCollider, polyM, isectsTerrain, max, countTerrain);
    if (poly.numVertices() < 31)
        testBoundPolyTerrain(terrain, poly, polyCollider, polyM, polyLast, isectsPoly, max, countPoly, sweep);
    else
        testBoundPolyPolyUseDot(poly, terrain, polyCollider, polyM, polyLast, nullptr, nullptr, isectsPoly,
                                max, countPoly, -FLT_MAX, &dir, sweep);
    return countTerrain + countPoly;
}

int testBoundTerrainLocalPoly(const BoundTerrainLocal& terrain, const BoundPolygonal& poly,
                              const Mat34& terrainM, const Mat34& polyM, const Mat34& polyLast,
                              Collider* polyCollider,
                              Collider* /*terrainCollider*/, Intersection* /*isectsTerrain*/,
                              Intersection* isectsPoly, int max, int& countTerrain, int& countPoly,
                              const Vec3& relPos, bool sweep) {
    // phBoundTerrainLocal::TestBoundTerrainPoly: relPos into the terrain's
    // frame, no terrain edges (phBoundTerrainLocal::TestBoundTerrainEdgesVsPoly
    // only clears the count, and only without hot edges: with hot edges
    // countTerrain keeps the caller's value), and the bound's segments
    // against the terrain through TestBoundPolyPolyUseDot whatever its
    // vertex count (phBoundTerrainLocal::TestBoundPolyTerrain is empty).
    const Vec3 dir{dot3(terrainM.m0, relPos), dot3(terrainM.m1, relPos), dot3(terrainM.m2, relPos)};
    if (!terrain.grid.useHotEdges)
        countTerrain = 0;
    testBoundPolyPolyUseDot(poly, terrain, polyCollider, polyM, polyLast, &terrainM, &terrainM, isectsPoly,
                            max, countPoly, -FLT_MAX, &dir, sweep);
    return countTerrain + countPoly;
}

// --- Spheres and hotdogs against terrain ------------------------------------------------------

int findImpactsSphereToTerrain(const BoundTerrain& terrain, const BoundSphere& sphere, const Mat34& sphereM,
                               const Mat34& terrainM, Collider* sphereCollider, Collider* terrainCollider,
                               Impact* impacts, int max, const Vec3& relPos, const Vec3& relDisp) {
    // phBoundTerrain::FindImpactsSphereToTerrain: relPos is the sphere's
    // position in the terrain's frame, relDisp its motion over the sample.
    // Each nearby polygon gives at most one impact at the middle of the
    // penetration; a sphere that passed through a face during the sample
    // takes the face.
    if (max <= 0)
        return 0; // OpenMM2: MM2 writes the first impact regardless
    Vec3 center = relPos;
    if (sphere.isOffset) {
        const Vec3& c = sphere.centroid;
        const Vec3 offset{sphereM.m1.x * c.y + sphereM.m2.x * c.z + sphereM.m0.x * c.x,
                          sphereM.m1.y * c.y + sphereM.m2.y * c.z + sphereM.m0.y * c.x,
                          sphereM.m1.z * c.y + sphereM.m2.z * c.z + sphereM.m0.z * c.x};
        center = {dot3(terrainM.m0, offset) + center.x, center.y + dot3(terrainM.m1, offset),
                  center.z + dot3(terrainM.m2, offset)};
    }
    const float r = sphere.radius;
    const float dx = center.x - terrainM.m3.x;
    const float dy = center.y - terrainM.m3.y;
    const float dz = center.z - terrainM.m3.z;
    const float reach = r + terrain.radius;
    if (!(dz * dz + dy * dy + dx * dx < reach * reach))
        return 0;

    int count = 0;
    const SectionList sections = sectionsNearSphere(terrain, center, r);
    std::span<const Vec3> tverts = terrain.vertices;
    forEachNewPolygon(terrain, sections, [&](int p) {
        const Polygon& polygon = terrain.polygons[static_cast<std::size_t>(p)];
        const int n = polygon.vertexCount();
        std::array<Vec3, 4> verts{};
        for (int k = 0; k < n; ++k)
            verts[static_cast<std::size_t>(k)] = terrain.vertex(polygon.v[static_cast<std::size_t>(k)]);
        const Vec3& normal = polygon.normal;
        Vec3 position, contactNormal;
        int feature = 0;
        float depth = 0.0f;
        int result = geom::findImpactPolygonToSphere(center, r, verts.data(), n, normal, position, feature,
                                                     contactNormal, depth);
        int element = 0;
        if (result == 0) {
            element = polygon.v[static_cast<std::size_t>(feature)];
        } else if (result == 1) {
            element = polygon.edges[static_cast<std::size_t>(feature)];
        } else if (result == 2) {
            element = p;
        } else {
            if (relDisp.x * normal.x + relDisp.z * normal.z + relDisp.y * normal.y >= 0.0f)
                return true;
            Segment seg;
            seg.kind = Segment::Probe;
            seg.a = {center.x - relDisp.x, center.y - relDisp.y, center.z - relDisp.z};
            seg.b = center;
            IntersectionPoint hit;
            if (!polygon.testSegmentDirected(tverts, seg, hit, 2.0f))
                return true;
            result = 2;
            const Vec3 deepest{center.x - r * normal.x, center.y - r * normal.y, center.z - r * normal.z};
            const Vec3 d{verts[0].x - deepest.x, verts[0].y - deepest.y, verts[0].z - deepest.z};
            depth = d.x * normal.x + d.z * normal.z + d.y * normal.y;
            // (MM2 also computes the midpoint of this penetration here; the
            // position below replaces it.)
            contactNormal = normal;
            element = p;
        }
        const float s = r - depth * 0.5f;
        position = {center.x - s * contactNormal.x, center.y - contactNormal.y * s,
                    center.z - contactNormal.z * s};
        impacts[count].startMakingNewImpact(depth, contactNormal, position, sphereCollider, terrainCollider,
                                            &terrainM, 0, -1, polygon.material);
        if (count > 0 && !Impact::addImpactSpherePlaneTest(impacts, count, sphereM.m3, r))
            return true;
        impacts[count].finishMakingNewImpact(result, element, sphere, terrain, 0);
        return ++count != max;
    });

    if (count > 1)
        Impact::cullImpactList(impacts, count, relDisp);
    return count;
}

int findImpactsHotdogToTerrain(const BoundTerrain& terrain, const BoundHotdog& hotdog, const Mat34& hotdogM,
                               const Mat34& terrainM, Collider* hotdogCollider, Collider* terrainCollider,
                               Impact* impacts, int max, const Vec3& relPos, const Vec3& relDisp) {
    // phBoundTerrain::FindImpactsHotdogToTerrain: relPos (the hotdog's
    // position relative to the terrain), relDisp and the hotdog's axis are
    // used as they are, i.e. the terrain's frame is taken to be the world's
    // rotation (true of world-space terrain bounds).
    HotdogInTerrain q;
    q.axis = hotdogM.m1;
    q.caps = {Vec3{-q.axis.x, -q.axis.y, -q.axis.z}, q.axis};
    q.center = relPos;
    if (hotdog.isOffset) {
        const Vec3& c = hotdog.centroid;
        const Vec3 offset{hotdogM.m2.x * c.z + c.x * hotdogM.m0.x + c.y * hotdogM.m1.x,
                          hotdogM.m1.y * c.y + hotdogM.m0.y * c.x + hotdogM.m2.y * c.z,
                          hotdogM.m1.z * c.y + hotdogM.m0.z * c.x + hotdogM.m2.z * c.z};
        q.center = {q.center.x + offset.x, q.center.y + offset.y, q.center.z + offset.z};
    }
    q.halfHeight = hotdog.height * 0.5f;
    q.relDisp = relDisp;
    return hotdogToTerrain(terrain, hotdog, hotdogM, terrainM, hotdogCollider, terrainCollider, impacts, max,
                           relDisp, q);
}

int findImpactsHotdogToTerrainLocal(const BoundTerrainLocal& terrain, const BoundHotdog& hotdog,
                                    const Mat34& hotdogM, const Mat34& terrainM, Collider* hotdogCollider,
                                    Collider* terrainCollider, Impact* impacts, int max, const Vec3& relPos,
                                    const Vec3& relDisp) {
    // phBoundTerrainLocal::FindImpactsHotdogToTerrainLocal: the axis, the
    // centre and relDisp turned into the terrain's frame (rotation only), then
    // the same search. AddImpactShaftPlaneTest gets the axis in the terrain's
    // frame with the hotdog's world position, and CullImpactList the world
    // relDisp, as in MM2.
    const Vec3& up = hotdogM.m1;
    HotdogInTerrain q;
    q.local = true;
    q.axis = {up.y * terrainM.m0.y + up.z * terrainM.m0.z + up.x * terrainM.m0.x, dot3(terrainM.m1, up),
              dot3(terrainM.m2, up)};
    q.caps = {Vec3{-q.axis.x, -q.axis.y, -q.axis.z}, q.axis};
    Vec3 center = relPos;
    if (hotdog.isOffset) {
        const Vec3& c = hotdog.centroid;
        const Vec3 offset{hotdogM.m2.x * c.z + c.y * hotdogM.m1.x + c.x * hotdogM.m0.x,
                          hotdogM.m1.y * c.y + hotdogM.m0.y * c.x + hotdogM.m2.y * c.z,
                          hotdogM.m1.z * c.y + hotdogM.m0.z * c.x + hotdogM.m2.z * c.z};
        center = {center.x + offset.x, center.y + offset.y, center.z + offset.z};
    }
    q.center = {center.z * terrainM.m0.z + center.x * terrainM.m0.x + center.y * terrainM.m0.y,
                dot3(terrainM.m1, center), dot3(terrainM.m2, center)};
    q.halfHeight = hotdog.height * 0.5f;
    q.relDisp = {relDisp.y * terrainM.m0.y + relDisp.z * terrainM.m0.z + relDisp.x * terrainM.m0.x,
                 dot3(terrainM.m1, relDisp), dot3(terrainM.m2, relDisp)};
    return hotdogToTerrain(terrain, hotdog, hotdogM, terrainM, hotdogCollider, terrainCollider, impacts, max,
                           relDisp, q);
}

} // namespace mm2::phys
