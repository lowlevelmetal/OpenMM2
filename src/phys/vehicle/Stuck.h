#pragma once

#include "phys/vehicle/TuneParams.h"

namespace mm2::phys {

class InertialCS;
class Engine;
class Transmission;

// vehStuck, ported from MM1's mmStuck (Open1560 game.asm). After an impact the
// car's position is watched; if it stays within PosThresh for TimeThresh
// seconds while the player holds the throttle and steers hard ("pegged"), the
// car is yawed in place until it moves MoveThresh away.
// MM2's Turn takes the role of MM1's RotAmount (inferred); MM2's Rotation and
// Translation fields are not used yet (unknown meaning).
class Stuck {
public:
    enum State : int { Idle = 0, Watching = 1, Stuck_ = 2 };

    void configure(const StuckParams& p);
    void reset();
    void impact() { impacted = true; }
    bool pegged(const Engine& engine, const Transmission& trans, float steering) const;
    void update(InertialCS& ics, float dt, const Engine& engine, const Transmission& trans, float steering);

    int state = Idle;
    bool impacted = false;
    float stuckTime = 0.0f;
    Vec3 lastPosition;
    float timeThresh = 0.3f;
    float posThresh = 1.25f;
    float moveThresh = 1.75f;
    float posThreshSqr = 1.5625f;
    float moveThreshSqr = 3.0625f;
    float rotAmount = 1.0f;
};

} // namespace mm2::phys
