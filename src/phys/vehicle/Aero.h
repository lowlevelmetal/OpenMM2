#pragma once

#include "phys/vehicle/TuneParams.h"

namespace mm2::phys {

class InertialCS;

// vehAero. The angular part is a port of MM1's asAero::Update (Open1560
// game.asm): in body axes, torque_i = -I_i * (AngCDamp_i * sign(w_i) +
// AngVelDamp_i * w_i + AngVel2Damp_i * trunc(|w_i| * w_i)). The quadratic
// term goes through an integer truncation (__ftol) in the original, so it is
// zero below 1 rad/s; that is kept.
//
// The linear part differs: MM1 had per-axis CDamp/VelDamp/Vel2Damp scaled by
// mass, MM2 has scalar Drag and Down. We apply Drag * |v| * v against the
// velocity and Down * v^2 along the car's -Y, in newtons (inferred).
class Aero {
public:
    void configure(const AeroParams& p) { params = p; }
    void update(InertialCS& ics) const;

    AeroParams params;
    bool enabled = true;
    float scale = 1.0f; // asAero +0x70
};

} // namespace mm2::phys
