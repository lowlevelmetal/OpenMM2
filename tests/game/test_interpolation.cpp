// Drawing between simulation steps (game/Interpolation.h): the blends, when
// an object is drawn where it is instead, and that keeping the history
// leaves the simulation as it was, whatever the frame times.
#include "TestData.h"
#include "ai/World.h"
#include "city/CityData.h"
#include "game/Interpolation.h"
#include "game/PlayerVehicle.h"
#include "game/fx/Particles.h"
#include "game/net/TrafficSync.h"
#include "net/Protocol.h"
#include "phys/AgeMath.h"
#include "phys/World.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

using namespace mm2;
using game::blendAngle;
using game::blendPose;
using game::blendTransform;
using game::Drawn;
using game::drawnKey;
using game::StepHistory;

namespace {

Mat34 placed(float yaw, const Vec3& at) {
    Mat34 m = Mat34::rotationY(yaw);
    m.m3 = at;
    return m;
}

void expectNear(const Mat34& a, const Mat34& b, float eps, const char* what) {
    for (int i = 0; i < 4; ++i)
        EXPECT_LT((a.row(i) - b.row(i)).mag(), eps) << what << " row " << i;
}

// A wheel as vehWheel::Update leaves it: its unspun frame turned about its
// own axle by the accumulated turn (Matrix34::Rotate).
Mat34 spunWheel(const Mat34& unspun, float turn) {
    Mat34 m = unspun;
    const Vec3 axle = m.m0;
    phys::age::rotate(m, axle, turn);
    return m;
}

} // namespace

TEST(Interpolation, BlendTransformEndsExactlyAndMovesBetween) {
    const Mat34 a = placed(0.0f, {0, 0, 0});
    const Mat34 b = placed(1.0f, {10, 2, -4});
    EXPECT_EQ(blendTransform(a, b, 0.0f).m3, a.m3);
    EXPECT_EQ(blendTransform(a, b, -1.0f).m0, a.m0);
    const Mat34 end = blendTransform(a, b, 1.0f);
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(end.row(i), b.row(i)); // exactly the live state once a step is due
    // Half way: half the way there, half the turn.
    expectNear(blendTransform(a, b, 0.5f), placed(0.5f, {5, 1, -2}), 1e-5f, "half");
    expectNear(blendTransform(a, b, 0.25f), placed(0.25f, {2.5f, 0.5f, -1}), 1e-5f, "quarter");
    // A scaled instance keeps its scale.
    Mat34 s = a;
    s.m0 = s.m0 * 2.0f;
    s.m1 = s.m1 * 2.0f;
    s.m2 = s.m2 * 2.0f;
    EXPECT_NEAR(blendTransform(s, s, 0.5f).m1.mag(), 2.0f, 1e-5f);
}

TEST(Interpolation, BlendAngleTakesTheShortWayRound) {
    EXPECT_FLOAT_EQ(blendAngle(1.0f, 2.0f, 0.5f, 6.28f), 1.5f);
    // A tyre's turn that wrapped from 6.2 to 0.1 went on by 0.18.
    EXPECT_NEAR(blendAngle(6.2f, 0.1f, 0.5f, 6.28f), 6.29f, 1e-5f);
    EXPECT_FLOAT_EQ(blendAngle(6.2f, 0.1f, 1.0f, 6.28f), 0.1f);
}

TEST(Interpolation, JumpsAreTooFarOrTooMuchTurnForOneStep) {
    const game::JumpLimits limits{5.0f, 1.6f};
    const Mat34 a = placed(0.0f, {0, 0, 0});
    EXPECT_FALSE(game::isJump(a, placed(0.3f, {2, 0, 0}), limits)); // 120 m/s, 18 rad/s
    EXPECT_TRUE(game::isJump(a, placed(0.0f, {6, 0, 0}), limits));  // a respawn down the road
    EXPECT_TRUE(game::isJump(a, placed(3.0f, {0, 0, 0}), limits));  // turned round where it was
}

// vehWheel's turn is accumulated: a wheel turning 4 rad in a step (69 m/s on
// a 0.29 m wheel) is drawn half way round, not the short way backwards.
TEST(Interpolation, FastWheelsTurnForwards) {
    game::VehiclePose from, to;
    from.body = placed(0.0f, {0, 0, 0});
    to.body = placed(0.05f, {0, 0, -1.15f});
    const Mat34 pivot = Mat34::translation({0.8f, 0.3f, -1.2f});
    from.wheelTurn[0] = 10.0f;
    to.wheelTurn[0] = 14.0f;
    from.wheelWorld[0] = spunWheel(pivot * from.body, from.wheelTurn[0]);
    to.wheelWorld[0] = spunWheel(pivot * to.body, to.wheelTurn[0]);
    from.wheelValid[0] = to.wheelValid[0] = true;
    from.hasWheelWorld = to.hasWheelWorld = true;
    from.hasWheelTurn = to.hasWheelTurn = true;
    to.brakeLights = true;

    const game::VehiclePose half = blendPose(from, to, 0.5f);
    const Mat34 body = blendTransform(from.body, to.body, 0.5f);
    expectNear(half.body, body, 1e-6f, "body");
    expectNear(half.wheelWorld[0], spunWheel(pivot * body, 12.0f), 1e-4f, "wheel");
    EXPECT_TRUE(half.brakeLights); // the flags are the live state's
    // Without the turns (a traffic car's cheap wheels) the matrices are
    // blended the short way.
    from.hasWheelTurn = false;
    const game::VehiclePose shortWay = blendPose(from, to, 0.5f);
    EXPECT_GT((shortWay.wheelWorld[0].m1 - half.wheelWorld[0].m1).mag(), 0.5f);
    // The wheel keeps its place on the body.
    EXPECT_LT((shortWay.wheelWorld[0].m3 - (pivot * body).m3).mag(), 1e-4f);
}

TEST(Interpolation, PlacePoseCarriesTheWheels) {
    game::VehiclePose p;
    p.body = placed(0.3f, {4, 0, 1});
    p.wheelWorld[2] = Mat34::translation({-0.8f, 0.3f, 1.3f}) * p.body;
    p.hasWheelWorld = true;
    const Mat34 there = placed(1.1f, {-20, 3, 7});
    const game::VehiclePose q = game::placePose(p, there);
    expectNear(q.body, there, 1e-6f, "body");
    expectNear(q.wheelWorld[2], Mat34::translation({-0.8f, 0.3f, 1.3f}) * there, 1e-4f, "wheel");
}

TEST(StepHistory, BlendsFromTheStateBeforeTheLastStep) {
    StepHistory h;
    const auto key = drawnKey(Drawn::Prop, 7);
    const Mat34 before = placed(0.0f, {0, 0, 0});
    const Mat34 now = placed(0.2f, {1, 0, 0});
    // Nothing recorded: drawn where it is.
    h.setAlpha(0.25f);
    EXPECT_EQ(h.transform(key, now).m3, now.m3);
    EXPECT_FALSE(h.blends(key, now));
    h.beginStep();
    h.record(key, before);
    // The step ran (the object is at `now`); a quarter of the next one has
    // gone by: drawn a quarter of the way from where it was.
    expectNear(h.transform(key, now), blendTransform(before, now, 0.25f), 1e-6f, "quarter");
    EXPECT_TRUE(h.blends(key, now));
    h.setAlpha(0.0f);
    expectNear(h.transform(key, now), before, 1e-6f, "start");
    h.setAlpha(2.0f); // clamped
    EXPECT_FLOAT_EQ(h.alpha(), 1.0f);
    EXPECT_EQ(h.transform(key, now).m3, now.m3);
    // Recorded at an earlier step but not the last one (it was not
    // simulated since): drawn where it is, and forgotten a step later.
    h.setAlpha(0.5f);
    h.beginStep();
    EXPECT_EQ(h.transform(key, now).m3, now.m3);
    h.beginStep();
    EXPECT_EQ(h.size(), 0u);
}

TEST(StepHistory, DiscontinuitiesAreDrawnWhereTheyAre) {
    StepHistory h;
    h.setAlpha(0.5f);
    const auto car = drawnKey(Drawn::Opponent, 0);
    const auto rail = drawnKey(Drawn::RailCar, 3);
    h.beginStep();
    h.record(car, placed(0.0f, {0, 0, 0}), 4);
    h.record(rail, placed(0.0f, {50, 0, 0}), 1, 6.0f);
    // A reset (the car's reset count went up): not blended across.
    EXPECT_EQ(h.transform(car, placed(0.0f, {1, 0, 0}), 5).m3, (Vec3{1, 0, 0}));
    EXPECT_NEAR(h.transform(car, placed(0.0f, {1, 0, 0}), 4).m3.x, 0.5f, 1e-6f);
    // A recycled traffic slot (another spawn): neither its place nor its
    // tyres blended.
    EXPECT_EQ(h.transform(rail, placed(0.0f, {-300, 0, 9}), 2).m3, (Vec3{-300, 0, 9}));
    EXPECT_FLOAT_EQ(h.turn(rail, placed(0.0f, {-300, 0, 9}), 0.5f, 6.28f, 2), 0.5f);
    EXPECT_NEAR(h.turn(rail, placed(0.0f, {50.5f, 0, 0}), 0.2f, 6.28f, 1), 6.24f, 1e-5f);
    // Moved further than a step allows (a teleport nobody counted).
    EXPECT_EQ(h.transform(car, placed(0.0f, {30, 0, 0}), 4).m3, (Vec3{30, 0, 0}));
    // A race restart forgets everything.
    h.clear();
    EXPECT_EQ(h.transform(car, placed(0.0f, {1, 0, 0}), 4).m3, (Vec3{1, 0, 0}));
}

TEST(StepHistory, PosesSnapOnAReset) {
    StepHistory h;
    h.setAlpha(0.5f);
    game::VehiclePose a, b;
    a.body = placed(0.0f, {0, 0, 0});
    b.body = placed(0.0f, {0, 0, -1});
    h.beginStep();
    h.record(drawnKey(Drawn::Player, 0), a, 1);
    EXPECT_NEAR(h.pose(drawnKey(Drawn::Player, 0), b, 1).body.m3.z, -0.5f, 1e-6f);
    EXPECT_EQ(h.pose(drawnKey(Drawn::Player, 0), b, 2).body.m3, b.body.m3);
}

// phys::World calls its observer once at the start of every sample: what
// it records there is the state the drawing blends from.
TEST(StepHistory, PhysicsObserverRecordsEachSampleBeforeItRuns) {
    phys::World world;
    phys::Body body;
    body.ics.setMass(1, 1, 1, 10.0f);
    body.collideTerrain = body.collideInstances = body.collideMovers = false;
    Mat34 m = Mat34::identity();
    m.m3 = {0, 100, 0};
    body.place(m);
    world.add(&body);
    StepHistory h;
    int samples = 0;
    world.setStepObserver([&] {
        ++samples;
        h.beginStep();
        h.record(drawnKey(Drawn::Prop, 0), body.ics.matrix);
    });
    EXPECT_EQ(world.advanceFixed(1.0f / 144.0f), 0);
    EXPECT_EQ(samples, 0);
    std::vector<float> heights{body.ics.matrix.m3.y};
    int total = 0;
    for (int frame = 0; frame < 20; ++frame) {
        const int n = world.advanceFixed(1.0f / 144.0f);
        total += n;
        EXPECT_EQ(samples, total) << "one call per sample";
        if (n > 0)
            heights.push_back(body.ics.matrix.m3.y);
        h.setAlpha(world.interpolationAlpha());
        const float drawn = h.transform(drawnKey(Drawn::Prop, 0), body.ics.matrix).m3.y;
        if (heights.size() >= 2) {
            const float a = heights[heights.size() - 2], b = heights.back();
            EXPECT_NEAR(drawn, a + (b - a) * h.alpha(), 1e-4f) << frame;
        }
    }
    EXPECT_GT(samples, 5);
}

namespace {

// A flat floor 2 km across for the car's wheels.
phys::PolygonSoup floorSoup() {
    phys::SoupGeometry g;
    constexpr float half = 1000.0f;
    g.vertices = {{-half, 0, -half}, {-half, 0, half}, {half, 0, half}, {half, 0, -half}};
    phys::SoupGeometry::Poly p;
    p.v = {0, 1, 2, 3};
    p.count = 4;
    g.polys.push_back(p);
    g.materialNames = {"_default"};
    phys::PolygonSoup soup;
    soup.add(g, Mat34::identity(), phys::MaterialTable{});
    soup.finalize(64.0f);
    return soup;
}

struct CarState {
    Mat34 matrix;
    Vec3 velocity, spin;
    float rpm = 0.0f;
    int gear = 0;
    bool operator==(const CarState& o) const {
        for (int i = 0; i < 4; ++i)
            if (!(matrix.row(i) == o.matrix.row(i)))
                return false;
        return velocity == o.velocity && spin == o.spin && rpm == o.rpm && gear == o.gear;
    }
};

CarState stateOf(const game::SimVehicle& car) {
    const auto& s = car.sim();
    return {s.body.ics.matrix, s.body.ics.linearVelocity, s.body.ics.angularVelocity, s.engine.rpm,
            s.trans.getCurrentGear()};
}

// The car after each of `samples` physics samples, driven with fixed
// pedals over frames of the lengths `frameTime` gives. With `drawn`, the
// race screen's presentation runs as it does in a race: the step observer
// records the car's pose before each sample and every frame asks for the
// pose as drawn.
std::vector<CarState> drive(const vfs::Vfs& vfs, int samples, const std::function<float(int)>& frameTime,
                            bool drawn) {
    phys::World world;
    world.setStatic(floorSoup());
    std::string error;
    auto car = game::SimVehicle::loadPlayer(vfs, "vpmustang99", &error);
    EXPECT_TRUE(car) << error;
    if (!car)
        return {};
    car->addTo(world);
    car->reset(Mat34::translation({0, 1.0f, 0}));
    std::vector<CarState> states;
    StepHistory history;
    const auto key = drawnKey(Drawn::Player, 0);
    world.setStepObserver([&] {
        states.push_back(stateOf(*car)); // the state after the sample before
        if (drawn) {
            history.beginStep();
            history.record(key, car->pose(), car->sim().resets);
        }
    });
    for (int frame = 0; static_cast<int>(states.size()) <= samples; ++frame) {
        car->sim().setInputs(1.0f, 0.0f, 0.15f, 0.0f);
        world.advanceFixed(frameTime(frame));
        if (drawn) {
            history.setAlpha(world.interpolationAlpha());
            const game::VehiclePose pose = history.pose(key, car->pose(), car->sim().resets);
            EXPECT_TRUE(std::isfinite(pose.body.m3.x));
        }
    }
    states.resize(static_cast<std::size_t>(samples) + 1);
    return states;
}

} // namespace

// The presentation only reads: the car's every sample is the same at 60, 144
// and 240 fps and with uneven frames, with and without the drawing's
// history.
TEST(StepHistory, TheSimulationIsTheSameWhateverTheFrameTimes) {
    MM2_REQUIRE_GAME_DATA();
    const vfs::Vfs& vfs = *test::gameData();
    constexpr int kSamples = 600; // 10 s
    const auto reference = drive(vfs, kSamples, [](int) { return 1.0f / 60.0f; }, false);
    ASSERT_EQ(reference.size(), static_cast<std::size_t>(kSamples + 1));
    EXPECT_GT(reference.back().velocity.mag(), 20.0f) << "the car is at speed";
    std::uint32_t seed = 12345;
    const std::function<float(int)> runs[] = {
        [](int) { return 1.0f / 60.0f; },
        [](int) { return 1.0f / 144.0f; },
        [](int) { return 1.0f / 240.0f; },
        [](int) { return 1.0f / 90.0f; },
        [&seed](int) {
            seed = seed * 1103515245u + 12345u;
            return 0.002f + static_cast<float>((seed >> 16) % 1000) * 0.00004f; // 2..42 ms
        },
    };
    for (std::size_t r = 0; r < std::size(runs); ++r) {
        const auto states = drive(vfs, kSamples, runs[r], true);
        ASSERT_EQ(states.size(), reference.size()) << r;
        for (std::size_t k = 0; k < states.size(); ++k)
            ASSERT_TRUE(states[k] == reference[k]) << "run " << r << " differs at sample " << k;
    }
}

// The AI's traffic and pedestrians step for step, with the drawing's
// history kept by its step observer or not.
TEST(StepHistory, TheTrafficIsTheSameWhateverTheFrameTimes) {
    MM2_REQUIRE_GAME_DATA();
    const vfs::Vfs& vfs = *test::gameData();
    const auto city = city::loadCity(vfs, "london");
    ASSERT_TRUE(city);
    const Vec3 spot{430, 0, -150};
    constexpr int kSteps = 150; // 5 s
    auto run = [&](float frame, bool drawn) {
        ai::Settings settings;
        auto world = ai::World::create(*city, vfs, settings);
        std::vector<std::vector<Mat34>> states;
        StepHistory history;
        auto snapshot = [&] {
            std::vector<Mat34> s;
            for (const auto& c : world->cars())
                s.push_back(c.transform);
            for (const auto& p : world->peds())
                s.push_back(p.transform);
            return s;
        };
        world->setStepObserver([&] {
            states.push_back(snapshot());
            if (drawn)
                game::recordAiStep(history, *world);
        });
        while (static_cast<int>(states.size()) <= kSteps) {
            world->update(frame, spot, {});
            if (drawn) {
                history.setAlpha(world->interpolationAlpha());
                for (const auto& c : world->cars())
                    history.transform(drawnKey(Drawn::RailCar, static_cast<std::uint64_t>(c.id)), c.transform,
                                      static_cast<std::uint32_t>(c.spawns));
            }
        }
        states.resize(kSteps + 1);
        return states;
    };
    const auto reference = run(1.0f / 30.0f, false);
    ASSERT_GT(reference.back().size(), 10u) << "traffic and pedestrians";
    for (const float frame : {1.0f / 60.0f, 1.0f / 144.0f, 1.0f / 240.0f}) {
        const auto states = run(frame, true);
        for (std::size_t k = 0; k < states.size(); ++k) {
            ASSERT_EQ(states[k].size(), reference[k].size()) << frame << " step " << k;
            for (std::size_t i = 0; i < states[k].size(); ++i)
                for (int row = 0; row < 4; ++row)
                    ASSERT_EQ(states[k][i].row(row), reference[k][i].row(row)) << frame << " step " << k;
        }
    }
}

// A client's shared traffic is drawn a physics step behind the time its
// physics proxies are placed at (TrafficClient::transformAt), from the
// snapshots update() keeps for it; before a car's first state, at that state.
TEST(StepHistory, SharedTrafficIsSampledWhereTheSceneIsDrawn) {
    auto car = [](float z) {
        game::SharedCar c;
        c.id = 5;
        c.model = 1;
        c.transform = Mat34::translation({0, 0, z});
        c.speed = 10.0f;
        c.velocity = {0, 0, -10.0f};
        return c;
    };
    auto message = [](std::uint32_t time, const game::SharedCar& c) {
        game::TrafficHost host;
        const std::vector<game::SharedCar> list{c};
        net::AmbientStateMsg out;
        const auto msg = host.build({1, {0, 0, 0}}, list, time, 0, 0x1234);
        EXPECT_TRUE(net::decodeMessage(net::encodeMessage(msg), out));
        return out;
    };
    game::TrafficClient client(8, 0x1234);
    client.receive(message(1000, car(0.0f)));
    client.receive(message(1050, car(-0.5f)));
    client.receive(message(1100, car(-1.0f)));
    client.update(1100.0);
    ASSERT_EQ(client.cars().size(), 1u);
    const float now = client.cars()[0].transform.m3.z; // the positions are quantised on the wire
    EXPECT_NEAR(now, -1.0f, 0.02f);
    // At 10 m/s the car was a step's 16.7 cm further back.
    const double drawn = 1100.0 - phys::kFixedSampleStep * 1000.0;
    const auto m = client.transformAt(5, drawn);
    ASSERT_TRUE(m);
    EXPECT_NEAR(m->m3.z - now, 10.0f * phys::kFixedSampleStep, 1e-3f);
    // Before the car's first state: there.
    const auto first = client.transformAt(5, 900.0);
    ASSERT_TRUE(first);
    EXPECT_NEAR(first->m3.z - now, 1.0f, 1e-3f);
    EXPECT_FALSE(client.transformAt(6, drawn));
}

// The particles the effects update at a fixed 60 Hz are drawn back along
// their velocities by FixedTicker::behind: where they were between their last
// two updates, as the rest of the scene is drawn.
TEST(StepHistory, ParticlesAreDrawnBetweenTheirLastTwoUpdates) {
    using game::fx::FixedTicker;
    FixedTicker ticker;
    EXPECT_EQ(ticker.advance(0.01f), 0);
    EXPECT_NEAR(ticker.behind(), FixedTicker::kStep - 0.01f, 1e-6f);
    EXPECT_EQ(ticker.advance(0.01f), 1);
    EXPECT_NEAR(ticker.behind(), 2.0f * FixedTicker::kStep - 0.02f, 1e-6f);

    game::fx::BirthRule rule;
    rule.velocity = {3.0f, 8.0f, -2.0f};
    rule.life = 5.0f;
    rule.drag = 0.1f;
    rule.gravity = -9.8f;
    game::fx::ParticleSystem system;
    system.init(4, 1, 1);
    system.setBirthRule(&rule);
    system.blast(1);
    system.update(FixedTicker::kStep);
    ASSERT_EQ(system.count(), 1);
    const Vec3 before = system.positions()[0].position;
    system.update(FixedTicker::kStep);
    const Vec3 now = system.positions()[0].position;
    EXPECT_GT((now - before).mag(), 0.1f);
    // A whole step behind: where it was at the update before.
    EXPECT_LT((now - system.info()[0].velocity * FixedTicker::kStep - before).mag(), 1e-5f);
}
