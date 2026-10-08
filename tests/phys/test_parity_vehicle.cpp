// Parity checks for the vehicle area (docs/parity/vehicle.md): behaviours of
// MM2's vehCarSim family that are easy to lose in a port.

#include "phys/Constants.h"
#include "phys/PolygonSoup.h"
#include "phys/World.h"
#include "phys/vehicle/CarSim.h"
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
    g.materialNames = {"_default"};
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
