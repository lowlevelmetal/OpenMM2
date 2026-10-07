#pragma once

#include "phys/vehicle/TuneParams.h"

namespace mm2::phys {

class InertialCS;

// vehAero (Midtown Madness 2): angular damping about the body axes
// (AngCDamp, AngVelDamp, AngVel2Damp), drag (Drag * forward speed * v) and
// downforce (Down * forward speed^2 along the car's up axis).
class Aero {
public:
    void configure(const AeroParams& p) { params = p; }
    // `forwardSpeed` is vehCarSim's speed: |velocity . car Z axis| (m/s).
    void update(InertialCS& ics, float forwardSpeed, float dt, float invDt) const;

    AeroParams params;
    bool enabled = true;
};

} // namespace mm2::phys
