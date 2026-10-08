#include "TestData.h"
#include "core/File.h"
#include "data/DatFile.h"
#include "phys/Joint.h"
#include "phys/TrailerJoint.h"
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
#include <optional>
#include <string>

using namespace mm2;
using namespace mm2::phys;

namespace {

constexpr float kDt = kFixedSampleStep;
constexpr float kDeg = 3.14159265f / 180.0f;

template <class T, class F>
T loadBlock(const char* text, F load) {
    auto dat = data::parseDat(text);
    EXPECT_TRUE(dat && dat->top());
    T out;
    EXPECT_TRUE(dat && dat->top() && load(*dat->top(), out));
    return out;
}

// A free body of `mass` kg (no gravity) at `pos`.
void makeBody(InertialCS& ics, float mass, const Vec3& pos, const Mat34& rotation = Mat34::identity()) {
    ics.setMass(2.0f, 2.0f, 2.0f, mass);
    ics.zero();
    ics.gravity = {};
    ics.matrix = rotation;
    ics.matrix.m3 = pos;
}

TrailerJointParams noTorques() {
    TrailerJointParams p;
    p.jointStatus = 0;
    return p;
}

} // namespace

// --- phJoint -----------------------------------------------------------------

TEST(Joint, InitFindsTheSecondOffsetAndResets) {
    InertialCS a, b;
    makeBody(a, 1000.0f, {0, 0, 0});
    makeBody(b, 1000.0f, {0, 0, 2});
    Joint j;
    j.init(&a, &b, {0, 0, 1});
    EXPECT_NEAR((j.offset2 - Vec3{0, 0, -1}).mag(), 0.0f, 1e-6f);
    EXPECT_NEAR((j.position - Vec3{0, 0, 1}).mag(), 0.0f, 1e-6f);
    a.matrix.m3 = {0, 1, 0};
    j.computeInvMassMatrix();
    EXPECT_NEAR(j.position.y, 1.0f, 1e-6f);
    j.reset();
    EXPECT_NEAR(j.position.y, 0.0f, 1e-6f); // back to where Init put it
    EXPECT_FALSE(j.isBroken());
}

TEST(Joint, GenericStepPullsTheBodiesTogether) {
    InertialCS a, b;
    makeBody(a, 1000.0f, {0, 0, 0});
    makeBody(b, 500.0f, {0, 0, 2});
    Joint j;
    j.init(&a, &b, {0, 0, 1}, {0, 0, -1});
    b.linearMomentum = {500.0f, 0, 0}; // 1 m/s sideways
    b.linearVelocity = {1.0f, 0, 0};
    b.matrix.m3.y = 0.2f; // a gap of 0.2 m
    j.update(1.0f / kDt);
    // Equal and opposite forces that cancel the relative velocity in one
    // sample, and pushes along the gap.
    EXPECT_NEAR(a.linearForce.x, -b.linearForce.x, 1e-3f);
    EXPECT_GT(a.linearForce.x, 0.0f);
    EXPECT_GT(a.linearPush.y, 0.0f);
    EXPECT_LT(b.linearPush.y, 0.0f);
}

// --- dgTrailerJoint ----------------------------------------------------------

TEST(TrailerJoint, InitDefaultsMatchDgTrailerJointInit) {
    InertialCS a, b;
    makeBody(a, 1000.0f, {0, 0, 0});
    makeBody(b, 1000.0f, {0, 0, 3});
    TrailerJoint j;
    j.init(TrailerJointParams{}, &a, &b, {0, 0, 1}, {0, 0, -2});
    EXPECT_EQ(j.forceLimit, 0.0f);
    EXPECT_EQ(j.status, TrailerJoint::kForce);
    EXPECT_FLOAT_EQ(j.leanLimit, 3.1415927f);
    EXPECT_FLOAT_EQ(j.negativeRollLimit, -0.3f);
    EXPECT_FLOAT_EQ(j.positiveRollLimit, 0.3f);
    EXPECT_FLOAT_EQ(j.leanElasticity, 1.0f);
    EXPECT_FLOAT_EQ(j.rollElasticity, 0.0f);
    EXPECT_FLOAT_EQ(j.frictionLean.x, 2.0f);
    EXPECT_FLOAT_EQ(j.frictionLean.y, 2.0f);
    EXPECT_FLOAT_EQ(j.frictionLean.z, 0.9f);
    EXPECT_FLOAT_EQ(j.frictionRoll.x, 2.0f);
    EXPECT_FLOAT_EQ(j.frictionRoll.y, 0.1f);
    EXPECT_FLOAT_EQ(j.frictionRoll.z, 2.0f);
    EXPECT_FLOAT_EQ(j.freeRange, 0.15f);
    EXPECT_FLOAT_EQ(j.freeLean, 0.1f);
    EXPECT_FLOAT_EQ(j.cosFreeLean, std::cos(0.1f));
    EXPECT_FLOAT_EQ(j.freeRoll, 0.1f);
    EXPECT_NEAR((j.position - Vec3{0, 0, 1}).mag(), 0.0f, 1e-6f);
}

TEST(TrailerJoint, JointStatusReplacesTheFlags) {
    InertialCS a, b;
    makeBody(a, 1000.0f, {0, 0, 0});
    makeBody(b, 1000.0f, {0, 0, 3});
    TrailerJoint j;
    TrailerJointParams p; // frictions set, but the file's status wins
    p.jointStatus = 0;
    j.init(p, &a, &b, {0, 0, 1}, {0, 0, -2});
    EXPECT_EQ(j.status, 0);
    p.jointStatus = TrailerJoint::kBroken | TrailerJoint::kForce;
    j.init(p, &a, &b, {0, 0, 1}, {0, 0, -2});
    EXPECT_TRUE(j.isBroken());
    j.reset(); // dgTrailerJoint::Reset unbreaks
    EXPECT_FALSE(j.isBroken());
    // The setters recompute the force flag.
    j.setFrictionLean(0, 0, 0);
    j.setFrictionRoll(0, 0, 0);
    j.setLeanLimit(3.1415927f, 0.0f);
    EXPECT_EQ(j.status & TrailerJoint::kForce, 0);
    j.setLeanLimit(3.0f, 0.0f);
    EXPECT_EQ(j.status & TrailerJoint::kForce, TrailerJoint::kForce);
    j.setRollLimit(0.5f, 0.2f);
    EXPECT_FLOAT_EQ(j.negativeRollLimit, -0.5f);
    EXPECT_FLOAT_EQ(j.positiveRollLimit, 0.5f);
    EXPECT_FLOAT_EQ(j.rollElasticity, 0.2f);
}

TEST(TrailerJoint, SetPositionAndRestOrientation) {
    InertialCS a, b;
    makeBody(a, 1000.0f, {0, 0, 0});
    makeBody(b, 1000.0f, {0, 0, 3}, Mat34::rotationY(0.4f));
    TrailerJoint j;
    j.init(TrailerJointParams{}, &a, &b, {0, 1, 1}, {0, 1, -2});
    j.setPosition({5, 0, 5});
    EXPECT_NEAR((a.matrix.transform(j.offset1) - Vec3{5, 0, 5}).mag(), 0.0f, 1e-5f);
    EXPECT_NEAR((b.matrix.transform(j.offset2) - Vec3{5, 0, 5}).mag(), 0.0f, 1e-5f);
    j.setRestOrientation(); // body 2's rest = R1 R2^T, body 1's identity
    const Mat34 expected = Mat34::mul(a.matrix, b.matrix.fastInverse());
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) {
            EXPECT_NEAR(j.restOrient2.row(r)[c], expected.row(r)[c], 1e-5f);
            EXPECT_NEAR(j.restOrient1.row(r)[c], r == c ? 1.0f : 0.0f, 1e-6f);
        }
}

// Two bodies sharing a joint point 1 m above both centres of mass, the
// trailer turned by `yaw` about the vertical: the joint force is zero, so
// the torques are the restoring torques alone.
struct LeanRig {
    InertialCS a, b;
    TrailerJoint j;
    explicit LeanRig(float yaw, const TrailerJointParams& p = {}) {
        makeBody(a, 1000.0f, {0, 0, 0});
        makeBody(b, 1000.0f, {0, 0, 0}, Mat34::rotationY(yaw));
        j.init(p, &a, &b, {0, 1, 0}, {0, 1, 0});
    }
    // Sign of the turn from the tractor's to the trailer's Z axis.
    float turnSign() const { return a.matrix.m2.cross(b.matrix.m2).y > 0.0f ? 1.0f : -1.0f; }
};

TEST(TrailerJoint, LeanTorqueOnlyBeyondFreeLean) {
    {
        LeanRig r(0.08f); // inside FreeLean (0.1 rad)
        r.j.update(kDt, 1.0f / kDt);
        EXPECT_EQ(r.j.lean, 0.0f);
        EXPECT_NEAR(r.b.angularTorque.mag(), 0.0f, 1e-3f);
    }
    {
        LeanRig r(0.3f);
        r.j.update(kDt, 1.0f / kDt);
        EXPECT_NEAR(r.j.lean, 0.3f, 1e-4f);
        // RestoreForceLean * lean * (40/pi) * m_eff, m_eff = (|o1| |o2|) /
        // (|o2| / m1 + |o1| / m2) = 500 kg m^2; no constant damping at rest.
        const float expected = 2.0f * 0.3f * 12.732395f * 500.0f;
        EXPECT_NEAR(std::abs(r.b.angularTorque.y), expected, 1e-3f * expected);
        EXPECT_LT(r.b.angularTorque.y * r.turnSign(), 0.0f); // restoring
        EXPECT_NEAR(r.a.angularTorque.y, -r.b.angularTorque.y, 1e-3f);
    }
}

TEST(TrailerJoint, LeanDampingIsConstantAndDampLinearLeanHasNoEffect) {
    auto torque = [](float dampLinear) {
        TrailerJointParams p;
        p.dampLinearLean = dampLinear;
        LeanRig r(0.3f, p);
        r.b.angularVelocity = {0, 0.5f, 0};
        r.j.update(kDt, 1.0f / kDt);
        return r.b.angularTorque.y;
    };
    const float t1 = torque(0.9f);
    EXPECT_EQ(t1, torque(50.0f));
    // DampConstLean * 10 * m_eff against the relative turn, on top of the
    // restoring torque.
    LeanRig still(0.3f);
    still.j.update(kDt, 1.0f / kDt);
    EXPECT_NEAR(t1 - still.b.angularTorque.y, -2.0f * 10.0f * 500.0f, 5.0f);
}

TEST(TrailerJoint, RollIsFree) {
    // Relative roll about the trailer's length: MM2 never computes it.
    InertialCS a, b;
    makeBody(a, 1000.0f, {0, 0, 0});
    makeBody(b, 1000.0f, {0, 0, 0}, Mat34::rotationZ(0.6f));
    TrailerJoint j;
    j.init(TrailerJointParams{}, &a, &b, {0, 0, 1}, {0, 0, 1});
    b.angularVelocity = {0, 0, 1.0f};
    j.update(kDt, 1.0f / kDt);
    EXPECT_EQ(j.roll, 0.0f);
    EXPECT_EQ(j.lean, 0.0f);
    EXPECT_EQ(b.angularImpulse.mag(), 0.0f); // no roll limit either
}

TEST(TrailerJoint, LeanLimitStopsTheApproach) {
    TrailerJointParams p;
    p.leanLimit = 0.5f;
    LeanRig r(0.6f, p);
    r.j.update(kDt, 1.0f / kDt);
    EXPECT_GT(r.b.angularImpulse.mag(), 0.0f);
    EXPECT_LT(r.b.angularImpulse.y * r.turnSign(), 0.0f);
    EXPECT_NEAR(r.a.angularImpulse.y, -r.b.angularImpulse.y, 1e-3f);
}

TEST(TrailerJoint, ForceLimitIsInUnitsOf10kN) {
    // Joint at both centres of mass: a 30 kN pull on the trailer needs a
    // 15 kN joint force to keep the pair together.
    auto run = [](float limit) {
        InertialCS a, b;
        makeBody(a, 1000.0f, {0, 0, 0});
        makeBody(b, 1000.0f, {0, 0, 0});
        TrailerJointParams p = noTorques();
        p.forceLimit = limit;
        TrailerJoint j;
        j.init(p, &a, &b, {}, {});
        b.linearForce = {30000.0f, 0, 0};
        j.update(kDt, 1.0f / kDt);
        EXPECT_NEAR(j.jointForce.x, 15000.0f, 1.0f);
        return j.isBroken();
    };
    EXPECT_TRUE(run(1.0f));  // 10 kN
    EXPECT_FALSE(run(2.0f)); // 20 kN
    EXPECT_FALSE(run(0.0f)); // never
}

TEST(TrailerJoint, FreeRangeMovesBothBodiesHalfTheExcess) {
    InertialCS a, b;
    makeBody(a, 1000.0f, {0, 0, 0});
    makeBody(b, 3000.0f, {0, 0, 0.5f});
    TrailerJoint j;
    j.init(noTorques(), &a, &b, {}, {});
    j.update(kDt, 1.0f / kDt);
    EXPECT_NEAR(j.gap.z, 0.5f, 1e-6f);
    EXPECT_NEAR(a.matrix.m3.z, 0.175f, 1e-6f);
    EXPECT_NEAR(b.matrix.m3.z, 0.325f, 1e-6f);
    EXPECT_NEAR(j.position.z, 0.25f, 1e-6f);
    EXPECT_EQ(a.linearVelocity.mag(), 0.0f);
    // Within FreeRange nothing moves.
    j.update(kDt, 1.0f / kDt);
    EXPECT_NEAR(b.matrix.m3.z - a.matrix.m3.z, 0.15f, 1e-6f);
}

TEST(TrailerJoint, ForceTurnsThroughTheInverseMassMatrix) {
    // A pair spinning at 2 rad/s about Y: MM2 turns the joint force with a
    // matrix that still holds the trailer's inverse mass matrix, so the
    // force across the spin axis all but vanishes.
    auto force = [](bool mm2) {
        InertialCS a, b;
        makeBody(a, 1000.0f, {0, 0, 0});
        makeBody(b, 1000.0f, {0, 0, 0});
        a.angularVelocity = b.angularVelocity = {0, 2.0f, 0};
        TrailerJoint j;
        j.init(noTorques(), &a, &b, {}, {});
        j.mm2ForceRotation = mm2;
        b.linearForce = {30000.0f, 6000.0f, 0};
        j.update(kDt, 1.0f / kDt);
        return j.jointForce;
    };
    const Vec3 plain = force(false);
    const Vec3 mm2 = force(true);
    EXPECT_NEAR(plain.x, 15000.0f, 20.0f);
    EXPECT_NEAR(plain.y, 3000.0f, 1.0f);
    // The part along the spin axis passes; the rest is scaled by 1/1000 kg.
    EXPECT_NEAR(mm2.y, 3000.0f, 1.0f);
    EXPECT_NEAR(std::hypot(mm2.x, mm2.z), 15.0f, 0.1f);
}

TEST(TrailerJoint, FreeLinkConservesMomentumAndKeepsTheHitch) {
    for (bool mm2 : {true, false}) {
        InertialCS a, b;
        makeBody(a, 3500.0f, {0, 0, 0});
        makeBody(b, 2000.0f, {0, 0, 7});
        a.setMass(3, 2, 5, 3500.0f);
        b.setMass(2.5f, 1, 12, 2000.0f);
        TrailerJoint j;
        j.init(TrailerJointParams{}, &a, &b, {0, 1, 2.5f}, {0, 1, -4.5f});
        j.mm2ForceRotation = mm2;
        a.linearMomentum = {3000, 0, -20000};
        a.angularMomentum = {0, 2000, 0};
        a.update(kDt, 1.0f / kDt); // velocities from the momenta
        const Vec3 p0 = a.linearMomentum + b.linearMomentum;
        float maxGap = 0.0f;
        for (int i = 0; i < 600; ++i) {
            a.update(kDt, 1.0f / kDt);
            b.update(kDt, 1.0f / kDt);
            j.update(kDt, 1.0f / kDt);
            maxGap = std::max(maxGap, (a.matrix.transform(j.offset1) - b.matrix.transform(j.offset2)).mag());
        }
        const Vec3 p1 = a.linearMomentum + b.linearMomentum;
        EXPECT_LT((p1 - p0).mag(), 1e-3f * p0.mag()) << mm2;
        // After the FreeRange correction the hitch points are never more
        // than FreeRange apart.
        EXPECT_LE(maxGap, j.freeRange + 1e-4f) << mm2;
        EXPECT_TRUE(std::isfinite(a.matrix.m3.x) && std::isfinite(b.matrix.m3.x)) << mm2;
    }
}

TEST(TrailerJoint, InverseMassMatrixThroughTheJoint) {
    // Two identical bodies joined at the midpoint: at the joint, an impulse
    // moves the pair, so the matrix through the joint is half a body's own.
    Body ba, bb;
    makeBody(ba.ics, 100.0f, {0, 0, -2});
    makeBody(bb.ics, 100.0f, {0, 0, 2});
    TrailerJoint j;
    j.init(TrailerJointParams{}, &ba.ics, &bb.ics, {0, 0, 2}, {0, 0, -2});
    Mat34 own, through;
    ba.collider.invMassMatrix(j.position, own); // no joint attached yet
    ba.collider.joint = &j;
    ba.collider.invMassMatrix(j.position, through);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            EXPECT_NEAR(through.row(r)[c], 0.5f * own.row(r)[c], 1e-6f) << r << c;
    j.breakJoint();
    ba.collider.invMassMatrix(j.position, through);
    EXPECT_NEAR(through.m0.x, own.m0.x, 1e-9f);
    // The two-body overload stays finite and symmetric.
    Mat34 c2;
    j.computeInvMassMatrix(&ba.ics, &bb.ics, c2, j.position);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) {
            EXPECT_TRUE(std::isfinite(c2.row(r)[c]));
            EXPECT_NEAR(c2.row(r)[c], c2.row(c)[r], 1e-5f);
        }
}

// --- Tune files --------------------------------------------------------------

TEST(TrailerTune, DgTrailerJointFieldsAndDefaults) {
    const auto defaults =
        loadBlock<TrailerJointParams>("type: a\ndgTrailerJoint {\n}\n", loadTrailerJointParams);
    EXPECT_EQ(defaults.jointStatus, 2);
    EXPECT_FLOAT_EQ(defaults.leanLimit, 3.1415927f);
    EXPECT_FLOAT_EQ(defaults.limitElasticityLean, 1.0f);
    EXPECT_FLOAT_EQ(defaults.freeRange, 0.15f);
    const auto p = loadBlock<TrailerJointParams>(R"(type: a
dgTrailerJoint {
  Offset0 1 2 3
  Offset1 4 5 6
  ForceLimit 12.5
  JointStatus 3
  RestoreForceLean 1.5
  DampConstLean 1.25
  DampLinearLean 0.75
  RestoreForceRoll 2.5
  DampConstRoll 0.5
  DampLinearRoll 3.5
  LeanLimit 1.2
  LimitElasticityLean 0.4
  LimitElasticityRoll 0.6
  NegativeRollLimit -1
  FreeRange 0.3
  FreeLean 0.2
  FreeRoll 0.25
}
)",
                                                 loadTrailerJointParams);
    EXPECT_FLOAT_EQ(p.forceLimit, 12.5f);
    EXPECT_EQ(p.jointStatus, 3);
    EXPECT_FLOAT_EQ(p.restoreForceLean, 1.5f);
    EXPECT_FLOAT_EQ(p.dampConstLean, 1.25f);
    EXPECT_FLOAT_EQ(p.dampLinearLean, 0.75f);
    EXPECT_FLOAT_EQ(p.restoreForceRoll, 2.5f);
    EXPECT_FLOAT_EQ(p.dampConstRoll, 0.5f);
    EXPECT_FLOAT_EQ(p.dampLinearRoll, 3.5f);
    EXPECT_FLOAT_EQ(p.leanLimit, 1.2f);
    EXPECT_FLOAT_EQ(p.limitElasticityLean, 0.4f);
    EXPECT_FLOAT_EQ(p.limitElasticityRoll, 0.6f);
    EXPECT_FLOAT_EQ(p.freeRange, 0.3f);
    EXPECT_FLOAT_EQ(p.freeLean, 0.2f);
    EXPECT_FLOAT_EQ(p.freeRoll, 0.25f);
}

TEST(TrailerTune, VehTrailerFieldsAndDefaults) {
    const auto defaults = loadBlock<TrailerParams>("type: a\nvehTrailer {\n}\n", loadTrailerParams);
    EXPECT_FLOAT_EQ(defaults.mass, 3000.0f);
    EXPECT_FLOAT_EQ(defaults.inertiaBox.y, 4.0f);
    EXPECT_FALSE(defaults.carHitchOffset);
    EXPECT_FLOAT_EQ(defaults.drivetrain.angInertia, 5000.0f);
    const auto p = loadBlock<TrailerParams>(R"(type: a
vehTrailer {
  Mass 1500
  InertiaBox 1 2 3
  CarHitchOffset 0 0.5 2
  TrailerHitchOffset 0 0.6 -5
  WheelBack {
    SuspensionExtent 0.3
  }
  Drivetrain {
    AngInertia 100
  }
}
)",
                                            loadTrailerParams);
    EXPECT_FLOAT_EQ(p.mass, 1500.0f);
    EXPECT_FLOAT_EQ(p.inertiaBox.z, 3.0f);
    ASSERT_TRUE(p.carHitchOffset && p.trailerHitchOffset);
    EXPECT_FLOAT_EQ(p.carHitchOffset->z, 2.0f);
    EXPECT_FLOAT_EQ(p.trailerHitchOffset->y, 0.6f);
    EXPECT_FLOAT_EQ(p.wheelBack.suspensionExtent, 0.3f);
    EXPECT_FLOAT_EQ(p.drivetrain.angInertia, 100.0f);
}

// --- vpsemi and vpcentury from the retail data -------------------------------

namespace {

std::optional<data::DatFile> readDat(const vfs::Vfs& fs, const std::string& path) {
    auto bytes = fs.readAll(path);
    if (!bytes)
        return std::nullopt;
    return data::parseDat(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
}

std::optional<Mat34> readPivot(const vfs::Vfs& fs, const std::string& path) {
    auto bytes = fs.readAll(path);
    if (!bytes || bytes->size() < 48)
        return std::nullopt;
    Mat34 m;
    for (int r = 0; r < 4; ++r)
        m.row(r) = {loadLE<float>(bytes->data() + r * 12), loadLE<float>(bytes->data() + r * 12 + 4),
                    loadLE<float>(bytes->data() + r * 12 + 8)};
    return m;
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

float energy(const InertialCS& ics) {
    const float ke =
        0.5f * ics.mass * ics.linearVelocity.mag2() + 0.5f * ics.angularMomentum.dot(ics.angularVelocity);
    return ke + ics.mass * kGravity * ics.matrix.m3.y;
}

// A tractor and its trailer on a flat plane, as vehCar::Init builds them.
struct Rig {
    World world;
    std::unique_ptr<CarSim> car = std::make_unique<CarSim>();
    std::unique_ptr<Trailer> trailer = std::make_unique<Trailer>();
    TrailerJointParams jointParams;
    ArcadeControls controls;
    float maxGap = 0.0f;
    float maxHitchAngle = 0.0f;
    float minUp = 1.0f;
    int bottomedSamples = 0;        // samples with a trailer wheel bottomed out
    int tractorBottomedSamples = 0; // the same for the tractor's wheels

    static std::unique_ptr<Rig> load(const std::string& name, const TrailerOptions& options = {}) {
        const vfs::Vfs* fs = test::gameData();
        auto r = std::make_unique<Rig>();
        auto sim = readDat(*fs, "tune/vehicle/" + name + ".vehcarsim");
        auto trailer = readDat(*fs, "tune/vehicle/" + name + ".vehtrailer");
        auto joint = readDat(*fs, "tune/vehicle/" + name + ".dgtrailerjoint");
        CarSimParams cp;
        TrailerParams tp;
        if (!sim || !sim->top() || !loadCarSimParams(*sim->top(), cp) || !trailer || !trailer->top() ||
            !loadTrailerParams(*trailer->top(), tp) || !joint || !joint->top() ||
            !loadTrailerJointParams(*joint->top(), r->jointParams))
            return nullptr;
        VehicleGeometry g = VehicleGeometry::placeholder();
        TrailerGeometry tg;
        for (int i = 0; i < 4; ++i) {
            const auto w = readPivot(*fs, "geometry/" + name + "_whl" + std::to_string(i) + ".mtx");
            const auto t = readPivot(*fs, "geometry/" + name + "_trailer_twhl" + std::to_string(i) + ".mtx");
            if (!w || !t)
                return nullptr;
            g.wheels[static_cast<std::size_t>(i)] = VehicleGeometry::wheelFromPivot(*w);
            tg.wheels[static_cast<std::size_t>(i)] = VehicleGeometry::wheelFromPivot(*t);
        }
        if (auto h = readPivot(*fs, "geometry/" + name + "_trailer_hitch.mtx"))
            tg.carHitch = h->m3;
        if (auto h = readPivot(*fs, "geometry/" + name + "_trailer_trailer_hitch.mtx"))
            tg.trailerHitch = h->m3;
        r->world.setStatic(flatGround());
        r->car->init(cp, g);
        r->car->reset(Mat34::identity());
        r->world.add(&r->car->body);
        r->trailer->init(tp, r->jointParams, tg, *r->car, options);
        r->trailer->addTo(r->world);
        return r;
    }
    void step(const PedalInput& in) {
        controls.apply(*car, in);
        world.step(kDt);
        maxGap = std::max(maxGap, trailer->hitchGap());
        maxHitchAngle = std::max(maxHitchAngle, std::abs(trailer->hitchAngle()));
        minUp = std::min({minUp, trailer->body.ics.matrix.m1.y, car->body.ics.matrix.m1.y});
        if (trailer->bottomedOut() > 0)
            ++bottomedSamples;
        if (std::ranges::any_of(car->wheels, [](const Wheel& w) { return w.bottomedOut; }))
            ++tractorBottomedSamples;
    }
    void run(float seconds, const PedalInput& in) {
        const int n = static_cast<int>(seconds / kDt + 0.5f);
        for (int i = 0; i < n; ++i)
            step(in);
    }
    // Lets the rig settle on its suspension (it starts with the trailer's
    // wheels off the ground), then clears the statistics.
    void settle(float seconds = 2.0f) {
        run(seconds, {});
        clear();
    }
    void clear() {
        maxGap = 0.0f;
        maxHitchAngle = 0.0f;
        minUp = 1.0f;
        bottomedSamples = 0;
        tractorBottomedSamples = 0;
    }
    float energy() const { return ::energy(car->body.ics) + ::energy(trailer->body.ics); }
    Vec3 tractorHitch() const { return car->body.ics.matrix.transform(trailer->carHitchOffset); }
    Vec3 trailerHitch() const { return trailer->body.ics.matrix.transform(trailer->trailerHitchOffset); }
    bool finite() const {
        for (const InertialCS* ics : {&car->body.ics, &trailer->body.ics}) {
            const Vec3& p = ics->matrix.m3;
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                return false;
        }
        return true;
    }
};

// The two trucks with a trailer.
const char* const kTrucks[] = {"vpcentury", "vpsemi"};

} // namespace

TEST(Trailer, SetupFollowsVehTrailerInit) {
    MM2_REQUIRE_GAME_DATA();
    {
        // vpcentury: the hitches come from the models' trailer_hitch pivots
        // (its .dgTrailerJoint Offset0/Offset1 are not read).
        auto r = Rig::load("vpcentury");
        ASSERT_TRUE(r);
        const Trailer& t = *r->trailer;
        EXPECT_NEAR(t.carHitchOffset.z, 2.4225f, 1e-3f);
        EXPECT_NEAR(t.trailerHitchOffset.z, -4.8876f, 1e-3f);
        EXPECT_NEAR((t.originOffset - (t.carHitchOffset - t.trailerHitchOffset)).mag(), 0.0f, 1e-6f);
        // The joint uses the hitches in each body's InertialCS space.
        EXPECT_NEAR((t.joint.offset1 - t.carHitchOffset).mag(), 0.0f, 1e-6f);
        EXPECT_NEAR((t.joint.offset2 - t.trailerHitchOffset).mag(), 0.0f, 1e-6f);
        EXPECT_EQ(t.body.joint, &t.joint);
        EXPECT_EQ(r->car->body.joint, &t.joint);
        // Reset closes the hitch.
        EXPECT_LT((r->tractorHitch() - r->trailerHitch()).mag(), 1e-4f);
        // Mass 2000, the trailer's centre of mass at its model origin.
        EXPECT_FLOAT_EQ(t.body.ics.mass, 2000.0f);
        EXPECT_FLOAT_EQ(t.body.ics.maxAngVelocity.x, 5.0f);
        // Static loads (both axles behind the origin): the axle carries
        // W |hz| / L, shared by the two wheels that reach the ground.
        const float w = 2000.0f * 19.6f;
        const float zm = (t.wheels[0].center.z + t.wheels[2].center.z) * 0.5f;
        const float lever = std::abs(t.trailerHitchOffset.z - zm);
        EXPECT_NEAR(t.wheels[2].normalLoad, w * std::abs(t.trailerHitchOffset.z) / lever * 0.5f, 1.0f);
        EXPECT_NEAR(t.wheels[3].normalLoad, t.wheels[2].normalLoad, 1e-3f);
    }
    {
        // MM2's own values: a quarter of the hitch's share on each trailer
        // wheel, the axle's share added to the tractor.
        TrailerOptions o;
        o.mm2StaticLoads = true;
        auto r = Rig::load("vpcentury", o);
        auto plain = Rig::load("vpcentury");
        ASSERT_TRUE(r && plain);
        const Trailer& t = *r->trailer;
        const float w = 2000.0f * 19.6f;
        const float zm = (t.wheels[0].center.z + t.wheels[2].center.z) * 0.5f;
        const float lever = std::abs(t.trailerHitchOffset.z - zm);
        for (const Wheel& wh : t.wheels)
            EXPECT_NEAR(wh.normalLoad, std::abs(zm) * w * 0.25f / lever, 1.0f);
        const float share = std::abs(t.trailerHitchOffset.z) * w / lever;
        float added = 0.0f;
        for (std::size_t i = 0; i < 4; ++i)
            added += r->car->wheels[i].normalLoad - plain->car->wheels[i].normalLoad;
        const float plainShare = std::abs(zm) * w / lever;
        EXPECT_NEAR(added, share - plainShare, 2.0f);
    }
    {
        // vpsemi: the hitches from its .vehTrailer, and a tiller axle in
        // front of the origin, so the wheels keep a quarter of the weight.
        auto r = Rig::load("vpsemi");
        ASSERT_TRUE(r);
        const Trailer& t = *r->trailer;
        EXPECT_NEAR(t.carHitchOffset.y, 0.406f, 1e-4f);
        EXPECT_NEAR(t.trailerHitchOffset.y, 0.576f, 1e-4f);
        EXPECT_FLOAT_EQ(r->jointParams.freeRange, 0.14f);
        EXPECT_FLOAT_EQ(r->jointParams.forceLimit, 40.0f); // 400 kN
        for (const Wheel& wh : t.wheels)
            EXPECT_NEAR(wh.normalLoad, 2000.0f * 19.6f * 0.25f, 0.5f);
        EXPECT_LT((r->tractorHitch() - r->trailerHitch()).mag(), 1e-4f);
    }
}

TEST(Trailer, StaysAtRest) {
    MM2_REQUIRE_GAME_DATA();
    for (const char* name : kTrucks) {
        auto r = Rig::load(name);
        ASSERT_TRUE(r) << name;
        r->settle(3.0f);
        const float e0 = r->energy();
        const Vec3 p0 = r->trailer->body.ics.matrix.m3;
        r->run(30.0f, {});
        ASSERT_TRUE(r->finite()) << name;
        EXPECT_LT((r->trailer->body.ics.matrix.m3 - p0).mag(), 0.05f) << name;
        EXPECT_LT(r->energy(), e0 + 100.0f) << name; // no energy from nowhere (J)
        EXPECT_LT(r->maxGap, 0.02f) << name;
        EXPECT_GT(r->minUp, 0.99f) << name;
        EXPECT_EQ(r->bottomedSamples, 0) << name;
        // vehTrailer::Init adds the hitch's share to the tractor's springs.
        EXPECT_EQ(r->tractorBottomedSamples, 0) << name;
    }
}

TEST(Trailer, MM2StaticLoadsLeaveVpcenturyOnItsBumpStops) {
    // Why OpenMM2 corrects vehTrailer::Init's loads by default: with MM2's
    // values vpcentury's trailer wheels bottom out at rest and the rig does
    // not come to rest.
    MM2_REQUIRE_GAME_DATA();
    TrailerOptions o;
    o.mm2StaticLoads = true;
    auto r = Rig::load("vpcentury", o);
    ASSERT_TRUE(r);
    r->settle(3.0f);
    r->run(10.0f, {});
    EXPECT_GT(r->bottomedSamples, 300);
    EXPECT_GT(r->maxGap, r->trailer->joint.freeRange);
}

TEST(Trailer, StraightRunIsStable) {
    MM2_REQUIRE_GAME_DATA();
    for (const char* name : kTrucks) {
        auto r = Rig::load(name);
        ASSERT_TRUE(r) << name;
        r->settle();
        PedalInput in;
        in.accelerator = 1.0f;
        const Vec3 start = r->car->modelMatrix().m3;
        float lastDist = 0.0f, top = 0.0f;
        for (int s = 0; s < 60; ++s) {
            r->run(1.0f, in);
            ASSERT_TRUE(r->finite()) << name << " t=" << s;
            const float dist = (r->car->modelMatrix().m3 - start).mag();
            EXPECT_GT(dist, lastDist) << name << " distance must grow, t=" << s;
            lastDist = dist;
            top = std::max(top, r->car->speedMph());
        }
        EXPECT_GT(top, 80.0f) << name;
        EXPECT_LT(top, 130.0f) << name;
        EXPECT_LT(r->maxGap, 0.05f) << name;
        EXPECT_LT(r->maxHitchAngle, 3.0f * kDeg) << name;
        EXPECT_GT(r->minUp, 0.99f) << name;
        EXPECT_EQ(r->bottomedSamples, 0) << name;
        EXPECT_EQ(r->tractorBottomedSamples, 0) << name;
    }
}

TEST(Trailer, CoastingNeverGainsEnergy) {
    MM2_REQUIRE_GAME_DATA();
    for (const char* name : kTrucks) {
        auto r = Rig::load(name);
        ASSERT_TRUE(r) << name;
        r->settle();
        PedalInput in;
        in.accelerator = 1.0f;
        in.steering = 0.1f;
        r->run(15.0f, in);
        ASSERT_GT(r->car->speedMph(), 30.0f) << name;
        float e = r->energy();
        for (int s = 0; s < 20; ++s) {
            r->run(1.0f, {});
            const float e1 = r->energy();
            EXPECT_LE(e1, e + 2000.0f) << name << " t=" << s; // J; suspension bounce only
            e = std::min(e, e1);
        }
        ASSERT_TRUE(r->finite()) << name;
    }
}

TEST(Trailer, SteadyCircleStaysBounded) {
    MM2_REQUIRE_GAME_DATA();
    for (const char* name : kTrucks) {
        for (bool mm2Rotation : {true, false}) {
            TrailerOptions o;
            o.mm2ForceRotation = mm2Rotation;
            auto r = Rig::load(name, o);
            ASSERT_TRUE(r) << name;
            r->settle();
            PedalInput in;
            in.accelerator = 0.5f;
            in.steering = 0.3f;
            r->run(10.0f, in);
            r->clear();
            r->run(20.0f, in);
            ASSERT_TRUE(r->finite()) << name;
            EXPECT_GT(r->car->speedMph(), 20.0f) << name;
            EXPECT_LT(r->maxHitchAngle, 20.0f * kDeg) << name;
            EXPECT_GT(r->minUp, 0.98f) << name;
            // MM2 transmits little of the hitch force while the pair turns
            // and leans on the FreeRange correction; the plain rotation
            // keeps the hitch within FreeRange on its own.
            const float limit = mm2Rotation ? 1.0f : r->trailer->joint.freeRange + 0.02f;
            EXPECT_LT(r->maxGap, limit) << name << " mm2 rotation " << mm2Rotation;
        }
    }
}

TEST(Trailer, HardBrakingDoesNotJackknife) {
    MM2_REQUIRE_GAME_DATA();
    for (const char* name : kTrucks) {
        for (float steer : {0.0f, 0.15f}) {
            auto r = Rig::load(name);
            ASSERT_TRUE(r) << name;
            r->settle();
            PedalInput go;
            go.accelerator = 1.0f;
            go.steering = steer;
            r->run(12.0f, go);
            ASSERT_GT(r->car->speedMph(), 40.0f) << name << " " << steer;
            r->clear();
            PedalInput stop;
            stop.brake = 1.0f;
            stop.steering = steer;
            float t = 0.0f;
            while (r->car->speed() > 0.5f && t < 10.0f) {
                r->step(stop);
                t += kDt;
            }
            EXPECT_LT(t, 6.0f) << name << " " << steer;
            EXPECT_LT(r->maxHitchAngle, 10.0f * kDeg) << name << " " << steer;
            EXPECT_GT(r->minUp, 0.98f) << name << " " << steer;
            EXPECT_LT(r->maxGap, 1.0f) << name << " " << steer;
        }
    }
}

TEST(Trailer, DeterministicAndResettable) {
    MM2_REQUIRE_GAME_DATA();
    auto runOnce = [] {
        auto r = Rig::load("vpcentury");
        PedalInput in;
        in.accelerator = 1.0f;
        in.steering = 0.4f;
        r->run(8.0f, in);
        return r->trailer->body.ics.matrix;
    };
    const Mat34 a = runOnce();
    const Mat34 b = runOnce();
    EXPECT_EQ(std::memcmp(&a, &b, sizeof(Mat34)), 0);

    auto r = Rig::load("vpcentury");
    ASSERT_TRUE(r);
    PedalInput in;
    in.accelerator = 1.0f;
    in.steering = 0.4f;
    r->run(5.0f, in);
    Mat34 m = Mat34::rotationY(1.0f);
    m.m3 = {30, 0, -40};
    r->car->reset(m);
    r->trailer->reset();
    EXPECT_LT((r->tractorHitch() - r->trailerHitch()).mag(), 1e-4f);
    EXPECT_NEAR(r->trailer->body.ics.matrix.m2.dot(r->car->body.ics.matrix.m2), 1.0f, 1e-5f);
    EXPECT_FALSE(r->trailer->joint.isBroken());
    r->settle();
    r->run(3.0f, {});
    ASSERT_TRUE(r->finite());
    EXPECT_LT(r->maxGap, 0.02f);
}
