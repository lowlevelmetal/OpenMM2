// Parity checks for the collision bounds (phBound and its family) against
// MM2's own code (midtown2.exe build 3393, MM2Recomp). See
// docs/parity/phys-bounds.md.
#include "phys/Bound.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>

using namespace mm2;
using namespace mm2::phys;

namespace {

// Geometry data from vertices and triangles/quads (material 0).
GeometryData geometry(std::vector<Vec3> verts, std::vector<std::array<std::uint16_t, 4>> polys) {
    GeometryData g;
    g.vertices = std::move(verts);
    for (const auto& p : polys) {
        GeometryData::Poly poly;
        poly.v = p;
        g.polys.push_back(poly);
    }
    return g;
}

} // namespace

// phBoundSphere and phBoundHotdog embed a phMaterial (elasticity 0.1,
// friction 0.5) and report one material; the dg* bounds own an lvlMaterial
// (elasticity 0.5, friction 1).
TEST(ParityBounds, SphereAndHotdogEmbedAPhMaterial) {
    BoundSphere sphere(1.0f);
    EXPECT_EQ(sphere.numMaterials(), 1);
    EXPECT_FLOAT_EQ(sphere.material(0).elasticity, 0.1f);
    EXPECT_FLOAT_EQ(sphere.material(0).friction, 0.5f);
    BoundHotdog hotdog(0.5f, 2.0f);
    EXPECT_EQ(hotdog.numMaterials(), 1);
    EXPECT_FLOAT_EQ(hotdog.material(0).elasticity, 0.1f);
    EXPECT_FLOAT_EQ(hotdog.material(0).friction, 0.5f);

    sphere.makeOwnMaterial();
    EXPECT_FLOAT_EQ(sphere.material(0).elasticity, 0.5f);
    EXPECT_FLOAT_EQ(sphere.material(0).friction, 1.0f);

    // lvlMaterial's defaults, with phMaterial's sound index -1.
    EXPECT_EQ(defaultBoundMaterial().sound, -1);
    EXPECT_FLOAT_EQ(defaultBoundMaterial().width, 1.0f);
}

// phBoundGeometry::ReComputeEdgeNormals scans the polygons in order until it
// has seen a face on each side of the edge; each side keeps the last face
// found before then.
TEST(ParityBounds, EdgeNormalUsesTheLastFaceBeforeBothSidesAreSeen) {
    // Edge (0, 1) runs forwards in the first two triangles (normals +y and
    // +z) and backwards in the third (normal +z): the edge normal is +z, not
    // the bisector of +y and +z the first forward face would give.
    auto g = geometry(
        {
            {0, 0, 0},  // 0
            {1, 0, 0},  // 1
            {0, 0, -1}, // 2: triangle 0, 1, 2 faces +y
            {0, 1, 0},  // 3: triangle 0, 1, 3 faces +z
            {0, -1, 0}, // 4: triangle 1, 0, 4 faces +z too
        },
        {{0, 1, 2, 0}, {0, 1, 3, 0}, {1, 0, 4, 0}});
    auto bound = makeGeometryBound(g);
    ASSERT_TRUE(bound);
    ASSERT_GE(bound->numEdges(), 1);
    // The first edge listed is (v[n-1], v[0]) of the first polygon: (2, 0);
    // find (0, 1).
    int edge = -1;
    for (int e = 0; e < bound->numEdges(); ++e) {
        const auto& ab = bound->edges[static_cast<std::size_t>(e)];
        if ((ab[0] == 0 && ab[1] == 1) || (ab[0] == 1 && ab[1] == 0))
            edge = e;
    }
    ASSERT_GE(edge, 0);
    const Vec3 forward = bound->polygons[1].normal; // the second forward face, not the first
    const Vec3 reverse = bound->polygons[2].normal;
    Vec3 sum = forward + reverse;
    sum = sum * (1.0f / std::sqrt(sum.z * sum.z + sum.y * sum.y + sum.x * sum.x));
    const Vec3 n = bound->edgeNormal(edge);
    EXPECT_NEAR(n.x, sum.x, 1e-6f);
    EXPECT_NEAR(n.y, sum.y, 1e-6f);
    EXPECT_NEAR(n.z, sum.z, 1e-6f);
}

// phBoundPolygonal::MaxDot / MinDot: the extreme projection of the placed
// vertices on a world direction.
TEST(ParityBounds, MaxDotAndMinDot) {
    BoundBox box(Vec3{2, 4, 6});
    Mat34 m = Mat34::identity();
    m.m3 = {10, 0, 0};
    Vec3 local;
    EXPECT_FLOAT_EQ(box.maxDot({1, 0, 0}, m, local), 11.0f);
    EXPECT_FLOAT_EQ(box.minDot({1, 0, 0}, m, local), 9.0f);
    EXPECT_FLOAT_EQ(box.maxDot({0, 0, 1}, m, local), 3.0f);
}

// phBound::GetCenter: the offset carried by the matrix.
TEST(ParityBounds, CenterAddsTheRotatedOffset) {
    BoundBox box;
    box.setOffset({1, 2, 3});
    Mat34 m = Mat34::identity();
    m.m3 = {5, 6, 7};
    const Vec3 c = box.center(m);
    EXPECT_FLOAT_EQ(c.x, 6.0f);
    EXPECT_FLOAT_EQ(c.y, 8.0f);
    EXPECT_FLOAT_EQ(c.z, 10.0f);
}
