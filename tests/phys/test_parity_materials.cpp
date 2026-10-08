// lvlMaterial::Load (build 3393) reads a material's numbers with
// datAsciiTokenizer's GetFloat / GetInt and its sound with atoi.
#include "phys/Material.h"

#include <gtest/gtest.h>

using namespace mm2;

TEST(ParityMaterials, NumbersReadLikeLvlMaterialLoad) {
    const char* text = "mtl odd {\n"
                       "  elasticity: abc\n"
                       "  friction: 1.5x\n"
                       "  effect: none\n"
                       "  sound: 12abc\n"
                       "  drag: +2\n"
                       "  width: .5\n"
                       "  height: -1e1\n"
                       "  depth: 3\n"
                       "  ptxindex: +3 7.9\n"
                       "  ptxthreshold: 0.25 x\n"
                       "}\n"
                       "mtl quiet {\n"
                       "  elasticity: 0.9\n"
                       "  friction: 0.8\n"
                       "  effect: none\n"
                       "  sound: NONE\n"
                       "}\n";
    const auto list = phys::parseMaterials(text);
    ASSERT_TRUE(list);
    ASSERT_EQ(list->size(), 2u);
    const phys::Material& m = (*list)[0];
    EXPECT_FLOAT_EQ(m.elasticity, 0.0f); // not a number: GetFloat's 0
    EXPECT_FLOAT_EQ(m.friction, 1.5f);   // atof's prefix
    EXPECT_EQ(m.sound, 12);              // atoi's prefix
    EXPECT_FLOAT_EQ(m.drag, 0.0f);       // GetFloat takes no '+'
    EXPECT_FLOAT_EQ(m.width, 0.5f);
    EXPECT_FLOAT_EQ(m.height, -10.0f);
    EXPECT_FLOAT_EQ(m.depth, 3.0f);
    EXPECT_EQ(m.ptxIndex[0], 0); // GetInt takes no '+'
    EXPECT_EQ(m.ptxIndex[1], 7);
    EXPECT_FLOAT_EQ(m.ptxThreshold[0], 0.25f);
    EXPECT_FLOAT_EQ(m.ptxThreshold[1], 0.0f);
    // "none" in any case is sound 0; the rest keeps lvlMaterial's defaults.
    const phys::Material& q = (*list)[1];
    EXPECT_EQ(q.sound, 0);
    EXPECT_FLOAT_EQ(q.width, 1.0f);
    EXPECT_EQ(q.ptxIndex[0], -1);
}
