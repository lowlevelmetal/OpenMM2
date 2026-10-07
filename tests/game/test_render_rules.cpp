#include "game/CityRenderer.h"
#include "game/MeshDraw.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace mm2;
using namespace mm2::game;

namespace {

city::CityData litCity() {
    city::CityData c;
    city::LightingDef lt;
    lt.keyHeading = 0.0f;
    lt.keyPitch = -1.0f;
    lt.keyColor = {1, 1, 1};
    lt.fill1Heading = 3.14159265f;
    lt.fill1Pitch = 0.0f;
    lt.fill1Color = {0.5f, 0.5f, 0.5f};
    lt.fill2Heading = 0.0f;
    lt.fill2Pitch = 0.0f;
    lt.fill2Color = {0.25f, 0.25f, 0.25f};
    lt.ambient = 0xFF284050u;
    c.lighting[static_cast<std::size_t>(city::lightingIndex(1, 0))] = lt;
    c.fog.resize(16);
    c.fog[static_cast<std::size_t>(city::lightingIndex(1, 0))] = {155, 172, 195, 900.0f, 1000.0f, "clear-noon"};
    return c;
}

} // namespace

TEST(RenderRules, LightDirectionsAndQuality) {
    const auto c = litCity();
    // cityLevel::SetLightDirection: (cos h cos p, sin p, sin h cos p).
    const auto env = makeEnvironment(c, TimeOfDay::Noon, Weather::Clear);
    EXPECT_NEAR(env.frame.lights[0].direction.x, std::cos(-1.0f), 1e-5f);
    EXPECT_NEAR(env.frame.lights[0].direction.y, std::sin(-1.0f), 1e-5f);
    EXPECT_NEAR(env.frame.lights[0].direction.z, 0.0f, 1e-5f);
    EXPECT_NEAR(env.frame.lights[1].direction.x, -1.0f, 1e-5f);
    // Quality 3: all lights, the file's ambient.
    EXPECT_NEAR(env.frame.ambient.x, 0x28 / 255.0f, 1e-5f);
    EXPECT_FLOAT_EQ(env.frame.lights[2].color.x, 0.25f);
    // Quality 1: the key light only; ambient 66% of the way to white.
    EnvironmentOptions low;
    low.lightQuality = 1;
    const auto dim = makeEnvironment(c, TimeOfDay::Noon, Weather::Clear, low);
    EXPECT_FLOAT_EQ(dim.frame.lights[1].color.x, 0.0f);
    EXPECT_FLOAT_EQ(dim.frame.lights[0].color.x, 1.0f);
    EXPECT_NEAR(dim.frame.ambient.x, (0x28 + ((255 - 0x28) * 168 >> 8)) / 255.0f, 1e-5f);
}

TEST(RenderRules, FogIsClampedByTheFarClip) {
    const auto c = litCity();
    EnvironmentOptions near;
    near.farClip = 400.0f;
    const auto env = makeEnvironment(c, TimeOfDay::Noon, Weather::Clear, near);
    // lvlSky::SetupFog: start min(far - 30, start), end min(far, end).
    EXPECT_FLOAT_EQ(env.frame.fogStart, 370.0f);
    EXPECT_FLOAT_EQ(env.frame.fogEnd, 400.0f);
    EXPECT_FLOAT_EQ(env.clearColor.x, 155.0f / 255.0f);
    const auto far = makeEnvironment(c, TimeOfDay::Noon, Weather::Clear);
    EXPECT_FLOAT_EQ(far.frame.fogStart, 900.0f);
    EXPECT_FLOAT_EQ(far.frame.fogEnd, 1000.0f);
}

TEST(RenderRules, WallShades) {
    const auto c = litCity();
    const auto env = makeEnvironment(c, TimeOfDay::Noon, Weather::Clear);
    // Entry 16 faces +X (angle 0): lit by fill1 (travelling -X) at 0.5.
    const std::uint32_t east = env.wallShades[16];
    EXPECT_EQ((east >> 16) & 0xFF, static_cast<std::uint32_t>(0x28 + 0.5f * 255.0f));
    // Entry 48 faces -X: the key light (travelling +X, cos 1) and fill2.
    const std::uint32_t west = env.wallShades[48];
    const float expected = 0x28 + std::cos(-1.0f) * 255.0f + 0.25f * 255.0f;
    EXPECT_EQ((west >> 16) & 0xFF, static_cast<std::uint32_t>(std::min(expected, 255.0f)));
    EXPECT_EQ(east >> 24, 0xFFu);
}

TEST(RenderRules, MissingLodsFillUpwards) {
    GpuModel m;
    GpuMesh vl, l, h;
    vl.lod = asset::Lod::VeryLow;
    l.lod = asset::Lod::Low;
    h.lod = asset::Lod::High;
    m.meshes = {vl, l, h};
    // M is missing: it takes L; H and VL are their own.
    EXPECT_EQ(findFilledLod(m, "", asset::Lod::Medium), &m.meshes[1]);
    EXPECT_EQ(findFilledLod(m, "", asset::Lod::High), &m.meshes[2]);
    EXPECT_EQ(findFilledLod(m, "", asset::Lod::VeryLow), &m.meshes[0]);
    GpuModel noVl;
    noVl.meshes = {h};
    EXPECT_EQ(findFilledLod(noVl, "", asset::Lod::VeryLow), nullptr); // nothing drawn
    EXPECT_EQ(findFilledLod(noVl, "", asset::Lod::Low), nullptr);
}

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
