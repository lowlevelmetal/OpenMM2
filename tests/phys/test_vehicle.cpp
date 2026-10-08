#include "TestData.h"
#include "phys/TestLevel.h"
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
    SoupGeometry g;
    g.vertices = {{-half, 0, -half}, {-half, 0, half}, {half, 0, half}, {half, 0, -half}};
    g.polys.push_back({{0, 1, 2, 3}, 4, 0});
    g.materialNames = {"default"};
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
float launchSpeedMph(const CarSimParams& p, const VehicleGeometry& g, float seconds, float dt) {
    World world;
    world.setStatic(flatGround());
    CarSim car;
    car.init(p, g);
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

TEST(Engine, TorqueCurveMatchesVehEngine) {
    Engine e;
    EngineParams p;
    p.maxHorsePower = 260;
    p.idleRPM = 800;
    p.optRPM = 5800;
    p.maxRPM = 8500;
    e.configure(p);
    const float optW = e.optRotationSpeed;
    // T = Pmax/w_opt * (1 + x - x^2) below OptRPM: 1.25 * OptTorque at x = 0.5.
    const float optTorque = 260.0f * 746.0f / optW;
    EXPECT_NEAR(e.calcTorqueAtFullThrottle(0.5f * optW), 1.25f * optTorque, 1e-3f * optTorque);
    EXPECT_NEAR(e.calcTorqueAtFullThrottle(optW), optTorque, 1e-3f * optTorque);
    // vehEngine always tapers above OptRPM, to zero at MaxRPM.
    const float mid = 0.5f * (optW + e.maxRotationSpeed);
    EXPECT_LT(e.calcTorqueAtFullThrottle(mid), e.calcTorqueAtFullThrottle(optW));
    EXPECT_GT(e.calcTorqueAtFullThrottle(mid), 0.0f);
    EXPECT_NEAR(e.calcTorqueAtFullThrottle(e.maxRotationSpeed), 0.0f, 1e-3f);
    EXPECT_EQ(e.calcTorqueAtFullThrottle(e.maxRotationSpeed * 1.01f), 0.0f);
    // Engine braking crosses zero at IdleRPM, -0.75 * OptTorque at OptRPM.
    e.rotationSpeed = e.idleRotationSpeed;
    EXPECT_NEAR(e.calcTorqueAtZeroThrottle(), 0.0f, 1e-3f);
    e.rotationSpeed = optW;
    EXPECT_NEAR(e.calcTorqueAtZeroThrottle(), -0.75f * optTorque, 1e-3f * optTorque);
}

TEST(Transmission, GearSpeedsAreAtOptRpm) {
    const CarSimParams p = bugParams();
    Engine e;
    e.configure(p.engine);
    Transmission t;
    t.configure(p.trans);
    const float radius = 0.336f;
    t.computeConstants(e, radius);
    EXPECT_EQ(t.numGears, 6);
    EXPECT_EQ(t.manualNumGears, 7);
    const float optW = p.engine.optRPM * 2.0f * kPi / 60.0f;
    // GearRatioFromMPH uses 1609.344 m per mile (not the HUD's MetricFactor).
    auto mphAtOpt = [&](int gear) { return optW / t.ratio(gear) * radius * (3600.0f / 1609.344f); };
    EXPECT_NEAR(mphAtOpt(Transmission::kFirst), p.trans.low, 0.01f);
    EXPECT_NEAR(mphAtOpt(t.numGears - 1), p.trans.high, 0.01f);
    EXPECT_LT(t.ratio(Transmission::kReverse), 0.0f);
    EXPECT_EQ(t.ratio(Transmission::kNeutral), 0.0f);
    for (int g = Transmission::kFirst + 1; g < t.numGears; ++g)
        EXPECT_LT(t.ratio(g), t.ratio(g - 1));
    // Upshift points: between OptRPM and MaxRPM; the top gear's is MaxRPM.
    for (int g = Transmission::kFirst; g < t.numGears - 1; ++g) {
        EXPECT_GT(t.upshiftRPM[static_cast<std::size_t>(g)], p.engine.optRPM);
        EXPECT_LT(t.upshiftRPM[static_cast<std::size_t>(g)], p.engine.maxRPM * (1.0f + p.trans.upshiftBias));
        // At the upshift point the next gear gives (within UpshiftBias) the same power.
        const float x = t.upshiftRPM[static_cast<std::size_t>(g)] / (1.0f + p.trans.upshiftBias);
        const float f = t.ratio(g + 1) / t.ratio(g);
        const float w = x * 0.10471976f;
        EXPECT_NEAR(e.calcHPAtFullThrottle(w * f), e.calcHPAtFullThrottle(w), 0.01f * e.calcHPAtFullThrottle(w));
    }
    EXPECT_FLOAT_EQ(t.upshiftRPM[static_cast<std::size_t>(t.numGears - 1)], p.engine.maxRPM);
}

TEST(Transmission, ShiftLogic) {
    Engine e;
    e.configure(EngineParams{});
    Transmission t;
    t.configure(TransmissionParams{});
    t.computeConstants(e, 0.33f);
    EXPECT_EQ(t.getCurrentGear(), 1);
    // Too early (shift pending from the reset): no shift.
    e.rpm = 7900.0f;
    e.throttle = 1.0f;
    t.update(0.25f, e, 4);
    EXPECT_EQ(t.getCurrentGear(), 1);
    t.gearChanged = false; // the engine clears it after its gear-change lag
    t.update(0.25f, e, 4);
    t.update(0.25f, e, 4);
    t.update(0.25f, e, 4);
    EXPECT_EQ(t.getCurrentGear(), 1); // GearChangeTime (0.8 s) not exceeded before this update
    t.update(0.25f, e, 4);
    EXPECT_EQ(t.getCurrentGear(), 2);
    // No shifting while airborne, and the time in gear stands still.
    t.gearChanged = false;
    e.rpm = 1000.0f;
    const float before = t.timeInGear;
    for (int i = 0; i < 20; ++i)
        t.update(0.25f, e, 0);
    EXPECT_EQ(t.getCurrentGear(), 2);
    EXPECT_FLOAT_EQ(t.timeInGear, before);
    for (int i = 0; i < 5; ++i)
        t.update(0.25f, e, 4);
    EXPECT_EQ(t.getCurrentGear(), 1);
    // Manual shifting through the manual box; the automatic only leaves
    // reverse/neutral upwards and drops to neutral downwards.
    EXPECT_EQ(t.upshift(), Transmission::kFirst);
    EXPECT_EQ(t.downshift(), Transmission::kNeutral);
    t.automatic(false);
    t.setDrive();
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
    w.init(p, {{0, 0.3f, 1.2f}, 0.3f, 0.2f, true}, 1000.0f, true, 0.0f);
    float slide = 0.0f;
    EXPECT_NEAR(w.computeFriction(0.16f, slide), 3.0f, 1e-5f);
    EXPECT_NEAR(slide, 0.5f, 1e-6f);
    EXPECT_NEAR(w.computeFriction(0.08f, slide), 3.0f * (2 * 0.5f - 0.25f), 1e-5f);
    EXPECT_FLOAT_EQ(w.computeFriction(0.9f, slide), 2.7f);
    EXPECT_FLOAT_EQ(slide, 1.0f);
    EXPECT_FLOAT_EQ(w.computeFriction(0.0f, slide), 0.0f);
    // Static load = m * 19.6 / 4 with the centre of gravity at z = 0; the
    // tyre stiffness is 2 * load / TireDispLimit, the brake torque
    // StaticFric * radius * load * BrakeCoef.
    EXPECT_NEAR(w.normalLoad, 1000.0f * 19.6f / 4.0f, 1e-2f);
    EXPECT_NEAR(w.stiffLong, 2.0f * w.normalLoad / p.tireDispLimitLong, 1.0f);
    EXPECT_NEAR(w.maxBrakeTorque, p.staticFric * 0.3f * w.normalLoad * p.brakeCoef, 1e-1f);
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
    EXPECT_EQ(maxGear, 4); // AutoNumGears 6 = reverse, neutral, 4 forward
    // High is the top gear's speed at OptRPM; past it the car revs on towards
    // MaxRPM until drag balances its power.
    const CarSimParams p = bugParams();
    EXPECT_GT(top, p.trans.high + 5.0f);
    EXPECT_LT(top, p.trans.high * p.engine.maxRPM / p.engine.optRPM + 0.5f);
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
    // Letting go of the brake pedal (the swapped throttle) returns to drive.
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
    // The level: the ground and a wall across the road at z = -60, facing
    // +Z (lvlSDL polygons, collided through dgPhysManager::CollideTerrain).
    fixtures::TestLevel level;
    level.floor(1000.0f);
    level.add({{-50, 0, -60}, {50, 0, -60}, {50, 10, -60}, {-50, 10, -60}});
    t.world.setLevel(&level);
    t.car->setPolygonalBound(true);
    CarDamageParams dp;
    dp.impactThreshold = 1500.0f;
    t.car->setDamageParams(dp);
    int impacts = 0;
    t.car->onImpactCallback = [&](const CarImpact& im) { impacts += im.damaging ? 1 : 0; };
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
        // *_opp / *_cop leftovers (MM1-era layouts the game never loads)
        // need not.
        const bool used = e.path.find("_opp") == std::string::npos && e.path.find("_cop") == std::string::npos &&
                          e.path.find("copy of") == std::string::npos;
        if (used) {
            EXPECT_GT(t.car->speedMph(), 10.0f) << e.path;
        }
        ++loaded;
    }
    EXPECT_GE(loaded, 45);
}

// vehDrivetrain: the brakes stop the wheels and hold them, never reverse them.
TEST(Drivetrain, BrakesStopWithoutReversing) {
    TestCar t;
    t.run(1.0f, {});
    PedalInput go;
    go.accelerator = 1.0f;
    t.run(3.0f, go);
    // Lock the brakes with the automatic box in neutral (no auto reverse).
    t.car->trans.setNeutral();
    float minRot = 0.0f, maxRot = 0.0f;
    for (int i = 0; i < 6 * 60; ++i) {
        t.car->setInputs(0.0f, 1.0f, 0.0f, 0.0f);
        t.world.step(kFixedSampleStep);
        for (const Wheel& w : t.car->wheels) {
            minRot = std::min(minRot, w.rotationSpeed);
            maxRot = std::max(maxRot, w.rotationSpeed);
        }
    }
    EXPECT_LT(t.car->speed(), 0.5f);
    EXPECT_LE(maxRot, 1e-3f); // forward rotation is negative
    for (const Drivetrain& d : t.car->drivetrains)
        EXPECT_EQ(d.rotationSpeed, 0.0f);
}

// vehDrivetrain's limited-slip differential stays within diffRatioMax and
// relaxes to 1 when stopped.
TEST(Drivetrain, DifferentialRatioIsLimited) {
    TestCar t;
    t.run(1.0f, {});
    PedalInput in;
    in.accelerator = 1.0f;
    in.steering = 0.6f;
    float lo = 1.0f, hi = 1.0f;
    for (int i = 0; i < 8 * 60; ++i) {
        t.controls.apply(*t.car, in);
        t.world.step(kFixedSampleStep);
        const Drivetrain& d = t.car->drivetrains[2];
        lo = std::min(lo, d.diffRatio);
        hi = std::max(hi, d.diffRatio);
    }
    EXPECT_GE(lo, 1.0f / Drivetrain::kDiffRatioMax - 1e-4f);
    EXPECT_LE(hi, Drivetrain::kDiffRatioMax + 1e-4f);
    EXPECT_NE(lo, hi); // the turn loads the wheels unevenly
}

// Every retail player tune launches (MM2 builds opponents from the same
// tunes, see aiVehiclePhysics::Init).
TEST(CarSim, RetailTunesLaunch) {
    MM2_REQUIRE_GAME_DATA();
    for (const char* name : {"vpcoop", "vpbug", "vpcaddie", "vppanoz", "vpmustang99", "vpcop"}) {
        auto car = retailCar(name);
        ASSERT_TRUE(car) << name;
        const float v = launchSpeedMph(car->first, car->second, 4.0f, kFixedSampleStep);
        EXPECT_GT(v, 25.0f) << name;
        EXPECT_LT(v, 90.0f) << name;
    }
}

// vehSplash: a car below a water room's level floats, first high, then
// lower as the buoyancy decays from 0.7 to 0.4 per point.
TEST(Splash, CarFloatsAndSettlesLower) {
    World world; // no ground: only the water holds the car
    CarSim car;
    car.init(bugParams(), bugGeometry());
    Mat34 start = Mat34::identity();
    start.m3 = {0.0f, -1.0f, 0.0f};
    car.reset(start);
    world.add(&car.body);
    const float level = 0.0f;
    auto run = [&](float seconds) {
        for (int i = 0; i < static_cast<int>(seconds * 60.0f + 0.5f); ++i) {
            car.setWaterLevel(level);
            car.setInputs(0.0f, 0.0f, 0.0f, 0.0f);
            world.step(kFixedSampleStep);
        }
    };
    run(5.0f);
    ASSERT_TRUE(car.splash.active());
    const float early = car.modelMatrix().m3.y;
    EXPECT_GT(early, -3.0f); // afloat, not sunk
    EXPECT_LT(car.body.ics.linearVelocity.mag(), 3.0f);
    run(15.0f);
    EXPECT_NEAR(car.splash.buoyancy, 0.4f, 1e-4f);
    EXPECT_LT(car.modelMatrix().m3.y, early); // lower once the buoyancy has decayed
    EXPECT_GT(car.modelMatrix().m3.y, -3.0f);
}
