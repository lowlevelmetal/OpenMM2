// OpenMM2's network prediction support in the physics: a car's state saved
// and put back (CarSim::saveState / restoreState), a sample of one car on
// its own (World::replaySample), the sample hooks and the car's own random
// stream. See docs/multiplayer.md, "Players' cars".
#include "phys/Bound.h"
#include "phys/World.h"
#include "phys/vehicle/CarSim.h"
#include "phys/vehicle/Controls.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <memory>

using namespace mm2;
using namespace mm2::phys;

namespace {

// vpbug's wheels and body box (geometry/vpbug_whl*.mtx, bound/vpbug_bound.bnd).
VehicleGeometry bugGeometry() {
    VehicleGeometry g;
    const float x[4] = {-0.754f, 0.754f, -0.754f, 0.754f};
    const float z[4] = {-1.184f, -1.184f, 1.236f, 1.236f};
    for (int i = 0; i < 4; ++i)
        g.wheels[static_cast<std::size_t>(i)] = {{x[i], 0.307f, z[i]}, 0.336f, 0.255f, true};
    g.body = Aabb{{-0.92f, 0.245f, -1.98f}, {0.94f, 1.56f, 2.01f}};
    return g;
}

PolygonSoup flatGround(float half = 2000.0f) {
    SoupGeometry g;
    g.vertices = {{-half, 0, -half}, {-half, 0, half}, {half, 0, half}, {half, 0, -half}};
    g.polys.push_back({{0, 1, 2, 3}, 4, 0});
    g.materialNames = {"default"};
    PolygonSoup soup;
    soup.add(g, Mat34::identity(), MaterialTable{});
    soup.finalize(2048.0f);
    return soup;
}

std::unique_ptr<CarSim> makeCar(World& world, const Vec3& at) {
    auto car = std::make_unique<CarSim>();
    car->init(CarSimParams{}, bugGeometry());
    Mat34 m = Mat34::identity();
    m.m3 = at;
    car->reset(m);
    world.add(&car->body);
    return car;
}

// The inputs of sample `i` of a test drive: throttle, then a turn, then the
// brakes.
PedalInput drive(int i) {
    PedalInput in;
    in.accelerator = i < 120 ? 1.0f : 0.6f;
    in.steering = i >= 90 && i < 150 ? 0.4f : 0.0f;
    in.brake = i >= 170 ? 0.8f : 0.0f;
    return in;
}

bool same(const Mat34& a, const Mat34& b) { return std::memcmp(&a, &b, sizeof(Mat34)) == 0; }
bool same(const Vec3& a, const Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

void expectSameCar(const CarSim& a, const CarSim& b) {
    EXPECT_TRUE(same(a.body.ics.matrix, b.body.ics.matrix));
    EXPECT_TRUE(same(a.body.ics.linearVelocity, b.body.ics.linearVelocity));
    EXPECT_TRUE(same(a.body.ics.angularVelocity, b.body.ics.angularVelocity));
    for (std::size_t w = 0; w < 4; ++w) {
        EXPECT_EQ(a.wheels[w].rotationSpeed, b.wheels[w].rotationSpeed);
        EXPECT_EQ(a.wheels[w].suspension, b.wheels[w].suspension);
    }
    EXPECT_EQ(a.engine.rpm, b.engine.rpm);
    EXPECT_EQ(a.trans.currentGear, b.trans.currentGear);
}

} // namespace

TEST(NetPrediction, ARestoredCarRunsTheSameSamplesAgain) {
    World world;
    world.setStatic(flatGround());
    auto car = makeCar(world, {0, 0.6f, 0});
    ArcadeControls controls;
    const float dt = kFixedSampleStep;
    for (int i = 0; i < 60; ++i) {
        controls.apply(*car, drive(i));
        world.step(dt);
    }
    const CarSimState saved = car->saveState();
    const ArcadeControls savedControls = controls;
    for (int i = 60; i < 200; ++i) {
        controls.apply(*car, drive(i));
        world.step(dt);
    }
    const CarSimState first = car->saveState();
    EXPECT_GT(car->body.ics.matrix.m3.dist(Vec3{0, 0.6f, 0}), 5.0f); // it drove

    car->restoreState(saved);
    controls = savedControls;
    for (int i = 60; i < 200; ++i) {
        controls.apply(*car, drive(i));
        world.step(dt);
    }
    CarSim other;
    other.init(CarSimParams{}, bugGeometry());
    other.restoreState(first);
    expectSameCar(*car, other);
}

TEST(NetPrediction, ReplayingACarAloneIsTheWorldsSample) {
    // With nothing else around, replaySample is the world's own sample.
    World world;
    world.setStatic(flatGround());
    auto car = makeCar(world, {0, 0.6f, 0});
    ArcadeControls controls;
    const float dt = kFixedSampleStep;
    for (int i = 0; i < 30; ++i) {
        controls.apply(*car, drive(i));
        world.step(dt);
    }
    const CarSimState saved = car->saveState();
    const ArcadeControls savedControls = controls;
    for (int i = 30; i < 200; ++i) {
        controls.apply(*car, drive(i));
        world.step(dt);
    }
    const CarSimState stepped = car->saveState();

    car->restoreState(saved);
    controls = savedControls;
    Body* bodies[] = {&car->body};
    for (int i = 30; i < 200; ++i) {
        controls.apply(*car, drive(i));
        world.replaySample(bodies, dt);
    }
    CarSim other;
    other.init(CarSimParams{}, bugGeometry());
    other.restoreState(stepped);
    expectSameCar(*car, other);
}

TEST(NetPrediction, AReplayedCarPushesNothing) {
    // A replayed car that runs into another one bounces off it, while the
    // other one stays exactly where it was, at rest.
    World world;
    world.setStatic(flatGround());
    auto car = makeCar(world, {0, 0.6f, 0});
    auto parked = makeCar(world, {0, 0.6f, -8.0f});
    const float dt = kFixedSampleStep;
    for (int i = 0; i < 30; ++i)
        world.step(dt); // both settle
    const CarSimState parkedBefore = parked->saveState();
    ArcadeControls controls;
    Body* bodies[] = {&car->body};
    float slowest = 1e9f;
    bool moving = false;
    for (int i = 0; i < 240; ++i) {
        controls.apply(*car, {1.0f, 0.0f, 0.0f, 0.0f});
        world.replaySample(bodies, dt);
        const float v = -car->body.ics.linearVelocity.z;
        moving = moving || v > 3.0f;
        if (moving)
            slowest = std::min(slowest, v);
    }
    EXPECT_TRUE(moving);
    EXPECT_LT(slowest, 3.0f); // it hit the parked car
    EXPECT_GT(car->body.ics.matrix.m3.z, -8.0f);
    EXPECT_TRUE(same(parked->body.ics.matrix, parkedBefore.ics.matrix));
    EXPECT_TRUE(same(parked->body.ics.linearVelocity, parkedBefore.ics.linearVelocity));
}

TEST(NetPrediction, TheSampleHooksRunAroundEverySample) {
    World world;
    int before = 0, after = 0;
    bool inOrder = true;
    world.setSampleHooks([&] { inOrder = inOrder && before++ == after; },
                         [&] { inOrder = inOrder && ++after == before; });
    const int n = world.advanceFixed(0.051f);
    EXPECT_EQ(n, 3);
    EXPECT_EQ(before, 3);
    EXPECT_EQ(after, 3);
    EXPECT_TRUE(inOrder);
}

TEST(NetPrediction, ACarWithItsOwnStreamLeavesTheWorldsAlone) {
    // Bumpy ground draws numbers for the wheels: with its own stream the car
    // runs alike whatever else draws from the world's.
    auto run = [](std::uint32_t worldSeed) {
        World world;
        world.setStatic(flatGround());
        world.seedRandom(worldSeed);
        auto car = makeCar(world, {0, 0.6f, 0});
        car->ownRandom = true;
        car->randomState = 7;
        ArcadeControls controls;
        for (int i = 0; i < 200; ++i) {
            controls.apply(*car, drive(i));
            world.step(kFixedSampleStep);
        }
        return std::pair{car->saveState(), *world.randomSeed()};
    };
    const auto [a, seedA] = run(1);
    const auto [b, seedB] = run(12345);
    EXPECT_TRUE(same(a.ics.matrix, b.ics.matrix));
    EXPECT_EQ(seedA, 1u);
    EXPECT_EQ(seedB, 12345u);
}
