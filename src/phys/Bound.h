#pragma once

// MM2's collision bounds (phBound and its family), ported from the code of
// midtown2.exe build 3393 (MM2Recomp). The data follows the original's
// layout closely because the collision routines walk it in a fixed order:
// polygon vertex and edge numbering, the edge list built from the polygons,
// edge normals and the per-intersection bookkeeping all decide which impacts
// a collision produces. See docs/physics.md, "Collision".
//
// Every bound lives in its own local frame; colliders place it in the world
// with a matrix (row vectors, Angel conventions: p' = p * M).

#include "core/Math.h"
#include "phys/Material.h"

#include <array>
#include <cfloat>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace mm2::phys {

class Collider;
struct Impact;
struct Polygon;
class Bound;
class BoundPolygonal;
class BoundSphere;
class BoundHotdog;
class BoundBox;
class BoundGeometry;
class BoundTerrain;
class BoundTerrainLocal;

// phBound's type. ForceSphere (phForceSphere) and Level (the city's lvlSDL)
// complete the original's numbering.
enum class BoundType : int {
    Sphere = 0,
    Geometry = 1,
    Box = 2,
    ForceSphere = 3,
    Terrain = 4,
    TerrainLocal = 5,
    Hotdog = 6,
    Level = 7,
};

// The collision tolerances of phBoundCollision::SetPenetration (+ 0.3 of it
// for the "barely moved" test). MM2 starts with 0.035 m, but dgPhysManager's
// constructor calls phContact::DisableContacts, which sets 0 for the rest of
// the session, so every collision in a race runs with 0. Kept as constants to
// mark where the original reads the globals.
inline constexpr float kPenetration = 0.0f;
inline constexpr float kBarelyMovedDistance = 0.0f;

// phSegment (kind, a, b) with lvlSegment::CalculateInfo's cached values.
struct Segment {
    enum Kind : int { Probe = 0, Edge = 1, Ai = 2 };
    int kind = Probe;
    Vec3 a;
    Vec3 b;
    bool vertical = false; // a and b differ only in y
    float invLength = 0;   // 1 / |b - a| (1 / |dy| when vertical)

    // lvlSegment::CalculateInfo.
    void calculateInfo();
};

// phIntersectionPoint (0x24 bytes): where a segment crosses a polygon.
struct IntersectionPoint {
    Vec3 position;
    Vec3 normal;       // the polygon's normal
    float t = 0;       // fraction along the segment
    float depth = 0;   // how far the inside end lies behind the plane
    bool bInside = false; // the segment's end b is the end behind the plane

    // phIntersectionPoint::Transform: position by m, normal by its 3x3 part.
    void transform(const Mat34& m);
};

// phIntersection (0x9c bytes): a vertex sweep or an edge of one bound that
// pierces a polygon of another, plus the bookkeeping phBoundPolygonal's
// impact search keeps per intersection.
struct Intersection : IntersectionPoint {
    // Flags.
    static constexpr std::uint16_t kVertex = 0x1;      // a vertex sweep, not an edge
    static constexpr std::uint16_t kInterior = 0x2;    // AddInteriorEdges: an edge of a pierced polygon
    static constexpr std::uint16_t kEdgeEdge = 0x4;    // CheckSaveEdgeEdge kept an edge-edge candidate
    static constexpr std::uint16_t kSearched = 0x8;    // DoEndPtSearch visited this vertex
    static constexpr std::uint16_t kNeedsRetry = 0x10; // RetryVertPolyCollide should look at it
    static constexpr std::uint16_t kPaired = 0x20;     // MakeBsInside: same edge pierced twice nearby
    static constexpr std::uint16_t kSoon = 0x40;       // DoEndPtSearch: reaches the face within 1.5 samples
    static constexpr std::uint16_t kSkip = 0x80;       // superseded by another intersection
    static constexpr std::uint16_t kUsed = 0x100;      // produced an impact

    int polygon = 0;       // index of the pierced polygon in otherBound
    int element = 0;       // edge index, or vertex index for a sweep
    int vertexA = -1;      // the edge's first vertex (-1 for a sweep)
    int vertexB = -1;      // the edge's second vertex (the vertex of a sweep)
    Vec3 a;                // segment start (world after ToWorldCoords)
    Vec3 b;                // segment end
    Vec3 edgeNormal;       // the edge's normal in the world (edges only)
    const Polygon* poly = nullptr; // the pierced polygon
    int material = 0;      // its material index (the polygon's material byte)
    const Bound* bound = nullptr;      // the bound owning the edge or vertex
    float unused64 = 0;
    Collider* collider = nullptr;      // a collider of the pair (which one depends on the test)
    const Bound* otherBound = nullptr; // the bound owning the polygon
    float timeToImpact = 0;
    int edgeEdgeIndex = -1;            // partner intersection of an edge-edge candidate
    float edgeEdgeDistance = FLT_MAX;
    Vec3 edgeEdgePoint;
    Vec3 edgeEdgeNormal;
    float edgeEdgeDepth = 0;
    std::uint16_t flags = 0;
};

// phPolygon (0x60 bytes): a triangle or quad of a polygonal bound.
struct Polygon {
    Vec3 normal;    // unit, right-hand rule over v0, v1, v2
    float area = 0; // (MM2 keeps the material index in this float's low byte)
    std::uint8_t material = 0;
    // per edge i (v[i] -> v[i+1]) the in-plane unit normal n x edge,
    // which points into the polygon (stored as Vector4 with w = 1).
    std::array<Vec3, 4> edgeNormals{};
    std::array<std::uint16_t, 4> v{};     // ; v[3] == 0 marks a triangle
    std::array<std::uint16_t, 4> edges{}; // bound edge index of (v[i], v[i+1])

    int vertexCount() const { return v[3] != 0 ? 4 : 3; }

    // phPolygon::InitTriangle / InitQuad / CalculateNormal /
    // ComputeEdgeNormalCross.
    void initTriangle(std::uint16_t a, std::uint16_t b, std::uint16_t c, std::span<const Vec3> verts);
    void initQuad(std::uint16_t a, std::uint16_t b, std::uint16_t c, std::uint16_t d, std::span<const Vec3> verts);
    void calculateNormal(std::span<const Vec3> verts);
    void computeEdgeNormalCross(std::span<const Vec3> verts);

    // phPolygon::TestSegmentDirected: the segment enters through the front
    // face (a in front, b behind) at t <= maxT.
    bool testSegmentDirected(std::span<const Vec3> verts, const Segment& seg, IntersectionPoint& out,
                             float maxT) const;
    // phPolygon::TestSegmentUndirected: crosses the plane either way;
    // crossings with rejectFrom < t < rejectTo do not count (callers pass
    // (maxT, 2) to accept t <= maxT).
    bool testSegmentUndirected(std::span<const Vec3> verts, const Segment& seg, IntersectionPoint& out,
                               float rejectFrom, float rejectTo) const;
    // phPolygon::DetectSegmentDirected / DetectSegmentUndirected: whether
    // the line a->b passes inside the polygon's edges (no plane test).
    bool detectSegmentDirected(std::span<const Vec3> verts, const Vec3& a, const Vec3& b) const;
    bool detectSegmentUndirected(std::span<const Vec3> verts, const Vec3& a, const Vec3& b) const;
};

// phBound: the base of every bound.
class Bound {
public:
    explicit Bound(BoundType type);
    virtual ~Bound() = default;
    Bound(const Bound&) = delete;
    Bound& operator=(const Bound&) = delete;

    BoundType type;
    Vec3 boxMin, boxMax;    // local bounding box
    bool isOffset = false;  // centroid != 0
    Vec3 centroid;          // bounding sphere centre (the offset of boxes, spheres, hotdogs)
    float radius = 0;       // bounding sphere radius
    float flexibility = 0;
    bool flexible = false;
    float gravity = 1;
    float penetration = kPenetration;
    float barelyMoved = kBarelyMovedDistance;

    bool isPolygonal() const {
        return type == BoundType::Geometry || type == BoundType::Box || type == BoundType::Terrain ||
               type == BoundType::TerrainLocal;
    }

    // The bound's own material, as the dg* bounds (dgBoundBox,
    // dgBoundGeometry, dgBoundSphere, dgBoundHotdog) and vehBound carry: an
    // lvlMaterial (elasticity 0.5, friction 1 until SetFriction /
    // SetElasticity change them) that every material index returns. Null for
    // bounds whose polygons index shared materials (city instances).
    std::shared_ptr<Material> ownMaterial;
    // Gives the bound its own material (lvlMaterial defaults).
    void makeOwnMaterial();

    // phBound::GetMaterial (vtable 0x04): the own material when there is
    // one, else the bound's table (the default material for the base).
    virtual const Material& material(int index) const;
    virtual int numMaterials() const { return ownMaterial ? 1 : 0; }
    // dgBound*::SetFriction / SetElasticity: change the own material (no-ops
    // on bounds without one, as phBound's).
    void setFriction(float friction);
    void setElasticity(float elasticity);

    // Segment tests in the bound's local space (vtable 0x24 TestEdge, 0x28
    // TestProbe). TestEdge returns the number of intersections written;
    // TestProbe the nearest hit with t below maxT.
    virtual int testEdge(Segment& seg, Intersection* out, int max) const;
    virtual bool testProbe(Segment& seg, Intersection& out, float maxT) const;
    // phBound::GetVertex (vtable 0x40): polygonal bounds return their
    // vertices (the level returns the vertices of its polygons); others a
    // dummy point.
    virtual const Vec3& vertex(int index) const;
    // phBound::TestSegment: probes and vertex sweeps (kind Probe) go to
    // testProbe with maxT 2, edges (kind Edge) to testEdge.
    int testSegment(Segment& seg, Intersection* out, int max) const;

    // phBound::CalculateSphereFromBoundingBox / SetOffset / GetCenter /
    // SetPenetration.
    void calculateSphereFromBoundingBox();
    void setOffset(const Vec3& offset);
    Vec3 center(const Mat34& m) const;
    void setPenetration();
};

// phBoundPolygonal: vertices, polygons and their edges.
class BoundPolygonal : public Bound {
public:
    using Bound::Bound;

    std::vector<Vec3> vertices;
    std::vector<Polygon> polygons;
    std::vector<std::array<std::uint16_t, 2>> edges;

    int numVertices() const { return static_cast<int>(vertices.size()); }
    int numPolygons() const { return static_cast<int>(polygons.size()); }
    int numEdges() const { return static_cast<int>(edges.size()); }
    // vtable 0x40 GetVertex, 0x44 GetEdgeCosine, 0x48 GetEdgeNormal.
    const Vec3& vertex(int i) const override { return vertices[static_cast<std::size_t>(i)]; }
    virtual float edgeCosine(int edge) const = 0;
    virtual Vec3 edgeNormal(int edge) const = 0;

    int testEdge(Segment& seg, Intersection* out, int max) const override;
    bool testProbe(Segment& seg, Intersection& out, float maxT) const override;

    // phBoundPolygonal::MaxDot / MinDot: the largest (smallest) projection of
    // the bound's vertices, placed by m, on the world direction dir; `local`
    // receives dir in the bound's space.
    float maxDot(const Vec3& dir, const Mat34& m, Vec3& local) const;
    float minDot(const Vec3& dir, const Mat34& m, Vec3& local) const;
};

// phBoundGeometry: a polygonal bound loaded from a .bnd file.
class BoundGeometry : public BoundPolygonal {
public:
    BoundGeometry() : BoundPolygonal(BoundType::Geometry) {}

    // Per material index (phBoundGeometry's materials).
    std::vector<const Material*> materials;
    std::vector<Vec3> edgeNormals;
    std::vector<float> edgeCosines;

    const Material& material(int index) const override;
    int numMaterials() const override {
        return ownMaterial ? 1 : static_cast<int>(materials.size());
    }
    float edgeCosine(int edge) const override { return edgeCosines[static_cast<std::size_t>(edge)]; }
    Vec3 edgeNormal(int edge) const override { return edgeNormals[static_cast<std::size_t>(edge)]; }

    // phBoundGeometry::PostLoadCompute: ComputeEdges, ComputeEdgeNums,
    // ComputeEdgeNormals, SetQuickTestInfo.
    void postLoadCompute();
    void computeEdges();
    void computeEdgeNums();
    void computeEdgeNormals();
    // phBoundGeometry::SetQuickTestInfo: bounding box, penetration, sphere.
    void setQuickTestInfo();
    // phBoundGeometry::ShiftCentroid: moves every vertex by `shift`.
    void shiftCentroid(const Vec3& shift);
};

// The geometry of a .bnd/.bbnd file as the loaders hand it over.
struct GeometryData {
    struct Poly {
        std::array<std::uint16_t, 4> v{};
        // A text "quad" token: phBoundGeometry::Load keeps it a quad even when
        // its last index is 0 (it rotates the indices); binary files cannot
        // tell and make it a triangle (LoadBinary).
        bool quad = false;
        std::uint16_t material = 0;
    };
    std::vector<Vec3> vertices;
    std::vector<Poly> polys;
    std::vector<const Material*> materials;
};

// phBoundGeometry::Load + PostLoadCompute. Returns null for empty data
// (the original refuses bounds without vertices or polygons).
std::unique_ptr<BoundGeometry> makeGeometryBound(const GeometryData& data);

// phBoundBox: an axis-aligned box of `size` centred on `centroid`, with the
// fixed corner, polygon and edge numbering of the original.
class BoundBox : public BoundPolygonal {
public:
    BoundBox();
    explicit BoundBox(const Vec3& size);

    Vec3 size{1, 1, 1};

    // phBoundBox::GetEdgeCosine returns pi/4 for every edge.
    float edgeCosine(int) const override { return 0.7853982f; }
    Vec3 edgeNormal(int edge) const override;

    // phBoundBox::SetSize / ShiftCentroid / SetQuickTestInfo.
    void setSize(const Vec3& size);
    void shiftCentroid(const Vec3& shift);
    void setQuickTestInfo();

    int testEdge(Segment& seg, Intersection* out, int max) const override;
    bool testProbe(Segment& seg, Intersection& out, float maxT) const override;

    // Corner signs (unit cube corners * 0.5), face normals and edge normals
    // of the original's tables.
    static const std::array<Vec3, 8>& unitCorners();
    static const std::array<Vec3, 6>& faceNormals();
    static const std::array<Vec3, 12>& edgeNormalTable();
};

// phBoundSphere.
class BoundSphere : public Bound {
public:
    explicit BoundSphere(float radius = 1.0f);

    float sphereRadius = 1.0f; // the bounding radius equals it

    // phBoundSphere::SetRadius.
    void setRadius(float r);

    int testEdge(Segment& seg, Intersection* out, int max) const override;
    bool testProbe(Segment& seg, Intersection& out, float maxT) const override;
};

// phBoundHotdog: a capsule along the local y axis, `height` between the
// centres of its end spheres.
class BoundHotdog : public Bound {
public:
    BoundHotdog(float radius = 1.0f, float height = 1.0f);

    float capRadius = 1.0f;
    float height = 1.0f;    // between the centres of the end spheres

    // phBoundHotdog::SetSize / CalculateBoundingBox.
    void setSize(float radius, float height);
    void calculateBoundingBox();

    int testEdge(Segment& seg, Intersection* out, int max) const override;
    bool testProbe(Segment& seg, Intersection& out, float maxT) const override;
};

// The phBoundTerrain section grid of a .ter file (see docs/formats/bnd.md).
struct TerrainGrid {
    bool useHotEdges = false;
    Vec3 size;
    int widthSections = 0, heightSections = 0, depthSections = 0;
    std::vector<std::uint16_t> sectionOffsets;
    std::vector<std::uint16_t> sectionCounts;
    std::vector<std::uint16_t> sectionPolygons;
    Vec3 sectionSizeFactors;
};

// phBoundTerrain: a large static polygonal bound in world space with a
// section grid over its box; edges, polygon edge numbers, edge normals and
// cosines come from the .ter file.
class BoundTerrain : public BoundGeometry {
public:
    BoundTerrain() { type = BoundType::Terrain; }

    TerrainGrid grid;
    // polygons already tested by the current query.
    mutable std::vector<std::uint8_t> polyTouched;

    // phBoundTerrain::TestEdge / TestProbe: through the section grid.
    int testEdge(Segment& seg, Intersection* out, int max) const override;
    bool testProbe(Segment& seg, Intersection& out, float maxT) const override;
};

// phBoundTerrainLocal: a terrain bound placed by its instance's matrix.
class BoundTerrainLocal : public BoundTerrain {
public:
    BoundTerrainLocal() { type = BoundType::TerrainLocal; }
};

// The terrain data of a .ter file in phys terms (edges, per-polygon edge
// numbers, edge normals and cosines, the grid and the box).
struct TerrainData {
    TerrainGrid grid;
    Vec3 boxMin, boxMax;
    std::vector<std::array<std::uint16_t, 2>> edges;
    std::vector<std::array<std::uint32_t, 4>> polygonEdges;
    std::vector<Vec3> edgeNormals;
    std::vector<float> edgeCosines;
};

// phBoundTerrain::Load: geometry from `geometry`, the rest from `terrain`.
// Falls back to a plain geometry bound computation (PostLoadCompute, box
// grown by 0.0001) when the terrain data does not match, as the original.
std::unique_ptr<BoundTerrain> makeTerrainBound(const GeometryData& geometry, const TerrainData* terrain,
                                               bool local);

// The material a bound falls back to (lvlMaterialMgr's default lvlMaterial:
// elasticity 0.5, friction 1). The city's own "_default" (materials.mtl)
// replaces it for level and instance polygons through their tables.
const Material& defaultBoundMaterial();

// phBoundPolygonal::BackupDispByPenetration / BackupAbyPenetration: moves a
// away from b by 1.5 * penetration (a unchanged up to rounding when the
// penetration is 0).
void backupDispByPenetration(Vec3& a, const Vec3& b, float penetration);
void backupAByPenetration(Segment& seg, float penetration);

} // namespace mm2::phys
