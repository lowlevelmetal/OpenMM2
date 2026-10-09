#pragma once

// Midtown Madness 2's force feedback (build 3393, MM2Recomp): what the
// player's car makes the joystick feel (mmPlayer::UpdateFF, FFImpactCallback,
// ResetFF), the road wave's frequency and strength (mmCarRoadFF), the
// switches and intensities that gate it (mmInput::DoingFF, FFPlay / FFStop /
// FFIsPlaying / FFSetValues, StopAllFF, mmJoyMan's) and how each effect's
// values become DirectInput parameters (mmFrictionFF, mmCollideFF, mmRoadFF,
// mmSpringFF ::SetValues / Assign / Play / Stop). The device itself is the
// platform layer's (SDL haptics, or rumble).

#include "app/Controls.h"
#include "game/fx/Random.h"
#include "platform/ForceFeedback.h"

#include <array>

namespace mm2::phys {
class CarSim;
}

namespace mm2::app::controls {

// What mmPlayer::UpdateFF reads from the car (vehCarSim offsets).
struct FFCar {
    float speed = 0.0f;    // +0x248, m/s
    float speedMph = 0.0f; // +0x24c
    // (CurrentDamage - MedDamage) / (MaxDamage - MedDamage) (vehCarDamage
    // +0x30, +0x38, +0x34), clamped by UpdateFF.
    float damage = 0.0f;
    bool onGround = false; // vehCarSim::OnGround
    // The front left wheel: radius (+0x674), surface friction (+0x688),
    // bump height and width (+0x690, +0x694), slip (+0x6f8, +0x6fc).
    float radius = 0.3f;
    float friction = 1.0f;
    float bumpHeight = 0.0f, bumpWidth = 0.0f;
    float latSlip = 0.0f, longSlip = 0.0f;
    // Every wheel's suspension speed (+0x204 of each vehWheel).
    std::array<float, 4> suspensionSpeed{};
};
FFCar ffCarState(const phys::CarSim& car);

class ForceFeedback {
public:
    using Effect = platform::FFEffect;

    // The controller in use and the [Controls] options: FORCE FEEDBACK,
    // COLLISION and ROAD FORCE (mmPlayerConfig::SetControls).
    void configure(Controller c, const Options& o);
    // The race's joystick's force feedback (mmJoyMan: a joystick with
    // DIDC_FORCEFEEDBACK); null without one.
    void setDevice(platform::FFDevice* device);
    // mmInput::DoingFF: a force-feedback joystick, the option on, and the
    // joystick or wheel controller.
    bool doing() const;

    // mmPlayer::Init: mmCarRoadFF::AssignProperties.
    void start();
    // mmPlayer::Update calls UpdateFF while DoingFF; `paused` (asRoot)
    // resets instead.
    void update(const FFCar& car, float dt, bool paused);
    // mmPlayer::FFImpactCallback with the impact's summed value
    // (vehDamageImpactInfo +0x38), for impacts that do damage.
    void impact(float total, float speedMph);
    // mmPlayer::Reset: ResetFF, then mmCarRoadFF::Reset.
    void reset();
    // mmInput::StopAllFF (the popups): every effect's Stop.
    void stopAll();

    // mmPlayer's constructor values.
    float frictionScale = 0.7f;       // +0x236c
    float springMin = 0.0f;           // +0x2370
    float springMax = 1.0f;           // +0x2374
    float springLatSlip = 0.05f;      // +0x2378
    float springLongSlip = 0.05f;     // +0x237c
    float springSpeedLow = 10.0f;     // +0x2380
    float springSpeedHigh = 80.0f;    // +0x2384
    float bumpJolt = 20.0f;           // +0x2390
    float bumpSuspensionSpeed = 1.3f; // +0x2394
    float impactScale = 1.0f;         // +0x2398
    float holdOff = 0.5f;             // mmInput +0x168

private:
    // mmInput / mmJoyMan / the effects.
    bool play(Effect e);
    bool stop(Effect e);
    bool isPlaying(Effect e) const { return m_playing[static_cast<std::size_t>(e)]; }
    bool setValues(Effect e, float a, float b);
    bool ffJoystick() const { return m_device != nullptr; }
    // mmCarRoadFF.
    void roadSetFG(float frequency, float magnitude);
    void roadStart() { play(Effect::Road); }
    void roadStop() { stop(Effect::Road); }
    void roadUpdateVals();
    void resetFF();

    platform::FFDevice* m_device = nullptr;
    Controller m_controller = Controller::Keyboard;
    bool m_enabled = false;        // the FORCE FEEDBACK option (mmInput's global switch)
    float m_collisionScale = 1.0f; // mmInput +0x1ac
    float m_roadScale = 1.0f;      // mmInput +0x1b0
    float m_vehicleCollision = 1.0f, m_vehicleRoad = 1.0f; // +0x1b8 / +0x1bc (mmVehInfo, always 1)
    std::array<bool, 4> m_playing{};                       // mmEffectFF +0x84
    // mmCarRoadFF +0x20 / +0x24 and the values last sent (+0x34 / +0x38).
    float m_roadPeriod = 1.0f, m_roadMagnitude = 1.0f;
    float m_sentPeriod = -1.0f, m_sentMagnitude = -1.0f;
    // mmPlayer +0x234c / +0x2350 / +0x235c / +0x2358 / +0x2360 / +0x2388.
    float m_bumpHeight = 0.0f, m_bumpWidth = 0.0f, m_speedSeen = 0.0f;
    float m_lastFriction = 0.0f, m_lastSpring = 0.0f, m_lastImpact = 0.0f;
    float m_elapsed = 0.0f; // datTimeManager::ElapsedTime
    game::fx::Rand m_rand{0x2Au}; // frand (MM2 shares one stream; inferred own)
};

} // namespace mm2::app::controls
