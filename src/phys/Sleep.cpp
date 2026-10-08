// phSleep (Init, Reset, WakeUp, SendToSleep, Update) from the code of
// midtown2.exe build 3393 (MM2Recomp). See docs/physics.md.

#include "phys/Sleep.h"

#include "phys/InertialCS.h"

namespace mm2::phys {
namespace {

// Vector3::Mag2's order (x, y, z), and the order phSleep::Update writes out
// for the pushed velocity (z, y, x).
float mag2(const Vec3& v) {
    return (v.x * v.x + v.y * v.y) + v.z * v.z;
}
float mag2zyx(const Vec3& v) {
    return (v.z * v.z + v.y * v.y) + v.x * v.x;
}

} // namespace

void Sleep::init(InertialCS* ics) {
    m_ics = ics;
    stillUpdates = 0;
    dormantUpdates = 0;
    sleepAfter = 15;
    dormantAfter = 120;
    speed2 = 0.005f;
    spin2 = 0.01f;
    state = Awake;
    m_velocitySum = {};
    m_spinSum = {};
}

void Sleep::reset() {
    wakeUp();
}

void Sleep::wakeUp() {
    // phSleep::WakeUp: the body is active again.
    stillUpdates = 0;
    dormantUpdates = 0;
    state = Awake;
    // The body integrates again (phInertialCS active).
    if (m_ics && m_ics->state == InertialCS::Asleep)
        m_ics->state = InertialCS::Off;
    m_velocitySum = {};
    m_spinSum = {};
}

void Sleep::sendToSleep() {
    // phSleep::SendToSleep: the body is deactivated and frozen
    // (phInertialCS::Freeze: no motion, no pending forces or pushes).
    state = Asleep;
    dormantUpdates = 0;
    if (!m_ics)
        return;
    m_ics->state = InertialCS::Asleep;
    m_ics->freeze();
}

void Sleep::update(float invDt) {
    // phSleep::Update. (MM2 also wakes a sleeper whose WakeUpNextTime has
    // come; nothing in a race sets one.)
    if (!m_ics)
        return;
    const InertialCS& ics = *m_ics;
    bool moving = false;
    const Vec3& w = ics.angularVelocity;
    if (spin2 < mag2(w)) {
        m_spinSum = {w.x + m_spinSum.x, w.y + m_spinSum.y, w.z + m_spinSum.z};
        if (spin2 * 1.8f < mag2(m_spinSum)) {
            m_spinSum = w;
            moving = true;
        }
    }
    if (!moving) {
        m_spinSum = w;
        // The velocity with this sample's pushes.
        const Vec3 push{ics.linearPush.x + ics.framePush.x, ics.linearPush.y + ics.framePush.y,
                        ics.linearPush.z + ics.framePush.z};
        const Vec3 v{push.x * invDt + ics.linearVelocity.x, push.y * invDt + ics.linearVelocity.y,
                     push.z * invDt + ics.linearVelocity.z};
        if (speed2 < mag2zyx(v)) {
            m_velocitySum = {v.x + m_velocitySum.x, v.y + m_velocitySum.y, v.z + m_velocitySum.z};
            if (speed2 * 1.8f < mag2(m_velocitySum)) {
                m_velocitySum = v;
                moving = true;
            }
        }
        if (!moving) {
            m_velocitySum = v;
            // The pending impulse would not change the speed noticeably.
            const float im = ics.invMass;
            const Vec3 after{ics.linearImpulse.x * im + ics.linearVelocity.x,
                             ics.linearImpulse.y * im + ics.linearVelocity.y,
                             ics.linearImpulse.z * im + ics.linearVelocity.z};
            if (mag2(after) - mag2(ics.linearVelocity) <= speed2) {
                if (state != Awake) {
                    // Asleep: pending forces and pushes dropped
                    // (phInertialCS::ZeroForces); dormant after a while.
                    m_ics->zeroForces();
                    if (++dormantUpdates >= dormantAfter)
                        state = Dormant;
                    return;
                }
                if (++stillUpdates >= sleepAfter)
                    sendToSleep();
                return;
            }
            moving = true;
        }
    }
    if (state == Awake) {
        stillUpdates = 0;
        dormantUpdates = 0;
        return;
    }
    wakeUp();
}

} // namespace mm2::phys
