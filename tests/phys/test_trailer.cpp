#include "data/DatFile.h"
#include "phys/Joint3Dof.h"
#include "phys/World.h"
#include "phys/vehicle/CarSim.h"
#include "phys/vehicle/Controls.h"
#include "phys/vehicle/Trailer.h"
#include "phys/vehicle/TuneParams.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

using namespace mm2;
using namespace mm2::phys;

namespace {

// vpcentury's tune/vehicle files from the retail data, inlined so these tests
// run without it.
const char* kCenturySim = R"(type: a
vehCarSim {
  Mass 3500.000000
  InertiaBox 3.500000 2.000000 4.999996
  CenterOfGravity 0.000000 -0.200000 0.500000
  BoundFriction 0.900000
  BoundElasticity 0.500000
  DrivetrainType 0
  SSSValue 1.000000
  SSSThreshold 0.000000
  CarFrictionHandling 1.000000
  Aero {
    AngCDamp 6.000000 2.000000 6.000000
    AngVelDamp 2.999999 0.000000 0.000000
    AngVel2Damp 2.009999 0.000000 2.000000
    Drag 0.000000
    Down 0.000000
  }
  Engine {
    AngInertia 1.000000
    MaxHorsePower 750.000000
    IdleRPM 750.000000
    OptRPM 5000.000000
    MaxRPM 8000.000000
    GCL 0.250000
  }
  Trans {
    ManualNumGears 8
    AutoNumGears 8
    Reverse 20.000000
    Low 35.100079
    High 75.000046
    GearBias 0.500000
    UpshiftBias 0.050000
    DownshiftBiasMin 0.050000
    DownshiftBiasMax 0.300000
    GearChangeTime 0.800000
  }
  Drivetrain {
    AngInertia 30000.000000
    BrakeDynamicCoef 1.000000
    BrakeStaticCoef 1.200000
  }
  Freetrain {
    AngInertia 30000.000000
    BrakeDynamicCoef 1.000000
    BrakeStaticCoef 1.200000
  }
  WheelFront {
    SuspensionExtent 0.100000
    SuspensionLimit 0.200000
    SuspensionFactor 1.000000
    SuspensionDampCoef 0.100000
    SteeringLimit 0.450000
    SteeringOffset 0.000000
    BrakeCoef 0.500000
    HandbrakeCoef 2.000000
    CamberLimit -1.000000
    WobbleLimit 0.000000
    TireDispLimitLong 0.125000
    TireDampCoefLong 0.250000
    TireDragCoefLong 0.020000
    TireDispLimitLat 0.125000
    TireDampCoefLat 0.250000
    TireDragCoefLat 0.050000
    OptimumSlipPercent 0.739000
    StaticFric 3.000000
    SlidingFric 2.900000
  }
  WheelBack {
    SuspensionExtent 0.200000
    SuspensionLimit 0.100000
    SuspensionFactor 1.000000
    SuspensionDampCoef 0.100000
    SteeringLimit 0.000000
    SteeringOffset 0.000000
    BrakeCoef 0.500000
    HandbrakeCoef 2.000000
    CamberLimit -1.000000
    WobbleLimit 0.000000
    TireDispLimitLong 0.125000
    TireDampCoefLong 0.250000
    TireDragCoefLong 0.020000
    TireDispLimitLat 0.125000
    TireDampCoefLat 0.250000
    TireDragCoefLat 0.050000
    OptimumSlipPercent 0.484000
    StaticFric 3.000000
    SlidingFric 2.900000
  }
}
)";

const char* kCenturyTrailer = R"(type: a
vehTrailer {
  Mass 2000.000000
  InertiaBox 2.500000 0.600000 12.000000
  WheelFront {
    SuspensionExtent 0.200000
    SuspensionLimit 0.100000
    SuspensionFactor 1.000000
    SuspensionDampCoef 0.100000
    SteeringLimit 0.390000
    SteeringOffset 0.000000
    BrakeCoef 1.000000
    HandbrakeCoef 1.000000
    CamberLimit -1.000000
    WobbleLimit 0.000000
    TireDispLimitLong 0.075000
    TireDampCoefLong 0.250000
    TireDragCoefLong 0.020000
    TireDispLimitLat 0.075000
    TireDampCoefLat 0.250000
    TireDragCoefLat 0.050000
    OptimumSlipPercent 0.140000
    StaticFric 2.000000
    SlidingFric 1.900000
  }
  WheelBack {
    SuspensionExtent 0.200000
    SuspensionLimit 0.100000
    SuspensionFactor 1.000000
    SuspensionDampCoef 0.100000
    SteeringLimit 0.390000
    SteeringOffset 0.000000
    BrakeCoef 1.000000
    HandbrakeCoef 1.000000
    CamberLimit -1.000000
    WobbleLimit 0.000000
    TireDispLimitLong 0.075000
    TireDampCoefLong 0.250000
    TireDragCoefLong 0.020000
    TireDispLimitLat 0.075000
    TireDampCoefLat 0.250000
    TireDragCoefLat 0.050000
    OptimumSlipPercent 0.140000
    StaticFric 2.000000
    SlidingFric 1.900000
  }
}
)";

const char* kCenturyJoint = R"(type: a
dgTrailerJoint {
  Offset0 -0.065883 1.024690 2.572473
  Offset1 -0.066000 1.120000 -4.887576
  ForceLimit 0.000000
  JointStatus 2
  RestoreForceLean 2.000000
  DampConstLean 2.000000
  DampLinearLean 0.900000
  RestoreForceRoll 2.000000
  DampConstRoll 0.100000
  DampLinearRoll 2.000000
  LeanLimit 3.000000
  LimitElasticityLean 0.000000
  LimitElasticityRoll 0.000000
}
)";

template <class T, class F>
T loadBlock(const char* text, F load) {
    auto dat = data::parseDat(text);
    EXPECT_TRUE(dat && dat->top());
    T out;
    EXPECT_TRUE(load(*dat->top(), out));
    return out;
}

// geometry/vpcentury_whl*.mtx, bound/vpcentury_bound.bnd.
VehicleGeometry centuryGeometry() {
    VehicleGeometry g;
    const float x[4] = {-0.984f, 0.984f, -0.925f, 0.928f};
    const float z[4] = {-3.279f, -3.279f, 2.009f, 2.009f};
    const float w[4] = {0.408f, 0.408f, 0.584f, 0.584f};
    for (int i = 0; i < 4; ++i)
        g.wheels[static_cast<std::size_t>(i)] = {{x[i], 0.496f, z[i]}, 0.527f, w[i], true};
    g.body = Aabb{{-1.143f, 0.658f, -4.459f}, {1.171f, 4.001f, 4.021f}};
    return g;
}

// geometry/vpcentury_trailer_twhl*.mtx (TWHL0/1 are degenerate pivots that
// never reach the ground), bound/vpcentury_trailer_bound.bnd.
TrailerGeometry centuryTrailerGeometry() {
    TrailerGeometry g;
    g.wheels[0] = {{-0.004f, 0.625f, 2.739f}, 0.0335f, 0.0f, true};
    g.wheels[1] = {{0.540f, 0.597f, 2.739f}, 0.038f, 0.0f, true};
    g.wheels[2] = {{-0.821f, 0.496f, 2.719f}, 0.527f, 0.584f, true};
    g.wheels[3] = {{0.833f, 0.496f, 2.719f}, 0.527f, 0.584f, true};
    g.body = Aabb{{-1.269f, 0.879f, -3.106f}, {1.287f, 1.462f, 6.173f}};
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

float energy(const InertialCS& ics) {
    const float ke =
        0.5f * ics.mass * ics.linearVelocity.mag2() + 0.5f * ics.angularMomentum.dot(ics.angularVelocity);
    return ke + ics.mass * kGravity * ics.matrix.m3.y;
}

struct TestRig {
    World world;
    std::unique_ptr<CarSim> car = std::make_unique<CarSim>();
    std::unique_ptr<Trailer> trailer = std::make_unique<Trailer>();
    ArcadeControls controls;
    float maxGap = 0.0f;
    float maxHitchAngle = 0.0f;
    float minUp = 1.0f;

    TestRig() {
        world.setStatic(flatGround());
        car->init(loadBlock<CarSimParams>(kCenturySim,
                                          [](const auto& b, auto& o) { return loadCarSimParams(b, o); }),
                  centuryGeometry());
        car->reset(Mat34::identity());
        world.add(&car->body);
        trailer->init(loadBlock<TrailerParams>(kCenturyTrailer, loadTrailerParams),
                      loadBlock<TrailerJointParams>(kCenturyJoint, loadTrailerJointParams),
                      centuryTrailerGeometry(), *car);
        trailer->addTo(world);
    }
    void step(const PedalInput& in) {
        controls.apply(*car, in);
        world.step(kFixedSampleStep);
        maxGap = std::max(maxGap, trailer->hitchGap());
        maxHitchAngle = std::max(maxHitchAngle, std::abs(trailer->joint.roll));
        minUp = std::min({minUp, trailer->body.ics.matrix.m1.y, car->body.ics.matrix.m1.y});
    }
    void run(float seconds, const PedalInput& in) {
        const int n = static_cast<int>(seconds / kFixedSampleStep + 0.5f);
        for (int i = 0; i < n; ++i)
            step(in);
    }
    // Lets the rig settle on its suspension, then clears the statistics
    // (the hitch velocities do not match during the first samples after a
    // reset; the joint removes a third of the mismatch per sample).
    void settle(float seconds = 1.0f) {
        run(seconds, {});
        maxGap = 0.0f;
        maxHitchAngle = 0.0f;
        minUp = 1.0f;
    }
    float energy() const { return ::energy(car->body.ics) + ::energy(trailer->body.ics); }
    Vec3 tractorHitch() const { return car->modelMatrix().transform(trailer->jointParams.offset0); }
    Vec3 trailerHitch() const { return trailer->modelMatrix().transform(trailer->jointParams.offset1); }
    bool finite() const {
        for (const InertialCS* ics : {&car->body.ics, &trailer->body.ics}) {
            const Vec3& p = ics->matrix.m3;
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                return false;
        }
        return true;
    }
};

constexpr float kDeg = 3.14159265f / 180.0f;

} // namespace

TEST(Joint3Dof, InitDefaultsAndForceFlag) {
    Joint3Dof j;
    EXPECT_FLOAT_EQ(j.leanLimit1, 3.1415927f);
    EXPECT_FLOAT_EQ(j.rollLimit1, -3.1415927f);
    EXPECT_FLOAT_EQ(j.rollLimit2, 3.1415927f);
    EXPECT_EQ(j.jointFlags, 0);
    j.setFrictionRoll(0.0f, 0.1f, 0.0f);
    EXPECT_EQ(j.jointFlags & Joint3Dof::kForce, Joint3Dof::kForce);
    j.setFrictionRoll(0.0f, 0.0f, 0.0f);
    EXPECT_EQ(j.jointFlags & Joint3Dof::kForce, 0);
    j.setLeanLimit(3.0f, 0.0f); // < pi: a limit is active
    EXPECT_EQ(j.jointFlags & Joint3Dof::kForce, Joint3Dof::kForce);
}

TEST(Joint3Dof, LinkBreakAndUnbreak) {
    InertialCS a, b;
    a.setMass(1, 1, 1, 10);
    b.setMass(1, 1, 1, 10);
    b.matrix.m3 = {5, 0, 0};
    Joint3Dof j;
    j.initJoint3Dof(&a, {1, 0, 0}, &b, {-1, 0, 0});
    // SetPosition(a's position): both offsets land on it.
    EXPECT_NEAR((a.matrix.transform({1, 0, 0}) - b.matrix.transform({-1, 0, 0})).mag(), 0.0f, 1e-6f);
    EXPECT_EQ(a.constraints, InertialCS::kConstrainLink);
    EXPECT_EQ(b.joint, &j);
    j.breakJoint();
    EXPECT_TRUE(j.isBroken());
    EXPECT_EQ(a.constraints & InertialCS::kConstrainLink, 0);
    j.unbreakJoint();
    EXPECT_FALSE(j.isBroken());
    EXPECT_EQ(b.constraints & InertialCS::kConstrainLink, InertialCS::kConstrainLink);
}

TEST(Joint3Dof, FreeLinkConservesMomentumAndStaysClosed) {
    // Two bodies in free space, the first spinning and moving: the joint
    // force is internal, so linear momentum is conserved and the bodies stay
    // together.
    InertialCS a, b;
    a.setMass(2, 1, 4, 100);
    b.setMass(1, 1, 6, 60);
    a.gravity = b.gravity = {};
    a.state = b.state = InertialCS::Awake;
    a.matrix.m3 = {0, 0, 0};
    b.matrix.m3 = {0, 0, 4};
    Joint3Dof j;
    j.initJoint3Dof(&a, {0, 0, 1.5f}, &b, {0, 0, -2.5f});
    a.linearMomentum = {300, 0, -200};
    a.angularMomentum = {0, 150, 40};
    const Vec3 p0 = a.linearMomentum + b.linearMomentum;
    float maxGap = 0.0f;
    const float dt = kFixedSampleStep;
    for (int i = 0; i < 600; ++i) {
        a.update(dt, 1.0f / dt); // linked: does nothing
        b.update(dt, 1.0f / dt);
        j.update(dt, 1.0f / dt);
        // The initial relative velocity at the joint decays by the velocity
        // correction (a third per sample); measure the steady state.
        if (i == 0) {
            EXPECT_GT(j.discrepancy.mag(), 0.05f);
        }
        if (i >= 30)
            maxGap = std::max(maxGap, j.discrepancy.mag());
    }
    const Vec3 p1 = a.linearMomentum + b.linearMomentum;
    EXPECT_LT((p1 - p0).mag(), 1e-2f * p0.mag());
    EXPECT_LT(maxGap, 0.001f);
    EXPECT_LT((a.matrix.transform({0, 0, 1.5f}) - b.matrix.transform({0, 0, -2.5f})).mag(), 1e-4f);
}

TEST(Joint3Dof, CMatrixThroughTheJoint) {
    // Two identical bodies joined at the midpoint: at the joint, an impulse
    // moves the pair, so the joint-aware matrix is half the single body's.
    InertialCS a, b;
    a.setMass(2, 1, 4, 100);
    b.setMass(2, 1, 4, 100);
    a.matrix.m3 = {0, 0, -2};
    b.matrix.m3 = {0, 0, 2};
    Joint3Dof j;
    j.initJoint3Dof(&a, {0, 0, 2}, &b, {0, 0, -2});
    Mat34 c, cj;
    a.calcCMatrix(c, j.position);
    a.getCMatrix(cj, j.position);
    for (int r = 0; r < 3; ++r)
        for (int col = 0; col < 3; ++col)
            EXPECT_NEAR(cj.row(r)[col], 0.5f * c.row(r)[col], 1e-6f) << r << col;
    // The two-body overload stays finite and symmetric here.
    Mat34 c2;
    j.getCMatrix(&a, &b, c2, j.position);
    for (int r = 0; r < 3; ++r)
        for (int col = 0; col < 3; ++col) {
            EXPECT_TRUE(std::isfinite(c2.row(r)[col]));
            EXPECT_NEAR(c2.row(r)[col], c2.row(col)[r], 1e-5f);
        }
}

TEST(Trailer, ResetPutsTheHitchesTogether) {
    TestRig r;
    EXPECT_LT((r.tractorHitch() - r.trailerHitch()).mag(), 1e-4f);
    EXPECT_EQ(r.trailer->body.ics.constraints & InertialCS::kConstrainLink, InertialCS::kConstrainLink);
    EXPECT_EQ(r.car->body.ics.joint, &r.trailer->joint);
    // The dgTrailerJoint values reach Joint3Dof (names match MM1's).
    EXPECT_FLOAT_EQ(r.trailer->joint.frictionLean.x, 2.0f);
    EXPECT_FLOAT_EQ(r.trailer->joint.frictionLean.y, 2.0f);
    EXPECT_FLOAT_EQ(r.trailer->joint.frictionLean.z, 0.9f);
    EXPECT_FLOAT_EQ(r.trailer->joint.frictionRoll.y, 0.1f);
    EXPECT_FLOAT_EQ(r.trailer->joint.leanLimit1, 3.0f);
}

TEST(Trailer, StaysAtRestFor60Seconds) {
    TestRig r;
    r.settle(2.0f);
    const float e0 = r.energy();
    const Vec3 p0 = r.trailer->body.ics.matrix.m3;
    r.run(59.0f, {});
    const Vec3 p59 = r.trailer->body.ics.matrix.m3;
    r.run(1.0f, {});
    ASSERT_TRUE(r.finite());
    EXPECT_LT((r.trailer->body.ics.matrix.m3 - p0).mag(), 0.05f);
    EXPECT_LT((r.trailer->body.ics.matrix.m3 - p59).mag(), 0.01f);
    EXPECT_LT(r.energy(), e0 + 100.0f); // no energy from nowhere (J)
    EXPECT_LT(r.maxGap, 0.005f);
    EXPECT_GT(r.minUp, 0.99f);
}

TEST(Trailer, StraightRun60SecondsIsStable) {
    TestRig r;
    r.settle();
    PedalInput in;
    in.accelerator = 1.0f;
    const Vec3 start = r.car->modelMatrix().m3;
    float lastDist = 0.0f, top = 0.0f;
    for (int s = 0; s < 60; ++s) {
        r.run(1.0f, in);
        ASSERT_TRUE(r.finite()) << s;
        const float dist = (r.car->modelMatrix().m3 - start).mag();
        EXPECT_GT(dist, lastDist) << "distance must grow, t=" << s;
        lastDist = dist;
        top = std::max(top, r.car->speedMph());
    }
    EXPECT_GT(top, 50.0f);
    EXPECT_LT(top, 100.0f);
    EXPECT_LT(r.maxGap, 0.01f);
    EXPECT_LT(r.maxHitchAngle, 3.0f * kDeg);
    EXPECT_GT(r.minUp, 0.99f);
    // vpcentury's tractor pulls slightly to one side on its own (its wheel
    // pivots are not symmetric); the trailer must not add to it.
    EXPECT_LT(std::abs(r.car->body.ics.angularVelocity.y), 0.1f);
}

TEST(Trailer, CoastingNeverGainsEnergy) {
    TestRig r;
    r.settle();
    PedalInput in;
    in.accelerator = 1.0f;
    in.steering = 0.1f;
    r.run(15.0f, in);
    ASSERT_GT(r.car->speedMph(), 30.0f);
    float e = r.energy();
    for (int s = 0; s < 20; ++s) {
        r.run(1.0f, {});
        const float e1 = r.energy();
        EXPECT_LE(e1, e + 500.0f) << "t=" << s; // J; suspension bounce only
        e = std::min(e, e1);
    }
    EXPECT_LT(r.maxGap, 0.01f);
}

TEST(Trailer, SteadyCircleHasBoundedHitchAngle) {
    TestRig r;
    r.settle();
    PedalInput in;
    in.accelerator = 0.5f;
    in.steering = 0.3f;
    r.run(10.0f, in);
    r.maxHitchAngle = 0.0f;
    float minAngle = 10.0f;
    for (int i = 0; i < 20 * 60; ++i) {
        r.step(in);
        minAngle = std::min(minAngle, std::abs(r.trailer->joint.roll));
    }
    ASSERT_TRUE(r.finite());
    EXPECT_GT(r.car->speedMph(), 15.0f);
    // A steady articulation: no jackknife, no oscillation.
    EXPECT_LT(r.maxHitchAngle, 15.0f * kDeg);
    EXPECT_LT(r.maxHitchAngle - minAngle, 2.0f * kDeg);
    EXPECT_GT(r.minUp, 0.99f);
    EXPECT_LT(r.maxGap, 0.01f);
}

TEST(Trailer, HardBrakingDoesNotJackknife) {
    for (float steer : {0.0f, 0.15f}) {
        TestRig r;
        r.settle();
        PedalInput go;
        go.accelerator = 1.0f;
        go.steering = steer;
        r.run(12.0f, go);
        ASSERT_GT(r.car->speedMph(), 40.0f);
        r.maxHitchAngle = 0.0f;
        PedalInput stop;
        stop.brake = 1.0f;
        stop.steering = steer;
        float t = 0.0f;
        while (r.car->speed() > 0.5f && t < 10.0f) {
            r.step(stop);
            t += kFixedSampleStep;
        }
        EXPECT_LT(t, 6.0f) << steer;
        EXPECT_LT(r.maxHitchAngle, 10.0f * kDeg) << steer;
        EXPECT_GT(r.minUp, 0.98f) << steer;
        EXPECT_LT(r.maxGap, 0.01f) << steer;
    }
}

TEST(Trailer, DeterministicAndResettable) {
    auto runOnce = [] {
        TestRig r;
        PedalInput in;
        in.accelerator = 1.0f;
        in.steering = 0.4f;
        r.run(8.0f, in);
        return r.trailer->body.ics.matrix;
    };
    const Mat34 a = runOnce();
    const Mat34 b = runOnce();
    EXPECT_EQ(std::memcmp(&a, &b, sizeof(Mat34)), 0);

    TestRig r;
    PedalInput in;
    in.accelerator = 1.0f;
    in.steering = 0.4f;
    r.run(5.0f, in);
    Mat34 m = Mat34::rotationY(1.0f);
    m.m3 = {30, 0, -40};
    r.car->reset(m);
    r.trailer->reset();
    EXPECT_LT((r.tractorHitch() - r.trailerHitch()).mag(), 1e-3f);
    EXPECT_NEAR(r.trailer->body.ics.matrix.m2.dot(r.car->body.ics.matrix.m2), 1.0f, 1e-4f);
    r.settle();
    r.run(3.0f, {});
    ASSERT_TRUE(r.finite());
    EXPECT_LT(r.maxGap, 0.01f);
}
