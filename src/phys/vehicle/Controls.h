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
// handling. Ported from MM1's mmGame::UpdateSteeringBrakes and
// mmInput::SwapThrottle (Open1560): with an automatic gearbox, holding the
// brake (> AutoRevLevel) with no throttle (< 0.1) below AutoRevSpeed toggles
// between drive and reverse and swaps the pedals, so the brake pedal drives
// backwards and the accelerator brakes.
class ArcadeControls {
public:
    void apply(CarSim& car, const PedalInput& in);
    void reset() { swapThrottle = false; }

    bool autoReverse = true;   // mmInput +0x194
    float autoRevLevel = 0.8f; // mmGame::AutoRevLevel
    float autoRevSpeed = 5.0f; // mmGame::AutoRevSpeed (m/s)
    bool swapThrottle = false; // mmInput::SwapThrottle
};

} // namespace mm2::phys
