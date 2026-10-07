#pragma once

#include "core/Math.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mm2::asset {

// Collision bounds (bound/*.bnd text, *.bbnd binary, *.ter terrain grids).
// See docs/formats/bnd.md.

struct BoundMaterial {
    std::string name = "default"; // physics surface: default, grass, cobblestone, water, ...
    float elasticity = 0.1f;
    float friction = 0.5f;
    std::string effect = "none";
    std::string sound = "none";
};

struct BoundPolygon {
    // Vertex indices. Triangles leave indices[3] = 0: like the game, a
    // polygon is a quad exactly when indices[3] != 0.
    std::array<std::uint16_t, 4> indices{};
    std::uint16_t material = 0;

    bool isQuad() const { return indices[3] != 0; }
    int vertexCount() const { return isQuad() ? 4 : 3; }
};

// Polygon soup with per-polygon materials (phBoundGeometry in the game).
struct BoundGeometry {
    std::vector<Vec3> vertices;
    std::vector<BoundMaterial> materials;
    std::vector<BoundPolygon> polygons;
    // "edges:" from text files; every retail file declares 0.
    std::vector<std::array<std::uint16_t, 2>> edges;

    Aabb bounds() const;
};

// Text bounds ("version: 1.01").
std::optional<BoundGeometry> parseBnd(std::string_view text, std::string* error = nullptr);
// Binary bounds (version byte 1). Same content as the matching .bnd.
std::optional<BoundGeometry> parseBbnd(std::span<const std::byte> data, std::string* error = nullptr);

// Spatial acceleration data for a polygonal bound (phBoundTerrain): a 3D
// grid of sections over the bound's box, each listing the polygons that touch
// it, plus edge adjacency used for "hot edge" collision. Always paired with the
// .bbnd/.bnd of the same name, whose polygons it indexes.
struct TerrainBound {
    float version = 0; // 1.1 in all retail files
    bool useHotEdges = false;
    Vec3 size;
    std::uint32_t widthSections = 0, heightSections = 0, depthSections = 0;
    // Per section: offset into sectionPolygons and count. Section index is
    // (z * heightSections + y) * widthSections + x (x fastest), with cell
    // coordinates floor((p - min) * sectionSizeFactors). Verified on the
    // retail files by polygon centroids (7618/7619); the y term is unverified
    // because almost every grid has a single vertical section.
    std::vector<std::uint16_t> sectionOffsets;
    std::vector<std::uint16_t> sectionCounts;
    std::vector<std::uint16_t> sectionPolygons;
    Vec3 sectionSizeFactors; // sections per unit length on each axis (NaN on degenerate axes)
    Vec3 min, max;
    // Unique polygon edges as vertex index pairs.
    std::vector<std::array<std::uint16_t, 2>> edges;
    // For each polygon, the indices of its (up to) four edges.
    std::vector<std::array<std::uint32_t, 4>> polygonEdges;
    // Per edge, as computed by the Angel engine's ComputeEdgeNormals (MM1:
    // Open1560 game.asm, mmBoundTemplate::ComputeEdgeNormals): the normalised
    // sum of the two adjacent face normals, and the cosine between that edge
    // normal and a face normal, or exactly 2.0 as a sentinel when the edge is
    // not convex / has no usable neighbour (3740 of 19727 retail edges).
    std::vector<Vec3> edgeNormals;
    std::vector<float> edgeValues;

    std::uint32_t polygonCount() const { return static_cast<std::uint32_t>(polygonEdges.size()); }
    std::uint32_t sectionIndex(std::uint32_t x, std::uint32_t y, std::uint32_t z) const {
        return (z * heightSections + y) * widthSections + x;
    }
    // Polygons listed in one section.
    std::span<const std::uint16_t> sectionList(std::uint32_t section) const {
        return std::span(sectionPolygons).subspan(sectionOffsets[section], sectionCounts[section]);
    }
};

std::optional<TerrainBound> parseTer(std::span<const std::byte> data, std::string* error = nullptr);

} // namespace mm2::asset
