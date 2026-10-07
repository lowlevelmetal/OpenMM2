// Port of mmEngine from Open1560 (https://github.com/0x1F9F1/Open1560),
// GPL-3.0: code/midtown/game.asm, Midtown Madness 1 beta build 1560.

#include "phys/vehicle/Engine.h"

#include "phys/AgeMath.h"
#include "phys/vehicle/Drivetrain.h"
#include "phys/vehicle/Transmission.h"

#include <cmath>

namespace mm2::phys {

void Engine::configure(const EngineParams& p) {
    maxHorsePower = p.maxHorsePower;
    optRPM = p.optRPM;
    maxRPM = p.maxRPM;
    gcl = p.gcl;
    idleRPM = p.idleRPM;
    angInertia = p.angInertia;
    hpScale = 1.0f;
    // mmEngine::mmEngine.
    const float sq5 = std::sqrt(5.0f);
    pullUpModifier = age::mulD(sq5 - -1.0f, 0.5);
    breakDownModifier = age::mulD(sq5 - 1.0f, 0.5);
    computeConstants();
    reset();
}

void Engine::reset() {
    // mmEngine::Reset.
    hpScale = 1.0f;
    rotationSpeed = 0.0f;
    rpm = 0.0f;
    horsepower = 0.0f;
    torque = 0.0f;
    changingGear = false;
    throttle = 0.0f;
    gearChangeTime = 0.0f;
}

void Engine::computeConstants() {
    constexpr float kTwoPi = 6.2831855f;
    const float maxW = age::mulD(maxRPM * kTwoPi, 1.0 / 60.0);
    const float optW = age::mulD(optRPM * kTwoPi, 1.0 / 60.0);
    const float gap = maxW - optW;
    maxRotationSpeed = maxW;
    optRotationSpeed = optW;
    torqueDropoff = 1.0f / (gap * gap);
    torqueCoef = ((hpScale * maxHorsePower) * 746.0f) / ((optW * optW) * optW);
}

float Engine::calcTorqueAtFullThrottle() const {
    const float w = rotationSpeed;
    if (w <= optRotationSpeed) {
        const float a = pullUpModifier * optRotationSpeed - w;
        const float b = breakDownModifier * optRotationSpeed + w;
        return (a * b) * torqueCoef;
    }
    if (w <= maxRotationSpeed) {
        const float a = pullUpModifier * optRotationSpeed - w;
        const float b = breakDownModifier * optRotationSpeed + w;
        if (!mm1TorqueTaper) {
            // MM2 adaptation (inferred): no taper above OptRPM; the curve runs
            // up to the rev limit (clamped at zero).
            const float t = (a * b) * torqueCoef;
            return t > 0.0f ? t : 0.0f;
        }
        const float taper = (maxRotationSpeed + w) - (optRotationSpeed + optRotationSpeed);
        return (((taper * a) * b) * (maxRotationSpeed - w)) * torqueDropoff * torqueCoef;
    }
    return 0.0f;
}

float Engine::calcTorqueAtZeroThrottle() const {
    // Engine braking: zero at 160 rad/s (~1528 RPM), -Pmax/(2 w_opt) at OptRPM.
    const float t = ((hpScale * maxHorsePower) * 746.0f) / optRotationSpeed;
    return age::mulD(t * (160.0f - rotationSpeed), 0.5) / (optRotationSpeed - 160.0f);
}

float Engine::calcTorque(float t) const {
    const float zero = (1.0f - t) * calcTorqueAtZeroThrottle();
    return calcTorqueAtFullThrottle() * t + zero;
}

void Engine::update(float dt, Transmission& trans, Drivetrain& primary) {
    if (trans.gearChanged && !changingGear) {
        prevGearRPM = rpm;
        gearChangeTime = gcl;
        changingGear = true;
    }
    torque = calcTorque(throttle);
    // MM1 scales torque by (1.3 - damage) in one game mode (MMSTATE+0x12C);
    // MM2 does not (mm2hook adds that as an option), so it is omitted.

    if (trans.currentRatio() == 0.0f) {
        primary.detach();
        // MM1 divides by car mass * 0.001; MM2 has Engine.AngInertia.
        const float w = (torque / angInertia) * dt + rotationSpeed;
        if (w <= 0.0f)
            rotationSpeed = 0.0f;
        else
            rotationSpeed = w < maxRotationSpeed ? w : maxRotationSpeed;
    } else {
        primary.attach();
    }

    rpm = rotationSpeed * 9.549296f;
    if (gearChangeTime > 0.0f) {
        rpm = ((gcl - gearChangeTime) * rpm + prevGearRPM * gearChangeTime) / gcl;
        torque = 0.0f;
        gearChangeTime = gearChangeTime - dt;
    } else {
        changingGear = false;
        trans.gearChanged = false;
    }
    horsepower = (rotationSpeed * torque) * 0.0013404826f;
}

} // namespace mm2::phys
