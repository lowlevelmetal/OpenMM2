#include "game/MeshDraw.h"

#include <gtest/gtest.h>

using namespace mm2;
using namespace mm2::game;

TEST(RenderRules, ObjectDetailTables) {
    // cityLevel::SetObjectDetail.
    const auto low = ObjectDetail::forLevel(0);
    EXPECT_FLOAT_EQ(low.med, 20.0f);
    EXPECT_FLOAT_EQ(low.low, 70.0f);
    EXPECT_FLOAT_EQ(low.vlow, 150.0f);
    EXPECT_FLOAT_EQ(low.noDraw, 200.0f);
    const auto top = ObjectDetail::forLevel(3);
    EXPECT_FLOAT_EQ(top.med, 70.0f);
    EXPECT_FLOAT_EQ(top.low, 130.0f);
    EXPECT_FLOAT_EQ(top.vlow, 200.0f);
    EXPECT_FLOAT_EQ(top.noDraw, 300.0f);
    EXPECT_FLOAT_EQ(ObjectDetail{}.med, ObjectDetail::forLevel(2).med); // the static defaults are level 2
}

TEST(RenderRules, ObjectLodBands) {
    // lvlInstance::IsVisible: H up to Med, M up to Low, L up to VLow, VL beyond.
    const auto d = ObjectDetail::forLevel(2);
    EXPECT_EQ(objectLod(42.0f, 2.0f, d), asset::Lod::High);
    EXPECT_EQ(objectLod(42.5f, 2.0f, d), asset::Lod::Medium);
    EXPECT_EQ(objectLod(102.0f, 2.0f, d), asset::Lod::Medium);
    EXPECT_EQ(objectLod(152.0f, 2.0f, d), asset::Lod::Low);
    EXPECT_EQ(objectLod(252.0f, 2.0f, d), asset::Lod::VeryLow);
    // The NoDraw limit tests the centre's depth, without the radius.
    EXPECT_TRUE(objectLod(300.0f, 2.0f, d, d.noDraw));
    EXPECT_FALSE(objectLod(301.0f, 2.0f, d, d.noDraw));
    EXPECT_EQ(objectLod(301.0f, 2.0f, d), asset::Lod::VeryLow);
}

TEST(RenderRules, ViewDepth) {
    // The camera looks down -Z: a point 10 m ahead and 3 m to the side is 10 m deep.
    Mat34 cam = Mat34::identity();
    cam.m3 = {1, 2, 3};
    EXPECT_FLOAT_EQ(viewDepth(cam, {4, 2, -7}), 10.0f);
    EXPECT_FLOAT_EQ(viewDepth(cam, {1, 2, 8}), -5.0f);
}
