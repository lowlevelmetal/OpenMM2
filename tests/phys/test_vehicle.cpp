#include "TestData.h"
#include "core/File.h"
#include "data/DatFile.h"
#include "phys/World.h"
#include "phys/vehicle/CarSim.h"
#include "phys/vehicle/Controls.h"
#include "phys/vehicle/Engine.h"
#include "phys/vehicle/Transmission.h"
#include "phys/vehicle/TuneParams.h"
#include "phys/vehicle/Wheel.h"

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
#include <string>

using namespace mm2;
using namespace mm2::phys;

namespace {

// vpbug.vehCarSim from the retail data, inlined so these tests run without it.
const char* kBugSim = R"(type: a
vehCarSim {
  Mass 1000.000000
  InertiaBox 2.000000 2.000000 3.000000
  CenterOfGravity 0.000000 -0.100000 0.000000
  BoundFriction 0.500000
  BoundElasticity 0.500000
  DrivetrainType 1
  SSSValue 1.000000
  SSSThreshold 0.000000
  CarFrictionHandling 1.000000
  Aero {
    AngCDamp 1.300000 1.400000 1.000000
    AngVelDamp 0.000000 0.000000 0.000000
    AngVel2Damp 0.140000 2.000000 1.000000
    Drag 0.500000
    Down 0.000000
  }
  Engine {
    AngInertia 1.000000
    MaxHorsePower 260.000000
    IdleRPM 750.000000
    OptRPM 5800.000000
    MaxRPM 8500.000000
    GCL 0.250000
  }
  Trans {
    ManualNumGears 7
    AutoNumGears 6
    Reverse 30.000000
    Low 20.000000
    High 89.999985
    GearBias 0.500000
    UpshiftBias 0.050000
    DownshiftBiasMin 0.050000
    DownshiftBiasMax 0.300000
    GearChangeTime 0.800000
  }
  Drivetrain {
    AngInertia 2000.000000
    BrakeDynamicCoef 1.000000
    BrakeStaticCoef 1.200000
  }
  Freetrain {
    AngInertia 2000.000000
    BrakeDynamicCoef 1.000000
    BrakeStaticCoef 1.200000
  }
  WheelFront {
    SuspensionExtent 0.150000
    SuspensionLimit 0.050000
    SuspensionFactor 1.000000
    SuspensionDampCoef 0.100000
    SteeringLimit 0.400000
    SteeringOffset 0.250000
    BrakeCoef 0.625000
    HandbrakeCoef 2.000000
    CamberLimit 0.000000
    WobbleLimit 0.000000
    TireDispLimitLong 0.125000
    TireDampCoefLong 0.250000
    TireDragCoefLong 0.020000
    TireDispLimitLat 0.125000
    TireDampCoefLat 0.250000
    TireDragCoefLat 0.050000
    OptimumSlipPercent 0.160000
    StaticFric 3.000000
    SlidingFric 2.700000
  }
  WheelBack {
    SuspensionExtent 0.150000
    SuspensionLimit 0.050000
    SuspensionFactor 1.000000
    SuspensionDampCoef 0.100000
    SteeringLimit 0.030000
    SteeringOffset 0.000000
    BrakeCoef 0.606000
    HandbrakeCoef 2.000000
    CamberLimit -1.000000
    WobbleLimit 0.000000
    TireDispLimitLong 0.125000
    TireDampCoefLong 0.250000
    TireDragCoefLong 0.020000
    TireDispLimitLat 0.125000
    TireDampCoefLat 0.250000
    TireDragCoefLat 0.050000
    OptimumSlipPercent 0.130000
    StaticFric 3.000000
    SlidingFric 2.800000
  }
  AxleFront {
    TorqueCoef 0.000000
    DampCoef 0.000000
  }
  AxleBack {
    TorqueCoef 0.000000
    DampCoef 0.000000
  }
}
)";

CarSimParams bugParams() {
    auto dat = data::parseDat(kBugSim);
    EXPECT_TRUE(dat && dat->top());
    CarSimParams p;
    EXPECT_TRUE(loadCarSimParams(*dat->top(), p));
    return p;
}

// vpbug geometry from geometry/vpbug_whl*.mtx and bound/vpbug_bound.bnd.
VehicleGeometry bugGeometry() {
    VehicleGeometry g;
    const float x[4] = {-0.754f, 0.754f, -0.754f, 0.754f};
    const float z[4] = {-1.184f, -1.184f, 1.236f, 1.236f};
    for (int i = 0; i < 4; ++i)
        g.wheels[static_cast<std::size_t>(i)] = {{x[i], 0.307f, z[i]}, 0.336f, 0.255f, true};
    g.body = Aabb{{-0.92f, 0.245f, -1.98f}, {0.94f, 1.56f, 2.01f}};
    return g;
}

PolygonSoup flatGround(float half = 20000.0f) {
    BoundGeometry g;
    g.vertices = {{-half, 0, -half}, {-half, 0, half}, {half, 0, half}, {half, 0, -half}};
    g.polys.push_back({{0, 1, 2, 3}, 4, 0});
    g.materialNames = {"_default"};
    PolygonSoup soup;
    soup.add(g, Mat34::identity(), MaterialTable{});
    soup.finalize(2048.0f);
    return soup;
}

struct TestCar {
    World world;
    std::unique_ptr<CarSim> car = std::make_unique<CarSim>();
    ArcadeControls controls;

    explicit TestCar(const CarSimParams& p = bugParams(), const VehicleGeometry& g = bugGeometry()) {
        world.setStatic(flatGround());
        car->init(p, g);
        car->reset(Mat34::identity());
        world.add(&car->body);
    }
    void run(float seconds, const PedalInput& in, float dt = kFixedSampleStep) {
        const int n = static_cast<int>(seconds / dt + 0.5f);
        for (int i = 0; i < n; ++i) {
            controls.apply(*car, in);
            world.step(dt);
        }
    }
};

// Full throttle from rest (after 1 s at rest); speed in mph after `seconds`.
float launchSpeedMph(const CarSimParams& p, const VehicleGeometry& g, float seconds, float dt,
                     bool mm1ExplicitSpin = false) {
    World world;
    world.setStatic(flatGround());
    CarSim car;
    CarSimOptions o;
    o.mm1ExplicitSpin = mm1ExplicitSpin;
    car.init(p, g, o);
    car.reset(Mat34::identity());
    world.add(&car.body);
    ArcadeControls controls;
    PedalInput in;
    for (int i = 0; i < static_cast<int>(1.0f / dt + 0.5f); ++i) {
        controls.apply(car, {});
        world.step(dt);
    }
    in.accelerator = 1.0f;
    for (int i = 0; i < static_cast<int>(seconds / dt + 0.5f); ++i) {
        controls.apply(car, in);
        world.step(dt);
    }
    return car.speedMph();
}

// The bug tune with stiff, heavily damped tyres and an MM1-layout gearbox,
// like the retail *_opp tunes (TireDispLimit 0.075, TireDampCoef 0.75,
// front OptimumSlipPercent 0.01, explicit GearRatios).
CarSimParams stiffTyreParams() {
    CarSimParams p = bugParams();
    p.engine.maxHorsePower = 200.0f;
    p.trans = {};
    p.trans.hasExplicitRatios = true;
    p.trans.numGears = 7;
    p.trans.gearRatios = {-20.0f, 0.0f, 21.0f, 15.0f, 10.0f, 7.0f, 5.0f};
    p.trans.upshiftRPM = {6000, 6000, 6000, 6000, 6000, 6000, 6000};
    p.trans.downshiftRPM = {2000, 2000, 2000, 2000, 2000, 2000, 2000};
    for (WheelParams* w : {&p.wheelFront, &p.wheelBack}) {
        w->tireDispLimitLong = w->tireDispLimitLat = 0.075f;
        w->tireDampCoefLong = w->tireDampCoefLat = 0.75f;
        w->tireDragCoefLong = w->tireDragCoefLat = 0.0f;
    }
    p.wheelFront.optimumSlipPercent = 0.01f;
    p.wheelFront.slidingFric = 2.5f;
    return p;
}

// A retail tune with its model's wheel pivots (geometry/<model>_whlN.mtx: 12
// floats, the pivot in the last row) and a placeholder body.
std::optional<std::pair<CarSimParams, VehicleGeometry>> retailCar(const std::string& name) {
    const vfs::Vfs* fs = test::gameData();
    auto bytes = fs->readAll("tune/vehicle/" + name + ".vehcarsim");
    if (!bytes)
        return std::nullopt;
    auto dat = data::parseDat(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    CarSimParams p;
    if (!dat || !dat->top() || !loadCarSimParams(*dat->top(), p))
        return std::nullopt;
    VehicleGeometry g = VehicleGeometry::placeholder();
    const std::string model = name.substr(0, name.find('_'));
    for (int i = 0; i < 4; ++i) {
        auto mtx = fs->readAll("geometry/" + model + "_whl" + std::to_string(i) + ".mtx");
        if (!mtx || mtx->size() < 48)
            return std::nullopt;
        Mat34 m;
        for (int r = 0; r < 4; ++r)
            m.row(r) = {loadLE<float>(mtx->data() + r * 12), loadLE<float>(mtx->data() + r * 12 + 4),
                        loadLE<float>(mtx->data() + r * 12 + 8)};
        g.wheels[static_cast<std::size_t>(i)] = VehicleGeometry::wheelFromPivot(m);
    }
    return std::make_pair(p, g);
}

} // namespace

TEST(Engine, TorqueCurvePeaksAtHalfOptRpm) {
    Engine e;
    EngineParams p;
    p.maxHorsePower = 260;
    p.optRPM = 5800;
    p.maxRPM = 8500;
    e.configure(p);
    const float optW = e.optRotationSpeed;
    auto torqueAt = [&](float w) {
        e.rotationSpeed = w;
        return e.calcTorqueAtFullThrottle();
    };
    // T = Pmax/w_opt * (1 + x - x^2): 1.25 * OptTorque at x = 0.5, OptTorque at x = 1.
    const float optTorque = 260.0f * 746.0f / optW;
    EXPECT_NEAR(torqueAt(0.5f * optW), 1.25f * optTorque, 1e-3f * optTorque);
    EXPECT_NEAR(torqueAt(optW), optTorque, 1e-3f * optTorque);
    EXPECT_GT(torqueAt(0.5f * optW), torqueAt(0.3f * optW));
    EXPECT_EQ(torqueAt(e.maxRotationSpeed * 1.01f), 0.0f);
    // Engine braking crosses zero at 160 rad/s.
    e.rotationSpeed = 160.0f;
    EXPECT_NEAR(e.calcTorqueAtZeroThrottle(), 0.0f, 1e-3f);
    e.rotationSpeed = optW;
    EXPECT_NEAR(e.calcTorqueAtZeroThrottle(), -0.5f * optTorque, 1e-3f * optTorque);
}

TEST(Transmission, Mm2GearSpeedsHitLowAndHighAtMaxRpm) {
    const CarSimParams p = bugParams();
    Transmission t;
    const float radius = 0.336f;
    t.configure(p.trans, p.engine, radius);
    EXPECT_EQ(t.numGears, 6);
    EXPECT_EQ(t.manualNumGears, 7);
    const float maxW = p.engine.maxRPM * 2.0f * kPi / 60.0f;
    auto mphAtMax = [&](int gear) { return maxW / t.ratio(gear) * radius * kMetersPerSecondToMph; };
    EXPECT_NEAR(mphAtMax(Transmission::kFirst), 20.0f, 0.01f);
    EXPECT_NEAR(mphAtMax(t.numGears - 1), 90.0f, 0.01f);
    EXPECT_LT(t.ratio(Transmission::kReverse), 0.0f);
    EXPECT_EQ(t.ratio(Transmission::kNeutral), 0.0f);
    for (int g = Transmission::kFirst + 1; g < t.numGears; ++g)
        EXPECT_LT(t.ratio(g), t.ratio(g - 1));
}

TEST(Transmission, Mm1FormatAndShiftLogic) {
    TransmissionParams p;
    p.hasExplicitRatios = true;
    p.numGears = 5;
    p.gearRatios = {-20, 0, 28, 20, 16};
    p.upshiftRPM = {6000, 6000, 6000, 6000, 6000};
    p.downshiftRPM = {2000, 2000, 2000, 2000, 2000};
    Transmission t;
    t.configure(p, EngineParams{}, 0.33f);
    EXPECT_FLOAT_EQ(t.currentRatio(), 28.0f);
    EXPECT_EQ(t.getCurrentGear(), 1);
    Engine e;
    e.configure(EngineParams{});
    // Too early (GearChanged set by reset, TimeInGear 0): no shift.
    e.rpm = 7000.0f;
    t.update(0.1f, e);
    EXPECT_EQ(t.getCurrentGear(), 1);
    t.gearChanged = false; // the engine clears it after its gear-change lag
    t.update(0.1f, e);
    EXPECT_EQ(t.getCurrentGear(), 2);
    t.gearChanged = false;
    e.rpm = 1500.0f;
    t.update(0.1f, e);
    t.update(0.1f, e);
    EXPECT_EQ(t.getCurrentGear(), 1);
    // Manual shifting only when not automatic.
    EXPECT_EQ(t.upshift(), Transmission::kFirst);
    t.automatic(false);
    EXPECT_EQ(t.upshift(), Transmission::kFirst + 1);
    t.setReverse();
    EXPECT_EQ(t.getCurrentGear(), -1);
    t.setDrive();
    EXPECT_EQ(t.getCurrentGear(), 1);
}

TEST(Wheel, FrictionCurvePeaksAtOptimumSlip) {
    Wheel w;
    WheelParams p;
    p.optimumSlipPercent = 0.16f;
    p.staticFric = 3.0f;
    p.slidingFric = 2.7f;
    w.init(p, {{0, 0.3f, 0}, 0.3f, 0.2f, true}, {}, 1000.0f, kGravity, 4, 0);
    EXPECT_NEAR(w.frictionForSlip(0.16f, true), 3.0f, 1e-5f);
    EXPECT_NEAR(w.frictionForSlip(0.08f, true), 3.0f * (2 * 0.5f - 0.25f), 1e-5f);
    EXPECT_NEAR(w.frictionForSlip(w.unkFriction2, true), 2.7f, 1e-3f);
    EXPECT_FLOAT_EQ(w.frictionForSlip(0.9f, true), 2.7f);
    EXPECT_FLOAT_EQ(w.frictionForSlip(0.0f, true), 0.0f);
    // Static load per wheel = m g / 4.
    EXPECT_NEAR(w.normalLoad, 1000.0f * kGravity / 4.0f, 1e-2f);
}

TEST(CarSim, StaysAtRest) {
    TestCar t;
    t.run(1.0f, {}); // settle
    const Vec3 p0 = t.car->body.ics.matrix.m3;
    t.run(10.0f, {});
    const Vec3 p1 = t.car->body.ics.matrix.m3;
    EXPECT_LT(Vec3(p1.x - p0.x, 0, p1.z - p0.z).mag(), 0.01f);
    EXPECT_LT(std::abs(p1.y - p0.y), 0.01f);
    EXPECT_LT(t.car->speed(), 0.05f);
    EXPECT_GT(t.car->body.ics.matrix.m1.y, 0.999f);
    for (const Wheel& w : t.car->wheels)
        EXPECT_TRUE(w.onGround);
}

TEST(CarSim, RestsOnSuspensionAtRideHeight) {
    TestCar t;
    t.run(3.0f, {});
    // Static load balances at Suspension ~ 0 (wheel centres at their pivots).
    for (const Wheel& w : t.car->wheels)
        EXPECT_NEAR(w.suspension, 0.0f, 0.06f);
    EXPECT_NEAR(t.car->modelMatrix().m3.y, 0.03f, 0.08f);
}

TEST(CarSim, AcceleratesShiftsAndReachesTopGearSpeed) {
    TestCar t;
    t.run(1.0f, {});
    PedalInput in;
    in.accelerator = 1.0f;
    float t60 = -1.0f, top = 0.0f;
    int maxGear = 0;
    for (int i = 0; i < 40 * 60; ++i) {
        t.controls.apply(*t.car, in);
        t.world.step(kFixedSampleStep);
        top = std::max(top, t.car->speedMph());
        maxGear = std::max(maxGear, t.car->trans.getCurrentGear());
        if (t60 < 0 && t.car->speedMph() >= 60.0f)
            t60 = static_cast<float>(i) * kFixedSampleStep;
    }
    EXPECT_GT(t60, 3.0f);
    EXPECT_LT(t60, 12.0f);
    EXPECT_EQ(maxGear, 4);         // AutoNumGears 6 = reverse, neutral, 4 forward
    EXPECT_NEAR(top, 90.0f, 2.0f); // High: top gear at MaxRPM
    // Drives straight.
    EXPECT_LT(std::abs(t.car->modelMatrix().m3.x), 1.0f);
}

TEST(CarSim, BrakesStopTheCar) {
    TestCar t;
    t.run(1.0f, {});
    PedalInput go;
    go.accelerator = 1.0f;
    t.run(8.0f, go);
    const float v0 = t.car->speed();
    ASSERT_GT(v0, 20.0f);
    PedalInput stop;
    stop.brake = 1.0f;
    float time = 0.0f;
    while (t.car->speed() > 0.5f && time < 15.0f) {
        t.run(kFixedSampleStep, stop);
        time += kFixedSampleStep;
    }
    EXPECT_LT(time, 6.0f);
    EXPECT_GT(time, 0.5f);
}

TEST(CarSim, AutoReverse) {
    TestCar t;
    t.run(1.0f, {});
    PedalInput brake;
    brake.brake = 1.0f;
    t.run(0.1f, brake);
    EXPECT_EQ(t.car->trans.getCurrentGear(), -1);
    // Now the brake pedal drives backwards.
    t.run(3.0f, brake);
    EXPECT_GT(t.car->modelMatrix().m3.z, 1.0f); // moved towards +Z (backwards)
    // Accelerator brakes, then switches back to drive when slow.
    PedalInput accel;
    accel.accelerator = 1.0f;
    t.run(4.0f, accel);
    EXPECT_GE(t.car->trans.getCurrentGear(), 1);
}

TEST(CarSim, SteersInBothDirections) {
    for (float steer : {1.0f, -1.0f}) {
        TestCar t;
        t.run(1.0f, {});
        PedalInput in;
        in.accelerator = 0.6f;
        t.run(3.0f, in);
        in.steering = steer;
        t.run(2.0f, in);
        const Vec3 pos = t.car->modelMatrix().m3;
        // Right turns head towards +X (the car faces -Z).
        EXPECT_GT(pos.x * steer, 1.0f) << steer;
        EXPECT_GT(t.car->body.ics.matrix.m1.y, 0.9f);
    }
}

TEST(CarSim, Deterministic) {
    auto runOnce = [] {
        TestCar t;
        t.run(1.0f, {});
        PedalInput in;
        in.accelerator = 1.0f;
        in.steering = 0.3f;
        t.run(5.0f, in);
        in.handbrake = 1.0f;
        t.run(1.0f, in);
        return t.car->body.ics.matrix;
    };
    const Mat34 a = runOnce();
    const Mat34 b = runOnce();
    EXPECT_EQ(std::memcmp(&a, &b, sizeof(Mat34)), 0);
}

TEST(CarSim, StableWithLargeOversampleSteps) {
    TestCar t;
    PedalInput in;
    in.accelerator = 1.0f;
    in.steering = 0.5f;
    // 1/35 s samples (MM1's SampleStep) and a long frame.
    for (int i = 0; i < 400; ++i) {
        t.controls.apply(*t.car, in);
        t.world.advanceOversampled(1.0f / 20.0f);
        const Vec3& p = t.car->body.ics.matrix.m3;
        ASSERT_TRUE(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
        ASSERT_LT(t.car->speedMph(), 200.0f);
    }
}

TEST(CarSim, WallImpactStopsCarAndDamagesIt) {
    TestCar t;
    // A wall across the road at z = -60, facing +Z.
    BoundGeometry wall;
    wall.vertices = {{-50, 0, -60}, {50, 0, -60}, {50, 10, -60}, {-50, 10, -60}};
    wall.polys.push_back({{0, 1, 2, 3}, 4, 0});
    wall.materialNames = {"_default"};
    PolygonSoup soup;
    BoundGeometry ground;
    const float h = 1000.0f;
    ground.vertices = {{-h, 0, -h}, {-h, 0, h}, {h, 0, h}, {h, 0, -h}};
    ground.polys.push_back({{0, 1, 2, 3}, 4, 0});
    ground.materialNames = {"_default"};
    soup.add(ground, Mat34::identity(), MaterialTable{});
    soup.add(wall, Mat34::identity(), MaterialTable{});
    soup.finalize(64.0f);
    t.world.setStatic(std::move(soup));
    CarDamageParams dp;
    dp.impactThreshold = 1500.0f;
    t.car->setDamageParams(dp);
    int impacts = 0;
    t.car->onImpactCallback = [&](const Impact&) { ++impacts; };
    PedalInput in;
    in.accelerator = 1.0f;
    t.run(1.0f, {});
    t.run(10.0f, in);
    EXPECT_GT(impacts, 0);
    EXPECT_GT(t.car->modelMatrix().m3.z, -60.5f); // did not pass through
    EXPECT_GT(t.car->damage.currentDamage, 0.0f);
}

TEST(CarSim, LoadsEveryRetailCar) {
    MM2_REQUIRE_GAME_DATA();
    int loaded = 0;
    for (const auto& e : test::gameData()->listFiles()) {
        if (!e.path.starts_with("tune/vehicle/") || !e.path.ends_with(".vehcarsim"))
            continue;
        auto bytes = test::gameData()->readAll(e.path);
        ASSERT_TRUE(bytes);
        auto dat =
            data::parseDat(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
        ASSERT_TRUE(dat && dat->top()) << e.path;
        CarSimParams p;
        ASSERT_TRUE(loadCarSimParams(*dat->top(), p)) << e.path;
        // Drive it for a while on placeholder geometry: no NaNs, no explosions.
        TestCar t(p, VehicleGeometry::placeholder());
        PedalInput in;
        in.accelerator = 1.0f;
        in.steering = 0.2f;
        t.run(1.0f, {});
        t.run(6.0f, in);
        const Vec3& pos = t.car->body.ics.matrix.m3;
        EXPECT_TRUE(std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z)) << e.path;
        EXPECT_LT(t.car->speedMph(), 250.0f) << e.path;
        // Moves off: the slowest (the buses) reach about 18 mph in 6 s. The
        // stiff *_opp tyres once only crept (see Drivetrain tests below).
        EXPECT_GT(t.car->speedMph(), 10.0f) << e.path;
        ++loaded;
    }
    EXPECT_GE(loaded, 45);
}

// The drivetrain's implicit step must not add inertia: the tyre slopes only
// stabilise it. With MM1's per-radian slope (or the previous sample's tyre
// torque) stiff tyres turned into a flywheel worth tens of tonnes and the
// *_opp cars took 25 s to reach 60 mph; see docs/physics.md.
TEST(Drivetrain, StiffTyresLaunchBriskly) {
    const CarSimParams stiff = stiffTyreParams();
    const float v4 = launchSpeedMph(stiff, bugGeometry(), 4.0f, kFixedSampleStep);
    EXPECT_GT(v4, 35.0f);
    // Like the soft-tyred original within a few mph (same engine and mass).
    CarSimParams soft = bugParams();
    soft.engine.maxHorsePower = 200.0f;
    soft.trans = stiff.trans;
    EXPECT_NEAR(v4, launchSpeedMph(soft, bugGeometry(), 4.0f, kFixedSampleStep), 6.0f);
}

TEST(Drivetrain, LaunchDoesNotDependOnTheSampleStep) {
    // The original oversampled at 1/35 s or less; OpenMM2 defaults to 1/60.
    for (const CarSimParams& p : {bugParams(), stiffTyreParams()}) {
        const float ref = launchSpeedMph(p, bugGeometry(), 4.0f, 1.0f / 240.0f);
        EXPECT_NEAR(launchSpeedMph(p, bugGeometry(), 4.0f, kFixedSampleStep), ref, 0.05f * ref);
        EXPECT_NEAR(launchSpeedMph(p, bugGeometry(), 4.0f, 1.0f / 120.0f), ref, 0.05f * ref);
    }
}

TEST(Drivetrain, MatchesMm1ExplicitSpinWhereThatIsStable) {
    // MM1 build 1560 couples wheel and tyre explicitly; at small steps that
    // is stable, and both solvers converge to the same launch.
    for (const CarSimParams& p : {bugParams(), stiffTyreParams()}) {
        const float mm1 = launchSpeedMph(p, bugGeometry(), 4.0f, 1.0f / 240.0f, true);
        EXPECT_NEAR(launchSpeedMph(p, bugGeometry(), 4.0f, 1.0f / 240.0f), mm1, 0.03f * mm1);
        EXPECT_NEAR(launchSpeedMph(p, bugGeometry(), 4.0f, kFixedSampleStep), mm1, 0.08f * mm1);
    }
}

TEST(Drivetrain, RetailOpponentTunesLaunchLikeThePlayerCars) {
    MM2_REQUIRE_GAME_DATA();
    // MM1-layout gearboxes with stiff, front OptimumSlipPercent 0.01 tyres.
    for (const char* name : {"vpcoop", "vpbug", "vpcaddie", "vppanoz", "vpmustang99", "vpcop"}) {
        auto opp = retailCar(std::string(name) + "_opp");
        auto player = retailCar(name);
        ASSERT_TRUE(opp && player) << name;
        const float vOpp = launchSpeedMph(opp->first, opp->second, 4.0f, kFixedSampleStep);
        const float vPlayer = launchSpeedMph(player->first, player->second, 4.0f, kFixedSampleStep);
        EXPECT_GT(vOpp, 30.0f) << name;
        EXPECT_GT(vOpp, 0.5f * vPlayer) << name;
    }
}
