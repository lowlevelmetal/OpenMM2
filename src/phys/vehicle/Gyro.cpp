// vehGyro::Update from Midtown Madness 2, verified against the build 3393
// code (MM2Recomp). The handbrake and brake terms are enabled by vehCar::Update
// when those inputs exceed 0.01. See docs/physics.md.

#include "phys/vehicle/Gyro.h"

#include "phys/InertialCS.h"

#include <cmath>

namespace mm2::phys {

void Gyro::update(InertialCS& ics, const Inputs& in) const {
    if (!enabled)
        return;
    const Mat34& m = ics.matrix;
    // OnGround() / NumWheels in integer arithmetic: 1 only with every wheel down.
    const int all = in.wheelsOnGround / in.numWheels;
    const float w = in.drivetrainSpeed;
    if (0.0f < params.drift) {
        // The speed-sensitive steering s (rounded to a float) squared with
        // its sign, as vehGyro::Update multiplies it.
        const float s = in.sssFactor * in.steering;
        const float t = -((ics.inertia.y * params.drift) * ((static_cast<float>(all) * (std::abs(s) * s)) * -w));
        ics.applyTorque({m.m1.x * t, m.m1.y * t, t * m.m1.z});
    }
    if (std::abs(in.handbrake) > 0.01f && (0.0f < params.spin180 || 0.0f < params.reverse180)) {
        const float k = w < 0.0f ? params.spin180 : params.reverse180;
        const float t = -(static_cast<float>(all) * in.steering * -w * ics.inertia.y * k);
        ics.applyTorque({m.m1.x * t, m.m1.y * t, t * m.m1.z});
    }
    if (std::abs(in.brake) > 0.01f && (0.0f < params.pitch || 0.0f < params.roll)) {
        const float f = in.brake * in.brake * static_cast<float>(1 - all);
        const float p = ics.inertia.x * params.pitch * f;
        const float fz = m.m2.y;
        ics.applyTorque({p * (fz * m.m0.x), (fz * m.m0.y) * p, (fz * m.m0.z) * p});
        const float fx = m.m0.y;
        const float r = -(params.roll * ics.inertia.z * f);
        ics.applyTorque({r * (fx * m.m2.x), (fx * m.m2.y) * r, r * (fx * m.m2.z)});
    }
}

} // namespace mm2::phys
