// mmPlayer::UpdateFF / FFImpactCallback / ResetFF, mmCarRoadFF and the
// force-feedback switches of mmInput, mmJoyMan and the effects from Midtown
// Madness 2 (build 3393, MM2Recomp).

#include "app/ForceFeedback.h"

#include "phys/vehicle/CarSim.h"

#include <algorithm>

#include <cmath>

namespace mm2::app::controls {
namespace {

// __ftol: truncation towards zero.
// __ftol of an x87 product: the product keeps more precision than a float
// before it is truncated towards zero.
int ftol(double v) { return static_cast<int>(v); }
double x87(float v) { return static_cast<double>(v); }

} // namespace

FFCar ffCarState(const phys::CarSim& car) {
    FFCar c;
    c.speed = car.speed();
    c.speedMph = car.speedMph();
    const float med = car.damage.medDamage(), max = car.damage.maxDamage();
    c.damage = (car.damage.currentDamage - med) / (max - med);
    c.onGround = car.onGround();
    const auto& fl = car.wheels[0];
    c.radius = fl.radius;
    c.friction = fl.friction;
    c.bumpHeight = fl.bumpHeight;
    c.bumpWidth = fl.bumpWidth;
    c.latSlip = fl.latSlipPercent;
    c.longSlip = fl.longSlipPercent;
    for (std::size_t i = 0; i < 4; ++i)
        c.suspensionSpeed[i] = car.wheels[i].suspensionVelocity;
    return c;
}

void ForceFeedback::configure(Controller c, const Options& o) {
    m_controller = c;
    m_enabled = o.forceFeedback;
    // mmInput::SetForceFeedbackScale / SetRoadForceScale's 0 .. 2.
    m_collisionScale = std::clamp(o.ffCollision, 0.0f, 2.0f);
    m_roadScale = std::clamp(o.ffRoadForce, 0.0f, 2.0f);
}

void ForceFeedback::setDevice(platform::FFDevice* device) {
    if (device != m_device)
        m_playing.fill(false);
    m_device = device;
}

bool ForceFeedback::doing() const {
    // mmInput::DoingFF: +0x160 (mmInput::Init: a joystick type with a
    // force-feedback joystick), the option, and the joystick or the wheel.
    return ffJoystick() && m_enabled && (m_controller == Controller::Wheel || m_controller == Controller::Joystick);
}

// --- mmInput::FFPlay / FFStop / FFSetValues and the effects -------------------------------------

bool ForceFeedback::play(Effect e) {
    // mmJoyMan::FFPlay -> the effect's Play. The road and the spring play
    // only with a road force, the collision only with a collision intensity
    // (times the car's own scales, always 1); the friction always.
    if (!ffJoystick() || !m_device->has(e))
        return false;
    const auto i = static_cast<std::size_t>(e);
    switch (e) {
    case Effect::Collision:
        // mmCollideFF::Play does not mark the effect as playing.
        return m_vehicleCollision * m_collisionScale != 0.0f && m_device->play(e);
    case Effect::Road:
    case Effect::Spring:
        if (m_vehicleRoad * m_roadScale == 0.0f || !m_device->play(e))
            return false;
        m_playing[i] = true;
        return true;
    case Effect::Friction:
        if (!m_device->play(e))
            return false;
        m_playing[i] = true;
        return true;
    }
    return false;
}

bool ForceFeedback::stop(Effect e) {
    if (!ffJoystick() || !m_device->has(e))
        return false;
    const auto i = static_cast<std::size_t>(e);
    switch (e) {
    case Effect::Collision: return true; // mmCollideFF::Stop leaves the push to end itself
    case Effect::Friction:
        m_playing[i] = false; // cleared before the device call (mmFrictionFF::Stop)
        return m_device->stop(e);
    default:
        if (!m_device->stop(e))
            return false;
        m_playing[i] = false;
        return true;
    }
}

bool ForceFeedback::setValues(Effect e, float a, float b) {
    if (!ffJoystick() || !m_device->has(e))
        return false;
    switch (e) {
    case Effect::Friction: {
        // mmFrictionFF::SetValues / Assign: the coefficient x 10000, 0 ..
        // 10000.
        const int c = std::clamp(ftol(x87(a) * 10000.0), 0, 10000);
        return m_device->setCondition(e, c);
    }
    case Effect::Spring: {
        // mmSpringFF::Assign: the strength x 10000 times the road force
        // truncated to a whole number (so below 1 it has no spring, at 2 it
        // doubles), 0 .. 10000.
        if (m_vehicleRoad * m_roadScale == 0.0f)
            return false;
        const int scale = ftol(x87(m_vehicleRoad) * x87(m_roadScale));
        const int c = std::clamp(scale * ftol(x87(a) * 10000.0), 0, 10000);
        return m_device->setCondition(e, c);
    }
    case Effect::Road: {
        // mmRoadFF::SetValues(period, magnitude) / Assign: the period in
        // microseconds, the magnitude x 10000 times the road force.
        if (m_vehicleRoad * m_roadScale == 0.0f)
            return false;
        const int periodUs = ftol(x87(a) * 1000000.0);
        const int magnitude =
            ftol(x87(m_vehicleRoad) * x87(m_roadScale) * static_cast<double>(ftol(x87(b) * 10000.0)));
        return m_device->setRoad(magnitude, periodUs);
    }
    case Effect::Collision: {
        // mmCollideFF::SetValues(strength, direction) / Assign: the gain is
        // the strength x 10000 times the collision intensity, the direction
        // 0 .. 360 degrees in hundredths.
        if (m_vehicleCollision * m_collisionScale == 0.0f)
            return false;
        const int direction = std::clamp(ftol(b), 0, 360) * 100;
        const int gain =
            ftol(x87(m_vehicleCollision) * x87(m_collisionScale) * static_cast<double>(ftol(x87(a) * 10000.0)));
        return m_device->setCollision(gain, direction);
    }
    }
    return false;
}

void ForceFeedback::stopAll() {
    // mmInput::StopAllFF -> mmJoystick::InputStopEffect: each effect's Stop.
    if (!ffJoystick())
        return;
    for (Effect e : {Effect::Road, Effect::Spring, Effect::Collision, Effect::Friction})
        stop(e);
}

// --- mmCarRoadFF ----------------------------------------------------------------------------------

void ForceFeedback::roadUpdateVals() {
    // mmCarRoadFF::UpdateVals: sent when either value changed.
    if (m_roadPeriod != m_sentPeriod || m_roadMagnitude != m_sentMagnitude) {
        setValues(Effect::Road, m_roadPeriod, m_roadMagnitude);
        m_sentPeriod = m_roadPeriod;
        m_sentMagnitude = m_roadMagnitude;
    }
}

void ForceFeedback::roadSetFG(float frequency, float magnitude) {
    // mmCarRoadFF::SetFGVals: from 1 per second (+0x2c) the wave plays, its
    // frequency up to 20 (+0x30) as a period, its magnitude 0 .. 1; below,
    // it stops.
    constexpr float kMinFrequency = 1.0f, kMaxFrequency = 20.0f;
    if (kMinFrequency <= frequency) {
        if (!isPlaying(Effect::Road))
            roadStart(); // Start: not while paused (the caller resets then)
        float f = frequency;
        if (kMaxFrequency < f)
            f = kMaxFrequency;
        m_roadPeriod = 1.0f / f;
        m_roadMagnitude = magnitude < 0.0f ? 0.0f : (1.0f < magnitude ? 1.0f : magnitude);
        roadUpdateVals();
    } else if (isPlaying(Effect::Road)) {
        roadStop();
    }
}

// --- mmPlayer ----------------------------------------------------------------------------------

void ForceFeedback::start() {
    // mmPlayer::Init: AssignProperties sends the road wave's values.
    if (doing())
        roadUpdateVals();
}

void ForceFeedback::resetFF() {
    // mmPlayer::ResetFF.
    if (doing()) {
        roadStop();
        play(Effect::Friction);
        play(Effect::Spring);
    }
    m_bumpWidth = m_bumpHeight = 0.0f;
    m_lastSpring = 0.0f;
    m_speedSeen = 0.0f;
    m_lastFriction = 0.0f;
    m_lastImpact = 0.0f;
}

void ForceFeedback::reset() {
    resetFF();
    // mmCarRoadFF::Reset (a child of mmPlayer).
    roadStop();
    m_roadMagnitude = 1.0f;
    m_roadPeriod = 1.0f;
    m_sentPeriod = m_sentMagnitude = -1.0f;
    roadUpdateVals();
}

void ForceFeedback::update(const FFCar& car, float dt, bool paused) {
    if (m_device)
        m_device->update(dt);
    if (!doing())
        return;
    // mmPlayer::UpdateFF.
    if (paused) {
        resetFF();
        return;
    }
    m_elapsed = dt + m_elapsed;
    const float since = m_elapsed - m_lastImpact;
    const float speed = car.speed;
    float damage = car.damage;
    damage = damage < 0.0f ? 0.0f : (1.0f < damage ? 1.0f : damage);
    // The friction and the centring spring play all the time.
    if (!isPlaying(Effect::Friction))
        play(Effect::Friction);
    if (!isPlaying(Effect::Spring))
        play(Effect::Spring);
    // The friction follows the front left tyre's surface.
    if (car.friction != m_lastFriction) {
        setValues(Effect::Friction, frictionScale * car.friction, 0.0f);
        m_lastFriction = car.friction;
    }
    if (0.0f < damage) {
        // A damaged car shakes on the ground with its wheels' turning
        // (speed / (radius x 10)) at 0.4 of the damage, once 0.5 s have
        // passed since the last impact jolt.
        if (car.onGround) {
            if (holdOff < since)
                roadSetFG(speed / (car.radius * 10.0f), damage * 0.4f);
        } else if (isPlaying(Effect::Road)) {
            roadStop();
        }
    } else if (!(m_bumpHeight == car.bumpHeight && m_bumpWidth == car.bumpWidth && speed == m_speedSeen)) {
        // Otherwise the surface's bumps: their height at speed / width per
        // second. (+0x235c is never written, so only standing still with
        // the same surface skips this.)
        m_bumpHeight = car.bumpHeight;
        m_bumpWidth = car.bumpWidth;
        if (m_bumpHeight != 0.0f && 0.01f <= m_bumpWidth && 1.0f <= speed)
            roadSetFG(speed / m_bumpWidth, m_bumpHeight);
        else if (isPlaying(Effect::Road))
            roadStop();
    }
    // The centring spring: with the front left tyre gripping, from 0 at 10
    // m/s to 1 at 80 m/s, else none; sent when it moved by 0.01.
    float spring = springMin;
    if (std::abs(car.latSlip) < springLatSlip && std::abs(car.longSlip) < springLongSlip) {
        if (springSpeedHigh < speed)
            spring = springMax;
        else if (springSpeedLow < speed)
            spring = (springMax - springMin) * ((speed - springSpeedLow) / (springSpeedHigh - springSpeedLow)) +
                     springMin;
    }
    if (0.01f < std::abs(m_lastSpring - spring)) {
        setValues(Effect::Spring, spring, 0.0f);
        m_lastSpring = spring;
    }
    // A wheel's suspension compressing faster than 1.3 m/s above 5 mph: a
    // jolt of 0.2 in a random direction (the impact clock is not restarted).
    bool bump = false;
    for (float s : car.suspensionSpeed)
        bump = bump || bumpSuspensionSpeed < s;
    if (bump && 5.0f < car.speedMph && holdOff < since) {
        const float direction = m_rand.frand() * 360.0f;
        float strength = static_cast<float>(x87(bumpJolt) * x87(0.01f));
        strength = strength < 0.1f ? 0.1f : (1.0f < strength ? 1.0f : strength);
        setValues(Effect::Collision, strength, direction);
        play(Effect::Collision);
    }
}

void ForceFeedback::impact(float total, float speedMph) {
    // mmPlayer::FFImpactCallback: above 5 mph and 0.5 s after the last one,
    // a push of total / 100 (0.1 .. 1) in a random direction.
    const float since = m_elapsed - m_lastImpact;
    if (!doing() || !(5.0f < speedMph) || !(holdOff < since))
        return;
    const float direction = m_rand.frand() * 360.0f;
    m_lastImpact = m_elapsed;
    float strength = static_cast<float>(x87(total) * x87(0.01f));
    strength = strength < 0.1f ? 0.1f : (1.0f < strength ? 1.0f : strength);
    setValues(Effect::Collision, static_cast<float>(x87(strength) * x87(impactScale)), direction);
    play(Effect::Collision);
}

} // namespace mm2::app::controls
