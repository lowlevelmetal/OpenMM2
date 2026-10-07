// Port of mmDrivetrain from Open1560 (https://github.com/0x1F9F1/Open1560),
// GPL-3.0: code/midtown/game.asm, Midtown Madness 1 beta build 1560.

#include "phys/vehicle/Drivetrain.h"

#include "phys/AgeMath.h"
#include "phys/vehicle/Engine.h"
#include "phys/vehicle/Transmission.h"

#include <cmath>

namespace mm2::phys {

void Drivetrain::configure(const DrivetrainParams& p) {
    angInertia = p.angInertia;
    dynCoeff = p.angInertia * p.brakeDynamicCoef;
    statCoeff = p.angInertia * p.brakeStaticCoef;
}

bool Drivetrain::addWheel(Wheel* w) {
    if (m_numWheels == 4)
        return false; // "Too many wheels"
    m_wheels[static_cast<std::size_t>(m_numWheels++)] = w;
    return true;
}

void Drivetrain::attach(Engine* engine, Transmission* trans) {
    m_lastEngine = engine;
    m_lastTrans = trans;
    attach();
}

void Drivetrain::attach() {
    m_engine = m_lastEngine;
    m_trans = m_lastTrans;
    if (m_engine)
        engineAngInertia = m_engine->angInertia;
}

void Drivetrain::detach() {
    m_engine = nullptr;
    m_trans = nullptr;
}

void Drivetrain::update(InertialCS& ics, const WheelEnv& env, float brakes, float handBrake) {
    if (m_numWheels == 0)
        return;
    const int n = m_numWheels;
    Wheel* w0 = m_wheels[0];
    const float dt = env.dt;

    // Brake input. MM1: back drivetrains (handbrake flag) feel only the
    // handbrake, others only the foot brake (+0.002 constant drag). MM2 gives
    // every wheel BrakeCoef and HandbrakeCoef; we let the foot brake act on
    // all drivetrains and the handbrake on the back ones (inferred).
    const float foot = age::subD(brakes * w0->brakeRatio, -0.002);
    float brakeInput = foot;
    if (w0->flags & Wheel::kHandbrake) {
        const float hand = handBrake * w0->handbrakeCoef;
        brakeInput = foot > hand ? foot : hand;
    }
    const float coeff = w0->rotationSpeed == 0.0f ? statCoeff : dynCoeff;
    const float brakeTorque = coeff * brakeInput;
    const float perWheel = brakeTorque / w0->radius;
    float brakeSum = 0.0f;
    for (int i = 0; i < n; ++i)
        brakeSum = brakeSum + perWheel;

    // mmWheel::ComputeDwtdw's first half (probe, suspension) comes first so
    // that the tyre torques can be evaluated at the current body velocity.
    // MM1 calls ComputeDwtdw after summing them; the probe does not use them.
    for (int i = 0; i < n; ++i)
        m_wheels[static_cast<std::size_t>(i)]->probe(env);

    float drive = m_engine ? m_engine->torque : 0.0f;
    if (m_trans)
        drive = drive * m_trans->currentRatio();
    for (int i = 0; i < n; ++i) {
        const Wheel* w = m_wheels[static_cast<std::size_t>(i)];
        drive = drive - (mm1ExplicitSpin ? w->tireResistance : w->predictTireResistance(env));
    }

    float net;
    bool brakesDominate = false;
    if (w0->rotationSpeed == 0.0f) {
        // Static: the brakes hold the wheels until the drive exceeds them.
        if (!(drive < 0.0f)) {
            const float t = drive - brakeSum;
            net = 0.0f <= t ? t : 0.0f;
        } else {
            const float t = drive + brakeSum;
            net = !(0.0f < t) ? t : 0.0f;
        }
    } else {
        const float b = (w0->rotationSpeed < 0.0f ? -1.0f : 1.0f) * brakeSum;
        brakesDominate = !(std::abs(drive) > std::abs(b));
        net = drive + b;
    }

    float inertia;
    if (m_engine) {
        const float r = m_trans ? m_trans->currentRatio() : 0.0f;
        inertia = angInertia * 0.01f + (0.2f * engineAngInertia) * (r * r);
    } else {
        inertia = angInertia * 0.005f;
    }

    // MM1 calls ComputeDwtdw for wheel 0, and for wheel 1 only when the
    // drivetrain has exactly two wheels; MM2 has four-wheel drivetrains, so
    // every wheel is included (OpenMM2).
    float A[4], B[4], C[4];
    float D = 300.0f;
    for (int i = 0; i < n; ++i) {
        float slope = m_wheels[static_cast<std::size_t>(i)]->computeLimits(net, A[i], B[i], C[i], env);
        if (mm1ExplicitSpin) // build 1560 stores 0 to A and C and returns 0
            slope = A[i] = C[i] = 0.0f;
        D = slope + D;
    }

    float negNet = -net;
    float rot0 = w0->rotationSpeed;
    float newRot = rot0;
    for (int iter = 1;; ++iter) {
        newRot = (negNet / (dt * D + inertia)) * dt + rot0;
        // The governing limit: the lowest one when pushing the wheels
        // forward (net < 0), the highest otherwise.
        int idx = 0;
        float bsel = B[0];
        for (int i = 1; i < n; ++i) {
            const bool better = net < 0.0f ? B[i] <= bsel : B[i] >= bsel;
            if (better && B[i] != bsel) {
                bsel = B[i];
                idx = i;
            }
        }
        const bool crossed = (net < 0.0f && newRot > bsel) || (net > 0.0f && newRot < bsel);
        if (!crossed)
            break;
        negNet = negNet - (bsel - rot0) * D;
        rot0 = bsel;
        D = D + (C[idx] - A[idx]);
        B[idx] = (net < 0.0f ? -1.0f : 1.0f) * -1e11f;
        if (iter > 20)
            break;
    }

    // Brakes stop the wheels; they never spin them backwards.
    if (brakesDominate) {
        if (newRot < 0.0f) {
            if (w0->rotationSpeed > 0.0f)
                newRot = 0.0f;
        } else if (w0->rotationSpeed < 0.0f && newRot > 0.0f) {
            newRot = 0.0f;
        }
    }

    // Rev limiter: wheel speed capped at MaxRPM in the current gear.
    if (m_engine && m_trans) {
        const float s = newRot < 0.0f ? -1.0f : 1.0f;
        const float maxRot = m_engine->maxRotationSpeed / std::abs(m_trans->currentRatio());
        float v = std::abs(newRot);
        if (v <= 0.0f)
            v = 0.0f;
        else if (!(v < maxRot))
            v = maxRot;
        newRot = v * s;
    }
    for (int i = 0; i < n; ++i)
        m_wheels[static_cast<std::size_t>(i)]->rotationSpeed = newRot;
    if (m_engine) {
        const float e = (m_trans->currentRatio() * w0->rotationSpeed) * -1.0f;
        m_engine->rotationSpeed = e <= 0.0f ? 0.0f : e;
    }

    for (int i = 0; i < n; ++i)
        m_wheels[static_cast<std::size_t>(i)]->update(ics, env);
}

} // namespace mm2::phys
