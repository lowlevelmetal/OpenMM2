// Parity checks for the vehicle area (docs/parity/vehicle.md): behaviours of
// MM2's vehCarSim family that are easy to lose in a port.

#include "phys/Constants.h"
#include "phys/PolygonSoup.h"
#include "phys/World.h"
#include "phys/vehicle/CarSim.h"
#include "phys/vehicle/Controls.h"
#include "phys/vehicle/Trailer.h"
#include "phys/vehicle/VehicleGeometry.h"
#include "phys/vehicle/Wheel.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>

using namespace mm2;
using namespace mm2::phys;

namespace {

CarSimParams tunedParams() {
    CarSimParams p;
    p.mass = 1200.0f;
    p.wheelFront.handbrakeCoef = 0.5f;
    p.wheelFront.wobbleLimit = 0.2f;
    p.wheelBack.handbrakeCoef = 2.0f;
    p.wheelBack.wobbleLimit = 0.3f;
    p.wheelBack.brakeCoef = 0.6f;
    p.wheelBack.staticFric = 3.0f;
    return p;
}

PolygonSoup flatGround(float half = 2000.0f) {
    SoupGeometry g;
    g.vertices = {{-half, 0, -half}, {-half, 0, half}, {half, 0, half}, {half, 0, -half}};
    g.polys.push_back({{0, 1, 2, 3}, 4, 0});
    g.materialNames = {"default"};
    PolygonSoup soup;
    soup.add(g, Mat34::identity(), MaterialTable{});
    soup.finalize(512.0f);
    return soup;
}

struct Rig {
    World world;
    std::unique_ptr<CarSim> car = std::make_unique<CarSim>();

    explicit Rig(const CarSimParams& p = CarSimParams{}, const CarSimOptions& o = {}) {
        world.setStatic(flatGround());
        car->init(p, VehicleGeometry::placeholder(), o);
        car->reset(Mat34::identity());
        world.add(&car->body);
    }
    void run(int samples, float throttle = 0.0f, float brake = 0.0f) {
        for (int i = 0; i < samples; ++i) {
            car->setInputs(throttle, brake, 0.0f, 0.0f);
            world.step(kFixedSampleStep);
        }
    }
};

} // namespace

// vehWheel::CopyVars copies every tune field but HandbrakeCoef and
// WobbleLimit, so the right wheels keep the constructor's 1 and 0.
TEST(VehicleParity, RightWheelsKeepTheirHandbrakeCoef) {
    CarSim car;
    car.init(tunedParams(), VehicleGeometry::placeholder());
    EXPECT_FLOAT_EQ(car.wheels[0].params.handbrakeCoef, 0.5f);
    EXPECT_FLOAT_EQ(car.wheels[2].params.handbrakeCoef, 2.0f);
    EXPECT_FLOAT_EQ(car.wheels[1].params.handbrakeCoef, 1.0f);
    EXPECT_FLOAT_EQ(car.wheels[3].params.handbrakeCoef, 1.0f);
    EXPECT_FLOAT_EQ(car.wheels[1].params.wobbleLimit, 0.0f);
    EXPECT_FLOAT_EQ(car.wheels[3].params.wobbleLimit, 0.0f);
    // Everything else is copied, so only the handbrake torque differs.
    EXPECT_FLOAT_EQ(car.wheels[3].params.brakeCoef, 0.6f);
    EXPECT_FLOAT_EQ(car.wheels[3].params.staticFric, 3.0f);
    EXPECT_FLOAT_EQ(car.wheels[3].maxBrakeTorque, car.wheels[2].maxBrakeTorque);
    EXPECT_FLOAT_EQ(car.wheels[3].maxHandbrakeTorque, car.wheels[2].maxHandbrakeTorque * 0.5f);
}

// vehCarDamage::Update: between MedDamage and MaxDamage the wheels wobble,
// front-left and back-right by -0.15 of the damage fraction, the others by
// 0.35 (less when the front-left wheel spins).
TEST(VehicleParity, DamagedWheelsWobble) {
    Rig r;
    r.run(30);
    for (const Wheel& w : r.car->wheels)
        EXPECT_FLOAT_EQ(w.wobble, 0.0f);
    CarDamage& d = r.car->damage;
    d.currentDamage = d.params.medDamage + (d.params.maxDamage - d.params.medDamage) * 0.5f;
    r.run(1);
    EXPECT_NEAR(d.damage, 0.5f, 1e-4f);
    const float spin = std::abs(r.car->wheels[0].rotationSpeed) * kFixedSampleStep * 2.0f / 3.1415927f;
    const float f = (1.0f - spin) * d.damage;
    EXPECT_NEAR(r.car->wheels[0].wobble, f * -0.15f, 1e-5f);
    EXPECT_NEAR(r.car->wheels[3].wobble, f * -0.15f, 1e-5f);
    EXPECT_NEAR(r.car->wheels[1].wobble, f * 0.35f, 1e-5f);
    EXPECT_NEAR(r.car->wheels[2].wobble, f * 0.35f, 1e-5f);
}

// mmPlayer::Update takes the controls only once the car is strictly past
// MaxDamage (mmPlayer::IsMaxDamaged).
TEST(VehicleParity, PlayerLosesControlStrictlyPastMaxDamage) {
    CarSimOptions o;
    o.player = true;
    Rig r(CarSimParams{}, o);
    r.run(30);
    CarDamage& d = r.car->damage;
    d.currentDamage = d.params.maxDamage;
    r.run(1, 1.0f);
    EXPECT_FLOAT_EQ(r.car->engine.throttle, 1.0f);
    d.currentDamage = d.params.maxDamage * 1.001f;
    r.run(1, 1.0f);
    EXPECT_FLOAT_EQ(r.car->engine.throttle, 0.0f);
}

// vehCar::Update runs vehSplash (and vehStuck) only while the car is
// drivable: a car held on the start line in water does not float.
TEST(VehicleParity, HeldCarRunsNoSplash) {
    Rig held, free;
    held.car->drivable = false;
    for (Rig* r : {&held, &free}) {
        r->run(30);
        r->car->setWaterLevel(5.0f);
    }
    const float y0 = held.car->modelMatrix().m3.y;
    held.run(60);
    free.run(60);
    EXPECT_TRUE(held.car->splash.active());
    EXPECT_NEAR(held.car->modelMatrix().m3.y, y0, 0.01f);
    EXPECT_GT(free.car->modelMatrix().m3.y, y0 + 0.5f);
}

// vehCar::PreUpdate's hold (brake on, neutral) with the throttle down: the
// clutch is open, so the engine revs freely up to MaxRPM (its torque tapers
// to zero there) and the car stays put.
TEST(VehicleParity, ThrottleInNeutralRevsTheEngineOnly) {
    Rig r;
    r.run(30);
    r.car->trans.setNeutral();
    r.run(180, 1.0f, 1.0f);
    const float rpm = r.car->engine.rpm;
    r.run(60, 1.0f, 1.0f);
    EXPECT_EQ(r.car->trans.currentGear, Transmission::kNeutral);
    EXPECT_FALSE(r.car->drivetrains[2].attached());
    EXPECT_GT(rpm, r.car->engine.optRPM);
    EXPECT_GT(r.car->engine.rpm, rpm);
    EXPECT_LE(r.car->engine.rpm, r.car->engine.maxRPM);
    EXPECT_LT(r.car->speed(), 0.05f);
}

// vehCarSim::SetResetPos + Reset: the body at the position plus
// CenterOfGravity, so the model origin is offset by CG + R * CG.
TEST(VehicleParity, ResetAtPlacesTheBodyAtPositionPlusCenterOfGravity) {
    CarSimParams p;
    p.centerOfGravity = {0.0f, -0.1f, 0.15f};
    CarSim car;
    car.init(p, VehicleGeometry::placeholder());
    const Vec3 pos{10.0f, 2.0f, -5.0f};
    car.resetAt(pos, 0.0f);
    EXPECT_NEAR((car.body.ics.matrix.m3 - (pos + p.centerOfGravity)).mag(), 0.0f, 1e-5f);
    EXPECT_NEAR((car.modelMatrix().m3 - (pos + p.centerOfGravity * 2.0f)).mag(), 0.0f, 1e-5f);
    car.resetAt(pos, 3.1415927f);
    // Turned around, R * CG cancels the horizontal part of CG.
    EXPECT_NEAR(car.modelMatrix().m3.z, pos.z, 1e-5f);
    EXPECT_NEAR(car.modelMatrix().m3.y, pos.y - 0.2f, 1e-5f);
    EXPECT_NEAR(car.modelMatrix().m2.z, -1.0f, 1e-5f);
}

// vehCar::RequiresTerrainCollision: a car resting level on its wheels needs
// no terrain collision for its body.
TEST(VehicleParity, UprightCarOnItsWheelsSkipsTerrainCollision) {
    Rig r;
    r.run(60);
    EXPECT_EQ(r.car->wheelsOnGround(), 4);
    EXPECT_FALSE(r.car->requiresTerrainCollision());
    Mat34 tilted = Mat34::rotationZ(1.2f);
    tilted.m3 = {0.0f, 3.0f, 0.0f};
    r.car->reset(tilted);
    EXPECT_TRUE(r.car->requiresTerrainCollision());
}

// mmPlayer::UpdateRegen: above 5 m/s the damage heals by MaxDamage / 2000 a
// frame and is cleared once that empties it.
TEST(VehicleParity, RegenerationHealsWhileMoving) {
    CarSim car;
    car.init(CarSimParams{}, VehicleGeometry::placeholder());
    CarDamage& d = car.damage;
    d.currentDamage = d.params.maxDamage * 0.001f;
    car.body.ics.linearVelocity = {0.0f, 0.0f, 4.0f};
    EXPECT_FALSE(car.regenerate());
    EXPECT_FLOAT_EQ(d.currentDamage, d.params.maxDamage * 0.001f);
    car.body.ics.linearVelocity = {0.0f, 0.0f, 6.0f};
    EXPECT_FALSE(car.regenerate());
    EXPECT_FLOAT_EQ(d.currentDamage, d.params.maxDamage * 0.001f + d.params.maxDamage * -0.0005f);
    EXPECT_TRUE(car.regenerate());
    EXPECT_FLOAT_EQ(d.currentDamage, 0.0f);
}

// mmInput::FilterDiscreteSteering with mmPlayer::Update's defaults: at a
// standstill the speed blend is SpeedBaseLow / (Hi - Low) = 5 / 95; the
// first step from the centre uses DeltaIn (the signs differ), later ones
// DeltaOut; the car gets |position|^Filter.
TEST(VehicleParity, KeyboardSteeringFilter) {
    SteeringFilter s;
    s.setSpeed(0.0f);
    const float f = 5.0f / 95.0f;
    const float in = (1.5f - 2.5f) * f + 2.5f;
    const float out = (2.5f - 3.5f) * f + 3.5f;
    const float e = (1.0f - 2.0f) * f + 2.0f;
    const float dt = 1.0f / 60.0f;
    const float p1 = in * dt;
    EXPECT_NEAR(s.filter(1.0f, dt), std::pow(p1, e), 1e-6f);
    const float p2 = out * dt + p1;
    EXPECT_NEAR(s.filter(1.0f, dt), std::pow(p2, e), 1e-6f);
    // Letting go returns at DeltaIn and stops at the centre.
    EXPECT_NEAR(s.filter(0.0f, dt), std::pow(p2 - in * dt, e), 1e-6f);
    for (int i = 0; i < 60; ++i)
        s.filter(0.0f, dt);
    EXPECT_FLOAT_EQ(s.filter(0.0f, dt), 0.0f);
    // Fast, the curve is nearly linear and the rates slower.
    s.setSpeed(100.0f);
    const float fh = 100.0f / 95.0f;
    const float inHi = (1.5f - 2.5f) * fh + 2.5f;
    EXPECT_NEAR(s.filter(-1.0f, dt), -std::pow(inHi * dt, (1.0f - 2.0f) * fh + 2.0f), 1e-6f);
}

// dgTrailerJoint::Update's debug key: Ctrl+B breaks a holding hitch.
TEST(VehicleParity, CtrlBBreaksTheTrailerHitch) {
    World world;
    world.setStatic(flatGround());
    CarSim car;
    car.init(CarSimParams{}, VehicleGeometry::placeholder());
    car.reset(Mat34::identity());
    TrailerGeometry tg;
    for (std::size_t i = 0; i < 4; ++i)
        tg.wheels[i] = VehicleGeometry::placeholder().wheels[i];
    tg.carHitch = Vec3{0.0f, 0.5f, 2.5f};
    tg.trailerHitch = Vec3{0.0f, 0.5f, -3.0f};
    Trailer trailer;
    trailer.init(TrailerParams{}, TrailerJointParams{}, tg, car);
    world.add(&car.body);
    trailer.addTo(world);
    world.step(kFixedSampleStep);
    EXPECT_FALSE(trailer.joint.isBroken());
    Trailer::breakKeyPressed = true;
    world.step(kFixedSampleStep);
    Trailer::breakKeyPressed = false;
    EXPECT_TRUE(trailer.joint.isBroken());
}

// The level's sphere of a car (vehCarModel::GetPosition, lvlInstance::
// GetRadius): one up axis above the centre of mass, with the body
// geometry's radius; a trailer's (vehTrailerInstance) sits at its centre of
// mass.
TEST(VehicleParity, VehicleSphereIsMm2s) {
    CarSim car;
    car.init(CarSimParams{}, VehicleGeometry::placeholder());
    Mat34 m = Mat34::identity();
    m.m0 = {0.0f, 1.0f, 0.0f}; // on its side
    m.m1 = {-1.0f, 0.0f, 0.0f};
    m.m3 = {10.0f, 2.0f, -5.0f};
    car.reset(m);
    const Vec3& p = car.body.ics.matrix.m3;
    const Vec3 c = car.body.position();
    EXPECT_FLOAT_EQ(c.x, p.x - 1.0f);
    EXPECT_FLOAT_EQ(c.y, p.y);
    EXPECT_FLOAT_EQ(c.z, p.z);
    // Without its model, the bound's sphere; with it, the geometry's.
    EXPECT_FLOAT_EQ(car.body.radius(), car.body.Body::radius());
    car.body.geometryRadius = 3.25f;
    EXPECT_FLOAT_EQ(car.body.radius(), 3.25f);

    TrailerGeometry tg;
    for (std::size_t i = 0; i < 4; ++i)
        tg.wheels[i] = VehicleGeometry::placeholder().wheels[i];
    tg.carHitch = Vec3{0.0f, 0.5f, 2.5f};
    tg.trailerHitch = Vec3{0.0f, 0.5f, -3.0f};
    Trailer trailer;
    trailer.init(TrailerParams{}, TrailerJointParams{}, tg, car);
    const Vec3 t = trailer.body.position();
    EXPECT_EQ(t.x, trailer.body.ics.matrix.m3.x);
    EXPECT_EQ(t.y, trailer.body.ics.matrix.m3.y);
    EXPECT_EQ(t.z, trailer.body.ics.matrix.m3.z);
}
