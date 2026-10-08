// Parity checks for the bound file parser against MM2's loader
// (phBoundGeometry::Load reading through datBaseTokenizer). See
// docs/parity/phys-bounds.md.
#include "asset/Bound.h"

#include <gtest/gtest.h>

using namespace mm2;

// datBaseTokenizer: ';' comments run to the end of the line, a double-quoted
// token may hold spaces, NUL separates like whitespace.
TEST(ParityBoundsAsset, TokenizerSkipsCommentsAndReadsQuotes) {
    const std::string text = std::string("; a bound written by hand\n"
                                         "version: 1.01 verts: 3 materials: 1 edges: 0 polys: 1\n"
                                         "v 0 0 0 ; first corner\n"
                                         "v 1 0 0\n"
                                         "v 0 0 1\n"
                                         "mtl \"wet grass\" { elasticity: 0.2 friction: 0.6 effect: none sound: 0 }\n"
                                         "tri 0 1 2 0") +
                             std::string(1, '\0');
    std::string err;
    auto g = asset::parseBnd(text, &err);
    ASSERT_TRUE(g) << err;
    EXPECT_EQ(g->vertices.size(), 3u);
    ASSERT_EQ(g->materials.size(), 1u);
    EXPECT_EQ(g->materials[0].name, "wet grass");
    EXPECT_FLOAT_EQ(g->materials[0].friction, 0.6f);
    ASSERT_EQ(g->polygons.size(), 1u);
}
