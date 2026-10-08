// Parity checks for the vehicle area (docs/parity/vehicle.md): behaviours of
// MM2's vehCarSim family that are easy to lose in a port.

#include "phys/vehicle/CarSim.h"
#include "phys/vehicle/VehicleGeometry.h"
#include "phys/vehicle/Wheel.h"

#include <gtest/gtest.h>

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
