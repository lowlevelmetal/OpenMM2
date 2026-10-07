// Port of mmStuck from Open1560 (https://github.com/0x1F9F1/Open1560),
// GPL-3.0: code/midtown/game.asm, Midtown Madness 1 beta build 1560.

#include "phys/vehicle/Stuck.h"

#include "phys/AgeMath.h"
#include "phys/InertialCS.h"
#include "phys/vehicle/Engine.h"
#include "phys/vehicle/Transmission.h"

#include <cmath>

namespace mm2::phys {

void Stuck::configure(const StuckParams& p) {
    timeThresh = p.timeThresh;
    posThresh = p.posThresh;
    moveThresh = p.moveThresh;
    rotAmount = p.turn;
    // StuckCB.
    posThreshSqr = posThresh * posThresh;
    moveThreshSqr = moveThresh * moveThresh;
    reset();
}

void Stuck::reset() {
    state = Idle;
    stuckTime = 0.0f;
    impacted = false;
}

bool Stuck::pegged(const Engine& engine, const Transmission& trans, float steering) const {
    return age::mulD(engine.maxThrottle, 0.75) < engine.throttle &&
           static_cast<double>(std::abs(steering)) > 0.5 && trans.getCurrentGear() != -1;
}

void Stuck::update(InertialCS& ics, float dt, const Engine& engine, const Transmission& trans,
                   float steering) {
    if (ics.constraints & InertialCS::kConstrainRY)
        return;
    const Vec3& pos = ics.matrix.m3;
    auto dist2 = [&] {
        const float dy = lastPosition.y - pos.y, dz = lastPosition.z - pos.z, dx = lastPosition.x - pos.x;
        return (dy * dy + dz * dz) + dx * dx;
    };
    if (dist2() > moveThreshSqr)
        state = Idle;
    if (impacted) {
        if (state == Idle) {
            stuckTime = 0.0f;
            lastPosition = pos;
            state = Watching;
        }
        impacted = false;
    }
    if (state == Watching) {
        stuckTime = dt + stuckTime;
        const float dx = lastPosition.x - pos.x, dz = lastPosition.z - pos.z;
        if (dx * dx + dz * dz < posThreshSqr && !(stuckTime < timeThresh) &&
            pegged(engine, trans, steering)) {
            state = Stuck_;
            return;
        }
        if (dist2() > moveThreshSqr)
            state = Idle;
    } else if (state == Stuck_) {
        if (!(dist2() > moveThreshSqr) && pegged(engine, trans, steering)) {
            const float sgn = steering < 0.0f ? -1.0f : 1.0f;
            const float angle = -((((std::abs(steering) - -1.0f) * rotAmount) * dt) * sgn);
            age::rotate(ics.matrix, {0.0f, 1.0f, 0.0f}, angle);
        } else {
            state = Idle;
        }
    }
}

} // namespace mm2::phys
