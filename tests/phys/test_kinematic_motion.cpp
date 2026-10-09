// OpenMM2's moving kinematic instances (network cars): a body placed from
// outside the simulation that reports its motion strikes like a wall moving
// at that speed; one that does not is MM2's static wall.
#include "phys/Bound.h"
#include "phys/Collider.h"
#include "phys/World.h"

#include <gtest/gtest.h>

#include <memory>

using namespace mm2;
using namespace mm2::phys;

namespace {

struct BoxBody {
    Body body;
    std::unique_ptr<BoundBox> bound;
    BoxBody(const Vec3& size, float mass, const Vec3& position) {
        bound = std::make_unique<BoundBox>(size);
        bound->makeOwnMaterial();
        bound->setElasticity(0.5f);
        bound->setFriction(0.0f);
        body.ics.setMass(size.x, size.y, size.z, mass);
        body.ics.gravity = {0, 0, 0};
        body.collisionBound = bound.get();
        Mat34 m = Mat34::identity();
        m.m3 = position;
        body.place(m);
    }
};

// A kinematic box driven into a resting one at 10 m/s along +X; the struck
// box's speed afterwards.
float struckSpeed(bool reportsMotion) {
    World world;
    BoxBody a({1, 1, 1}, 1000.0f, {-1.1f, 5, 0});
    BoxBody b({1, 1, 1}, 100.0f, {0, 5, 0});
    a.body.kinematic = true;
    a.body.resetCollider();
    a.body.kinematicMoves = reportsMotion;
    a.body.kinematicVelocity = {10, 0, 0};
    world.add(&a.body);
    world.add(&b.body);
    constexpr float dt = 1.0f / 60.0f;
    for (int i = 0; i < 12; ++i) {
        Mat34 m = a.body.ics.matrix;
        m.m3.x += 10.0f * dt;
        a.body.place(m);
        world.step(dt);
    }
    return b.body.ics.linearVelocity.x;
}

} // namespace

TEST(KinematicMotion, AMovingKinematicBodyStrikesAtItsSpeed) {
    // MM2's static wall: the struck box is only pushed out of the way.
    EXPECT_LT(struckSpeed(false), 1.0f);
    // Moving: it takes the kinematic body's speed and more (the bounce).
    EXPECT_GT(struckSpeed(true), 9.5f);
}

TEST(KinematicMotion, ColliderVelocityIsTheReportedMotion) {
    Collider c;
    BoundBox box({1, 1, 1});
    Mat34 m = Mat34::identity();
    c.initStatic(&box, &m);
    EXPECT_EQ(c.localVelocity({1, 2, 3}).mag2(), 0.0f);
    c.moving = true;
    c.motionVelocity = {1, 0, 0};
    c.motionSpin = {0, 2, 0}; // about +Y
    c.motionCentre = {0, 0, 0};
    const Vec3 v = c.localVelocity({0, 0, -1}); // spin x r = (0,2,0) x (0,0,-1) = (-2,0,0)
    EXPECT_NEAR(v.x, -1.0f, 1e-6f);
    EXPECT_NEAR(v.y, 0.0f, 1e-6f);
    EXPECT_NEAR(v.z, 0.0f, 1e-6f);
    c.initStatic(&box, &m); // a new init forgets it
    EXPECT_FALSE(c.moving);
}
