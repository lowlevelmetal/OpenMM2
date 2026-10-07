// Angular damping ported from asAero::Update in Open1560
// (https://github.com/0x1F9F1/Open1560), GPL-3.0: code/midtown/game.asm.

#include "phys/vehicle/Aero.h"

#include "phys/InertialCS.h"

#include <cmath>

namespace mm2::phys {
namespace {

float signum(float v) {
    return v < 0.0f ? -1.0f : (v > 0.0f ? 1.0f : 0.0f);
}
float truncSquare(float v) {
    return static_cast<float>(static_cast<int>(std::abs(v) * v));
}

} // namespace

void Aero::update(InertialCS& ics) const {
    if (!enabled)
        return;
    const Mat34& m = ics.matrix;

    // MM2 linear drag and downforce (inferred).
    const Vec3& v = ics.linearVelocity;
    const float speed2 = v.mag2();
    if (params.drag != 0.0f && speed2 > 0.0f)
        ics.applyForce(v * (-params.drag * std::sqrt(speed2)));
    if (params.down != 0.0f && speed2 > 0.0f)
        ics.applyForce(m.m1 * (-params.down * speed2));

    // Angular damping in body axes (asAero::Update).
    const Vec3& w = ics.angularVelocity;
    const float lx = (m.m0.y * w.y + m.m0.z * w.z) + m.m0.x * w.x;
    const float ly = (w.y * m.m1.y + w.z * m.m1.z) + m.m1.x * w.x;
    const float lz = (m.m2.y * w.y + m.m2.z * w.z) + w.x * m.m2.x;
    const Vec3& c = params.angCDamp;
    const Vec3& d1 = params.angVelDamp;
    const Vec3& d2 = params.angVel2Damp;
    float tx = -(signum(lx) * c.x) - d1.x * lx;
    float ty = -(signum(ly) * c.y) - d1.y * ly;
    float tz = -(signum(lz) * c.z) - d1.z * lz;
    tx = tx - truncSquare(lx) * d2.x;
    ty = ty - truncSquare(ly) * d2.y;
    tz = tz - truncSquare(lz) * d2.z;
    tx = (ics.inertia.x * scale) * tx;
    ty = (ics.inertia.y * scale) * ty;
    tz = (ics.inertia.z * scale) * tz;
    ics.applyTorque(m.m0 * tx + m.m1 * ty + m.m2 * tz);
}

} // namespace mm2::phys
