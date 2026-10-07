#pragma once

#include "core/Math.h"
#include "phys/Material.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace mm2::phys {

// Polygon soup in the shape of the Angel bound files (.bnd text, .bbnd
// binary, .ter terrain), filled in by whoever parses those formats. Polygons
// are convex triangles or quads with counter-clockwise winding seen from the
// outside (the normal follows the right-hand rule). `materialNames` is
// indexed by Poly::material and resolved against a MaterialTable.
struct BoundGeometry {
    struct Poly {
        std::array<std::uint32_t, 4> v{};
        std::uint8_t count = 3; // 3 or 4
        std::uint16_t material = 0;
    };
    std::vector<Vec3> vertices;
    std::vector<Poly> polys;
    std::vector<std::string> materialNames;

    Aabb bounds() const;
};

// Preprocessed convex polygon in world space.
struct Polygon {
    std::array<Vec3, 4> v{};
    int count = 3;
    Vec3 normal;                   // unit, outward
    float d = 0;                   // plane: normal . p == d
    std::array<Vec3, 4> edgeOut{}; // in-plane unit normal of edge i (v[i] -> v[i+1]), pointing outward
    Aabb box;
    int material = 0; // MaterialTable index

    // Builds the derived fields; returns false for degenerate polygons.
    bool finalize();
    // True when `p` (assumed on or near the plane) lies inside all edges,
    // with `slack` metres of tolerance.
    bool containsProjected(const Vec3& p, float slack = 0.0f) const;
    Vec3 closestPoint(const Vec3& p) const;
};

struct RayHit {
    Vec3 position;
    Vec3 normal;
    float t = 1.0f;   // fraction along the segment
    int material = 0; // MaterialTable index
    int polygon = -1; // index in the soup that was hit
};

// Segment a->b against a polygon (either side). Returns t in [0,1] on hit.
bool segmentPolygon(const Vec3& a, const Vec3& b, const Polygon& poly, float& t);

// Static collision geometry with a uniform XZ grid (Angel phBoundTerrain /
// lvlLevel room lists play this role in the original).
class PolygonSoup {
public:
    // Appends geometry transformed by `xform`, resolving material names.
    void add(const BoundGeometry& geom, const Mat34& xform, const MaterialTable& materials);
    // Appends one polygon (already in world space).
    void add(const Polygon& poly);
    // Builds the grid. Must be called after the last add() and before queries.
    void finalize(float cellSize = 16.0f);

    std::size_t size() const { return m_polys.size(); }
    const Polygon& polygon(std::uint32_t i) const { return m_polys[i]; }
    const Aabb& bounds() const { return m_bounds; }

    // Polygon indices whose boxes overlap `box`, sorted and unique.
    void query(const Aabb& box, std::vector<std::uint32_t>& out) const;
    // Nearest hit along the segment a->b.
    bool raycast(const Vec3& a, const Vec3& b, RayHit& hit) const;

private:
    void cellRange(const Aabb& box, int& x0, int& z0, int& x1, int& z1) const;

    std::vector<Polygon> m_polys;
    Aabb m_bounds;
    float m_cell = 16.0f;
    float m_invCell = 1.0f / 16.0f;
    int m_nx = 0, m_nz = 0;
    // CSR layout: polygons of cell c are m_items[m_start[c] .. m_start[c+1]).
    std::vector<std::uint32_t> m_start;
    std::vector<std::uint32_t> m_items;
};

// Oriented box in world space.
struct Obb {
    Vec3 center;
    Vec3 axis[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    Vec3 half{0.5f, 0.5f, 0.5f};

    Vec3 corner(int i) const; // i in [0,8): bit0 x, bit1 y, bit2 z sign
    Aabb aabb() const;
    float projectRadius(const Vec3& dir) const;
};

} // namespace mm2::phys
