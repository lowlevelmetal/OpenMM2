// vehTransmission from Midtown Madness 2 (ComputeConstants, GearRatioFromMPH,
// Upshift, Downshift, Update, SetCurrentGear, SetForward), verified against
// the build 3393 code (MM2Recomp). See docs/physics.md.

#include "phys/vehicle/Transmission.h"

#include "core/Libm.h"
#include "phys/vehicle/Engine.h"

#include <cmath>

namespace mm2::phys {
namespace {

constexpr float kRpmToRad = 0.10471976f;

// vehTransmission::GearRatioFromMPH: engine OptRPM over the wheel RPM at
// `mph` (1609.344 m/mi * 1/60 min/s; 2 pi per turn).
float gearRatioFromMph(float mph, float optRPM, float radius) {
    const float metresPerMinute = 1609.344f * 0.016666668f;
    return optRPM / ((metresPerMinute * mph) / (radius * 6.2831855f));
}

// Ratios for one box: reverse, neutral, Low ... High (geometric, biased).
void fillRatios(std::array<float, Transmission::kSlots>& r, int n, const TransmissionParams& p, float optRPM,
                float radius) {
    r[0] = -gearRatioFromMph(p.reverse, optRPM, radius);
    r[1] = 0.0f;
    r[2] = gearRatioFromMph(p.low, optRPM, radius);
    r[static_cast<std::size_t>(n - 1)] = gearRatioFromMph(p.high, optRPM, radius);
    const int forward = n - 2;
    if (2 < forward) {
        const int m = forward - 1;
        const float fm = static_cast<float>(m);
        const double top = r[static_cast<std::size_t>(n - 1)] / r[2];
        const float q = static_cast<float>(libm::pow(top, static_cast<double>(1.0f / fm)));
        for (int i = 1; i < m; ++i) {
            const float fi = static_cast<float>(i);
            const float e = fi - ((static_cast<float>(i - m) * fi) * p.gearBias) / fm;
            r[static_cast<std::size_t>(2 + i)] =
                static_cast<float>(libm::pow(static_cast<double>(q), static_cast<double>(e))) * r[2];
        }
    }
}

} // namespace

void Transmission::configure(const TransmissionParams& p) {
    params = p;
    numGears = std::min(p.autoNumGears, kSlots);
    manualNumGears = std::min(p.manualNumGears, kSlots);
    // Constructor defaults until ComputeConstants: 30/i, 6000 / 2000 / 2000 rpm.
    for (int i = 2; i < kSlots; ++i) {
        gearRatios[static_cast<std::size_t>(i)] = manualGearRatios[static_cast<std::size_t>(i)] =
            30.0f / static_cast<float>(i);
        upshiftRPM[static_cast<std::size_t>(i)] = 6000.0f;
        kickdownRPM[static_cast<std::size_t>(i)] = 2000.0f;
        downshiftRPM[static_cast<std::size_t>(i)] = 2000.0f;
    }
    gearRatios[0] = manualGearRatios[0] = -10.0f;
    reset();
}

void Transmission::computeConstants(const Engine& engine, float radius) {
    const int n = numGears;
    fillRatios(gearRatios, n, params, engine.optRPM, radius);
    // Shift points: the RPM where the next gear gives as much power.
    for (int g = 2; g < n - 1; ++g) {
        const float f = gearRatios[static_cast<std::size_t>(g + 1)] / gearRatios[static_cast<std::size_t>(g)];
        float lo = engine.optRPM;
        float hi = engine.maxRPM;
        if (f * engine.optRPM < hi)
            hi = engine.optRPM / f;
        while (1.0f < hi - lo) {
            const float mid = (hi + lo) * 0.5f;
            const float w = mid * kRpmToRad;
            if (engine.calcHPAtFullThrottle(w) < engine.calcHPAtFullThrottle(w * f))
                hi = mid;
            else
                lo = mid;
        }
        const float x = (hi + lo) * 0.5f;
        upshiftRPM[static_cast<std::size_t>(g)] = (params.upshiftBias + 1.0f) * x;
        kickdownRPM[static_cast<std::size_t>(g + 1)] = (1.0f - params.downshiftBiasMin) * x * f;
        downshiftRPM[static_cast<std::size_t>(g + 1)] = (1.0f - params.downshiftBiasMax) * x * f;
    }
    upshiftRPM[static_cast<std::size_t>(n - 1)] = engine.maxRPM;
    kickdownRPM[2] = 0.0f;
    downshiftRPM[2] = 0.0f;
    fillRatios(manualGearRatios, manualNumGears, params, engine.optRPM, radius);
}

void Transmission::reset() {
    // vehTransmission::Reset: SetCurrentGear(2), then the time in gear cleared.
    setCurrentGear(kFirst);
    timeInGear = 0.0f;
}

int Transmission::setCurrentGear(int gear) {
    if (gear != currentGear && (!isAutomatic || gear < numGears)) {
        timeInGear = 0.0f;
        gearChanged = true;
        currentGear = gear;
    }
    return currentGear;
}

int Transmission::upshift() {
    if (!isAutomatic) {
        if (currentGear < manualNumGears - 1)
            setCurrentGear(currentGear + 1);
    } else if (currentGear < 2) {
        setCurrentGear(currentGear + 1);
    }
    return currentGear;
}

int Transmission::downshift() {
    if (!isAutomatic) {
        if (0 < currentGear)
            setCurrentGear(currentGear - 1);
    } else if (1 < currentGear) {
        setCurrentGear(kNeutral);
    } else if (0 < currentGear) {
        setCurrentGear(kReverse);
    }
    return currentGear;
}

void Transmission::setDrive() {
    if (currentGear == kReverse || currentGear == kNeutral)
        setCurrentGear(kFirst);
}

void Transmission::update(float dt, const Engine& engine, int wheelsOnGround) {
    if (wheelsOnGround == 0)
        return;
    const int g = currentGear;
    if (isAutomatic && 1 < g && params.gearChangeTime < timeInGear && !gearChanged) {
        int next = g;
        if (numGears - 1 <= g || engine.rpm <= upshiftRPM[static_cast<std::size_t>(g)]) {
            const float t = engine.throttle;
            if (3 <= g && engine.rpm < t * kickdownRPM[static_cast<std::size_t>(g)] +
                                           (1.0f - t) * downshiftRPM[static_cast<std::size_t>(g)])
                next = g - 1;
        } else {
            next = g + 1;
        }
        if (next != g)
            setCurrentGear(next);
    }
    timeInGear = dt + timeInGear;
}

} // namespace mm2::phys
