// vehStuck from Midtown Madness 2 (Impact, Pegged, Update, Reset), verified
// against the build 3393 code (MM2Recomp). See docs/physics.md.

#include "phys/vehicle/Stuck.h"

#include "phys/AgeMath.h"
#include "phys/InertialCS.h"

#include <cmath>

namespace mm2::phys {

void Stuck::configure(const StuckParams& p) {
    params = p;
    posThreshSqr = p.posThresh * p.posThresh;
    moveThreshSqr = p.moveThresh * p.moveThresh;
    reset();
}

void Stuck::reset() {
    active = false;
    state = Idle;
    stuckTime = 0.0f;
}

void Stuck::impact(const InertialCS& ics) {
    if (state != Idle)
        return;
    active = true;
    impactPosition = ics.matrix.m3;
    state = Watching;
}

bool Stuck::pegged(const Inputs& in) const {
    return in.maxThrottle * 0.75f < in.throttle && 0.5f < std::abs(in.steering);
}

bool Stuck::update(InertialCS& ics, float dt, const Inputs& in) {
    if (!active)
        return false;
    const Vec3& pos = ics.matrix.m3;
    const auto moved2 = [&] {
        const float dx = impactPosition.x - pos.x, dy = impactPosition.y - pos.y, dz = impactPosition.z - pos.z;
        return (dz * dz + dy * dy) + dx * dx; // vehStuck::Update's order
    };
    if (moveThreshSqr < moved2())
        reset();

    switch (state) {
    case Watching: {
        stuckTime = dt + stuckTime;
        const float dx = impactPosition.z - pos.z, dz = impactPosition.x - pos.x;
        const float near2 = dx * dx + dz * dz;
        if (near2 <= posThreshSqr && in.wheelsOnGround == 0 && params.timeThresh <= stuckTime &&
            (0.0f < params.translation || 0.0f < params.rotation)) {
            state = params.rotation <= 0.0f ? Flipping : Nudging;
            return false;
        }
        if (near2 <= posThreshSqr && params.timeThresh <= stuckTime && pegged(in)) {
            state = Pegged;
            return false;
        }
        if (moved2() <= moveThreshSqr && stuckTime <= params.timeThresh)
            return false;
        break;
    }
    case Flipping: {
        // Upright, keeping the heading, lifted by Translation.
        Mat34 m = ics.matrix;
        Vec3 x{m.m2.z - 0.0f, 0.0f, 0.0f - m.m2.x};
        const float l2 = x.x * x.x + x.z * x.z + 0.0f;
        const float inv = l2 == 0.0f ? 0.0f : 1.0f / std::sqrt(l2);
        x = {x.x * inv, x.y * inv, x.z * inv};
        const Vec3 y{0.0f, 1.0f, 0.0f};
        m.m0 = x;
        m.m1 = y;
        m.m2 = {x.y * y.z - x.z * y.y, x.z * y.x - y.z * x.x, y.y * x.x - x.y * y.x};
        m.m3.y = m.m3.y + params.translation;
        ics.matrix = m;
        reset();
        return false;
    }
    case Nudging:
        if (ics.matrix.m1.y <= 0.7f) {
            ics.applyImpulse({0.0f, std::abs(in.steering) * ics.mass * params.translation, 0.0f}, pos);
            const float r = in.steering * ics.mass * params.rotation;
            ics.applyAngImpulse({r * ics.matrix.m2.x, r * ics.matrix.m2.y, r * ics.matrix.m2.z});
            return true;
        }
        break;
    case Pegged:
        if (moved2() <= moveThreshSqr && pegged(in)) {
            float a = std::abs(in.steering) * params.turn * in.steering;
            if (in.gear == 0)
                a = -a;
            age::rotate(ics.matrix, {0.0f, 1.0f, 0.0f}, -(dt * a));
            return false;
        }
        break;
    default:
        return false;
    }
    reset();
    return false;
}

} // namespace mm2::phys
