#pragma once

#include "phys/vehicle/TuneParams.h"

#include <array>

namespace mm2::phys {

class Engine;

// vehTransmission, ported from Midtown Madness 1's mmTransmission.
//
// Gear slots: 0 = reverse, 1 = neutral, 2.. = forward (evidence: MM1 code and
// MM1-format GearRatios "-20 0 28 ..."). NumGears counts all slots.
// Ratios are engine speed / wheel speed (positive forward, negative reverse).
//
// MM1 stores ratios and shift RPMs per gear. MM2's format gives gear speeds
// instead (Low/High/Reverse in mph at MaxRPM, GearBias) and shift biases;
// configure() turns those into the MM1 tables (MM2 adaptation, inferred), and
// MM1-format files (a few retail *_opp cars) are used as is.
class Transmission {
public:
    static constexpr int kSlots = 8;
    static constexpr int kReverse = 0;
    static constexpr int kNeutral = 1;
    static constexpr int kFirst = 2;

    // `wheelRadius` is the driven wheel radius (for MM2 gear speeds).
    void configure(const TransmissionParams& p, const EngineParams& engine, float wheelRadius);
    void reset();

    // IsAutomatic ? GearRatios[gear] : ManualGearRatios[gear].
    float currentRatio() const;
    float ratio(int gear) const;

    // mmTransmission::Upshift/Downshift (manual only; return the new gear).
    int upshift();
    int downshift();
    void setReverse() { setCurrentGear(kReverse); }
    // mmTransmission::SetDrive: from reverse or neutral into first gear.
    void setDrive();
    void setNeutral() { setCurrentGear(kNeutral); }
    // -1 reverse, 0 neutral, 1.. forward (mmTransmission::GetCurrentGear).
    int getCurrentGear() const { return currentGear - 1; }
    void setCurrentGear(int gear);
    void automatic(bool on) { isAutomatic = on; }

    // mmTransmission::Update (automatic shifting).
    void update(float dt, const Engine& engine, bool drivingDisabled = false);

    // --- State (mmTransmission members) ---
    bool isAutomatic = true;
    float clutch = 1.0f;
    int numGears = 6;
    std::array<float, kSlots> gearRatios{};
    std::array<float, kSlots> upshiftRPM{};
    std::array<float, kSlots> downshiftRPM{};
    std::array<float, kSlots> manualGearRatios{};
    int manualNumGears = 0;
    float downshiftBias = 1.55f; // MM1 kickdown factor
    bool gearChanged = false;
    bool inPark = false;
    int currentGear = kFirst;
    float timeInGear = 0.0f;
    float gearChangeDelay = 0.0f; // MM2 GearChangeTime
    // MM2 kickdown threshold per gear (OpenMM2 addition, see configure()).
    std::array<float, kSlots> kickdownRPM{};
};

} // namespace mm2::phys
