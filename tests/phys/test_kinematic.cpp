// Kinematic bodies (the network players' cars, placed every frame from their
// snapshots): what a simulated body that touches one takes from it.
#include "phys/Bound.h"
#include "phys/World.h"

#include <gtest/gtest.h>

#include <memory>

using namespace mm2;
using namespace mm2::phys;

namespace {

struct Box {
    Body body;
    std::unique_ptr<BoundBox> bound;
    Box(float mass, const Vec3& position) {
        bound = std::make_unique<BoundBox>(Vec3{1, 1, 1});
        bound->makeOwnMaterial();
        bound->setElasticity(0.5f);
        bound->setFriction(1.0f);
        body.ics.setMass(1, 1, 1, mass);
        body.ics.gravity = {0, 0, 0};
        body.collisionBound = bound.get();
        Mat34 m = Mat34::identity();
        m.m3 = position;
        body.place(m);
    }
};

// A simulated box catches up with a kinematic box that moves at `ahead`
// m/s along +X (placed at each sample as RaceScreen places a remote car each
// frame); returns the simulated box's speed once they have touched.
float speedAfterCatchingUp(float ahead, float behind) {
    World world;
    Box chaser(1000.0f, {-1.06f, 5, 0});
    Box lead(1000.0f, {0, 5, 0});
    chaser.body.ics.linearVelocity = {behind, 0, 0};
    chaser.body.ics.linearMomentum = {behind * 1000.0f, 0, 0};
    lead.body.kinematic = true;
    lead.body.resetCollider();
    world.add(&chaser.body);
    world.add(&lead.body);
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 30; ++i) {
        Mat34 m = Mat34::identity();
        m.m3 = {ahead * dt * static_cast<float>(i), 5, 0};
        lead.body.place(m);
        lead.body.ics.linearVelocity = {ahead, 0, 0};
        world.step(dt);
    }
    return chaser.body.ics.linearVelocity.x;
}

} // namespace

TEST(Kinematic, TouchingAMovingKinematicBodyTakesTheRelativeSpeed) {
    // Closing at 1 m/s on a car doing 30 m/s: the chaser ends up a little
    // slower than the car in front (it bounces off at the relative speed),
    // not thrown back as from a wall at 31 m/s.
    const float v = speedAfterCatchingUp(30.0f, 31.0f);
    EXPECT_GT(v, 28.0f);
    EXPECT_LE(v, 30.0f);
}

TEST(Kinematic, AStillKinematicBodyIsAWall) {
    // At rest, a kinematic body is hit like a wall: the 5 m/s chaser stops.
    const float v = speedAfterCatchingUp(0.0f, 5.0f);
    EXPECT_LT(v, 0.5f);
}
