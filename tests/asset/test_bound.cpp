#include "AssetTestUtil.h"
#include "asset/Bound.h"

#include <gtest/gtest.h>

using namespace mm2;

namespace {

const char* kBndText = "version: 1.01\r\n"
                       "verts: 4 \r\n"
                       "materials: 1 \r\n"
                       "edges: 0 \r\n"
                       "polys: 2 \r\n\r\n"
                       "v\t0.0\t0.0\t0.0 \r\n"
                       "v\t1.0\t0.0\t0.0 \r\n"
                       "v\t1.0\t0.0\t1.0 \r\n"
                       "v\t0.0\t0.0\t1.0 \r\n\r\n"
                       "mtl grass {\r\n\telasticity: 0.100000 \r\n\tfriction: 0.700000 \r\n"
                       "\teffect: none\r\n\tsound: 0\r\n}\r\n\r\n"
                       "quad 0  1  2  3  0 \r\n"
                       "tri 3  2  1  0 \r\n";

} // namespace

TEST(Bnd, ParsesText) {
    std::string err;
    auto g = asset::parseBnd(kBndText, &err);
    ASSERT_TRUE(g) << err;
    EXPECT_EQ(g->vertices.size(), 4u);
    ASSERT_EQ(g->materials.size(), 1u);
    EXPECT_EQ(g->materials[0].name, "grass");
    EXPECT_FLOAT_EQ(g->materials[0].friction, 0.7f);
    EXPECT_EQ(g->materials[0].sound, "0");
    ASSERT_EQ(g->polygons.size(), 2u);
    EXPECT_TRUE(g->polygons[0].isQuad());
    EXPECT_FALSE(g->polygons[1].isQuad());
    EXPECT_EQ(g->polygons[1].indices[2], 1);
}

TEST(Bnd, RejectsBadCountsAndIndices) {
    std::string err;
    EXPECT_FALSE(asset::parseBnd("verts: 2\nv 0 0 0\n", &err));
    EXPECT_FALSE(asset::parseBnd("v 0 0 0\nv 1 0 0\nv 0 0 1\ntri 0 1 7 0\n", &err));
    EXPECT_FALSE(asset::parseBnd("bogus\n", &err));
}

TEST(Bbnd, MatchesTextLayout) {
    Bytes b;
    b.u8(1).u32(3).u32(1).u32(1);
    b.f32(0).f32(0).f32(0).f32(1).f32(0).f32(0).f32(0).f32(0).f32(1);
    b.fixed("cobblestone", 32).f32(0.2f).f32(0.9f).fixed("none", 32).fixed("none", 32);
    b.u16(0).u16(1).u16(2).u16(0).u16(0);
    std::string err;
    auto g = asset::parseBbnd(b.data, &err);
    ASSERT_TRUE(g) << err;
    EXPECT_EQ(g->materials[0].name, "cobblestone");
    EXPECT_FLOAT_EQ(g->materials[0].friction, 0.9f);
    EXPECT_EQ(g->polygons[0].vertexCount(), 3);
    b.u8(0);
    EXPECT_FALSE(asset::parseBbnd(b.data, &err));
}

TEST(Ter, ParsesGridAndEdges) {
    Bytes b;
    b.f32(1.1f).u32(1).u32(3).u8(0);
    b.f32(2).f32(1).f32(2);          // size
    b.u32(2).u32(1).u32(1).u32(2).u32(2); // 2x1x1 sections, 2 refs
    b.f32(1).f32(1).f32(0.5f);       // factors
    b.f32(-1).f32(0).f32(-1).f32(1).f32(1).f32(1);
    b.u16(0).u16(1);                 // offsets
    b.u16(1).u16(1);                 // counts
    b.u16(0).u16(0);                 // polygon refs
    b.u16(0).u16(1).u16(1).u16(2).u16(2).u16(0); // edges
    b.u32(0).u32(1).u32(2).u32(0);   // polygon edges
    for (int i = 0; i < 3; ++i)
        b.f32(0).f32(1).f32(0);
    b.f32(1).f32(2).f32(0.5f);
    std::string err;
    auto t = asset::parseTer(b.data, &err);
    ASSERT_TRUE(t) << err;
    EXPECT_EQ(t->polygonCount(), 1u);
    EXPECT_EQ(t->edges.size(), 3u);
    EXPECT_EQ(t->sectionIndex(1, 0, 0), 1u);
    EXPECT_EQ(t->sectionList(1).size(), 1u);
    EXPECT_FLOAT_EQ(t->edgeValues[1], 2.0f);
    b.u8(0);
    EXPECT_FALSE(asset::parseTer(b.data, &err));
}
