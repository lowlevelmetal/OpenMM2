#include "phys/AgeMath.h"
#include "phys/PolygonSoup.h"
#include "phys/InertialCS.h"
#include "phys/Material.h"
#include "phys/World.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace mm2;
using namespace mm2::phys;

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
    ics.setMaxAngVelocity(2.0f);
    ics.angularMomentum = {0, 100.0f, 0};
    ics.update(0.01f, 100.0f);
    EXPECT_NEAR(ics.angularVelocity.mag(), 2.0f, 1e-4f);
    // phInertialCS limits each body axis on its own, keeping the momentum
    // consistent with the limited velocity.
    EXPECT_NEAR(ics.angularMomentum.y, 2.0f * ics.inertia.y, 1e-4f);
}

TEST(InertialCS, ContactStiffnessIsImplicit) {
    // A 1000 kg body on a stiff, damped vertical spring (1e8 N/m, 4e5 N s/m),
    // stepped at 1/60 s (omega * dt = 5.3): an explicit step diverges;
    // phInertialCS's step, implicit in the contact matrix, settles. The
    // matrix carries d(force)/d(velocity) = damping + dt * stiffness, as
    // vehWheel passes it.
    InertialCS ics;
    ics.setMass(1, 1, 1, 1000.0f);
    ics.gravity = {};
    ics.matrix.m3 = {0, 0.1f, 0};
    const float stiffness = 1.0e8f, damping = 4.0e5f, dt = 1.0f / 60.0f;
    for (int i = 0; i < 120; ++i) {
        const float y = ics.matrix.m3.y, v = ics.linearVelocity.y;
        Mat34 k{{}, {0, damping + dt * stiffness, 0}, {}, {}};
        ics.applyContactForce({0, -stiffness * y - damping * v, 0}, ics.matrix.m3, k);
        ics.update(dt, 1.0f / dt);
        ASSERT_LT(std::abs(ics.matrix.m3.y), 0.1f);
    }
    EXPECT_LT(std::abs(ics.matrix.m3.y), 1e-3f);
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
    SoupGeometry g;
    g.vertices = {{-half, 0, -half}, {-half, 0, half}, {half, 0, half}, {half, 0, -half}};
    g.polys.push_back({{0, 1, 2, 3}, 4, 0});
    g.materialNames = {"_default"};
    PolygonSoup soup;
    soup.add(g, Mat34::identity(), MaterialTable{});
    soup.finalize(32.0f);
    return soup;
}

} // namespace

TEST(PolygonSoup, RaycastHitsGround) {
    PolygonSoup soup = groundSoup();
    RayHit hit;
    ASSERT_TRUE(soup.raycast({1, 5, 2}, {1, -5, 2}, hit));
    EXPECT_NEAR(hit.t, 0.5f, 1e-6f);
    EXPECT_NEAR(hit.normal.y, 1.0f, 1e-6f);
    EXPECT_FALSE(soup.raycast({1, 5, 2}, {1, 1, 2}, hit));
    EXPECT_FALSE(soup.raycast({500, 5, 2}, {500, -5, 2}, hit));
}

TEST(World, OversampleSplitsFrames) {
    World world;
    // dgPhysManager::Update with mmGame::Init's settings:
    // ceil((frame - 0.001) / (1/35)), at most 3.
    EXPECT_EQ(world.advanceOversampled(1.0f / 60.0f), 1);
    EXPECT_EQ(world.advanceOversampled(1.0f / 30.0f), 2);
    EXPECT_EQ(world.advanceOversampled(1.0f / 20.0f), 2);
    EXPECT_EQ(world.advanceOversampled(1.0f / 10.0f), 3);
    EXPECT_EQ(world.advanceOversampled(2.0f), 3); // MaxSamples
    EXPECT_EQ(world.advanceFixed(0.051f), 3);
}
