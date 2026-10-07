#pragma once

#include "phys/vehicle/TuneParams.h"

namespace mm2::phys {

class InertialCS;

// vehGyro (Midtown Madness 2), verified against the build 3393 code. Yaw
// assists while all four wheels are on the ground: Drift turns the car with
// the (speed-sensitive) steering in proportion to the drivetrain's speed;
// with the handbrake, Spin180 (rolling forward) or Reverse180 (backwards)
// adds a yaw torque. With the brake held in the air, Pitch and Roll level the
// car.
class Gyro {
public:
    void configure(const GyroParams& p) { params = p; }

    struct Inputs {
        float steering = 0;       // raw steering
        float sssFactor = 1;      // vehCarSim::GetSSSFactor at the current speed
        float drivetrainSpeed = 0; // primary drivetrain rad/s (negative forward)
        float brake = 0;
        float handbrake = 0;
        int wheelsOnGround = 0;
        int numWheels = 4;
    };
    void update(InertialCS& ics, const Inputs& in) const;

    GyroParams params;
    bool enabled = true;
};

} // namespace mm2::phys
