#pragma once

// Force feedback on the first joystick: the four effects Midtown Madness 2
// creates on a DirectInput force-feedback device (mmJoystick::
// InputCreateEffect: mmFrictionFF, mmCollideFF, mmRoadFF, mmSpringFF), taken
// in DirectInput's units and played through SDL's haptic API, or, on a
// device with only rumble motors, as rumble where that can show them.

#include <cstdint>
#include <memory>

namespace mm2::platform {

// mmJoystick::GetFFEffect's numbering.
enum class FFEffect : std::uint8_t { Friction = 0, Collision = 1, Road = 2, Spring = 3 };

class FFDevice {
public:
    enum class Kind : std::uint8_t { Haptic, Rumble };
    virtual ~FFDevice() = default;
    virtual Kind kind() const = 0;
    // The effect was created (mmEffectFF +0x80); a rumble device has only
    // the road and the collision.
    virtual bool has(FFEffect e) const = 0;
    // IDirectInputEffect::Start(1, 0) / Stop.
    virtual bool play(FFEffect e) = 0;
    virtual bool stop(FFEffect e) = 0;
    // mmJoystick::InputStopEffect.
    virtual void stopAll() = 0;
    // The condition effects (friction, spring) on the X axis: both
    // coefficients 0 .. 10000, saturation 10000, no dead band.
    virtual bool setCondition(FFEffect e, int coefficient) = 0;
    // mmRoadFF: the periodic wave on the X axis, magnitude 0 .. 10000,
    // period in microseconds, playing until stopped.
    virtual bool setRoad(int magnitude, int periodMicroseconds) = 0;
    // mmCollideFF: a 0.1 s push from a periodic wave with a 2 s period in a
    // direction (hundredths of a degree, polar on X and Y), gain 0 .. 10000.
    virtual bool setCollision(int gain, int direction) = 0;
    // Called every frame (rumble needs refreshing while the road plays).
    virtual void update(float dt) = 0;
};

// Opens force feedback for an SDL_Joystick (the race's first joystick):
// the haptic device when SDL has one for it (with autocentring off, as
// mmJoystick::DisableAutoCenter), else its rumble, else nothing.
std::unique_ptr<FFDevice> openForceFeedback(void* sdlJoystick);

} // namespace mm2::platform
