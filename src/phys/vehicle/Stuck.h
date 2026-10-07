#pragma once

#include "phys/vehicle/TuneParams.h"

namespace mm2::phys {

class InertialCS;

// vehStuck (Midtown Madness 2), verified against the build 3393 code.
//
// After an impact the car is watched. If it stays within PosThresh
// (horizontally) for TimeThresh:
//  * with no wheel on the ground: Rotation > 0 nudges it (state Nudging:
//    an upward impulse and a roll about its length while it leans past
//    ~45 degrees, throttle off, brakes on); otherwise it is set upright and
//    lifted by Translation (Flipping);
//  * with the throttle pegged and the steering turned: it yaws in place at
//    |steer| * steer * Turn rad/s (Pegged), backwards in reverse.
// Moving more than MoveThresh from the impact point ends it.
class Stuck {
public:
    enum State : int { Idle = 0, Watching = 1, Pegged = 2, Nudging = 3, Flipping = 4 };

    void configure(const StuckParams& p);
    void reset();
    // vehStuck::Impact (from vehCarDamage::Impact).
    void impact(const InertialCS& ics);

    struct Inputs {
        float throttle = 0;
        float maxThrottle = 1;
        float steering = 0;
        int gear = 2; // transmission slot (0 reverse)
        int wheelsOnGround = 0;
    };
    // Returns true when it took the controls (Nudging: throttle 0, brake 1).
    bool update(InertialCS& ics, float dt, const Inputs& in);
    bool pegged(const Inputs& in) const;

    StuckParams params;
    int state = Idle;
    bool active = false; // asNode active flag: set by Impact, cleared by Reset
    float stuckTime = 0.0f;
    Vec3 impactPosition;
    float posThreshSqr = 1.5625f;
    float moveThreshSqr = 3.0625f;
};

} // namespace mm2::phys
