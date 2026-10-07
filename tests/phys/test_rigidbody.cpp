#include "phys/AgeMath.h"
#include "phys/Bound.h"
#include "phys/Collide.h"
#include "phys/InertialCS.h"
#include "phys/Material.h"
#include "phys/World.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace mm2;
using namespace mm2::phys;

TEST(AgeMath, InvSqrtFastIsAccurate) {
    for (float x : {1e-6f, 0.01f, 0.5f, 1.0f, 2.0f, 3.14159f, 100.0f, 12345.678f, 1e6f}) {
        const float expected = 1.0f / std::sqrt(x);
        EXPECT_NEAR(age::invSqrtFast(x), expected, expected * 2e-6f) << x;
    }
    EXPECT_EQ(age::invSqrtFast(0.0f), 0.0f);
}

TEST(AgeMath, AxisRotationsMatchArbitraryPath) {
    const float angle = 0.7f;
    for (const Vec3& axis : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}, Vec3{-1, 0, 0}, Vec3{0, -1, 0}}) {
        Mat34 a = Mat34::rotationAxis(Vec3{0.3f, 0.8f, -0.52f}.normalized(), 0.4f);
        Mat34 b = a;
        age::rotate(a, axis, angle);
        const Mat34 r = age::arbitraryRotation(axis, angle);
        b.m0 = r.transformDir(b.m0);
        b.m1 = r.transformDir(b.m1);
        b.m2 = r.transformDir(b.m2);
        for (int i = 0; i < 3; ++i)
            EXPECT_LT(a.row(i).dist(b.row(i)), 1e-5f);
    }
}

TEST(AgeMath, RotationConventionMatchesMat34) {
    // Rotating rows about world Y by +a equals Mat34::rotationY.
    Mat34 m;
    age::rotate(m, {0, 1, 0}, 0.3f);
    const Mat34 r = Mat34::rotationY(0.3f);
    for (int i = 0; i < 3; ++i)
        EXPECT_LT(m.row(i).dist(r.row(i)), 1e-6f);
}

TEST(InertialCS, BoxInertia) {
    InertialCS ics;
    ics.setMass(2.0f, 1.0f, 4.0f, 1200.0f);
    EXPECT_FLOAT_EQ(ics.inertia.x, 1200.0f / 12.0f * (1.0f + 16.0f));
    EXPECT_FLOAT_EQ(ics.inertia.y, 1200.0f / 12.0f * (4.0f + 16.0f));
    EXPECT_FLOAT_EQ(ics.inertia.z, 1200.0f / 12.0f * (4.0f + 1.0f));
}

TEST(InertialCS, FreeFallIsSemiImplicitEuler) {
    InertialCS ics;
    ics.setMass(1, 1, 1, 10.0f);
    ics.gravity = {0, -10.0f, 0};
    const float dt = 0.01f;
    float v = 0, y = 0;
    for (int i = 0; i < 100; ++i) {
        ics.update(dt, 1.0f / dt);
        v += -10.0f * dt;
        y += v * dt;
    }
    EXPECT_NEAR(ics.linearVelocity.y, -10.0f, 1e-3f);
    EXPECT_NEAR(ics.matrix.m3.y, y, 1e-3f);
}

TEST(InertialCS, FreeSpinConservesAngularMomentum) {
    InertialCS ics;
    ics.setMass(1.0f, 2.0f, 3.0f, 100.0f);
    ics.gravity = {};
    ics.angularMomentum = {5.0f, 20.0f, -3.0f};
    const float l0 = ics.angularMomentum.mag();
    for (int i = 0; i < 600; ++i)
        ics.update(1.0f / 60.0f, 60.0f);
    EXPECT_NEAR(ics.angularMomentum.mag(), l0, 1e-4f * l0);
    // The basis stays orthonormal.
    EXPECT_NEAR(ics.matrix.m0.mag(), 1.0f, 1e-4f);
    EXPECT_NEAR(ics.matrix.m0.dot(ics.matrix.m1), 0.0f, 1e-4f);
}

TEST(InertialCS, AngularVelocityLimit) {
    InertialCS ics;
    ics.setMass(1, 1, 1, 6.0f);
    ics.gravity = {};
    ics.limitAngVelocity = true;
    ics.maxAngVelocity = 2.0f;
    ics.angularMomentum = {0, 100.0f, 0};
    ics.update(0.01f, 100.0f);
    EXPECT_NEAR(ics.angularVelocity.mag(), 2.0f, 1e-4f);
}

TEST(InertialCS, PushesDoNotStack) {
    InertialCS ics;
    ics.applyPush({0, 0.1f, 0});
    ics.applyPush({0, 0.1f, 0});  // same direction: already covered
    ics.applyPush({0, 0.15f, 0}); // only the extra 0.05 is added
    EXPECT_NEAR(ics.linearPush.y, 0.15f, 1e-6f);
    ics.applyPush({0.2f, 0, 0}); // perpendicular: added fully
    EXPECT_NEAR(ics.linearPush.x, 0.2f, 1e-6f);
}

TEST(InertialCS, SleepsWhenStill) {
    InertialCS ics;
    ics.state = InertialCS::Awake;
    ics.gravity = {};
    for (int i = 0; i < 100; ++i)
        ics.update(0.02f, 50.0f);
    EXPECT_EQ(ics.state, InertialCS::Asleep);
}

TEST(Material, ParsesMtlBlocks) {
    const char* text = "mtl grass {\n\telasticity: 0.9\n\tfriction: 0.8 \n\teffect: none\n\tsound: 2\n"
                       "\tdrag: 0.1\n\twidth: 0.45\n\theight: 0.04\n\tdepth: 0.1\n\tptxindex: 1 2\n"
                       "\tptxthreshold: 0.25 0.5\n}\nmtl water {\n friction: 0.68\n}\n";
    std::string err;
    auto mats = parseMaterials(text, &err);
    ASSERT_TRUE(mats) << err;
    ASSERT_EQ(mats->size(), 2u);
    EXPECT_EQ((*mats)[0].name, "grass");
    EXPECT_FLOAT_EQ((*mats)[0].friction, 0.8f);
    EXPECT_EQ((*mats)[0].sound, 2);
    EXPECT_EQ((*mats)[0].ptxIndex[1], 2);
    MaterialTable table;
    table.add(*mats);
    EXPECT_EQ(table.resolve("WATER"), table.find("water"));
    EXPECT_EQ(table.resolve("none"), 0);
}

namespace {

PolygonSoup groundSoup(float half = 100.0f) {
    BoundGeometry g;
    g.vertices = {{-half, 0, -half}, {-half, 0, half}, {half, 0, half}, {half, 0, -half}};
    g.polys.push_back({{0, 1, 2, 3}, 4, 0});
    g.materialNames = {"_default"};
    PolygonSoup soup;
    soup.add(g, Mat34::identity(), MaterialTable{});
    soup.finalize(32.0f);
    return soup;
}

} // namespace

TEST(Bound, RaycastHitsGround) {
    PolygonSoup soup = groundSoup();
    RayHit hit;
    ASSERT_TRUE(soup.raycast({1, 5, 2}, {1, -5, 2}, hit));
    EXPECT_NEAR(hit.t, 0.5f, 1e-6f);
    EXPECT_NEAR(hit.normal.y, 1.0f, 1e-6f);
    EXPECT_FALSE(soup.raycast({1, 5, 2}, {1, 1, 2}, hit));
    EXPECT_FALSE(soup.raycast({500, 5, 2}, {500, -5, 2}, hit));
}

TEST(Collide, BoxRestingOnPolygon) {
    PolygonSoup soup = groundSoup();
    Obb box;
    box.center = {0, 0.45f, 0};
    box.half = {1, 0.5f, 2};
    Contact c[kMaxContacts];
    const int n = collideObbPolygon(box, soup.polygon(0), c, kMaxContacts);
    ASSERT_EQ(n, 4);
    for (int i = 0; i < n; ++i) {
        EXPECT_NEAR(c[i].depth, 0.05f, 1e-5f);
        EXPECT_NEAR(c[i].normal.y, 1.0f, 1e-6f);
    }
}

TEST(Collide, BoxBoxSeparatesAlongShortestAxis) {
    Obb a, b;
    a.center = {0, 0, 0};
    b.center = {0.9f, 0.05f, 0};
    Contact c[kMaxContacts];
    const int n = collideObbObb(a, b, c, kMaxContacts);
    ASSERT_GT(n, 0);
    EXPECT_NEAR(c[0].normal.x, -1.0f, 1e-5f);
    EXPECT_NEAR(c[0].depth, 0.1f, 1e-4f);
}

TEST(World, DroppedBoxComesToRestOnGround) {
    World world;
    world.setStatic(groundSoup());
    Body box;
    box.shape.half = {0.5f, 0.5f, 0.5f};
    box.ics.setMass(1, 1, 1, 100.0f);
    box.ics.matrix.m3 = {0, 3.0f, 0};
    box.ics.elasticity = 0.3f;
    box.ics.friction = 0.8f;
    world.add(&box);
    for (int i = 0; i < 600; ++i)
        world.step(1.0f / 60.0f);
    EXPECT_NEAR(box.ics.matrix.m3.y, 0.5f, 0.02f);
    EXPECT_LT(box.ics.linearVelocity.mag(), 0.05f);
}

TEST(World, BounceUsesElasticity) {
    World world;
    world.setStatic(groundSoup());
    Body ball;
    ball.shape.kind = Shape::Kind::Sphere;
    ball.shape.radius = 0.5f;
    ball.ics.setMass(1, 1, 1, 10.0f);
    ball.ics.matrix.m3 = {0, 0.6f, 0};
    ball.ics.linearMomentum = {0, -50.0f, 0}; // 5 m/s down
    ball.ics.linearVelocity = {0, -5.0f, 0};
    ball.ics.elasticity = 0.5f;
    world.add(&ball);
    float maxUp = 0;
    for (int i = 0; i < 60; ++i) {
        world.step(1.0f / 120.0f);
        maxUp = std::max(maxUp, ball.ics.linearVelocity.y);
    }
    // Ground _default elasticity 0.9 * 0.5 = 0.45 restitution; gravity eats a bit.
    EXPECT_GT(maxUp, 1.5f);
    EXPECT_LT(maxUp, 2.6f);
}

TEST(World, OversampleSplitsFrames) {
    World world;
    EXPECT_EQ(world.advanceOversampled(1.0f / 60.0f), 1);
    EXPECT_EQ(world.advanceOversampled(1.0f / 20.0f), 2); // 0.05 / (1/35) = 1.75 -> 2
    EXPECT_EQ(world.advanceOversampled(2.0f), 20);        // MaxSamples
    EXPECT_EQ(world.advanceFixed(0.051f), 3);
}
