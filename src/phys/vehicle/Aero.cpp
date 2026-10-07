// vehAero::Update from Midtown Madness 2, verified against the build 3393
// code (MM2Recomp). See docs/physics.md.

#include "phys/vehicle/Aero.h"

#include "phys/InertialCS.h"

#include <cmath>

namespace mm2::phys {
namespace {

float signum(float v) {
    return v < 0.0f ? -1.0f : (v > 0.0f ? 1.0f : 0.0f);
}

} // namespace

void Aero::update(InertialCS& ics, float forwardSpeed, float dt, float invDt) const {
    if (!enabled)
        return;
    const Mat34& m = ics.matrix;
    const Vec3& w = ics.angularVelocity;

    // Angular damping about the body axes: constant, linear and quadratic.
    const float l[3] = {m.m0.x * w.x + w.y * m.m0.y + m.m0.z * w.z, m.m1.x * w.x + m.m1.z * w.z + m.m1.y * w.y,
                        m.m2.x * w.x + m.m2.z * w.z + m.m2.y * w.y};
    const float c[3] = {params.angCDamp.x, params.angCDamp.y, params.angCDamp.z};
    const float v1[3] = {params.angVelDamp.x, params.angVelDamp.y, params.angVelDamp.z};
    const float v2[3] = {params.angVel2Damp.x, params.angVel2Damp.y, params.angVel2Damp.z};
    float t[3];
    for (int i = 0; i < 3; ++i)
        t[i] = (-(signum(l[i]) * c[i]) - l[i] * v1[i]) - std::abs(l[i]) * l[i] * v2[i];
    // Never more than stops the rotation in one sample.
    for (int i = 0; i < 3; ++i)
        if (std::abs(l[i]) < std::abs(t[i]) * dt)
            t[i] = -(invDt * l[i]);
    // Faded out below 1 rad/s. (The original compares the world-space
    // components of the angular velocity here.)
    const float ww[3] = {w.x, w.y, w.z};
    for (int i = 0; i < 3; ++i)
        if (std::abs(ww[i]) < 1.0f)
            t[i] = std::abs(ww[i]) * t[i];
    const float tx = t[0] * ics.inertia.x, ty = t[1] * ics.inertia.y, tz = t[2] * ics.inertia.z;
    ics.applyTorque({ty * m.m1.x + tx * m.m0.x + tz * m.m2.x, tx * m.m0.y + ty * m.m1.y + tz * m.m2.y,
                     tx * m.m0.z + ty * m.m1.z + tz * m.m2.z});

    // Drag against the velocity, scaled by the forward speed; downforce
    // along the car's up axis.
    const float drag = -(forwardSpeed * params.drag);
    const Vec3& v = ics.linearVelocity;
    ics.applyForce({drag * v.x, drag * v.y, drag * v.z});
    const float down = -(forwardSpeed * forwardSpeed * params.down);
    ics.applyForce({down * m.m1.x, down * m.m1.y, down * m.m1.z});
}

} // namespace mm2::phys
