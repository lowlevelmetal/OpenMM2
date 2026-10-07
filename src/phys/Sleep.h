#pragma once

// phSleep (Midtown Madness 2, from the code of midtown2.exe build 3393): puts
// a body that has come to rest to sleep. Traffic cars that left their rail
// (aiVehicleActive) and knocked-over props (dgBangerActive) are handed back
// to their owners once theirs sleeps.

#include "core/Math.h"

namespace mm2::phys {

class InertialCS;

class Sleep {
public:
    enum State : int { Asleep = 0, Awake = 1, Dormant = 2 };

    // phSleep::Init: 15 still updates to fall asleep, 120 more to go
    // dormant, thresholds 0.005 (speed^2) and 0.01 (spin^2).
    void init(InertialCS* ics);
    // phSleep::Reset / WakeUp: awake, counters and sums cleared, the body
    // active again.
    void reset();
    void wakeUp();
    // phSleep::Update, before the body's integration each sample: the body
    // is still when its spin and its velocity (with this sample's pushes)
    // are below the thresholds, or jitter (two updates in opposite
    // directions summing within 1.8 times the threshold), and its pending
    // impulse would not change its speed noticeably.
    void update(float invDt);

    int state = Awake;
    int stillUpdates = 0;
    int dormantUpdates = 0;
    int sleepAfter = 15;
    int dormantAfter = 120;
    float speed2 = 0.005f;
    float spin2 = 0.01f;

private:
    void sendToSleep();

    InertialCS* m_ics = nullptr;
    Vec3 m_velocitySum;
    Vec3 m_spinSum;
};

} // namespace mm2::phys
