#include "phys/vehicle/Gyro.h"

#include "phys/InertialCS.h"

#include <cmath>

namespace mm2::phys {

void Gyro::update(InertialCS& ics, float dt, float steering, float handBrake, float throttle, float latSlip,
                  bool onGround) {
    (void)dt;
    if (!onGround || steering == 0.0f)
        return;
    const Mat34& m = ics.matrix;
    const float fwdSpeed = -ics.linearVelocity.dot(m.m2);
    float accel = 0.0f; // yaw angular acceleration, rad/s^2, positive = turn right
    if (handBrake > 0.0f && std::abs(fwdSpeed) > 2.0f) {
        const float gain = fwdSpeed >= 0.0f ? params.spin180 : params.reverse180 / 3.14159265f;
        accel = gain * 6.2831855f * steering * handBrake;
    } else if (throttle > 0.0f && std::abs(latSlip) > 0.3f) {
        accel = params.drift * 6.2831855f * steering * throttle;
    }
    if (accel == 0.0f)
        return;
    // Turning right is a negative rotation about the car's up axis.
    ics.applyTorque(m.m1 * (-accel * ics.inertia.y));
}

} // namespace mm2::phys
