// Port of mmTransmission from Open1560 (https://github.com/0x1F9F1/Open1560),
// GPL-3.0: code/midtown/game.asm, Midtown Madness 1 beta build 1560.
// configure() for MM2-format files is an OpenMM2 adaptation (inferred).

#include "phys/vehicle/Transmission.h"

#include "phys/Constants.h"
#include "phys/vehicle/Engine.h"

#include <algorithm>
#include <cmath>

namespace mm2::phys {

void Transmission::configure(const TransmissionParams& p, const EngineParams& engine, float wheelRadius) {
    // mmTransmission::mmTransmission defaults.
    clutch = 1.0f;
    numGears = 6;
    gearRatios.fill(0.0f);
    upshiftRPM.fill(6000.0f);
    downshiftRPM.fill(2000.0f);
    kickdownRPM.fill(0.0f);
    manualGearRatios.fill(0.0f);
    manualNumGears = 0;
    isAutomatic = true;
    downshiftBias = 1.55f;
    gearChangeDelay = 0.0f;

    auto copyInto = [](std::array<float, kSlots>& dst, const std::vector<float>& src) {
        for (std::size_t i = 0; i < dst.size() && i < src.size(); ++i)
            dst[i] = src[i];
    };

    if (p.hasExplicitRatios) {
        // MM1 format: tables as given.
        numGears = std::clamp(p.numGears > 0 ? p.numGears : 6, 3, kSlots);
        copyInto(gearRatios, p.gearRatios);
        copyInto(upshiftRPM, p.upshiftRPM);
        copyInto(downshiftRPM, p.downshiftRPM);
        copyInto(manualGearRatios, p.manualGearRatios);
        manualNumGears = p.manualGearRatios.empty() ? 0 : std::clamp(p.manualNumGears, 3, kSlots);
        downshiftBias = p.downshiftBias;
        for (int g = 0; g < kSlots; ++g)
            kickdownRPM[static_cast<std::size_t>(g)] =
                downshiftBias * downshiftRPM[static_cast<std::size_t>(g)];
    } else {
        // MM2 format (inferred): Low/High/Reverse are gear speeds in mph at
        // MaxRPM ("GearRatioFromMPH"); intermediate gears interpolate between
        // linear and geometric spacing by GearBias.
        const float radius = wheelRadius > 0.01f ? wheelRadius : 0.33f;
        const float maxW = engine.maxRPM * 6.2831855f / 60.0f;
        auto ratioFromMph = [&](float mph) {
            const float speed = std::max(mph, 1.0f) / kMetersPerSecondToMph;
            return maxW / (speed / radius);
        };
        auto build = [&](std::array<float, kSlots>& table, int slots) {
            slots = std::clamp(slots, 3, kSlots);
            table.fill(0.0f);
            table[kReverse] = -ratioFromMph(p.reverse);
            table[kNeutral] = 0.0f;
            const int forward = slots - 2;
            for (int i = 0; i < forward; ++i) {
                const float t = forward > 1 ? static_cast<float>(i) / static_cast<float>(forward - 1) : 1.0f;
                const float linear = p.low + (p.high - p.low) * t;
                const float geometric = p.low > 0.0f ? p.low * std::pow(p.high / p.low, t) : linear;
                table[static_cast<std::size_t>(kFirst + i)] =
                    ratioFromMph(lerp(linear, geometric, p.gearBias));
            }
            return slots;
        };
        numGears = build(gearRatios, p.autoNumGears);
        manualNumGears = build(manualGearRatios, p.manualNumGears);
        // Shift points (inferred): up at MaxRPM * (1 - UpshiftBias); down
        // when the next lower gear would turn at MaxRPM * (1 - bias), with
        // DownshiftBiasMax normally and DownshiftBiasMin for kickdown.
        for (int g = kFirst; g < kSlots; ++g) {
            const auto gi = static_cast<std::size_t>(g);
            upshiftRPM[gi] = engine.maxRPM * (1.0f - p.upshiftBias);
            const float r = gearRatios[gi], rl = g > kFirst ? gearRatios[gi - 1] : 0.0f;
            if (r > 0.0f && rl > 0.0f) {
                downshiftRPM[gi] = engine.maxRPM * (1.0f - p.downshiftBiasMax) * (r / rl);
                kickdownRPM[gi] = engine.maxRPM * (1.0f - p.downshiftBiasMin) * (r / rl);
            } else {
                downshiftRPM[gi] = 0.0f;
                kickdownRPM[gi] = 0.0f;
            }
        }
        gearChangeDelay = p.gearChangeTime;
    }

    // mmTransmission::Init: manual table defaults to the automatic one.
    if (manualNumGears == 0) {
        manualGearRatios = gearRatios;
        manualNumGears = numGears;
    }
    reset();
}

void Transmission::reset() {
    setCurrentGear(kFirst);
    clutch = 1.0f;
    timeInGear = 0.0f;
}

float Transmission::ratio(int gear) const {
    const auto g = static_cast<std::size_t>(std::clamp(gear, 0, kSlots - 1));
    return isAutomatic ? gearRatios[g] : manualGearRatios[g];
}

float Transmission::currentRatio() const {
    return ratio(currentGear);
}

void Transmission::setCurrentGear(int gear) {
    if (isAutomatic && gear >= numGears) {
        currentGear = numGears - 1;
        return;
    }
    timeInGear = 0.0f;
    gearChanged = true;
    currentGear = gear;
}

int Transmission::upshift() {
    if (!isAutomatic && currentGear != manualNumGears - 1)
        setCurrentGear(currentGear + 1);
    return currentGear;
}

int Transmission::downshift() {
    if (!isAutomatic && currentGear > 0)
        setCurrentGear(currentGear - 1);
    return currentGear;
}

void Transmission::setDrive() {
    if (currentGear == kReverse || currentGear == kNeutral)
        setCurrentGear(kFirst);
}

void Transmission::update(float dt, const Engine& engine, bool drivingDisabled) {
    if (drivingDisabled)
        return;
    const int g = currentGear;
    if (isAutomatic && g > kNeutral && timeInGear > gearChangeDelay) {
        const auto gi = static_cast<std::size_t>(g);
        const float rpm = engine.rpm;
        if (rpm > upshiftRPM[gi] && g != numGears - 1 && !gearChanged) {
            setCurrentGear(g + 1);
        } else if (rpm < downshiftRPM[gi] && g != kFirst && !gearChanged) {
            setCurrentGear(g - 1);
        } else if (static_cast<double>(engine.throttle) > 0.8 && kickdownRPM[gi] > rpm && g != kFirst &&
                   !gearChanged) {
            setCurrentGear(g - 1);
        }
    }
    timeInGear = dt + timeInGear;
}

} // namespace mm2::phys
