// vehDrivetrain from Midtown Madness 2 (Update, Attach/Detach, AddWheel,
// FileIO), verified against the build 3393 code (MM2Recomp); the x87 order of
// operations follows the original. See docs/physics.md.

#include "phys/vehicle/Drivetrain.h"

#include "phys/vehicle/Engine.h"
#include "phys/vehicle/Transmission.h"

#include <cmath>

namespace mm2::phys {

void Drivetrain::configure(const DrivetrainParams& p) {
    angInertia = p.angInertia;
    brakeDynamicCoef = p.brakeDynamicCoef;
    brakeStaticCoef = p.brakeStaticCoef;
}

void Drivetrain::reset() {
    rotationSpeed = 0.0f;
    diffRatio = 1.0f;
}

bool Drivetrain::addWheel(Wheel* w) {
    if (m_numWheels == 4)
        return false;
    m_wheels[static_cast<std::size_t>(m_numWheels++)] = w;
    return true;
}

void Drivetrain::attach(Engine* engine, Transmission* trans) {
    m_engine = m_lastEngine = engine;
    m_trans = m_lastTrans = trans;
}

void Drivetrain::attach() {
    m_engine = m_lastEngine;
    m_trans = m_lastTrans;
}

void Drivetrain::detach() {
    m_engine = nullptr;
    m_trans = nullptr;
}

void Drivetrain::update(const WheelEnv& env, float carMass) {
    const float dt = env.dt;
    const int n = m_numWheels;

    // Brake torque: static while the wheels are stopped.
    float brake = 50.0f;
    const float coef = rotationSpeed == 0.0f ? brakeStaticCoef : brakeDynamicCoef;
    for (int i = 0; i < n; ++i)
        brake = brake + m_wheels[static_cast<std::size_t>(i)]->brakeTorque * coef;

    // Net torque resisting the rotation: the engine coupling (zero while the
    // engine turns with the wheels), the engine torque and last sample's tyre
    // torques.
    float drive = 0.0f;
    float net = 0.0f;
    float ratio = 0.0f;
    if (m_engine) {
        ratio = m_trans->currentRatio();
        drive = ratio * m_engine->torque;
        net = (ratio * rotationSpeed + m_engine->rotationSpeed) * m_engine->angInertia * env.invDt * ratio;
    }
    net = net + drive;
    for (int i = 0; i < n; ++i)
        net = net - m_wheels[static_cast<std::size_t>(i)]->tireResistance;

    // The brake opposes the rotation; at rest it holds against the net torque.
    float eff;
    bool brakeHolds = false;
    if (rotationSpeed == 0.0f) {
        if (net < 0.0f) {
            const float x = brake + net;
            eff = 0.0f < x ? 0.0f : x;
        } else {
            const float x = net - brake;
            eff = 0.0f <= x ? x : 0.0f;
        }
    } else {
        const float s = 0.0f < rotationSpeed ? 1.0f : (rotationSpeed < 0.0f ? -1.0f : 0.0f);
        const float b = brake * s;
        brakeHolds = !(std::abs(b) < std::abs(net));
        eff = net + b;
    }

    float inertia;
    if (m_engine && m_trans)
        inertia = ratio * m_engine->angInertia * ratio + 0.02f;
    else
        inertia = carMass * 0.005f;

    // Wheel breakpoints (vehWheel::ComputeDwtdw; the slopes are always 0).
    std::array<float, 4> limit{};
    for (int i = 0; i < n; ++i)
        limit[static_cast<std::size_t>(i)] = m_wheels[static_cast<std::size_t>(i)]->computeDwtdw(eff, env);

    float torque = -eff;
    // Limited-slip differential between the wheels of each pair.
    if (1.0f < kDiffRatioMax) {
        const float s = std::abs(rotationSpeed);
        float lim;
        if (s < kDiffRatioHighSpeedLevel)
            lim = ((kDiffRatioHighSpeedLevel - s) * kDiffRatioMax + kDiffRatioMaxHighSpeed * s) *
                  (1.0f / kDiffRatioHighSpeedLevel);
        else
            lim = kDiffRatioMaxHighSpeed;
        if (n <= 1 || (rotationSpeed < 0.001f && !(rotationSpeed <= -0.001f))) {
            diffRatio = 1.0f;
        } else {
            const float invLim = 1.0f / lim;
            float imbalance = 0.0f;
            const float slopes = 0.0f; // sum of the (zero) wheel slopes
            for (int i = 0; i + 1 < n; i += 2)
                imbalance = imbalance + (m_wheels[static_cast<std::size_t>(i)]->tireResistance -
                                         m_wheels[static_cast<std::size_t>(i + 1)]->tireResistance);
            float r = imbalance / ((slopes + angInertia) * rotationSpeed) + diffRatio;
            if (lim < r)
                r = lim;
            else if (r < invLim)
                r = invLim;
            const float old = diffRatio;
            diffRatio = (old * 9.0f + r) * 0.1f;
            const float invRatio = 1.0f / diffRatio;
            torque = torque - (old - diffRatio) * slopes * rotationSpeed;
            for (int i = 0; i + 1 < n; i += 2) {
                limit[static_cast<std::size_t>(i)] = invRatio * limit[static_cast<std::size_t>(i)];
                limit[static_cast<std::size_t>(i + 1)] = diffRatio * limit[static_cast<std::size_t>(i + 1)];
            }
        }
    }

    // Integrate, stopping at a crossed breakpoint. With the original's
    // starting values (1e9 for the largest, -1e10 for the smallest limit)
    // only the sentinels returned for wheels in the air or past the optimum
    // slip qualify, so in practice the first step stands.
    const float damp = angInertia;
    float w = rotationSpeed;
    float wNew = w;
    std::size_t crossed = 0;
    for (int iter = 0;;) {
        wNew = (torque / (dt * damp + inertia)) * dt + w;
        ++iter;
        float m;
        if (eff < 0.0f) {
            m = 1e9f;
            for (int i = 0; i < n; ++i)
                if (m < limit[static_cast<std::size_t>(i)]) {
                    m = limit[static_cast<std::size_t>(i)];
                    crossed = static_cast<std::size_t>(i);
                }
            if (!(m < wNew))
                break;
        } else {
            m = -1e10f;
            for (int i = 0; i < n; ++i)
                if (!(m <= limit[static_cast<std::size_t>(i)])) {
                    m = limit[static_cast<std::size_t>(i)];
                    crossed = static_cast<std::size_t>(i);
                }
            if (!(0.0f < eff && wNew < m))
                break;
        }
        torque = torque - (m - w) * damp;
        w = m;
        const float s = 0.0f < eff ? 1.0f : (eff < 0.0f ? -1.0f : 0.0f);
        limit[crossed] = s * -1e11f;
        if (iter > 20)
            break;
    }
    // The brakes stop the wheels but never reverse them.
    if (brakeHolds && ((wNew < 0.0f && 0.0f < rotationSpeed) || (rotationSpeed < 0.0f && 0.0f < wNew)))
        wNew = 0.0f;

    // In gear the wheels cannot outrun the engine's MaxRPM.
    if (m_engine && m_trans) {
        const float s = 0.0f < wNew ? 1.0f : (wNew < 0.0f ? -1.0f : 0.0f);
        const float a = std::abs(wNew);
        const float lim = m_engine->maxRotationSpeed / std::abs(ratio);
        float v;
        if (a < 0.0f)
            v = 0.0f;
        else if (lim < a)
            v = lim;
        else
            v = a;
        wNew = v * s;
    }
    rotationSpeed = wNew;
    if (m_engine) {
        // The engine follows the wheels, never backwards or past MaxRPM.
        const float e = -(ratio * wNew);
        if (e < 0.0f) {
            rotationSpeed = 0.0f;
        } else if (m_engine->maxRotationSpeed < e) {
            rotationSpeed = -(m_engine->maxRotationSpeed / ratio);
            m_engine->rotationSpeed = m_engine->maxRotationSpeed;
        } else {
            m_engine->rotationSpeed = e;
        }
    }

    const float invRatio = 1.0f / diffRatio;
    for (int i = 0; i < n - 1; i += 2) {
        m_wheels[static_cast<std::size_t>(i)]->rotationSpeed = diffRatio * rotationSpeed;
        m_wheels[static_cast<std::size_t>(i + 1)]->rotationSpeed = invRatio * rotationSpeed;
    }
    if (n & 1)
        m_wheels[static_cast<std::size_t>(n - 1)]->rotationSpeed = rotationSpeed;

    for (int i = 0; i < n; ++i)
        m_wheels[static_cast<std::size_t>(i)]->update(env);
}

} // namespace mm2::phys
