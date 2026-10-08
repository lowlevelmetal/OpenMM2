// mmGame::UpdateSteeringBrakes, mmInput::FilterDiscreteSteering /
// FilterGamepadSteering and the steering part of mmPlayer::Update from
// Midtown Madness 2, verified against the build 3393 code (MM2Recomp).

#include "phys/vehicle/Controls.h"

#include "phys/vehicle/CarSim.h"

#include <cmath>

namespace mm2::phys {

void ArcadeControls::apply(CarSim& car, const PedalInput& in) {
    // mmInput swaps the pedals while reversing.
    const float throttle = swapThrottle ? in.brake : in.accelerator;
    const float brakes = swapThrottle ? in.accelerator : in.brake;
    car.setInputs(throttle, brakes, in.steering, in.handbrake);
    if (!car.trans.isAutomatic || !autoReverse)
        return;
    // In a forward gear, nearly stopped, brake held and no throttle: reverse.
    if (Transmission::kFirst <= car.trans.currentGear && car.speed() < autoRevSpeed && autoRevLevel < brakes &&
        car.engine.throttle < 0.1f) {
        swapThrottle = true;
        car.trans.setReverse();
        return;
    }
    // In reverse, letting go of the (swapped) throttle returns to drive.
    if (car.trans.currentGear == Transmission::kReverse && car.engine.throttle < autoRevLevel) {
        swapThrottle = false;
        car.trans.setDrive();
    }
}

void SteeringFilter::setSpeed(float speed) {
    // mmPlayer::Update: the speed clamped to [SpeedBaseLow, SpeedBaseHi]
    // over the range (not the position within it, so the blend runs from
    // Low / range to High / range), then Lo + (Hi - Lo) * f.
    const Params& p = params;
    if (p.speedSensitive == 2) {
        const float range = p.speedBaseHi - p.speedBaseLow;
        float s = p.speedBaseLow;
        if (!(speed < p.speedBaseLow))
            s = speed <= p.speedBaseHi ? speed : p.speedBaseHi;
        const float f = s / range;
        m_deltaIn = (p.deltaInHi - p.deltaInLo) * f + p.deltaInLo;
        m_deltaOut = (p.deltaOutHi - p.deltaOutLo) * f + p.deltaOutLo;
        m_exponent = (p.filterHi - p.filterLo) * f + p.filterLo;
    } else if (p.speedSensitive == 0) {
        m_deltaIn = p.deltaInLo;
        m_deltaOut = p.deltaOutLo;
        m_exponent = p.filterLo;
    } else {
        m_deltaIn = p.deltaInHi;
        m_deltaOut = p.deltaOutHi;
        m_exponent = p.filterHi;
    }
}

float SteeringFilter::filter(float target, float dt) {
    // mmInput::FilterDiscreteSteering (keys: target -1, 0 or +1) and
    // FilterGamepadSteering (the stick): further out the same way at
    // DeltaOut, back in or across at DeltaIn.
    const auto sign = [](float v) { return 0.0f < v ? 1.0f : (v < 0.0f ? -1.0f : 0.0f); };
    float rate = m_deltaIn;
    if (std::abs(m_position) <= std::abs(target) && sign(m_position) == sign(target))
        rate = m_deltaOut;
    if (target <= m_position) {
        if (target < m_position) {
            m_position = m_position - rate * dt;
            if (m_position < target)
                m_position = target;
        }
    } else {
        m_position = rate * dt + m_position;
        if (target < m_position)
            m_position = target;
    }
    const float curve =
        static_cast<float>(std::pow(static_cast<double>(std::abs(m_position)), static_cast<double>(m_exponent)));
    return curve * sign(m_position);
}

} // namespace mm2::phys
