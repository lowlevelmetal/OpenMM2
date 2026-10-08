#pragma once

namespace mm2::phys {

class CarSim;

// Pedal-style driving input, as the game reads it from the player.
struct PedalInput {
    float accelerator = 0.0f; // 0..1
    float brake = 0.0f;       // 0..1
    float steering = 0.0f;    // -1 left .. +1 right
    float handbrake = 0.0f;   // 0..1
};

// Maps pedals onto a CarSim, including the automatic gearbox's reverse
// handling (MM2's mmGame::UpdateSteeringBrakes and mmInput's pedal swap):
// with an automatic gearbox, in a forward gear below AutoRevSpeed, holding
// the brake (> AutoRevLevel) with no throttle (< 0.1) selects reverse and
// swaps the pedals, so the brake pedal drives backwards; in reverse, a
// throttle (brake pedal) below AutoRevLevel returns to drive.
class ArcadeControls {
public:
    void apply(CarSim& car, const PedalInput& in);
    void reset() { swapThrottle = false; }

    bool autoReverse = true;   // mmInput +0x18c
    float autoRevLevel = 0.8f; // mmGame +0x68
    float autoRevSpeed = 5.0f; // mmGame +0x6c (m/s, vehCarSim's forward speed)
    bool swapThrottle = false; // mmInput +0x1d4 (the pedals are swapped)
};

// The steering filter of the keyboard and gamepad (mmInput::
// FilterDiscreteSteering / FilterGamepadSteering) with the speed-sensitive
// rates and response curve mmPlayer::Update sets from the player's tune
// (mmPlayer::FileIO; the retail game has no file, so the constructor's
// values). The filtered position moves towards the target (the key's full
// lock, or the stick) at DeltaOut per second when turning further the same
// way and DeltaIn per second otherwise; the car gets sign * |position|^Filter.
class SteeringFilter {
public:
    struct Params {
        int speedSensitive = 2;   // 0: the Lo values, 2: blended by speed, else the Hi values
        float speedBaseLow = 5.0f; // m/s
        float speedBaseHi = 100.0f;
        float deltaInLo = 2.5f; // DiscreteSteeringDeltaInLo
        float deltaOutLo = 3.5f;
        float filterLo = 2.0f;
        float deltaInHi = 1.5f;
        float deltaOutHi = 2.5f;
        float filterHi = 1.0f;
    };

    // mmPlayer::Update: the rates and exponent for the car's forward speed
    // (vehCarSim's, m/s). The game filters with last frame's values, so call
    // this after filter() each frame.
    void setSpeed(float speed);
    // One frame of the filter towards `target` (-1..1); returns the steering.
    float filter(float target, float dt);
    void reset() { m_position = 0.0f; }

    Params params;

private:
    float m_position = 0.0f; // mmInput +0x19c (+0x1a0 for the gamepad)
    // mmInput +0x1c8 / +0x1cc / +0x1d0, as the constructor sets them.
    float m_deltaIn = 1.0f;
    float m_deltaOut = 1.0f;
    float m_exponent = 2.0f;
};

} // namespace mm2::phys
