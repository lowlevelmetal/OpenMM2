#pragma once

#include "phys/vehicle/TuneParams.h"

namespace mm2::phys {

class InertialCS;

// vehGyro (MM2: Drift, Spin180, Reverse180). MM1's VehGyro was a different
// helper (steer torque ~ v^2 and a self-righting "weeble" torque) and MM2's
// behaviour is undocumented, so this is an OpenMM2 approximation and is
// DISABLED by default (CarSim::Options::gyro). Inferred behaviour: while the
// handbrake is held on the ground, yaw towards the steering at Spin180 turns
// per second squared (Reverse180 radians when rolling backwards); while
// sliding with throttle, Drift does the same with a smaller gain.
class Gyro {
public:
    void configure(const GyroParams& p) { params = p; }
    void update(InertialCS& ics, float dt, float steering, float handBrake, float throttle, float latSlip,
                bool onGround);

    GyroParams params;
};

} // namespace mm2::phys
